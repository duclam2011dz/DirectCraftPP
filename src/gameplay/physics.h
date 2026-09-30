#pragma once

#include "voxel/voxel.h"

#include <array>

namespace directcraft::gameplay {

struct Aabb {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

struct PhysicsState {
    std::array<float, 3> position{7.5f, 0.0f, 7.5f};
    std::array<float, 3> velocity{};
    std::array<float, 3> acceleration{};
    bool grounded{};
    bool hitCeiling{};
    bool hitWall{};
};

struct PhysicsConfig {
    float width{0.6f};
    float height{1.8f};
    float depth{0.6f};
    float maxSpeed{4.3f};
    float acceleration{18.0f};
    float friction{14.0f};
    float gravity{20.0f};
    float jumpVelocity{7.0f};
};

Aabb playerAabb(const PhysicsState& state, const PhysicsConfig& config);
bool overlaps(const Aabb& left, const Aabb& right);
int surfaceHeight(const voxel::Chunk& chunk, int x, int z);
PhysicsState spawnAtCenter(const voxel::Chunk& chunk, const PhysicsConfig& config);
void simulate(PhysicsState& state, const PhysicsConfig& config, const voxel::Chunk& chunk,
              const std::array<float, 3>& wishDirection, float deltaSeconds, bool jump);
int surfaceHeight(const voxel::World& world, int x, int z);
PhysicsState spawnAtCenter(const voxel::World& world, const PhysicsConfig& config);
void simulate(PhysicsState& state, const PhysicsConfig& config, const voxel::World& world,
              const std::array<float, 3>& wishDirection, float deltaSeconds, bool jump);

} // namespace directcraft::gameplay
