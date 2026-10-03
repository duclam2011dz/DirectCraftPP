#include "voxel/voxel.h"

#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>

using directcraft::voxel::BlockType;
using directcraft::voxel::Chunk;

int main() {
    const auto atlasA = directcraft::voxel::makeProceduralTextureAtlas(0x5A17C0DE);
    const auto atlasB = directcraft::voxel::makeProceduralTextureAtlas(0x5A17C0DE);
    if (atlasA.tileSize != 16 || atlasA.tilesX != 4 || atlasA.tilesY != 2 || atlasA.pixels != atlasB.pixels) return 10;
    Chunk first(2026);
    Chunk second(2026);
    if (first.get(0, 0, 0) == BlockType::Air) return 1;
    if (first.buildMesh().indices.empty()) return 2;
    for (int y = 0; y < directcraft::voxel::ChunkHeight; ++y)
        for (int z = 0; z < directcraft::voxel::ChunkSize; ++z)
            for (int x = 0; x < directcraft::voxel::ChunkSize; ++x)
                if (first.get(x, y, z) != second.get(x, y, z)) return 3;
    first.set(2, 20, 2, BlockType::Stone);
    if (first.get(2, 20, 2) != BlockType::Stone) return 4;
    const auto hit = directcraft::voxel::raycast(first, {2.5f, 20.5f, -2.0f}, {0.0f, 0.0f, 1.0f}, 8.0f);
    if (!hit.hit || hit.block.x != 2 || hit.block.y != 20) return 5;
    const auto mesh = first.buildMesh();
    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
        const auto& a = mesh.vertices[mesh.indices[index]];
        const auto& b = mesh.vertices[mesh.indices[index + 1]];
        const auto& c = mesh.vertices[mesh.indices[index + 2]];
        const float ab[3] = {b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]};
        const float ac[3] = {c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]};
        const float normal[3] = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
        const float dot = normal[0] * a.normal[0] + normal[1] * a.normal[1] + normal[2] * a.normal[2];
        if (!(dot > 0.0f)) return 6;
    }
    directcraft::voxel::World world(2026, 9, 8);
    if (world.renderRadius() != 8 || world.loadRadius() != 9) return 11;
    if (!world.setRenderDistanceChecked(16) || world.renderRadius() != 16 || world.loadRadius() != 17) return 12;
    if (world.setRenderDistanceChecked(12)) return 13;
    world.setRenderDistance(8);
    for (int attempt = 0; attempt < 200 && world.stats().loadedChunks < 4; ++attempt) { world.updateStreaming(7.5f, 7.5f); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    if (world.stats().loadedChunks < 4 || !world.isResident(16, 0)) return 7;
    world.set(17, 20, 0, BlockType::Stone);
    const auto crossChunkHit = world.raycast({15.5f, 20.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 8.0f);
    if (!crossChunkHit.hit || crossChunkHit.block.x != 17 || crossChunkHit.previous.x != 16 || crossChunkHit.normal.x != -1) return 8;
    world.set(17, 20, 0, BlockType::Air);
    if (world.get(17, 20, 0) != BlockType::Air) return 9;
    std::cout << "DirectCraft unit tests passed\n";
    return 0;
}
