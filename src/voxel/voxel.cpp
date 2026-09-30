#include "voxel/voxel.h"
#include "voxel/simd.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <future>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>

namespace directcraft::voxel {
namespace {

class ChunkJobPool {
public:
    ChunkJobPool() {
        const unsigned count = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
        for (unsigned index = 0; index < count; ++index) workers_.emplace_back([this] { workerLoop(); });
    }
    ~ChunkJobPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) worker.join();
    }
    template <typename Function>
    auto submit(Function function) -> std::future<decltype(function())> {
        using Result = decltype(function());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(function));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.emplace([task] { (*task)(); });
        }
        condition_.notify_one();
        return result;
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop();
            }
            job();
        }
    }
    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    bool stopping_{};
};

ChunkJobPool& chunkJobPool() {
    static ChunkJobPool pool;
    return pool;
}

std::uint32_t fixedHash(int x, int z, std::int32_t seed) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u;
    value ^= static_cast<std::uint32_t>(z) * 668265263u;
    value ^= static_cast<std::uint32_t>(seed) * 2246822519u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    return value ^ (value >> 16u);
}

int fixedHeight(int x, int z, std::int32_t seed) {
    const std::uint32_t hash = fixedHash(x / 4, z / 4, seed);
    return std::clamp(8 + static_cast<int>((hash % 9u) / 2u), 3, ChunkHeight - 2);
}

void addFace(Mesh& mesh, const float p[4][3], const float normal[3], BlockType type) {
    const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
    const float shade = type == BlockType::Grass ? 0.78f : (type == BlockType::Stone ? 0.56f : 0.68f);
    const float color[4] = {shade, type == BlockType::Grass ? 0.86f : shade, type == BlockType::Grass ? 0.38f : shade, 1.0f};
    constexpr float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        Vertex vertex{};
        for (int axis = 0; axis < 3; ++axis) {
            vertex.position[axis] = p[i][axis];
            vertex.normal[axis] = normal[axis];
        }
        vertex.uv[0] = uv[i][0];
        vertex.uv[1] = uv[i][1];
        for (int channel = 0; channel < 4; ++channel) vertex.color[channel] = color[channel];
        mesh.vertices.push_back(vertex);
    }
    // The points are ordered so the geometric normal agrees with the stored normal.
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

void appendMesh(Mesh& destination, const Mesh& source) {
    const auto offset = static_cast<std::uint32_t>(destination.vertices.size());
    destination.vertices.insert(destination.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const auto index : source.indices) destination.indices.push_back(index + offset);
}

template <typename Sample>
RayHit raycastSample(Sample&& sample, const std::array<float, 3>& origin,
                     const std::array<float, 3>& direction, float maxDistance) {
    RayHit result;
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length < 0.000001f || maxDistance <= 0.0f) return result;
    const std::array<float, 3> ray = {direction[0] / length, direction[1] / length, direction[2] / length};
    std::array<int, 3> cell = {static_cast<int>(std::floor(origin[0])), static_cast<int>(std::floor(origin[1])), static_cast<int>(std::floor(origin[2]))};
    result.previous = {cell[0], cell[1], cell[2]};
    if (sample(cell[0], cell[1], cell[2]) != BlockType::Air) {
        result.hit = true;
        result.block = {cell[0], cell[1], cell[2]};
        result.distance = 0.0f;
        return result;
    }
    std::array<int, 3> step{};
    std::array<float, 3> tMax{};
    std::array<float, 3> tDelta{};
    for (int axis = 0; axis < 3; ++axis) {
        if (ray[axis] > 0.0f) {
            step[axis] = 1;
            tMax[axis] = (static_cast<float>(cell[axis] + 1) - origin[axis]) / ray[axis];
            tDelta[axis] = 1.0f / ray[axis];
        } else if (ray[axis] < 0.0f) {
            step[axis] = -1;
            tMax[axis] = (static_cast<float>(cell[axis]) - origin[axis]) / ray[axis];
            tDelta[axis] = -1.0f / ray[axis];
        } else {
            step[axis] = 0;
            tMax[axis] = std::numeric_limits<float>::infinity();
            tDelta[axis] = std::numeric_limits<float>::infinity();
        }
    }
    while (true) {
        int axis = 0;
        if (tMax[1] < tMax[axis]) axis = 1;
        if (tMax[2] < tMax[axis]) axis = 2;
        const float distance = tMax[axis];
        if (!std::isfinite(distance) || distance > maxDistance) break;
        result.previous = {cell[0], cell[1], cell[2]};
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
        if (sample(cell[0], cell[1], cell[2]) != BlockType::Air) {
            result.hit = true;
            result.block = {cell[0], cell[1], cell[2]};
            result.normal = {};
            result.normal.x = axis == 0 ? -step[axis] : 0;
            result.normal.y = axis == 1 ? -step[axis] : 0;
            result.normal.z = axis == 2 ? -step[axis] : 0;
            result.distance = distance;
            return result;
        }
    }
    return result;
}

} // namespace

Chunk::Chunk(std::int32_t seed, ChunkCoord coordinate) : seed_(seed), coordinate_(coordinate) { generate(); }

std::size_t Chunk::index(int x, int y, int z) {
    return static_cast<std::size_t>((y * ChunkSize + z) * ChunkSize + x);
}

bool Chunk::inBounds(int x, int y, int z) {
    return x >= 0 && x < ChunkSize && y >= 0 && y < ChunkHeight && z >= 0 && z < ChunkSize;
}

BlockType Chunk::get(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return BlockType::Air;
    return blocks_[index(x, y, z)];
}

void Chunk::set(int x, int y, int z, BlockType block) {
    if (inBounds(x, y, z)) blocks_[index(x, y, z)] = block;
}

int Chunk::heightAt(int x, int z) const {
    return fixedHeight(worldX() + x, worldZ() + z, seed_);
}

void Chunk::generate() {
    blocks_.fill(BlockType::Air);
    for (int z = 0; z < ChunkSize; ++z) {
        for (int x = 0; x < ChunkSize; ++x) {
            const int height = heightAt(x, z);
            for (int y = 0; y <= height; ++y) {
                BlockType block = BlockType::Stone;
                if (y == height) block = BlockType::Grass;
                else if (y > height - 3) block = BlockType::Dirt;
                set(x, y, z, block);
            }
        }
    }
}

Mesh Chunk::buildMesh() const {
    return buildMesh([this](int x, int y, int z) { return get(x - worldX(), y, z - worldZ()); });
}

Mesh Chunk::buildMesh(const std::function<BlockType(int, int, int)>& sample) const {
    Mesh mesh;
    static constexpr int offsets[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    static constexpr float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};

    for (int face = 0; face < 6; ++face) {
        const int slices = face < 2 ? ChunkSize : (face < 4 ? ChunkSize : ChunkHeight);
        const int uSize = face < 4 ? (face < 2 ? ChunkSize : ChunkSize) : ChunkSize;
        const int vSize = face < 4 ? ChunkHeight : ChunkSize;
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(uSize * vSize));
        for (int slice = 0; slice < slices; ++slice) {
            zeroMask(mask.data(), mask.size());
            for (int v = 0; v < vSize; ++v) for (int u = 0; u < uSize; ++u) {
                int x = 0, y = 0, z = 0;
                if (face < 2) { x = u; y = v; z = slice; }
                else if (face < 4) { x = slice; y = v; z = u; }
                else { x = u; y = slice; z = v; }
                const BlockType block = get(x, y, z);
                if (block == BlockType::Air) continue;
                const BlockType neighbor = sample(worldX() + x + offsets[face][0], y + offsets[face][1], worldZ() + z + offsets[face][2]);
                if (neighbor != BlockType::Air) continue;
                mask[static_cast<std::size_t>(v * uSize + u)] = static_cast<std::uint8_t>(block);
            }

            for (int v = 0; v < vSize; ++v) for (int u = 0; u < uSize; ++u) {
                const std::uint8_t value = mask[static_cast<std::size_t>(v * uSize + u)];
                if (value == 0) continue;
                int width = 1;
                while (u + width < uSize && mask[static_cast<std::size_t>(v * uSize + u + width)] == value) ++width;
                int height = 1;
                bool extend = true;
                while (v + height < vSize && extend) {
                    for (int test = 0; test < width; ++test) {
                        if (mask[static_cast<std::size_t>((v + height) * uSize + u + test)] != value) { extend = false; break; }
                    }
                    if (extend) ++height;
                }
                for (int row = 0; row < height; ++row) for (int column = 0; column < width; ++column)
                    mask[static_cast<std::size_t>((v + row) * uSize + u + column)] = 0;

                const BlockType block = static_cast<BlockType>(value);
                const float xf = static_cast<float>(worldX());
                const float yf = 0.0f;
                const float zf = static_cast<float>(worldZ());
                const float U = static_cast<float>(u);
                const float V = static_cast<float>(v);
                const float W = static_cast<float>(width);
                const float H = static_cast<float>(height);
                float p[4][3]{};
                if (face == 0) {
                    const float x = xf + U, y = V, z = zf + slice;
                    p[0][0]=x;p[0][1]=y;p[0][2]=z; p[1][0]=x;p[1][1]=y+H;p[1][2]=z; p[2][0]=x+W;p[2][1]=y+H;p[2][2]=z; p[3][0]=x+W;p[3][1]=y;p[3][2]=z;
                } else if (face == 1) {
                    const float x = xf + U, y = V, z = zf + slice + 1.0f;
                    p[0][0]=x;p[0][1]=y;p[0][2]=z; p[1][0]=x+W;p[1][1]=y;p[1][2]=z; p[2][0]=x+W;p[2][1]=y+H;p[2][2]=z; p[3][0]=x;p[3][1]=y+H;p[3][2]=z;
                } else if (face == 2) {
                    const float x = xf + slice, y = V, z = zf + U;
                    p[0][0]=x;p[0][1]=y;p[0][2]=z; p[1][0]=x;p[1][1]=y;p[1][2]=z+W; p[2][0]=x;p[2][1]=y+H;p[2][2]=z+W; p[3][0]=x;p[3][1]=y+H;p[3][2]=z;
                } else if (face == 3) {
                    const float x = xf + slice + 1.0f, y = V, z = zf + U;
                    p[0][0]=x;p[0][1]=y;p[0][2]=z; p[1][0]=x;p[1][1]=y+H;p[1][2]=z; p[2][0]=x;p[2][1]=y+H;p[2][2]=z+W; p[3][0]=x;p[3][1]=y;p[3][2]=z+W;
                } else if (face == 4) {
                    const float x = xf + U, y = static_cast<float>(slice), z = zf + V;
                    p[0][0]=x;p[0][1]=y;p[0][2]=z; p[1][0]=x+W;p[1][1]=y;p[1][2]=z; p[2][0]=x+W;p[2][1]=y;p[2][2]=z+H; p[3][0]=x;p[3][1]=y;p[3][2]=z+H;
                } else {
                    const float x = xf + U, y = static_cast<float>(slice) + 1.0f, z = zf + V;
                    p[0][0]=x;p[0][1]=y;p[0][2]=z; p[1][0]=x;p[1][1]=y;p[1][2]=z+H; p[2][0]=x+W;p[2][1]=y;p[2][2]=z+H; p[3][0]=x+W;p[3][1]=y;p[3][2]=z;
                }
                addFace(mesh, p, normals[face], block);
            }
        }
    }
    return mesh;
}

RayHit raycast(const Chunk& chunk, const std::array<float, 3>& origin, const std::array<float, 3>& direction, float maxDistance) {
    return raycastSample([&chunk](int x, int y, int z) { return chunk.get(x, y, z); }, origin, direction, maxDistance);
}

Frustum makeFrustum(const std::array<float, 16>& m) {
    Frustum result;
    result.planes[0].equation = {m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]};
    result.planes[1].equation = {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]};
    result.planes[2].equation = {m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]};
    result.planes[3].equation = {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]};
    result.planes[4].equation = {m[2], m[6], m[10], m[14]};
    result.planes[5].equation = {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]};
    for (auto& plane : result.planes) {
        const float length = std::sqrt(plane.equation[0] * plane.equation[0] + plane.equation[1] * plane.equation[1] + plane.equation[2] * plane.equation[2]);
        if (length > 0.000001f) for (float& value : plane.equation) value /= length;
    }
    return result;
}

bool intersects(const Frustum& frustum, const std::array<float, 3>& minimum, const std::array<float, 3>& maximum) {
    for (const auto& plane : frustum.planes) {
        const auto& e = plane.equation;
        const float x = e[0] >= 0.0f ? maximum[0] : minimum[0];
        const float y = e[1] >= 0.0f ? maximum[1] : minimum[1];
        const float z = e[2] >= 0.0f ? maximum[2] : minimum[2];
        if (e[0] * x + e[1] * y + e[2] * z + e[3] < 0.0f) return false;
    }
    return true;
}

World::World(std::int32_t seed, int loadRadius, int renderRadius)
    : seed_(seed), loadRadius_(std::max(1, loadRadius)), renderRadius_(std::clamp(renderRadius, 1, loadRadius_)) {}

int World::floorDiv(int value, int divisor) {
    if (value >= 0) return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

ChunkCoord World::chunkCoordFor(int worldX, int worldZ) const { return {floorDiv(worldX, ChunkSize), floorDiv(worldZ, ChunkSize)}; }

Chunk* World::findChunk(ChunkCoord coordinate) {
    const auto found = chunks_.find(coordinate);
    return found == chunks_.end() ? nullptr : found->second.get();
}

const Chunk* World::findChunk(ChunkCoord coordinate) const {
    const auto found = chunks_.find(coordinate);
    return found == chunks_.end() ? nullptr : found->second.get();
}

void World::updateStreaming(float playerX, float playerZ) {
    const ChunkCoord center = chunkCoordFor(static_cast<int>(std::floor(playerX)), static_cast<int>(std::floor(playerZ)));
    stats_.generatedChunks = 0;
    stats_.unloadedChunks = 0;
    std::vector<std::pair<ChunkCoord, std::future<std::unique_ptr<Chunk>>>> jobs;
    for (int z = center.z - loadRadius_; z <= center.z + loadRadius_; ++z) for (int x = center.x - loadRadius_; x <= center.x + loadRadius_; ++x) {
        const int dx = x - center.x;
        const int dz = z - center.z;
        if (dx * dx + dz * dz > loadRadius_ * loadRadius_) continue;
        const ChunkCoord coordinate{x, z};
        if (chunks_.find(coordinate) == chunks_.end()) {
            jobs.emplace_back(coordinate, chunkJobPool().submit([seed = seed_, coordinate] { return std::make_unique<Chunk>(seed, coordinate); }));
        }
    }
    for (auto& job : jobs) {
        chunks_.emplace(job.first, job.second.get());
        ++stats_.generatedChunks;
    }
    for (auto iterator = chunks_.begin(); iterator != chunks_.end();) {
        const int dx = iterator->first.x - center.x;
        const int dz = iterator->first.z - center.z;
        if (dx * dx + dz * dz > loadRadius_ * loadRadius_) {
            iterator = chunks_.erase(iterator);
            ++stats_.unloadedChunks;
        } else ++iterator;
    }
    stats_.loadedChunks = chunks_.size();
}

BlockType World::get(int worldX, int worldY, int worldZ) const {
    if (worldY < 0 || worldY >= ChunkHeight) return BlockType::Air;
    const Int3 coordinate{worldX, worldY, worldZ};
    const auto edited = edits_.find(coordinate);
    if (edited != edits_.end()) return edited->second;
    const ChunkCoord chunkCoordinate = chunkCoordFor(worldX, worldZ);
    const Chunk* chunk = findChunk(chunkCoordinate);
    if (!chunk) return BlockType::Air;
    const int localX = worldX - chunkCoordinate.x * ChunkSize;
    const int localZ = worldZ - chunkCoordinate.z * ChunkSize;
    return chunk->get(localX, worldY, localZ);
}

void World::set(int worldX, int worldY, int worldZ, BlockType block) {
    if (worldY < 0 || worldY >= ChunkHeight) return;
    const ChunkCoord coordinate = chunkCoordFor(worldX, worldZ);
    Chunk* chunk = findChunk(coordinate);
    if (!chunk) return;
    const int localX = worldX - coordinate.x * ChunkSize;
    const int localZ = worldZ - coordinate.z * ChunkSize;
    chunk->set(localX, worldY, localZ, block);
    edits_[{worldX, worldY, worldZ}] = block;
}

bool World::isResident(int worldX, int worldZ) const { return findChunk(chunkCoordFor(worldX, worldZ)) != nullptr; }

Mesh World::buildRenderMesh(float playerX, float playerZ, const std::optional<Frustum>& frustum) {
    Mesh result;
    stats_.meshedChunks = stats_.visibleChunks = stats_.distanceCulledChunks = stats_.frustumCulledChunks = 0;
    const ChunkCoord center = chunkCoordFor(static_cast<int>(std::floor(playerX)), static_cast<int>(std::floor(playerZ)));
    const auto sample = [this](int x, int y, int z) { return get(x, y, z); };
    for (const auto& entry : chunks_) {
        const ChunkCoord coordinate = entry.first;
        const int dx = coordinate.x - center.x;
        const int dz = coordinate.z - center.z;
        if (dx * dx + dz * dz > renderRadius_ * renderRadius_) { ++stats_.distanceCulledChunks; continue; }
        const std::array<float, 3> minimum = {static_cast<float>(coordinate.x * ChunkSize), 0.0f, static_cast<float>(coordinate.z * ChunkSize)};
        const std::array<float, 3> maximum = {minimum[0] + ChunkSize, static_cast<float>(ChunkHeight), minimum[2] + ChunkSize};
        if (frustum && !intersects(*frustum, minimum, maximum)) { ++stats_.frustumCulledChunks; continue; }
        ++stats_.visibleChunks;
        appendMesh(result, entry.second->buildMesh(sample));
        ++stats_.meshedChunks;
    }
    stats_.loadedChunks = chunks_.size();
    return result;
}

RayHit World::raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction, float maxDistance) const {
    return raycastSample([this](int x, int y, int z) { return get(x, y, z); }, origin, direction, maxDistance);
}

} // namespace directcraft::voxel
