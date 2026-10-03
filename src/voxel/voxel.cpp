#include "voxel/voxel.h"
#include "voxel/simd.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <chrono>
#include <future>
#include <limits>
#include <mutex>
#include <memory_resource>
#include <queue>
#include <thread>

namespace directcraft::voxel {
namespace {

class ChunkJobPool {
public:
    ChunkJobPool() {
        const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
        const unsigned count = std::min(6u, std::max(1u, hardware - 1u));
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
    auto submit(int priority, Function function) -> std::future<decltype(function())> {
        using Result = decltype(function());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(function));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push(Job{priority, sequence_++, [task] { (*task)(); }});
        }
        condition_.notify_one();
        return result;
    }

private:
    struct Job { int priority{}; std::uint64_t sequence{}; std::function<void()> function; };
    struct JobCompare { bool operator()(const Job& left, const Job& right) const { if (left.priority != right.priority) return left.priority > right.priority; return left.sequence > right.sequence; } };
    void workerLoop() {
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty()) return;
                job = std::move(const_cast<Job&>(jobs_.top()));
                jobs_.pop();
            }
            job.function();
        }
    }
    std::mutex mutex_;
    std::condition_variable condition_;
    std::priority_queue<Job, std::vector<Job>, JobCompare> jobs_;
    std::vector<std::thread> workers_;
    bool stopping_{};
    std::uint64_t sequence_{};
};

ChunkJobPool& chunkJobPool() {
    static ChunkJobPool pool;
    return pool;
}

int lodForDistance(int distance, int renderRadius) {
    if (distance <= 4) return static_cast<int>(MeshLod::FullVoxel);
    if (distance <= 8 || renderRadius <= 8) return static_cast<int>(MeshLod::SimplifiedVoxel);
    return static_cast<int>(MeshLod::FarTerrain);
}

std::uint32_t fixedHash(int x, int z, std::int32_t seed) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u;
    value ^= static_cast<std::uint32_t>(z) * 668265263u;
    value ^= static_cast<std::uint32_t>(seed) * 2246822519u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    return value ^ (value >> 16u);
}

TextureAtlas makeProceduralTextureAtlasImpl(std::int32_t seed) {
    TextureAtlas atlas; atlas.pixels.resize(static_cast<std::size_t>(atlas.tileSize * atlas.tilesX * atlas.tileSize * atlas.tilesY));
    const std::array<std::array<std::uint8_t, 3>, 6> base{{{{24, 24, 28}}, {{116, 176, 52}}, {{132, 82, 48}}, {{116, 116, 124}}, {{212, 178, 82}}, {{50, 120, 180}}}};
    for (int material = 0; material < 6; ++material) for (int py = 0; py < atlas.tileSize; ++py) for (int px = 0; px < atlas.tileSize; ++px) { const int tileX = material % atlas.tilesX; const int tileY = material / atlas.tilesX; const int x = tileX * atlas.tileSize + px; const int y = tileY * atlas.tileSize + py; const std::uint32_t noise = fixedHash(x, y, seed + material * 97); const int variation = static_cast<int>(noise % 25u) - 12; const auto clamp = [](int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }; const auto& color = base[material]; const std::uint8_t r = clamp(static_cast<int>(color[0]) + variation); const std::uint8_t g = clamp(static_cast<int>(color[1]) + variation); const std::uint8_t b = clamp(static_cast<int>(color[2]) + variation); atlas.pixels[static_cast<std::size_t>(y * atlas.tileSize * atlas.tilesX + x)] = 0xff000000u | (static_cast<std::uint32_t>(b) << 16u) | (static_cast<std::uint32_t>(g) << 8u) | r; }
    return atlas;
}

int fixedHeight(int x, int z, std::int32_t seed) {
    const std::uint32_t hash = fixedHash(x / 4, z / 4, seed);
    return std::clamp(8 + static_cast<int>((hash % 9u) / 2u), 3, ChunkHeight - 2);
}

void addFace(Mesh& mesh, const float p[4][3], const float normal[3], BlockType type, const float aoValues[4]) {
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
        vertex.ao = aoValues[i];
        vertex.material = static_cast<std::uint32_t>(type);
        mesh.vertices.push_back(vertex);
    }
    // The points are ordered so the geometric normal agrees with the stored normal.
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

void appendMesh(Mesh& destination, const Mesh& source) {
    const auto offset = static_cast<std::uint32_t>(destination.vertices.size());
    destination.vertices.insert(destination.vertices.end(), source.vertices.begin(), source.vertices.end());
    destination.packedVertices.insert(destination.packedVertices.end(), source.packedVertices.begin(), source.packedVertices.end());
    for (const auto index : source.indices) destination.indices.push_back(index + offset);
}

Mesh buildLodMesh(const Chunk& chunk, MeshLod lod, const std::function<BlockType(int, int, int)>& sample) {
    if (lod == MeshLod::FullVoxel) return chunk.buildMesh(sample);
    Mesh mesh; const int step = lod == MeshLod::FarTerrain ? 2 : 1;
    for (int z = 0; z < ChunkSize; z += step) for (int x = 0; x < ChunkSize; x += step) {
        int top = -1; BlockType material = BlockType::Air;
        for (int y = ChunkHeight - 1; y >= 0; --y) if (chunk.get(x, y, z) != BlockType::Air) { top = y; material = chunk.get(x, y, z); break; }
        if (top < 0) continue;
        const float p[4][3] = {{static_cast<float>(chunk.worldX()+x), static_cast<float>(top+1), static_cast<float>(chunk.worldZ()+z)}, {static_cast<float>(chunk.worldX()+std::min(x+step, ChunkSize)), static_cast<float>(top+1), static_cast<float>(chunk.worldZ()+z)}, {static_cast<float>(chunk.worldX()+std::min(x+step, ChunkSize)), static_cast<float>(top+1), static_cast<float>(chunk.worldZ()+std::min(z+step, ChunkSize))}, {static_cast<float>(chunk.worldX()+x), static_cast<float>(top+1), static_cast<float>(chunk.worldZ()+std::min(z+step, ChunkSize))}};
        const float normal[3] = {0,1,0}; const float ao[4] = {1,1,1,1}; addFace(mesh, p, normal, material, ao);
    }
    mesh.packedVertices.reserve(mesh.vertices.size()); for (const auto& vertex : mesh.vertices) mesh.packedVertices.push_back(World::packVertex(vertex)); return mesh;
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

TextureAtlas makeProceduralTextureAtlas(std::int32_t seed) { return makeProceduralTextureAtlasImpl(seed); }

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
                const float aoValues[4] = {0.94f, 0.88f, 0.88f, 0.94f};
                addFace(mesh, p, normals[face], block, aoValues);
            }
        }
    }
    mesh.packedVertices.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) mesh.packedVertices.push_back(World::packVertex(vertex));
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
    : seed_(seed), loadRadius_(std::max(1, loadRadius)), renderRadius_(std::clamp(renderRadius, 1, loadRadius_)), atlas_(makeProceduralTextureAtlas(seed ^ 0x5A17C0DE)) { stats_.atlasBytes = atlas_.pixels.size() * sizeof(std::uint32_t); stats_.atlasTileSize = atlas_.tileSize; stats_.atlasTilesX = atlas_.tilesX; stats_.atlasTilesY = atlas_.tilesY; }

void World::setRenderDistance(int chunks) {
    renderRadius_ = std::clamp(chunks, 8, 16);
    loadRadius_ = streamingRadiusFor(renderRadius_);
    ++streamingToken_;
}

bool World::setRenderDistanceChecked(int chunks) {
    if (chunks != 8 && chunks != 16) return false;
    setRenderDistance(chunks);
    return true;
}

ChunkState World::chunkState(ChunkCoord coordinate) const {
    const auto found = chunkStates_.find(coordinate);
    return found == chunkStates_.end() ? ChunkState::Unloaded : found->second;
}

PackedVertex World::packVertex(const Vertex& vertex) {
    const auto quantize = [](float value) { return static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(value)) + 512, 0, 1023)); };
    const auto normalCode = [&vertex]() { int code = 0; if (vertex.normal[0] > 0.5f) code = 1; else if (vertex.normal[0] < -0.5f) code = 2; else if (vertex.normal[1] > 0.5f) code = 3; else if (vertex.normal[1] < -0.5f) code = 4; else if (vertex.normal[2] > 0.5f) code = 5; return static_cast<std::uint32_t>(code); };
    const std::uint32_t x = quantize(vertex.position[0]); const std::uint32_t y = quantize(vertex.position[1]); const std::uint32_t z = quantize(vertex.position[2]);
    PackedVertex packed{}; packed.position = x | (y << 10u) | (z << 20u); const std::uint32_t ao = std::clamp(static_cast<std::uint32_t>(std::lround((1.0f - vertex.ao) * 3.0f)), 0u, 3u); const std::uint32_t uv = (vertex.uv[0] > 0.5f ? 1u : 0u) | (vertex.uv[1] > 0.5f ? 2u : 0u); packed.attributes = normalCode() | (ao << 3u) | ((vertex.material & 7u) << 5u) | (uv << 8u); return packed;
}

Vertex World::unpackVertex(const PackedVertex& packed) {
    Vertex vertex{}; const auto decode = [](std::uint32_t value) { return static_cast<float>(static_cast<int>(value & 1023u) - 512); }; vertex.position[0] = decode(packed.position); vertex.position[1] = decode(packed.position >> 10u); vertex.position[2] = decode(packed.position >> 20u); const std::uint32_t normal = packed.attributes & 7u; if (normal == 1) vertex.normal[0] = 1; else if (normal == 2) vertex.normal[0] = -1; else if (normal == 3) vertex.normal[1] = 1; else if (normal == 4) vertex.normal[1] = -1; else if (normal == 5) vertex.normal[2] = 1; else vertex.normal[2] = -1; vertex.ao = 1.0f - static_cast<float>((packed.attributes >> 3u) & 3u) / 3.0f; vertex.material = (packed.attributes >> 5u) & 7u; const std::uint32_t uv = (packed.attributes >> 8u) & 3u; vertex.uv[0] = (uv & 1u) ? 1.0f : 0.0f; vertex.uv[1] = (uv & 2u) ? 1.0f : 0.0f; const float shade = vertex.material == static_cast<std::uint32_t>(BlockType::Grass) ? 0.78f : (vertex.material == static_cast<std::uint32_t>(BlockType::Stone) ? 0.56f : 0.68f); vertex.color[0] = shade; vertex.color[1] = shade; vertex.color[2] = shade; vertex.color[3] = 1.0f; return vertex;
}

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

void World::pollGenerationJobs(ChunkCoord center) {
    for (auto iterator = pendingGeneration_.begin(); iterator != pendingGeneration_.end();) {
        if (iterator->future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) { ++iterator; continue; }
        const double latency = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - iterator->submitted).count(); stats_.averageJobLatencyMs = stats_.workerJobsCompleted == 0 ? latency : (stats_.averageJobLatencyMs * 0.9 + latency * 0.1); stats_.maxJobLatencyMs = std::max(stats_.maxJobLatencyMs, latency);
        std::unique_ptr<Chunk> chunk = iterator->future.get(); const ChunkCoord coordinate = iterator->coordinate; const int dx = coordinate.x - center.x; const int dz = coordinate.z - center.z;
        if (dx * dx + dz * dz <= loadRadius_ * loadRadius_) { chunks_[coordinate] = std::move(chunk); chunkStates_[coordinate] = ChunkState::Generated; meshCache_.erase(coordinate); meshCache_.erase({coordinate.x - 1, coordinate.z}); meshCache_.erase({coordinate.x + 1, coordinate.z}); meshCache_.erase({coordinate.x, coordinate.z - 1}); meshCache_.erase({coordinate.x, coordinate.z + 1}); ++stats_.generatedChunks; ++stats_.workerJobsCompleted; } else { ++stats_.workerJobsCancelled; }
        iterator = pendingGeneration_.erase(iterator);
    }
}

void World::pollMeshJobs() {
    for (auto iterator = pendingMesh_.begin(); iterator != pendingMesh_.end();) {
        if (iterator->future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) { ++iterator; continue; }
        Mesh mesh = iterator->future.get(); if (iterator->editEpoch == meshEpoch_[iterator->coordinate]) { meshCache_[iterator->coordinate] = std::move(mesh); meshLod_[iterator->coordinate] = iterator->lod; chunkStates_[iterator->coordinate] = ChunkState::MeshReady; ++stats_.workerJobsCompleted; } else ++stats_.workerJobsCancelled; iterator = pendingMesh_.erase(iterator);
    }
}

void World::scheduleGenerationJobs(ChunkCoord center) {
    const std::size_t maxOutstanding = 64;
    struct Candidate { ChunkCoord coordinate{}; int priority{}; int distanceSquared{}; };
    std::vector<Candidate> candidates;
    for (int z = center.z - loadRadius_; z <= center.z + loadRadius_; ++z) for (int x = center.x - loadRadius_; x <= center.x + loadRadius_; ++x) {
        const int dx = x - center.x; const int dz = z - center.z; const int distanceSquared = dx * dx + dz * dz; if (distanceSquared > loadRadius_ * loadRadius_) continue; const ChunkCoord coordinate{x, z}; if (chunks_.find(coordinate) != chunks_.end() || cachedChunks_.find(coordinate) != cachedChunks_.end()) continue; const auto state = chunkStates_.find(coordinate); if (state != chunkStates_.end() && state->second != ChunkState::Evict && state->second != ChunkState::Unloaded) continue; bool pending = false; for (const auto& job : pendingGeneration_) if (job.coordinate == coordinate) { pending = true; break; } if (pending) continue;
        candidates.push_back({coordinate, distanceSquared <= 16 ? 0 : (distanceSquared <= 64 ? 1 : 2), distanceSquared});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) { if (left.priority != right.priority) return left.priority < right.priority; return left.distanceSquared < right.distanceSquared; });
    for (const auto& candidate : candidates) {
        if (pendingGeneration_.size() >= maxOutstanding) break;
        const auto coordinate = candidate.coordinate; chunkStates_[coordinate] = ChunkState::Loading;
        pendingGeneration_.push_back({coordinate, chunkJobPool().submit(candidate.priority, [seed = seed_, coordinate] { return std::make_unique<Chunk>(seed, coordinate); }), std::chrono::steady_clock::now()});
    }
}

void World::evictCacheIfNeeded() {
    constexpr std::size_t maxCached = 64;
    while (cachedChunks_.size() > maxCached && !cacheOrder_.empty()) { const ChunkCoord coordinate = cacheOrder_.front(); cacheOrder_.pop_front(); if (cachedChunks_.erase(coordinate)) { chunkStates_[coordinate] = ChunkState::Evict; ++stats_.cacheEvictions; } }
}

void World::updateStreaming(float playerX, float playerZ) {
    const auto streamingStart = std::chrono::steady_clock::now();
    const ChunkCoord center = chunkCoordFor(static_cast<int>(std::floor(playerX)), static_cast<int>(std::floor(playerZ)));
    stats_.generatedChunks = 0; stats_.unloadedChunks = 0; stats_.queueHigh = 0; stats_.queueMedium = 0; stats_.queueLow = pendingGeneration_.size();
    pollGenerationJobs(center); pollMeshJobs();
    const auto invalidateBoundary = [this](ChunkCoord coordinate) {
        meshCache_.erase(coordinate);
        meshCache_.erase({coordinate.x - 1, coordinate.z});
        meshCache_.erase({coordinate.x + 1, coordinate.z});
        meshCache_.erase({coordinate.x, coordinate.z - 1});
        meshCache_.erase({coordinate.x, coordinate.z + 1});
    };
    for (int z = center.z - loadRadius_; z <= center.z + loadRadius_; ++z) for (int x = center.x - loadRadius_; x <= center.x + loadRadius_; ++x) { const int dx=x-center.x, dz=z-center.z; if (dx*dx+dz*dz > loadRadius_*loadRadius_) continue; const ChunkCoord coordinate{x,z}; auto cached = cachedChunks_.find(coordinate); if (cached != cachedChunks_.end()) { chunks_[coordinate] = std::move(cached->second); cachedChunks_.erase(cached); chunkStates_[coordinate] = ChunkState::Generated; ++stats_.cacheHits; } }
    scheduleGenerationJobs(center);
    stats_.generationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - streamingStart).count();
    for (auto iterator = chunks_.begin(); iterator != chunks_.end();) {
        const int dx = iterator->first.x - center.x;
        const int dz = iterator->first.z - center.z;
        if (dx * dx + dz * dz > loadRadius_ * loadRadius_) {
            const ChunkCoord coordinate = iterator->first; chunkStates_[coordinate] = ChunkState::Inactive; cachedChunks_[coordinate] = std::move(iterator->second); cacheOrder_.push_back(coordinate); iterator = chunks_.erase(iterator); ++stats_.unloadedChunks;
        } else ++iterator;
    }
    evictCacheIfNeeded(); stats_.loadedChunks = chunks_.size(); stats_.activeChunks = chunks_.size(); stats_.cachedChunks = cachedChunks_.size();
    stats_.streamingMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - streamingStart).count();
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
    const ChunkCoord neighbors[] = {{coordinate.x, coordinate.z}, {coordinate.x - 1, coordinate.z}, {coordinate.x + 1, coordinate.z}, {coordinate.x, coordinate.z - 1}, {coordinate.x, coordinate.z + 1}};
    for (const auto neighbor : neighbors) { meshCache_.erase(neighbor); ++meshEpoch_[neighbor]; }
}

bool World::isResident(int worldX, int worldZ) const { return findChunk(chunkCoordFor(worldX, worldZ)) != nullptr; }

Mesh World::buildRenderMesh(float playerX, float playerZ, const std::optional<Frustum>& frustum) {
    return buildRenderMeshInternal(playerX, playerZ, 0.0f, 0.0f, frustum);
}

Mesh World::buildRenderMesh(float playerX, float playerZ, float lookX, float lookZ, const std::optional<Frustum>& frustum) {
    return buildRenderMeshInternal(playerX, playerZ, lookX, lookZ, frustum);
}

Mesh World::buildRenderMeshInternal(float playerX, float playerZ, float lookX, float lookZ, const std::optional<Frustum>& frustum) {
    const auto meshingStart = std::chrono::steady_clock::now();
    Mesh result;
    pollMeshJobs(); stats_.meshedChunks = stats_.visibleChunks = stats_.distanceCulledChunks = stats_.frustumCulledChunks = 0; stats_.fullLodChunks = stats_.simplifiedLodChunks = stats_.farTerrainChunks = 0; stats_.occlusionTested = stats_.occlusionCulled = 0;
    const ChunkCoord center = chunkCoordFor(static_cast<int>(std::floor(playerX)), static_cast<int>(std::floor(playerZ)));
    float lookLength = std::sqrt(lookX * lookX + lookZ * lookZ);
    if (lookLength > 0.0001f) { lookX /= lookLength; lookZ /= lookLength; }
    auto snapshot = std::make_shared<std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash>>(); for (const auto& entry : chunks_) snapshot->emplace(entry.first, *entry.second);
    const auto sample = [snapshot](int x, int y, int z) { if (y < 0 || y >= ChunkHeight) return BlockType::Air; const auto floorDiv = [](int value, int divisor) { return value >= 0 ? value / divisor : -(((-value) + divisor - 1) / divisor); }; const ChunkCoord coordinate{floorDiv(x, ChunkSize), floorDiv(z, ChunkSize)}; const auto found = snapshot->find(coordinate); if (found == snapshot->end()) return BlockType::Air; return found->second.get(x - coordinate.x * ChunkSize, y, z - coordinate.z * ChunkSize); };
    for (const auto& entry : chunks_) {
        const ChunkCoord coordinate = entry.first;
        const int dx = coordinate.x - center.x;
        const int dz = coordinate.z - center.z;
        if (dx * dx + dz * dz > renderRadius_ * renderRadius_) { ++stats_.distanceCulledChunks; continue; }
        const std::array<float, 3> minimum = {static_cast<float>(coordinate.x * ChunkSize), 0.0f, static_cast<float>(coordinate.z * ChunkSize)};
        const std::array<float, 3> maximum = {minimum[0] + ChunkSize, static_cast<float>(ChunkHeight), minimum[2] + ChunkSize};
        const int distance = std::max(std::abs(dx), std::abs(dz)); const MeshLod lod = static_cast<MeshLod>(lodForDistance(distance, renderRadius_));
        auto cached = meshCache_.find(coordinate); if (cached == meshCache_.end() || meshLod_[coordinate] != lod) {
            bool alreadyPending = false; for (const auto& job : pendingMesh_) if (job.coordinate == coordinate && job.lod == lod) { alreadyPending = true; break; }
            if (!alreadyPending) { chunkStates_[coordinate] = ChunkState::Meshing; Chunk copy = *entry.second; const float centerX = static_cast<float>(coordinate.x * ChunkSize + ChunkSize / 2) - playerX; const float centerZ = static_cast<float>(coordinate.z * ChunkSize + ChunkSize / 2) - playerZ; const float facing = lookLength > 0.0001f ? centerX * lookX + centerZ * lookZ : 0.0f; const int priority = distance <= 4 ? 0 : (distance <= 8 ? 1 : (facing > 0.0f ? 2 : 3)); const auto epoch = meshEpoch_[coordinate]; pendingMesh_.push_back({coordinate, lod, epoch, chunkJobPool().submit(priority, [copy = std::move(copy), lod, sample] { return buildLodMesh(copy, lod, sample); }), std::chrono::steady_clock::now()}); }
            ++stats_.cacheMisses;
        } else if (!frustum || intersects(*frustum, minimum, maximum)) { appendMesh(result, cached->second); ++stats_.cacheHits; ++stats_.visibleChunks; if (lod == MeshLod::FullVoxel) ++stats_.fullLodChunks; else if (lod == MeshLod::SimplifiedVoxel) ++stats_.simplifiedLodChunks; else ++stats_.farTerrainChunks; }
        else { ++stats_.frustumCulledChunks; }
        ++stats_.meshedChunks;
    }
    stats_.loadedChunks = chunks_.size(); stats_.activeChunks = chunks_.size(); stats_.cachedChunks = cachedChunks_.size(); stats_.unpackedVertexBytes = result.vertices.size() * sizeof(Vertex); stats_.packedVertexBytes = result.packedVertices.size() * sizeof(PackedVertex); stats_.aoVertices = result.vertices.size();
    stats_.meshingMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - meshingStart).count();
    return result;
}

RayHit World::raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction, float maxDistance) const {
    return raycastSample([this](int x, int y, int z) { return get(x, y, z); }, origin, direction, maxDistance);
}

} // namespace directcraft::voxel
