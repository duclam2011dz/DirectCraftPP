#include "gameplay/physics.h"

#include <algorithm>
#include <cmath>

namespace directcraft::gameplay {
namespace {
bool solidAt(const voxel::Chunk& chunk, int x, int y, int z) {
    return chunk.get(x, y, z) != voxel::BlockType::Air;
}

bool overlapsBlock(const Aabb& box, int x, int y, int z) {
    return box.minimum[0] < x + 1.0f && box.maximum[0] > x &&
           box.minimum[1] < y + 1.0f && box.maximum[1] > y &&
           box.minimum[2] < z + 1.0f && box.maximum[2] > z;
}

using SolidQuery = std::function<bool(int, int, int)>;

void moveAxis(PhysicsState& state, const PhysicsConfig& config, const SolidQuery& solidAt,
              int axis, float amount) {
    state.position[axis] += amount;
    const Aabb box = playerAabb(state, config);
    const int minX = static_cast<int>(std::floor(box.minimum[0])) - 1;
    const int maxX = static_cast<int>(std::floor(box.maximum[0])) + 1;
    const int minY = std::max(0, static_cast<int>(std::floor(box.minimum[1])) - 1);
    const int maxY = static_cast<int>(std::floor(box.maximum[1])) + 1;
    const int minZ = static_cast<int>(std::floor(box.minimum[2])) - 1;
    const int maxZ = static_cast<int>(std::floor(box.maximum[2])) + 1;
    for (int y = minY; y <= maxY; ++y) for (int z = minZ; z <= maxZ; ++z) for (int x = minX; x <= maxX; ++x) {
        if (!solidAt(x, y, z) || !overlapsBlock(box, x, y, z)) continue;
        if (axis == 0) state.position[0] = amount > 0.0f ? x - config.width * 0.5f : x + 1.0f + config.width * 0.5f;
        if (axis == 1) {
            if (amount < 0.0f) { state.position[1] = y + 1.0f; state.grounded = true; }
            else { state.position[1] = y - config.height; state.hitCeiling = true; }
        }
        if (axis == 2) state.position[2] = amount > 0.0f ? z - config.depth * 0.5f : z + 1.0f + config.depth * 0.5f;
        state.velocity[axis] = 0.0f;
        if (axis != 1) state.hitWall = true;
    }
}

float approach(float current, float target, float amount) {
    if (current < target) return std::min(current + amount, target);
    return std::max(current - amount, target);
}
}

Aabb playerAabb(const PhysicsState& state, const PhysicsConfig& config) {
    return {{state.position[0] - config.width * 0.5f, state.position[1], state.position[2] - config.depth * 0.5f},
            {state.position[0] + config.width * 0.5f, state.position[1] + config.height, state.position[2] + config.depth * 0.5f}};
}

bool overlaps(const Aabb& left, const Aabb& right) {
    return left.minimum[0] < right.maximum[0] && left.maximum[0] > right.minimum[0] &&
           left.minimum[1] < right.maximum[1] && left.maximum[1] > right.minimum[1] &&
           left.minimum[2] < right.maximum[2] && left.maximum[2] > right.minimum[2];
}

int surfaceHeight(const voxel::Chunk& chunk, int x, int z) {
    for (int y = voxel::ChunkHeight - 1; y >= 0; --y) if (chunk.get(x, y, z) != voxel::BlockType::Air) return y;
    return 0;
}

PhysicsState spawnAtCenter(const voxel::Chunk& chunk, const PhysicsConfig&) {
    PhysicsState state;
    state.position = {7.5f, static_cast<float>(surfaceHeight(chunk, 7, 7)) + 1.001f, 7.5f};
    return state;
}

void simulate(PhysicsState& state, const PhysicsConfig& config, const voxel::Chunk& chunk,
              const std::array<float, 3>& wishDirection, float deltaSeconds, bool jump) {
    const SolidQuery solidAt = [&chunk](int x, int y, int z) { return chunk.get(x, y, z) != voxel::BlockType::Air; };
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.05f);
    state.acceleration = {wishDirection[0] * config.acceleration, 0.0f, wishDirection[2] * config.acceleration};
    const float wishX = wishDirection[0] * config.maxSpeed;
    const float wishZ = wishDirection[2] * config.maxSpeed;
    if (std::abs(wishX) > 0.001f || std::abs(wishZ) > 0.001f) {
        state.velocity[0] = approach(state.velocity[0], wishX, config.acceleration * dt);
        state.velocity[2] = approach(state.velocity[2], wishZ, config.acceleration * dt);
    } else {
        state.velocity[0] = approach(state.velocity[0], 0.0f, config.friction * dt);
        state.velocity[2] = approach(state.velocity[2], 0.0f, config.friction * dt);
    }
    if (state.grounded && jump) { state.velocity[1] = config.jumpVelocity; state.grounded = false; }
    state.velocity[1] -= config.gravity * dt;
    state.hitCeiling = false; state.hitWall = false; state.grounded = false;
    moveAxis(state, config, solidAt, 0, state.velocity[0] * dt);
    moveAxis(state, config, solidAt, 2, state.velocity[2] * dt);
    moveAxis(state, config, solidAt, 1, state.velocity[1] * dt);
}

int surfaceHeight(const voxel::World& world, int x, int z) {
    for (int y = voxel::ChunkHeight - 1; y >= 0; --y) if (world.get(x, y, z) != voxel::BlockType::Air) return y;
    return 0;
}

PhysicsState spawnAtCenter(const voxel::World& world, const PhysicsConfig&) {
    PhysicsState state;
    state.position = {7.5f, static_cast<float>(surfaceHeight(world, 7, 7)) + 1.001f, 7.5f};
    return state;
}

void simulate(PhysicsState& state, const PhysicsConfig& config, const voxel::World& world,
              const std::array<float, 3>& wishDirection, float deltaSeconds, bool jump) {
    const SolidQuery solidAt = [&world](int x, int y, int z) { return world.get(x, y, z) != voxel::BlockType::Air; };
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.05f);
    state.acceleration = {wishDirection[0] * config.acceleration, 0.0f, wishDirection[2] * config.acceleration};
    const float wishX = wishDirection[0] * config.maxSpeed;
    const float wishZ = wishDirection[2] * config.maxSpeed;
    if (std::abs(wishX) > 0.001f || std::abs(wishZ) > 0.001f) {
        state.velocity[0] = approach(state.velocity[0], wishX, config.acceleration * dt);
        state.velocity[2] = approach(state.velocity[2], wishZ, config.acceleration * dt);
    } else {
        state.velocity[0] = approach(state.velocity[0], 0.0f, config.friction * dt);
        state.velocity[2] = approach(state.velocity[2], 0.0f, config.friction * dt);
    }
    if (state.grounded && jump) { state.velocity[1] = config.jumpVelocity; state.grounded = false; }
    state.velocity[1] -= config.gravity * dt;
    state.hitCeiling = false; state.hitWall = false; state.grounded = false;
    moveAxis(state, config, solidAt, 0, state.velocity[0] * dt);
    moveAxis(state, config, solidAt, 2, state.velocity[2] * dt);
    moveAxis(state, config, solidAt, 1, state.velocity[1] * dt);
}
} // namespace directcraft::gameplay
