#include "voxel/voxel.h"

#include <cmath>
#include <iostream>

using directcraft::voxel::BlockType;
using directcraft::voxel::Chunk;

int main() {
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
    std::cout << "DirectCraft unit tests passed\n";
    return 0;
}

