#include "gameplay/physics.h"

#include <cmath>
#include <iostream>

using directcraft::gameplay::PhysicsConfig;
using directcraft::gameplay::PhysicsState;
using directcraft::voxel::BlockType;
using directcraft::voxel::Chunk;

int main() {
    Chunk chunk(2026);
    PhysicsConfig config;
    const PhysicsState spawn = directcraft::gameplay::spawnAtCenter(chunk, config);
    if (std::abs(spawn.position[0] - 7.5f) > 0.001f || std::abs(spawn.position[2] - 7.5f) > 0.001f) return 1;

    PhysicsState falling = spawn;
    falling.position[1] += 4.0f;
    for (int frame = 0; frame < 180 && !falling.grounded; ++frame) directcraft::gameplay::simulate(falling, config, chunk, {0.0f, 0.0f, 0.0f}, 1.0f / 60.0f, false);
    if (!falling.grounded || falling.velocity[1] != 0.0f) return 2;

    chunk.set(4, 20, 2, BlockType::Stone);
    PhysicsState wall{}; wall.position = {3.5f, 20.0f, 2.5f}; wall.grounded = true;
    bool touchedWall = false;
    for (int frame = 0; frame < 30; ++frame) { directcraft::gameplay::simulate(wall, config, chunk, {1.0f, 0.0f, 0.0f}, 1.0f / 60.0f, false); touchedWall = touchedWall || wall.hitWall; }
    if (!touchedWall) return 3;
    PhysicsState acceleration = spawn; directcraft::gameplay::simulate(acceleration, config, chunk, {1.0f, 0.0f, 0.0f}, 1.0f / 60.0f, false);
    if (acceleration.velocity[0] <= 0.0f) return 4;
    std::cout << "DirectCraft physics tests passed\n";
    return 0;
}
