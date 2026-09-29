#include "platform/win32_window.h"
#include "renderer/d3d12_renderer.h"
#include "voxel/voxel.h"
#include "debug/debug_server.h"
#include "debug/json.h"

#include <DirectXMath.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace DirectX;
using directcraft::debug::DebugServer;
using directcraft::debug::Json;
using directcraft::voxel::BlockType;

namespace {
struct Player { XMFLOAT3 position{8.0f, 17.0f, -18.0f}; float yaw{0.0f}; float pitch{0.0f}; float verticalVelocity{0.0f}; };

XMVECTOR directionFor(const Player& player) { return XMVector3Normalize(XMVectorSet(std::sin(player.yaw) * std::cos(player.pitch), std::sin(player.pitch), std::cos(player.yaw) * std::cos(player.pitch), 0)); }
Json numberArray(const float* values, int count) { Json result = Json::array(); for (int i = 0; i < count; ++i) result.values().emplace_back(static_cast<double>(values[i])); return result; }
Json matrixJson(const XMMATRIX& matrix) { XMFLOAT4X4 value{}; XMStoreFloat4x4(&value, matrix); return numberArray(&value._11, 16); }
std::wstring commandLineValue(const std::wstring& commandLine, const std::wstring& name) { const std::wstring token = L"--" + name + L" "; const std::size_t position = commandLine.find(token); if (position == std::wstring::npos) return {}; std::size_t start = position + token.size(); if (start < commandLine.size() && commandLine[start] == L'\"') { ++start; const std::size_t end = commandLine.find(L'\"', start); return commandLine.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start); } const std::size_t end = commandLine.find(L' ', start); return commandLine.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start); }
bool hasArgument(const std::wstring& commandLine, const std::wstring& name) { return commandLine.find(L"--" + name) != std::wstring::npos; }
int keyCode(const std::string& key) { if (key.size() == 1) return static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(key[0]))); if (key == "Space") return VK_SPACE; if (key == "Escape") return VK_ESCAPE; if (key == "F3") return VK_F3; if (key == "F4") return VK_F4; if (key == "F12") return VK_F12; return -1; }
BlockType blockType(const std::string& name) { if (name == "Stone") return BlockType::Stone; if (name == "Dirt") return BlockType::Dirt; if (name == "Sand") return BlockType::Sand; return BlockType::Grass; }
std::string safeName(std::string value) { for (char& character : value) if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-') character = '_'; return value.empty() ? "capture" : value; }
}

class Game {
public:
    Game(HINSTANCE instance, bool debugTools, bool renderTest, std::wstring pipeName)
        : window_(instance, 1280, 720, !renderTest), chunk_(1337), renderer_(window_.handle(), 1280, 720, renderTest), debugTools_(debugTools), renderTest_(renderTest), pipeName_(std::move(pipeName)) {}

    int run(const std::filesystem::path& renderTestPath) {
        mesh_ = chunk_.buildMesh(); renderer_.setMesh(mesh_);
        if (renderTest_) {
            const XMVECTOR eye = XMVectorSet(8.0f, 16.0f, -24.0f, 1.0f); const XMVECTOR target = XMVectorSet(8.0f, 8.0f, 8.0f, 1.0f);
            view_ = XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0)); projection_ = XMMatrixPerspectiveFovLH(XM_PIDIV4, 1280.0f / 720.0f, 0.1f, 200.0f); viewProjection_ = view_ * projection_; renderer_.render(viewProjection_, renderTestPath); return 0;
        }
        if (debugTools_) { server_ = std::make_unique<DebugServer>(pipeName_); server_->start(); }
        auto previous = std::chrono::steady_clock::now();
        while (window_.pumpMessages() && running_) {
            const auto now = std::chrono::steady_clock::now(); const float delta = std::min(0.05f, std::chrono::duration<float>(now - previous).count()); previous = now; deltaSeconds_ = delta;
            pollRequests(); processTransaction(); processInput(delta); updateMatrices();
            std::filesystem::path capturePath;
            if (captureName_) {
                capturePath = captureDirectory() / (*captureName_ + ".png");
                std::filesystem::create_directories(capturePath.parent_path());
            }
            renderer_.render(viewProjection_, capturePath);
            if (captureName_) { writeMetadata(capturePath); if (active_ && active_->captureName == captureName_) active_->screenshots.push_back(capturePath.string()); captureName_.reset(); }
            finishTransactionIfReady(); updateOverlay(); ++frameId_;
        }
        if (server_) server_->stop(); return 0;
    }

private:
    struct Transaction { std::vector<Json> commands; std::size_t cursor{0}; int waitFrames{0}; bool ready{false}; bool stateRequested{false}; bool quitRequested{false}; std::string captureName; std::vector<std::string> screenshots; std::vector<std::string> errors; std::shared_ptr<std::promise<std::string>> response; };
    void pollRequests() { if (!server_) return; const auto requests = server_->takeRequests(); for (const auto& request : requests) queued_.push_back(request); if (!active_ && !queued_.empty()) { const auto request = queued_.front(); queued_.erase(queued_.begin()); try { const Json root = Json::parse(request.line); const Json* commands = root.find("commands"); if (!commands || !commands->isArray()) throw std::runtime_error("scenario requires a commands array"); active_ = Transaction{}; active_->response = request.response; for (const auto& command : commands->values()) active_->commands.push_back(command); } catch (const std::exception& error) { Json response = Json::object(); response["ok"] = false; response["errors"] = Json::array(); response["errors"].values().emplace_back(error.what()); request.response->set_value(response.dump()); } } }
    void processTransaction() {
        if (!active_) return; if (active_->waitFrames > 0) { --active_->waitFrames; return; }
        while (active_ && active_->cursor < active_->commands.size()) { const Json command = active_->commands[active_->cursor++]; if (!command.isObject() || !command.find("op")) { active_->errors.push_back("command requires op"); continue; } const std::string op = command.at("op").string();
            if (op == "waitFrames") { active_->waitFrames = std::max(0, command.find("count") ? command.at("count").integer() : 1); return; }
            if (op == "keyDown" || op == "keyUp") { const int key = keyCode(command.find("key") ? command.at("key").string() : ""); if (key >= 0 && key < 256) injectedKeys_[key] = op == "keyDown"; else active_->errors.push_back("unknown key"); continue; }
            if (op == "mouseMove") { const std::string mode = command.find("mode") ? command.at("mode").string() : "relative"; if (mode == "absolute") { player_.yaw = static_cast<float>(command.find("x") ? command.at("x").number() : player_.yaw); player_.pitch = static_cast<float>(command.find("y") ? command.at("y").number() : player_.pitch); } else { injectedMouse_.x += command.find("dx") ? static_cast<float>(command.at("dx").number()) : 0.0f; injectedMouse_.y += command.find("dy") ? static_cast<float>(command.at("dy").number()) : 0.0f; } continue; }
            if (op == "mouseButtonDown" || op == "mouseButtonUp") { const bool down = op == "mouseButtonDown"; const std::string button = command.find("button") ? command.at("button").string() : "left"; if (button == "right") injectedRight_ = down; else injectedLeft_ = down; continue; }
            if (op == "click") { const std::string button = command.find("button") ? command.at("button").string() : "left"; editBlock(button != "left", blockType(command.find("block") ? command.at("block").string() : "Grass")); continue; }
            if (op == "setCamera") { if (const Json* position = command.find("position"); position && position->isArray() && position->values().size() >= 3) { player_.position = {static_cast<float>(position->values()[0].number()), static_cast<float>(position->values()[1].number()), static_cast<float>(position->values()[2].number())}; } if (command.find("yaw")) player_.yaw = static_cast<float>(command.at("yaw").number()); if (command.find("pitch")) player_.pitch = std::clamp(static_cast<float>(command.at("pitch").number()), -1.5f, 1.5f); continue; }
            if (op == "breakBlock") { editBlock(false, BlockType::Air); continue; }
            if (op == "placeBlock") { editBlock(true, blockType(command.find("block") ? command.at("block").string() : "Grass")); continue; }
            if (op == "capture") { active_->captureName = safeName(command.find("name") ? command.at("name").string() : "capture"); captureName_ = active_->captureName; continue; }
            if (op == "getState") { active_->stateRequested = true; continue; }
            if (op == "quit") { active_->quitRequested = true; continue; }
            active_->errors.push_back("unknown command: " + op);
        }
        active_->ready = true;
    }
    void processInput(float delta) {
        const POINT mouse = window_.consumeMouseDelta(); injectedMouse_.x += static_cast<float>(mouse.x); injectedMouse_.y += static_cast<float>(mouse.y); if (window_.mouseCaptured()) { player_.yaw += injectedMouse_.x * 0.0025f; player_.pitch = std::clamp(player_.pitch + injectedMouse_.y * 0.0025f, -1.45f, 1.45f); } injectedMouse_ = {};
        const bool escape = window_.keyDown(VK_ESCAPE); if (escape && !escapeWasDown_) { if (window_.mouseCaptured()) window_.setMouseCaptured(false); else PostMessageW(window_.handle(), WM_CLOSE, 0, 0); } escapeWasDown_ = escape;
        const bool f3 = window_.keyDown(VK_F3); if (f3 && !f3WasDown_ && debugTools_) diagnostics_ = !diagnostics_; f3WasDown_ = f3; const bool f4 = window_.keyDown(VK_F4); if (f4 && !f4WasDown_ && debugTools_) renderer_.setWireframe(!renderer_.wireframe()); f4WasDown_ = f4;
        const bool f12 = window_.keyDown(VK_F12); if (f12 && !f12WasDown_ && debugTools_) captureName_ = "f12_" + std::to_string(frameId_); f12WasDown_ = f12;
        const auto key = [this](int code) { return window_.keyDown(code) || injectedKeys_[code]; }; XMVECTOR forward = directionFor(player_); forward = XMVectorSetY(forward, 0); forward = XMVector3Normalize(forward); const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward)); XMVECTOR movement = XMVectorZero(); if (key('W')) movement += forward; if (key('S')) movement -= forward; if (key('D')) movement += right; if (key('A')) movement -= right; if (XMVectorGetX(XMVector3LengthSq(movement)) > 0.0f) movement = XMVector3Normalize(movement); XMVECTOR position = XMLoadFloat3(&player_.position) + movement * (delta * 8.0f);
        constexpr float floorHeight = 14.0f; const bool grounded = player_.position.y <= floorHeight + 0.01f; if (grounded && key(VK_SPACE)) player_.verticalVelocity = 6.5f; player_.verticalVelocity -= 18.0f * delta; position = XMVectorSetY(position, XMVectorGetY(position) + player_.verticalVelocity * delta); if (XMVectorGetY(position) < floorHeight) { position = XMVectorSetY(position, floorHeight); player_.verticalVelocity = 0.0f; } XMStoreFloat3(&player_.position, position);
        const bool left = window_.mouseButtonDown(false) || injectedLeft_; const bool rightButton = window_.mouseButtonDown(true) || injectedRight_; if ((left && !leftWasDown_) || (rightButton && !rightWasDown_)) editBlock(rightButton && !rightWasDown_, BlockType::Grass); leftWasDown_ = left; rightWasDown_ = rightButton;
    }
    void editBlock(bool place, BlockType block) { XMFLOAT3 direction{}; XMStoreFloat3(&direction, directionFor(player_)); const auto hit = directcraft::voxel::raycast(chunk_, {player_.position.x, player_.position.y, player_.position.z}, {direction.x, direction.y, direction.z}, 8.0f); if (!hit.hit) return; if (place) chunk_.set(hit.previous.x, hit.previous.y, hit.previous.z, block); else chunk_.set(hit.block.x, hit.block.y, hit.block.z, BlockType::Air); mesh_ = chunk_.buildMesh(); renderer_.setMesh(mesh_); events_.push_back(place ? "placeBlock" : "breakBlock"); if (events_.size() > 32) events_.erase(events_.begin(), events_.begin() + (events_.size() - 32)); }
    void updateMatrices() { const XMVECTOR eye = XMLoadFloat3(&player_.position); view_ = XMMatrixLookToLH(eye, directionFor(player_), XMVectorSet(0, 1, 0, 0)); projection_ = XMMatrixPerspectiveFovLH(XM_PIDIV4, 1280.0f / 720.0f, 0.1f, 200.0f); viewProjection_ = view_ * projection_; XMFLOAT3 direction{}; XMStoreFloat3(&direction, directionFor(player_)); lastHit_ = directcraft::voxel::raycast(chunk_, {player_.position.x, player_.position.y, player_.position.z}, {direction.x, direction.y, direction.z}, 8.0f); }
    Json meshBounds() const { if (mesh_.vertices.empty()) return Json::object(); float minimum[3] = {mesh_.vertices[0].position[0], mesh_.vertices[0].position[1], mesh_.vertices[0].position[2]}; float maximum[3] = {minimum[0], minimum[1], minimum[2]}; for (const auto& vertex : mesh_.vertices) for (int axis = 0; axis < 3; ++axis) { minimum[axis] = std::min(minimum[axis], vertex.position[axis]); maximum[axis] = std::max(maximum[axis], vertex.position[axis]); } Json result = Json::object(); result["min"] = numberArray(minimum, 3); result["max"] = numberArray(maximum, 3); return result; }
    bool clipSpaceValid() const { const XMMATRIX uploaded = viewProjection_; for (const auto& vertex : mesh_.vertices) { XMFLOAT4 clip{}; XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f), uploaded)); if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) || std::abs(clip.w) < 0.0001f) return false; const float x = clip.x / clip.w; const float y = clip.y / clip.w; const float z = clip.z / clip.w; if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::abs(x) > 100000.0f || std::abs(y) > 100000.0f || std::abs(z) > 100000.0f) return false; } return true; }
    Json state() const { Json result = Json::object(); result["application"]["version"] = "1.0.1"; result["application"]["debugTools"] = debugTools_; result["application"]["frameId"] = static_cast<int>(frameId_); result["application"]["viewport"] = Json::array(); result["application"]["viewport"].values() = {1280, 720}; result["player"]["position"] = numberArray(&player_.position.x, 3); result["player"]["yaw"] = player_.yaw; result["player"]["pitch"] = player_.pitch; result["player"]["verticalVelocity"] = player_.verticalVelocity; result["player"]["grounded"] = player_.position.y <= 14.01f; result["input"]["pressedKeys"] = Json::array(); for (int key = 0; key < 256; ++key) if (injectedKeys_[key] || window_.keyDown(key)) result["input"]["pressedKeys"].values().emplace_back(key); result["input"]["mouseButtons"] = Json::array(); if (injectedLeft_ || window_.mouseButtonDown(false)) result["input"]["mouseButtons"].values().emplace_back("left"); if (injectedRight_ || window_.mouseButtonDown(true)) result["input"]["mouseButtons"].values().emplace_back("right"); result["input"]["mouseDelta"] = Json::array(); result["input"]["mouseDelta"].values() = {injectedMouse_.x, injectedMouse_.y}; result["camera"]["view"] = matrixJson(view_); result["camera"]["projection"] = matrixJson(projection_); result["camera"]["viewProjection"] = matrixJson(viewProjection_); result["camera"]["fovRadians"] = XM_PIDIV4; result["camera"]["nearPlane"] = 0.1; result["camera"]["farPlane"] = 200.0; result["world"]["seed"] = 1337; result["world"]["chunkSize"] = directcraft::voxel::ChunkSize; result["world"]["chunkHeight"] = directcraft::voxel::ChunkHeight; result["raycast"]["hit"] = lastHit_.hit; result["raycast"]["block"] = Json::array(); result["raycast"]["block"].values() = {lastHit_.block.x, lastHit_.block.y, lastHit_.block.z}; result["raycast"]["previous"] = Json::array(); result["raycast"]["previous"].values() = {lastHit_.previous.x, lastHit_.previous.y, lastHit_.previous.z}; result["raycast"]["distance"] = lastHit_.distance; result["mesh"]["vertices"] = static_cast<int>(mesh_.vertices.size()); result["mesh"]["indices"] = static_cast<int>(mesh_.indices.size()); result["mesh"]["vertexBytes"] = static_cast<int>(renderer_.vertexBytes()); result["mesh"]["indexBytes"] = static_cast<int>(renderer_.indexBytes()); result["renderer"]["adapter"] = renderer_.adapterName(); result["renderer"]["usingWarp"] = renderer_.usingWarp(); result["renderer"]["featureLevel"] = renderer_.featureLevel(); result["renderer"]["pipelineReady"] = renderer_.pipelineReady(); result["renderer"]["resourcesReady"] = renderer_.resourcesReady(); result["renderer"]["wireframe"] = renderer_.wireframe(); result["renderer"]["geometryValid"] = geometryValid(); result["renderer"]["clipSpaceValid"] = clipSpaceValid(); result["timing"]["deltaSeconds"] = deltaSeconds_; result["mesh"]["bounds"] = meshBounds(); result["events"] = Json::array(); for (const auto& event : events_) result["events"].values().emplace_back(event); return result; }
    bool geometryValid() const { if (mesh_.vertices.empty() || mesh_.indices.empty()) return false; for (const auto& vertex : mesh_.vertices) for (float value : {vertex.position[0], vertex.position[1], vertex.position[2], vertex.normal[0], vertex.normal[1], vertex.normal[2]}) if (!std::isfinite(value)) return false; for (const auto index : mesh_.indices) if (index >= mesh_.vertices.size()) return false; return true; }
    void writeMetadata(const std::filesystem::path& image) { if (image.empty()) return; std::filesystem::create_directories(image.parent_path()); auto metadata = image; metadata.replace_extension(".json"); std::ofstream output(metadata); output << state().dump() << '\n'; }
    std::filesystem::path captureDirectory() const { return std::filesystem::path("debug_captures"); }
    void finishTransactionIfReady() { if (!active_ || !active_->ready || captureName_) return; Json response = Json::object(); response["ok"] = active_->errors.empty(); response["frameId"] = static_cast<int>(frameId_); response["screenshots"] = Json::array(); for (const auto& screenshot : active_->screenshots) response["screenshots"].values().emplace_back(screenshot); response["state"] = state(); response["errors"] = Json::array(); for (const auto& error : active_->errors) response["errors"].values().emplace_back(error); const bool shouldQuit = active_->quitRequested; active_->response->set_value(response.dump()); active_.reset(); if (shouldQuit) running_ = false; }
    void updateOverlay() { if (!diagnostics_) { SetWindowTextW(window_.handle(), L"DirectCraft++ v1.0.1"); return; } const std::wstring title = L"DirectCraft++ v1.0.1 | F3 diagnostics | frame=" + std::to_wstring(frameId_) + L" mesh=" + std::to_wstring(mesh_.vertices.size()) + L"/" + std::to_wstring(mesh_.indices.size()) + (lastHit_.hit ? L" hit" : L" no-hit") + (renderer_.wireframe() ? L" wireframe" : L""); SetWindowTextW(window_.handle(), title.c_str()); }

    directcraft::platform::Win32Window window_; directcraft::voxel::Chunk chunk_; directcraft::voxel::Mesh mesh_; directcraft::renderer::D3D12Renderer renderer_; Player player_;
    bool debugTools_{}; bool renderTest_{}; std::wstring pipeName_; std::unique_ptr<DebugServer> server_; std::vector<directcraft::debug::PendingRequest> queued_; std::optional<Transaction> active_;
    bool running_{true}; bool diagnostics_{}; bool escapeWasDown_{}; bool f3WasDown_{}; bool f4WasDown_{}; bool f12WasDown_{}; bool leftWasDown_{}; bool rightWasDown_{}; bool injectedLeft_{}; bool injectedRight_{}; std::array<bool, 256> injectedKeys_{}; XMFLOAT2 injectedMouse_{};
    std::optional<std::string> captureName_; std::vector<std::string> events_; std::uint64_t frameId_{}; float deltaSeconds_{}; XMMATRIX view_{XMMatrixIdentity()}; XMMATRIX projection_{XMMatrixIdentity()}; XMMATRIX viewProjection_{XMMatrixIdentity()}; directcraft::voxel::RayHit lastHit_{};
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        const std::wstring arguments = commandLine ? commandLine : L""; const bool renderTest = hasArgument(arguments, L"render-test"); const bool debugTools = hasArgument(arguments, L"debug-tools");
        std::wstring pipeName = commandLineValue(arguments, L"pipe-name"); if (pipeName.empty()) pipeName = L"DirectCraftPP." + std::to_wstring(GetCurrentProcessId());
        const std::wstring output = commandLineValue(arguments, L"render-test"); const std::filesystem::path screenshot = output.empty() ? L"directcraft_smoke.bmp" : std::filesystem::path(output);
        Game game(instance, debugTools, renderTest, pipeName); const int result = game.run(screenshot); CoUninitialize(); return result;
    } catch (const std::exception& error) { std::ofstream log("directcraft_error.log", std::ios::app); log << error.what() << "\\n"; OutputDebugStringA(error.what()); OutputDebugStringA("\\n"); MessageBoxA(nullptr, error.what(), "DirectCraft++ error", MB_ICONERROR | MB_OK); CoUninitialize(); return 1; }
}

