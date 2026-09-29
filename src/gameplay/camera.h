#pragma once

#include <array>

namespace directcraft::gameplay {

struct CameraState {
    float yaw{};
    float pitch{};
};

void applyMouseDelta(CameraState& camera, const std::array<float, 2>& rawDelta, float sensitivity);
std::array<float, 3> forward(const CameraState& camera);
std::array<float, 3> right(const CameraState& camera);

} // namespace directcraft::gameplay
