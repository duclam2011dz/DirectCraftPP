#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
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

struct ChunkCoord {
    int x{};
    int z{};
    friend bool operator==(const ChunkCoord& left, const ChunkCoord& right) { return left.x == right.x && left.z == right.z; }
    friend bool operator!=(const ChunkCoord& left, const ChunkCoord& right) { return !(left == right); }
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& value) const noexcept {
        const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(value.x));
        const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(value.z));
        return static_cast<std::size_t>((x * 0x9e3779b97f4a7c15ull) ^ (z + 0x9e3779b97f4a7c15ull + (x << 6u) + (x >> 2u)));
    }
};

struct Int3Hash {
    std::size_t operator()(const Int3& value) const noexcept {
        const auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(value.x));
        const auto y = static_cast<std::uint64_t>(static_cast<std::int64_t>(value.y));
        const auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(value.z));
        return static_cast<std::size_t>((x * 73856093ull) ^ (y * 19349663ull) ^ (z * 83492791ull));
    }
};

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
    float ao{1.0f};
    std::uint32_t material{};
};

struct PackedVertex {
    std::uint32_t position{};
    std::uint32_t attributes{};
};

struct TextureAtlas {
    int tileSize{16};
    int tilesX{4};
    int tilesY{2};
    std::vector<std::uint32_t> pixels;
};

TextureAtlas makeProceduralTextureAtlas(std::int32_t seed = 1337);

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<PackedVertex> packedVertices;
};

enum class ChunkState : std::uint8_t { Unloaded, Loading, Generated, Meshing, MeshReady, Uploading, Ready, Inactive, Cache, Evict };
enum class MeshLod : std::uint8_t { FullVoxel, SimplifiedVoxel, FarTerrain };

class Chunk {
public:
    explicit Chunk(std::int32_t seed = 1337, ChunkCoord coordinate = {});

    BlockType get(int x, int y, int z) const;
    void set(int x, int y, int z, BlockType block);
    void generate();
    Mesh buildMesh() const;
    Mesh buildMesh(const std::function<BlockType(int, int, int)>& sample) const;
    ChunkCoord coordinate() const { return coordinate_; }
    int worldX() const { return coordinate_.x * ChunkSize; }
    int worldZ() const { return coordinate_.z * ChunkSize; }

private:
    static std::size_t index(int x, int y, int z);
    static bool inBounds(int x, int y, int z);
    int heightAt(int x, int z) const;

    std::int32_t seed_;
    ChunkCoord coordinate_;
    std::array<BlockType, ChunkSize * ChunkHeight * ChunkSize> blocks_{};
};

struct RayHit {
    bool hit{false};
    Int3 block{};
    Int3 previous{};
    Int3 normal{};
    float distance{};
};

RayHit raycast(const Chunk& chunk, const std::array<float, 3>& origin,
               const std::array<float, 3>& direction, float maxDistance);

struct RenderStats {
    std::size_t loadedChunks{};
    std::size_t meshedChunks{};
    std::size_t visibleChunks{};
    std::size_t distanceCulledChunks{};
    std::size_t frustumCulledChunks{};
    std::size_t generatedChunks{};
    std::size_t unloadedChunks{};
    double streamingMs{};
    double generationMs{};
    double meshingMs{};
    std::size_t fullLodChunks{};
    std::size_t simplifiedLodChunks{};
    std::size_t farTerrainChunks{};
    std::size_t occlusionTested{};
    std::size_t occlusionCulled{};
    std::size_t occlusionFallbacks{};
    std::size_t queueHigh{};
    std::size_t queueMedium{};
    std::size_t queueLow{};
    std::size_t cacheHits{};
    std::size_t cacheMisses{};
    std::size_t cacheEvictions{};
    std::size_t activeChunks{};
    std::size_t cachedChunks{};
    std::size_t aoVertices{};
    std::size_t packedVertexBytes{};
    std::size_t unpackedVertexBytes{};
    std::size_t atlasBytes{};
    int atlasTileSize{16};
    int atlasTilesX{4};
    int atlasTilesY{2};
    std::size_t workerJobsCompleted{};
    std::size_t workerJobsCancelled{};
    double averageJobLatencyMs{};
    double maxJobLatencyMs{};
};

struct FrustumPlane { std::array<float, 4> equation{}; };
struct Frustum { std::array<FrustumPlane, 6> planes{}; };

Frustum makeFrustum(const std::array<float, 16>& rowMajorViewProjection);
bool intersects(const Frustum& frustum, const std::array<float, 3>& minimum, const std::array<float, 3>& maximum);

class World {
public:
    explicit World(std::int32_t seed = 1337, int loadRadius = 9, int renderRadius = 8);

    void updateStreaming(float playerX, float playerZ);
    BlockType get(int worldX, int worldY, int worldZ) const;
    void set(int worldX, int worldY, int worldZ, BlockType block);
    bool isResident(int worldX, int worldZ) const;
    Mesh buildRenderMesh(float playerX, float playerZ, const std::optional<Frustum>& frustum = std::nullopt);
    Mesh buildRenderMesh(float playerX, float playerZ, float lookX, float lookZ, const std::optional<Frustum>& frustum = std::nullopt);
    RayHit raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction, float maxDistance) const;
    ChunkCoord chunkCoordFor(int worldX, int worldZ) const;
    const RenderStats& stats() const { return stats_; }
    std::size_t editedBlockCount() const { return edits_.size(); }
    const TextureAtlas& textureAtlas() const { return atlas_; }
    std::int32_t seed() const { return seed_; }
    int loadRadius() const { return loadRadius_; }
    int renderRadius() const { return renderRadius_; }
    void setRenderDistance(int chunks);
    bool setRenderDistanceChecked(int chunks);
    static int streamingRadiusFor(int renderDistance) { return renderDistance + 1; }
    ChunkState chunkState(ChunkCoord coordinate) const;
    static PackedVertex packVertex(const Vertex& vertex);
    static Vertex unpackVertex(const PackedVertex& vertex);

private:
    static int floorDiv(int value, int divisor);
    Chunk* findChunk(ChunkCoord coordinate);
    const Chunk* findChunk(ChunkCoord coordinate) const;
    void pollGenerationJobs(ChunkCoord center);
    void pollMeshJobs();
    void scheduleGenerationJobs(ChunkCoord center);
    void evictCacheIfNeeded();
    Mesh buildRenderMeshInternal(float playerX, float playerZ, float lookX, float lookZ, const std::optional<Frustum>& frustum);

    std::int32_t seed_;
    int loadRadius_;
    int renderRadius_;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks_;
    std::unordered_map<ChunkCoord, Mesh, ChunkCoordHash> meshCache_;
    std::unordered_map<ChunkCoord, MeshLod, ChunkCoordHash> meshLod_;
    std::unordered_map<ChunkCoord, ChunkState, ChunkCoordHash> chunkStates_;
    struct PendingGeneration { ChunkCoord coordinate{}; std::future<std::unique_ptr<Chunk>> future; std::chrono::steady_clock::time_point submitted{}; };
    struct PendingMesh { ChunkCoord coordinate{}; MeshLod lod{}; std::uint64_t editEpoch{}; std::future<Mesh> future; std::chrono::steady_clock::time_point submitted{}; };
    std::vector<PendingGeneration> pendingGeneration_;
    std::vector<PendingMesh> pendingMesh_;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> cachedChunks_;
    std::deque<ChunkCoord> cacheOrder_;
    std::unordered_map<Int3, BlockType, Int3Hash> edits_;
    RenderStats stats_{};
    std::uint64_t streamingToken_{};
    std::unordered_map<ChunkCoord, std::uint64_t, ChunkCoordHash> meshEpoch_;
    TextureAtlas atlas_;
};

} // namespace directcraft::voxel
