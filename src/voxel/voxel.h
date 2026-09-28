#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace directcraft::voxel {

constexpr int ChunkSize = 16;
constexpr int ChunkHeight = 32;

enum class BlockType : std::uint8_t { Air, Grass, Dirt, Stone, Sand, Water };

struct Int3 {
    int x{};
    int y{};
    int z{};
    friend bool operator==(const Int3& left, const Int3& right) {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }
};

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

class Chunk {
public:
    explicit Chunk(std::int32_t seed = 1337);

    BlockType get(int x, int y, int z) const;
    void set(int x, int y, int z, BlockType block);
    void generate();
    Mesh buildMesh() const;

private:
    static std::size_t index(int x, int y, int z);
    static bool inBounds(int x, int y, int z);
    int heightAt(int x, int z) const;

    std::int32_t seed_;
    std::array<BlockType, ChunkSize * ChunkHeight * ChunkSize> blocks_{};
};

struct RayHit {
    bool hit{false};
    Int3 block{};
    Int3 previous{};
    float distance{};
};

RayHit raycast(const Chunk& chunk, const std::array<float, 3>& origin,
               const std::array<float, 3>& direction, float maxDistance);

} // namespace directcraft::voxel



