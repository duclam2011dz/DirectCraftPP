#include "voxel/voxel.h"

#include <algorithm>
#include <cmath>

namespace directcraft::voxel {
namespace {

float hashNoise(int x, int z, std::int32_t seed) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u;
    value ^= static_cast<std::uint32_t>(z) * 668265263u;
    value ^= static_cast<std::uint32_t>(seed) * 2246822519u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    value ^= value >> 16u;
    return static_cast<float>(value & 0xffffu) / 65535.0f;
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
        vertex.uv[0] = uv[i][0]; vertex.uv[1] = uv[i][1];
        for (int channel = 0; channel < 4; ++channel) vertex.color[channel] = color[channel];
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

} // namespace

Chunk::Chunk(std::int32_t seed) : seed_(seed) { generate(); }

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
    const float broad = std::sin((x + seed_) * 0.23f) * 2.5f + std::cos((z - seed_) * 0.19f) * 2.0f;
    const float detail = (hashNoise(x, z, seed_) - 0.5f) * 2.0f;
    return std::clamp(10 + static_cast<int>(broad + detail), 3, ChunkHeight - 2);
}

void Chunk::generate() {
    blocks_.fill(BlockType::Air);
    for (int z = 0; z < ChunkSize; ++z) {
        for (int x = 0; x < ChunkSize; ++x) {
            const int height = heightAt(x, z);
            for (int y = 0; y <= height; ++y) {
                BlockType block = BlockType::Stone;
                if (y == height) block = height < 7 ? BlockType::Sand : BlockType::Grass;
                else if (y > height - 3) block = BlockType::Dirt;
                set(x, y, z, block);
            }
        }
    }
}

Mesh Chunk::buildMesh() const {
    Mesh mesh;
    static constexpr int offsets[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    static constexpr float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    for (int y = 0; y < ChunkHeight; ++y) for (int z = 0; z < ChunkSize; ++z) for (int x = 0; x < ChunkSize; ++x) {
        const BlockType block = get(x, y, z);
        if (block == BlockType::Air) continue;
        for (int face = 0; face < 6; ++face) {
            const int nx = x + offsets[face][0], ny = y + offsets[face][1], nz = z + offsets[face][2];
            if (get(nx, ny, nz) != BlockType::Air) continue;
            const float x0 = static_cast<float>(x), x1 = x0 + 1.0f;
            const float y0 = static_cast<float>(y), y1 = y0 + 1.0f;
            const float z0 = static_cast<float>(z), z1 = z0 + 1.0f;
            float points[4][3]{};
            switch (face) {
                case 0: points[0][0]=x0;points[0][1]=y0;points[0][2]=z0; points[1][0]=x1;points[1][1]=y0;points[1][2]=z0; points[2][0]=x1;points[2][1]=y1;points[2][2]=z0; points[3][0]=x0;points[3][1]=y1;points[3][2]=z0; break;
                case 1: points[0][0]=x1;points[0][1]=y0;points[0][2]=z1; points[1][0]=x0;points[1][1]=y0;points[1][2]=z1; points[2][0]=x0;points[2][1]=y1;points[2][2]=z1; points[3][0]=x1;points[3][1]=y1;points[3][2]=z1; break;
                case 2: points[0][0]=x0;points[0][1]=y0;points[0][2]=z1; points[1][0]=x0;points[1][1]=y0;points[1][2]=z0; points[2][0]=x0;points[2][1]=y1;points[2][2]=z0; points[3][0]=x0;points[3][1]=y1;points[3][2]=z1; break;
                case 3: points[0][0]=x1;points[0][1]=y0;points[0][2]=z0; points[1][0]=x1;points[1][1]=y0;points[1][2]=z1; points[2][0]=x1;points[2][1]=y1;points[2][2]=z1; points[3][0]=x1;points[3][1]=y1;points[3][2]=z0; break;
                case 4: points[0][0]=x0;points[0][1]=y0;points[0][2]=z1; points[1][0]=x1;points[1][1]=y0;points[1][2]=z1; points[2][0]=x1;points[2][1]=y0;points[2][2]=z0; points[3][0]=x0;points[3][1]=y0;points[3][2]=z0; break;
                default: points[0][0]=x0;points[0][1]=y1;points[0][2]=z0; points[1][0]=x1;points[1][1]=y1;points[1][2]=z0; points[2][0]=x1;points[2][1]=y1;points[2][2]=z1; points[3][0]=x0;points[3][1]=y1;points[3][2]=z1; break;
            }
            addFace(mesh, points, normals[face], block);
        }
    }
    return mesh;
}

RayHit raycast(const Chunk& chunk, const std::array<float, 3>& origin, const std::array<float, 3>& direction, float maxDistance) {
    RayHit result;
    result.previous = {static_cast<int>(std::floor(origin[0])), static_cast<int>(std::floor(origin[1])), static_cast<int>(std::floor(origin[2]))};
    for (float distance = 0.0f; distance <= maxDistance; distance += 0.05f) {
        const int x = static_cast<int>(std::floor(origin[0] + direction[0] * distance));
        const int y = static_cast<int>(std::floor(origin[1] + direction[1] * distance));
        const int z = static_cast<int>(std::floor(origin[2] + direction[2] * distance));
        if (chunk.get(x, y, z) != BlockType::Air) return {true, {x, y, z}, result.previous, distance};
        result.previous = {x, y, z};
    }
    return result;
}

} // namespace directcraft::voxel

