#include "platform/win32_window.h"
#include "renderer/d3d12_renderer.h"
#include "voxel/voxel.h"

#include <DirectXMath.h>
#include <windows.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace DirectX;
using directcraft::voxel::BlockType;

namespace {
struct Player {
    XMFLOAT3 position{8.0f, 17.0f, -18.0f};
    float yaw{0.0f};
    float pitch{0.0f};
    float verticalVelocity{0.0f};
};

XMVECTOR directionFor(const Player& player) {
    return XMVector3Normalize(XMVectorSet(std::sin(player.yaw) * std::cos(player.pitch), std::sin(player.pitch), std::cos(player.yaw) * std::cos(player.pitch), 0));
}

int run(HINSTANCE instance, bool renderTest, const std::filesystem::path& screenshotPath) {
    constexpr int width = 1280;
    constexpr int height = 720;
    directcraft::platform::Win32Window window(instance, width, height, !renderTest);
    directcraft::voxel::Chunk chunk(1337);
    directcraft::renderer::D3D12Renderer renderer(window.handle(), width, height, renderTest);
    renderer.setMesh(chunk.buildMesh());

    if (renderTest) {
        const XMVECTOR eye = XMVectorSet(8.0f, 16.0f, -24.0f, 1.0f);
        const XMVECTOR target = XMVectorSet(8.0f, 8.0f, 8.0f, 1.0f);
        const XMMATRIX view = XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0));
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(width) / height, 0.1f, 200.0f);
        renderer.render(view * projection, screenshotPath);
        return 0;
    }

    Player player;
    bool escapeWasDown = false;
    bool leftWasDown = false;
    bool rightWasDown = false;
    auto previous = std::chrono::steady_clock::now();
    while (window.pumpMessages()) {
        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::min(0.05f, std::chrono::duration<float>(now - previous).count());
        previous = now;
        const POINT mouse = window.consumeMouseDelta();
        if (window.mouseCaptured()) {
            player.yaw += static_cast<float>(mouse.x) * 0.0025f;
            player.pitch = std::clamp(player.pitch + static_cast<float>(mouse.y) * 0.0025f, -1.45f, 1.45f);
        }
        const bool escape = window.keyDown(VK_ESCAPE);
        if (escape && !escapeWasDown) {
            if (window.mouseCaptured()) window.setMouseCaptured(false);
            else PostMessageW(window.handle(), WM_CLOSE, 0, 0);
        }
        escapeWasDown = escape;

        XMVECTOR forward = directionFor(player); forward = XMVectorSetY(forward, 0); forward = XMVector3Normalize(forward);
        const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
        XMVECTOR movement = XMVectorZero();
        if (window.keyDown('W')) movement += forward;
        if (window.keyDown('S')) movement -= forward;
        if (window.keyDown('D')) movement += right;
        if (window.keyDown('A')) movement -= right;
        if (XMVectorGetX(XMVector3LengthSq(movement)) > 0.0f) movement = XMVector3Normalize(movement);
        XMVECTOR position = XMLoadFloat3(&player.position) + movement * (deltaSeconds * 8.0f);
        constexpr float floorHeight = 14.0f;
        const bool grounded = player.position.y <= floorHeight + 0.01f;
        if (grounded && window.keyDown(VK_SPACE)) player.verticalVelocity = 6.5f;
        player.verticalVelocity -= 18.0f * deltaSeconds;
        position = XMVectorSetY(position, XMVectorGetY(position) + player.verticalVelocity * deltaSeconds);
        if (XMVectorGetY(position) < floorHeight) { position = XMVectorSetY(position, floorHeight); player.verticalVelocity = 0.0f; }
        XMStoreFloat3(&player.position, position);

        const bool left = window.mouseButtonDown(false);
        const bool rightButton = window.mouseButtonDown(true);
        if (left && !leftWasDown || rightButton && !rightWasDown) {
            std::array<float, 3> origin{player.position.x, player.position.y, player.position.z};
            XMFLOAT3 direction{}; XMStoreFloat3(&direction, directionFor(player));
            const auto hit = directcraft::voxel::raycast(chunk, origin, {direction.x, direction.y, direction.z}, 8.0f);
            if (hit.hit) {
                if (left && !leftWasDown) chunk.set(hit.block.x, hit.block.y, hit.block.z, BlockType::Air);
                else if (chunk.get(hit.previous.x, hit.previous.y, hit.previous.z) == BlockType::Air) chunk.set(hit.previous.x, hit.previous.y, hit.previous.z, BlockType::Grass);
                renderer.setMesh(chunk.buildMesh());
            }
        }
        leftWasDown = left; rightWasDown = rightButton;
        const XMVECTOR eye = XMLoadFloat3(&player.position);
        const XMMATRIX view = XMMatrixLookToLH(eye, directionFor(player), XMVectorSet(0, 1, 0, 0));
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(width) / height, 0.1f, 200.0f);
        renderer.render(view * projection);
    }
    return 0;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    try {
        const std::wstring arguments = commandLine ? commandLine : L"";
        const std::wstring flag = L"--render-test";
        const std::size_t flagPosition = arguments.find(flag);
        if (flagPosition != std::wstring::npos) {
            std::wstring output = arguments.substr(flagPosition + flag.size());
            while (!output.empty() && output.front() == L' ') output.erase(output.begin());
            return run(instance, true, output.empty() ? L"directcraft_smoke.bmp" : std::filesystem::path(output));
        }
        return run(instance, false, {});
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "DirectCraft++ error", MB_ICONERROR | MB_OK);
        return 1;
    }
}


