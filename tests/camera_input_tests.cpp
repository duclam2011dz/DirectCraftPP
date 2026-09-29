#include "gameplay/camera.h"

#include <cmath>
#include <iostream>

int main() {
    directcraft::gameplay::CameraState camera{};
    directcraft::gameplay::applyMouseDelta(camera, {40.0f, -10.0f}, 0.0025f);
    if (std::abs(camera.yaw - 0.1f) > 0.00001f || std::abs(camera.pitch - 0.025f) > 0.00001f) return 1;
    const auto direction = directcraft::gameplay::forward(camera);
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (std::abs(length - 1.0f) > 0.00001f) return 2;
    directcraft::gameplay::applyMouseDelta(camera, {0.0f, 10000.0f}, 0.0025f);
    if (camera.pitch < -1.45f || camera.pitch > 1.45f) return 3;
    std::cout << "DirectCraft camera input tests passed\n";
    return 0;
}
