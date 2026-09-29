#include "gameplay/camera.h"

#include <algorithm>
#include <cmath>

namespace directcraft::gameplay {

void applyMouseDelta(CameraState& camera, const std::array<float, 2>& rawDelta, float sensitivity) {
    camera.yaw = std::remainder(camera.yaw + rawDelta[0] * sensitivity, 6.28318530717958647692f);
    camera.pitch = std::clamp(camera.pitch - rawDelta[1] * sensitivity, -1.45f, 1.45f);
}

std::array<float, 3> forward(const CameraState& camera) {
    const float cosine = std::cos(camera.pitch);
    return {std::sin(camera.yaw) * cosine, std::sin(camera.pitch), std::cos(camera.yaw) * cosine};
}

std::array<float, 3> right(const CameraState& camera) {
    const auto direction = forward(camera);
    const float length = std::sqrt(direction[0] * direction[0] + direction[2] * direction[2]);
    if (length < 0.0001f) return {1.0f, 0.0f, 0.0f};
    return {direction[2] / length, 0.0f, -direction[0] / length};
}

} // namespace directcraft::gameplay
