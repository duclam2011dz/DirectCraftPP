#include "platform/win32_window.h"
#include "renderer/d3d12_renderer.h"
#include "voxel/voxel.h"
#include "voxel/simd.h"
#include "gameplay/physics.h"
#include "gameplay/camera.h"
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
constexpr float EyeHeight = 1.62f;
constexpr float MouseSensitivity = 0.0025f;

XMVECTOR directionFor(float yaw, float pitch) {
    return XMVector3Normalize(XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch), 0));
}
Json numberArray(const float* values, int count) { Json result = Json::array(); for (int i = 0; i < count; ++i) result.values().emplace_back(static_cast<double>(values[i])); return result; }
Json numberArray(const std::array<float, 3>& values) { return numberArray(values.data(), 3); }
Json numberArray(const std::array<float, 2>& values) { return numberArray(values.data(), 2); }
Json intArray(const directcraft::voxel::Int3& value) { Json result = Json::array(); result.values() = {value.x, value.y, value.z}; return result; }
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
        : window_(instance, 1280, 720, !renderTest), world_(1337, 9, 8), renderer_(window_.handle(), 1280, 720, renderTest), debugTools_(debugTools), renderTest_(renderTest), pipeName_(std::move(pipeName)) {
        world_.updateStreaming(7.5f, 7.5f);
        physics_ = directcraft::gameplay::spawnAtCenter(world_, physicsConfig_);
    }

    int run(const std::filesystem::path& renderTestPath) {
        mesh_ = world_.buildRenderMesh(physics_.position[0], physics_.position[2]); renderer_.setMesh(mesh_); meshDirty_ = false;
        if (renderTest_) {
            const XMVECTOR eye = XMVectorSet(8.0f, 16.0f, -24.0f, 1.0f); const XMVECTOR target = XMVectorSet(8.0f, 8.0f, 8.0f, 1.0f);
            view_ = XMMatrixLookAtLH(eye, target, XMVectorSet(0, 1, 0, 0)); projection_ = XMMatrixPerspectiveFovLH(XM_PIDIV4, 1280.0f / 720.0f, 0.1f, 200.0f); viewProjection_ = view_ * projection_; renderer_.render(viewProjection_, renderTestPath); return 0;
        }
        if (debugTools_) { server_ = std::make_unique<DebugServer>(pipeName_); server_->start(); }
        auto previous = std::chrono::steady_clock::now();
        while (window_.pumpMessages() && running_) {
            const auto now = std::chrono::steady_clock::now(); const float delta = std::min(0.05f, std::chrono::duration<float>(now - previous).count()); previous = now; deltaSeconds_ = delta;
            pollRequests(); processTransaction(); processInput(delta); updateMatrices(); rebuildMeshIfNeeded(); writeTraceFrame();
            std::filesystem::path capturePath;
            if (captureName_) { capturePath = captureDirectory() / (*captureName_ + ".png"); std::filesystem::create_directories(capturePath.parent_path()); }
            renderer_.render(viewProjection_, capturePath);
            if (captureName_) { writeMetadata(capturePath); if (active_ && active_->captureName == captureName_) active_->screenshots.push_back(capturePath.string()); captureName_.reset(); }
            finishTransactionIfReady(); updateOverlay(); ++frameId_;
        }
        if (traceOutput_.is_open()) traceOutput_.close(); if (server_) server_->stop(); return 0;
    }

private:
    struct Transaction { std::vector<Json> commands; std::size_t cursor{0}; int waitFrames{0}; bool ready{false}; bool quitRequested{false}; std::string captureName; std::vector<std::string> screenshots; std::vector<std::string> traces; std::vector<std::string> errors; std::shared_ptr<std::promise<std::string>> response; };

    void pollRequests() {
        if (!server_) return; const auto requests = server_->takeRequests(); for (const auto& request : requests) queued_.push_back(request);
        if (!active_ && !queued_.empty()) { const auto request = queued_.front(); queued_.erase(queued_.begin()); try { const Json root = Json::parse(request.line); const Json* commands = root.find("commands"); if (!commands || !commands->isArray()) throw std::runtime_error("scenario requires a commands array"); active_ = Transaction{}; active_->response = request.response; for (const auto& command : commands->values()) active_->commands.push_back(command); } catch (const std::exception& error) { Json response = Json::object(); response["ok"] = false; response["errors"] = Json::array(); response["errors"].values().emplace_back(error.what()); request.response->set_value(response.dump()); } }
    }

    void processTransaction() {
        if (!active_) return; if (active_->waitFrames > 0) { --active_->waitFrames; return; }
        while (active_ && active_->cursor < active_->commands.size()) {
            const Json command = active_->commands[active_->cursor++]; if (!command.isObject() || !command.find("op")) { active_->errors.push_back("command requires op"); continue; }
            const std::string op = command.at("op").string();
            if (op == "waitFrames") { active_->waitFrames = std::max(0, command.find("count") ? command.at("count").integer() : 1); return; }
            if (op == "keyDown" || op == "keyUp") { const int key = keyCode(command.find("key") ? command.at("key").string() : ""); if (key >= 0 && key < 256) injectedKeys_[key] = op == "keyDown"; else active_->errors.push_back("unknown key"); continue; }
            if (op == "mouseMove") { injectedMouseDelta_[0] += static_cast<float>(command.find("dx") ? command.at("dx").number() : 0.0); injectedMouseDelta_[1] += static_cast<float>(command.find("dy") ? command.at("dy").number() : 0.0); continue; }
            if (op == "lockPointer") { window_.setMouseCaptured(true); continue; }
            if (op == "unlockPointer") { window_.setMouseCaptured(false); continue; }
            if (op == "mouseButtonDown" || op == "mouseButtonUp") { const bool down = op == "mouseButtonDown"; const std::string button = command.find("button") ? command.at("button").string() : "left"; if (button == "right") injectedRight_ = down; else injectedLeft_ = down; continue; }
            if (op == "click") { const std::string button = command.find("button") ? command.at("button").string() : "left"; editBlock(button != "left", blockType(command.find("block") ? command.at("block").string() : "Grass")); continue; }
            if (op == "setCamera") { if (const Json* position = command.find("position"); position && position->isArray() && position->values().size() >= 3) physics_.position = {static_cast<float>(position->values()[0].number()), static_cast<float>(position->values()[1].number()) - EyeHeight, static_cast<float>(position->values()[2].number())}; if (command.find("yaw")) camera_.yaw = static_cast<float>(command.at("yaw").number()); if (command.find("pitch")) camera_.pitch = std::clamp(static_cast<float>(command.at("pitch").number()), -1.45f, 1.45f); continue; }
            if (op == "breakBlock") { editBlock(false, BlockType::Air); continue; }
            if (op == "placeBlock") { editBlock(true, blockType(command.find("block") ? command.at("block").string() : "Grass")); continue; }
            if (op == "capture") { active_->captureName = safeName(command.find("name") ? command.at("name").string() : "capture"); captureName_ = active_->captureName; continue; }
            if (op == "startTrace") { startTrace(safeName(command.find("name") ? command.at("name").string() : "trace")); active_->traces.push_back(tracePath_.string()); continue; }
            if (op == "stopTrace") { stopTrace(); continue; }
            if (op == "getState") continue;
            if (op == "quit") { active_->quitRequested = true; continue; }
            active_->errors.push_back("unknown command: " + op);
        }
        active_->ready = true;
    }

    void processInput(float delta) {
        const POINT raw = window_.consumeMouseDelta(); rawMouseDelta_ = {static_cast<float>(raw.x), static_cast<float>(raw.y)}; appliedMouseDelta_ = {rawMouseDelta_[0] + injectedMouseDelta_[0], rawMouseDelta_[1] + injectedMouseDelta_[1]};
        if (window_.mouseCaptured() || std::abs(injectedMouseDelta_[0]) > 0.0f || std::abs(injectedMouseDelta_[1]) > 0.0f) directcraft::gameplay::applyMouseDelta(camera_, appliedMouseDelta_, MouseSensitivity);
        if (window_.mouseCaptured()) window_.recenterPointer(); injectedMouseDelta_ = {};
        const bool escape = window_.keyDown(VK_ESCAPE); if (escape && !escapeWasDown_) { if (window_.mouseCaptured()) window_.setMouseCaptured(false); else PostMessageW(window_.handle(), WM_CLOSE, 0, 0); } escapeWasDown_ = escape;
        const bool f3 = window_.keyDown(VK_F3); if (f3 && !f3WasDown_ && debugTools_) diagnostics_ = !diagnostics_; f3WasDown_ = f3; const bool f4 = window_.keyDown(VK_F4); if (f4 && !f4WasDown_ && debugTools_) renderer_.setWireframe(!renderer_.wireframe()); f4WasDown_ = f4; const bool f12 = window_.keyDown(VK_F12); if (f12 && !f12WasDown_ && debugTools_) captureName_ = "f12_" + std::to_string(frameId_); f12WasDown_ = f12;
        const auto key = [this](int code) { return window_.keyDown(code) || injectedKeys_[code]; }; XMVECTOR forward = directionFor(camera_.yaw, 0.0f); forward = XMVectorSetY(forward, 0); forward = XMVector3Normalize(forward); XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward)); XMVECTOR wish = XMVectorZero(); if (key('W')) wish += forward; if (key('S')) wish -= forward; if (key('D')) wish += right; if (key('A')) wish -= right; if (XMVectorGetX(XMVector3LengthSq(wish)) > 0.0f) wish = XMVector3Normalize(wish); XMFLOAT3 wishFloat{}; XMStoreFloat3(&wishFloat, wish);
        directcraft::gameplay::simulate(physics_, physicsConfig_, world_, {wishFloat.x, 0.0f, wishFloat.z}, delta, key(VK_SPACE));
        world_.updateStreaming(physics_.position[0], physics_.position[2]);
        if (world_.stats().generatedChunks > 0 || world_.stats().unloadedChunks > 0) meshDirty_ = true;
        const bool left = window_.mouseButtonDown(false) || injectedLeft_; const bool rightButton = window_.mouseButtonDown(true) || injectedRight_; if ((left && !leftWasDown_) || (rightButton && !rightWasDown_)) editBlock(rightButton && !rightWasDown_, BlockType::Grass); leftWasDown_ = left; rightWasDown_ = rightButton;
    }

    void editBlock(bool place, BlockType block) {
        XMFLOAT3 eye{}; XMStoreFloat3(&eye, eyePosition()); XMFLOAT3 direction{}; XMStoreFloat3(&direction, directionFor(camera_.yaw, camera_.pitch)); const auto hit = world_.raycast({eye.x, eye.y, eye.z}, {direction.x, direction.y, direction.z}, 8.0f); if (!hit.hit) { events_.push_back(place ? "placeMiss" : "breakMiss"); return; }
        const auto target = place ? hit.previous : hit.block; if (place && (!world_.isResident(target.x, target.z) || target.y < 0 || target.y >= directcraft::voxel::ChunkHeight || world_.get(target.x, target.y, target.z) != BlockType::Air)) { events_.push_back("placeRejectedBounds"); return; }
        if (place) { directcraft::gameplay::PhysicsState candidate = physics_; candidate.position = physics_.position; const directcraft::gameplay::Aabb blockBox{{static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.z)}, {target.x + 1.0f, target.y + 1.0f, target.z + 1.0f}}; if (directcraft::gameplay::overlaps(directcraft::gameplay::playerAabb(candidate, physicsConfig_), blockBox)) { events_.push_back("placeRejectedOverlap"); return; } }
        world_.set(target.x, target.y, target.z, block); editedBlocks_.push_back(target); meshDirty_ = true; events_.push_back(place ? "placeBlock" : "breakBlock"); if (events_.size() > 64) events_.erase(events_.begin(), events_.begin() + (events_.size() - 64));
    }

    XMVECTOR eyePosition() const { return XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(physics_.position.data())) + XMVectorSet(0, EyeHeight, 0, 0); }
    void updateMatrices() { const XMVECTOR eye = eyePosition(); view_ = XMMatrixLookToLH(eye, directionFor(camera_.yaw, camera_.pitch), XMVectorSet(0, 1, 0, 0)); projection_ = XMMatrixPerspectiveFovLH(XM_PIDIV4, 1280.0f / 720.0f, 0.1f, 200.0f); viewProjection_ = view_ * projection_; XMFLOAT3 direction{}; XMStoreFloat3(&direction, directionFor(camera_.yaw, camera_.pitch)); lastHit_ = world_.raycast({XMVectorGetX(eye), XMVectorGetY(eye), XMVectorGetZ(eye)}, {direction.x, direction.y, direction.z}, 8.0f); }
    void rebuildMeshIfNeeded() {
        if (!meshDirty_) return;
        XMFLOAT4X4 matrix{};
        XMStoreFloat4x4(&matrix, viewProjection_);
        std::array<float, 16> values{};
        std::copy(&matrix._11, &matrix._11 + 16, values.begin());
        mesh_ = world_.buildRenderMesh(physics_.position[0], physics_.position[2], directcraft::voxel::makeFrustum(values));
        renderer_.setMesh(mesh_);
        meshDirty_ = false;
    }
    Json meshBounds() const { if (mesh_.vertices.empty()) return Json::object(); float minimum[3] = {mesh_.vertices[0].position[0], mesh_.vertices[0].position[1], mesh_.vertices[0].position[2]}; float maximum[3] = {minimum[0], minimum[1], minimum[2]}; for (const auto& vertex : mesh_.vertices) for (int axis = 0; axis < 3; ++axis) { minimum[axis] = std::min(minimum[axis], vertex.position[axis]); maximum[axis] = std::max(maximum[axis], vertex.position[axis]); } Json result = Json::object(); result["min"] = numberArray(minimum, 3); result["max"] = numberArray(maximum, 3); return result; }
    Json aabbJson() const { const auto box = directcraft::gameplay::playerAabb(physics_, physicsConfig_); Json result = Json::object(); result["min"] = numberArray(box.minimum); result["max"] = numberArray(box.maximum); return result; }
    Json state() const {
        Json result = Json::object(); XMFLOAT3 eye{}, forward{}, right{}, up{}; XMStoreFloat3(&eye, eyePosition()); XMStoreFloat3(&forward, directionFor(camera_.yaw, camera_.pitch)); const XMVECTOR rightVector = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), XMLoadFloat3(&forward))); const XMVECTOR upVector = XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&forward), rightVector)); XMStoreFloat3(&right, rightVector); XMStoreFloat3(&up, upVector);
        result["application"]["version"] = "1.1.0"; result["application"]["debugTools"] = debugTools_; result["application"]["frameId"] = static_cast<int>(frameId_); result["application"]["viewport"] = Json::array(); result["application"]["viewport"].values() = {1280, 720};
        result["input"]["pointerLocked"] = window_.mouseCaptured(); result["input"]["cursorCenter"] = Json::array(); result["input"]["cursorCenter"].values().emplace_back(static_cast<int>(window_.cursorCenter().x)); result["input"]["cursorCenter"].values().emplace_back(static_cast<int>(window_.cursorCenter().y)); result["input"]["rawMouseDelta"] = numberArray(rawMouseDelta_); result["input"]["appliedMouseDelta"] = numberArray(appliedMouseDelta_); result["input"]["pressedKeys"] = Json::array(); for (int key = 0; key < 256; ++key) if (injectedKeys_[key] || window_.keyDown(key)) result["input"]["pressedKeys"].values().emplace_back(key); result["input"]["mouseButtons"] = Json::array(); if (injectedLeft_ || window_.mouseButtonDown(false)) result["input"]["mouseButtons"].values().emplace_back("left"); if (injectedRight_ || window_.mouseButtonDown(true)) result["input"]["mouseButtons"].values().emplace_back("right");
        result["player"]["position"] = numberArray(physics_.position); result["player"]["eyePosition"] = numberArray({eye.x, eye.y, eye.z}); result["player"]["velocity"] = numberArray(physics_.velocity); result["player"]["acceleration"] = numberArray(physics_.acceleration); result["player"]["grounded"] = physics_.grounded; result["player"]["hitCeiling"] = physics_.hitCeiling; result["player"]["hitWall"] = physics_.hitWall; result["player"]["aabb"] = aabbJson();
        result["camera"]["position"] = numberArray({eye.x, eye.y, eye.z}); result["camera"]["yaw"] = camera_.yaw; result["camera"]["pitch"] = camera_.pitch; result["camera"]["forward"] = numberArray({forward.x, forward.y, forward.z}); result["camera"]["right"] = numberArray({right.x, right.y, right.z}); result["camera"]["up"] = numberArray({up.x, up.y, up.z}); result["camera"]["view"] = matrixJson(view_); result["camera"]["projection"] = matrixJson(projection_); result["camera"]["viewProjection"] = matrixJson(viewProjection_); result["camera"]["fovRadians"] = XM_PIDIV4; result["camera"]["nearPlane"] = 0.1; result["camera"]["farPlane"] = 200.0;
        const auto playerChunk = world_.chunkCoordFor(static_cast<int>(std::floor(physics_.position[0])), static_cast<int>(std::floor(physics_.position[2]))); result["world"]["seed"] = world_.seed(); result["world"]["chunkCoord"] = Json::array(); result["world"]["chunkCoord"].values() = {playerChunk.x, playerChunk.z}; result["world"]["chunkSize"] = directcraft::voxel::ChunkSize; result["world"]["chunkHeight"] = directcraft::voxel::ChunkHeight; result["world"]["loadRadius"] = world_.loadRadius(); result["world"]["renderRadius"] = world_.renderRadius(); result["world"]["playerBlock"] = Json::array(); result["world"]["playerBlock"].values() = {static_cast<int>(std::floor(physics_.position[0])), static_cast<int>(std::floor(physics_.position[1])), static_cast<int>(std::floor(physics_.position[2]))}; result["world"]["editedBlocks"] = Json::array(); for (const auto& block : editedBlocks_) result["world"]["editedBlocks"].values().emplace_back(intArray(block));
        result["raycast"]["origin"] = numberArray({eye.x, eye.y, eye.z}); result["raycast"]["direction"] = numberArray({forward.x, forward.y, forward.z}); result["raycast"]["hit"] = lastHit_.hit; result["raycast"]["block"] = intArray(lastHit_.block); result["raycast"]["previous"] = intArray(lastHit_.previous); result["raycast"]["normal"] = intArray(lastHit_.normal); result["raycast"]["distance"] = lastHit_.distance;
        result["physics"]["width"] = physicsConfig_.width; result["physics"]["height"] = physicsConfig_.height; result["physics"]["depth"] = physicsConfig_.depth; result["physics"]["maxSpeed"] = physicsConfig_.maxSpeed; result["physics"]["acceleration"] = physicsConfig_.acceleration; result["physics"]["friction"] = physicsConfig_.friction; result["physics"]["gravity"] = physicsConfig_.gravity; result["physics"]["jumpVelocity"] = physicsConfig_.jumpVelocity;
        result["mesh"]["vertices"] = static_cast<int>(mesh_.vertices.size()); result["mesh"]["indices"] = static_cast<int>(mesh_.indices.size()); result["mesh"]["vertexBytes"] = static_cast<int>(renderer_.vertexBytes()); result["mesh"]["indexBytes"] = static_cast<int>(renderer_.indexBytes()); result["mesh"]["bounds"] = meshBounds(); result["mesh"]["loadedChunks"] = static_cast<int>(world_.stats().loadedChunks); result["mesh"]["meshedChunks"] = static_cast<int>(world_.stats().meshedChunks); result["mesh"]["visibleChunks"] = static_cast<int>(world_.stats().visibleChunks); result["mesh"]["distanceCulledChunks"] = static_cast<int>(world_.stats().distanceCulledChunks); result["mesh"]["frustumCulledChunks"] = static_cast<int>(world_.stats().frustumCulledChunks);
        result["renderer"]["adapter"] = renderer_.adapterName(); result["renderer"]["usingWarp"] = renderer_.usingWarp(); result["renderer"]["featureLevel"] = renderer_.featureLevel(); result["renderer"]["pipelineReady"] = renderer_.pipelineReady(); result["renderer"]["resourcesReady"] = renderer_.resourcesReady(); result["renderer"]["wireframe"] = renderer_.wireframe(); result["renderer"]["geometryValid"] = geometryValid(); result["renderer"]["clipSpaceValid"] = clipSpaceValid(); result["renderer"]["deviceRemovedReason"] = static_cast<int>(renderer_.deviceRemovedReason()); result["renderer"]["gpuValidationEnabled"] = renderer_.gpuValidationEnabled(); result["renderer"]["dredEnabled"] = renderer_.dredEnabled();
        result["trace"]["active"] = traceOutput_.is_open(); result["trace"]["path"] = tracePath_.string(); result["timing"]["deltaSeconds"] = deltaSeconds_; result["events"] = Json::array(); for (const auto& event : events_) result["events"].values().emplace_back(event); return result;
    }
    bool geometryValid() const { if (mesh_.vertices.empty() || mesh_.indices.empty()) return false; for (const auto& vertex : mesh_.vertices) for (float value : {vertex.position[0], vertex.position[1], vertex.position[2], vertex.normal[0], vertex.normal[1], vertex.normal[2]}) if (!std::isfinite(value)) return false; for (const auto index : mesh_.indices) if (index >= mesh_.vertices.size()) return false; return true; }
    bool clipSpaceValid() const { for (const auto& vertex : mesh_.vertices) { XMFLOAT4 clip{}; XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f), viewProjection_)); if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) || std::abs(clip.w) < 0.0001f) return false; } return true; }
    void writeMetadata(const std::filesystem::path& image) { if (image.empty()) return; auto metadata = image; metadata.replace_extension(".json"); std::ofstream output(metadata); output << state().dump() << '\n'; }
    std::filesystem::path captureDirectory() const { return std::filesystem::path("debug_captures"); }
    void startTrace(const std::string& name) { if (traceOutput_.is_open()) traceOutput_.close(); tracePath_ = captureDirectory() / (name + ".jsonl"); std::filesystem::create_directories(tracePath_.parent_path()); traceOutput_.open(tracePath_, std::ios::out | std::ios::trunc); if (!traceOutput_) throw std::runtime_error("Could not open trace output."); }
    void stopTrace() { if (traceOutput_.is_open()) traceOutput_.close(); }
    void writeTraceFrame() { if (traceOutput_.is_open()) traceOutput_ << state().dump() << '\n'; }
    void finishTransactionIfReady() { if (!active_ || !active_->ready || captureName_) return; Json response = Json::object(); response["ok"] = active_->errors.empty(); response["frameId"] = static_cast<int>(frameId_); response["screenshots"] = Json::array(); for (const auto& screenshot : active_->screenshots) response["screenshots"].values().emplace_back(screenshot); response["traces"] = Json::array(); for (const auto& trace : active_->traces) response["traces"].values().emplace_back(trace); response["state"] = state(); response["errors"] = Json::array(); for (const auto& error : active_->errors) response["errors"].values().emplace_back(error); const bool shouldQuit = active_->quitRequested; active_->response->set_value(response.dump()); active_.reset(); if (shouldQuit) running_ = false; }
    void updateOverlay() { if (!diagnostics_) { SetWindowTextW(window_.handle(), L"DirectCraft++ v1.1.0"); return; } const std::wstring title = L"DirectCraft++ v1.1.0 | F3 diagnostics | frame=" + std::to_wstring(frameId_) + L" chunks=" + std::to_wstring(world_.stats().visibleChunks) + L" pos=" + std::to_wstring(physics_.position[0]) + L"," + std::to_wstring(physics_.position[1]) + L"," + std::to_wstring(physics_.position[2]) + L" yaw=" + std::to_wstring(camera_.yaw) + L" pitch=" + std::to_wstring(camera_.pitch) + (physics_.grounded ? L" grounded" : L" airborne") + (lastHit_.hit ? L" hit" : L" no-hit") + (renderer_.wireframe() ? L" wireframe" : L""); SetWindowTextW(window_.handle(), title.c_str()); }

    directcraft::platform::Win32Window window_; directcraft::voxel::World world_; directcraft::voxel::Mesh mesh_; directcraft::renderer::D3D12Renderer renderer_; directcraft::gameplay::PhysicsConfig physicsConfig_; directcraft::gameplay::PhysicsState physics_; directcraft::gameplay::CameraState camera_;
    bool debugTools_{}; bool renderTest_{}; std::wstring pipeName_; std::unique_ptr<DebugServer> server_; std::vector<directcraft::debug::PendingRequest> queued_; std::optional<Transaction> active_;
    bool running_{true}; bool diagnostics_{}; bool escapeWasDown_{}; bool f3WasDown_{}; bool f4WasDown_{}; bool f12WasDown_{}; bool leftWasDown_{}; bool rightWasDown_{}; bool injectedLeft_{}; bool injectedRight_{}; std::array<bool, 256> injectedKeys_{}; std::array<float, 2> injectedMouseDelta_{}; std::array<float, 2> rawMouseDelta_{}; std::array<float, 2> appliedMouseDelta_{};
    std::optional<std::string> captureName_; std::vector<std::string> events_; std::vector<directcraft::voxel::Int3> editedBlocks_; std::uint64_t frameId_{}; float deltaSeconds_{}; bool meshDirty_{true}; XMMATRIX view_{XMMatrixIdentity()}; XMMATRIX projection_{XMMatrixIdentity()}; XMMATRIX viewProjection_{XMMatrixIdentity()}; directcraft::voxel::RayHit lastHit_{}; std::ofstream traceOutput_; std::filesystem::path tracePath_;
};

int runBenchmark(const std::filesystem::path& output, int seed, int frames) {
    using Clock = std::chrono::high_resolution_clock;
    directcraft::voxel::World world(seed, 9, 8);
    directcraft::gameplay::PhysicsConfig physicsConfig;
    world.updateStreaming(7.5f, 7.5f);
    std::vector<double> frameMilliseconds;
    frameMilliseconds.reserve(static_cast<std::size_t>(frames));
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;
    std::size_t meshSamples = 0;
    std::size_t latestVertices = 0;
    std::size_t latestIndices = 0;
    double totalMilliseconds = 0.0;
    float playerX = 7.5f;
    float playerZ = 7.5f;
    for (int frame = 0; frame < frames; ++frame) {
        const auto start = Clock::now();
        world.updateStreaming(playerX, playerZ);
        directcraft::voxel::Mesh mesh;
        if (frame == 0 || frame % 30 == 0) mesh = world.buildRenderMesh(playerX, playerZ);
        const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        frameMilliseconds.push_back(milliseconds);
        totalMilliseconds += milliseconds;
        if (!mesh.vertices.empty()) {
            totalVertices += mesh.vertices.size();
            totalIndices += mesh.indices.size();
            latestVertices = mesh.vertices.size();
            latestIndices = mesh.indices.size();
            ++meshSamples;
        }
        playerX += 0.015f;
        playerZ += (frame % 120 == 0) ? 0.01f : 0.0f;
    }
    std::sort(frameMilliseconds.begin(), frameMilliseconds.end());
    const auto percentile = [&frameMilliseconds](double fraction) {
        if (frameMilliseconds.empty()) return 0.0;
        const std::size_t index = std::min(frameMilliseconds.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(frameMilliseconds.size() - 1)));
        return frameMilliseconds[index];
    };
    Json report = Json::object();
    report["version"] = "1.1.0";
    report["seed"] = seed;
    report["frames"] = frames;
    report["simdBackend"] = directcraft::voxel::simdBackendName();
    report["world"]["chunkSize"] = directcraft::voxel::ChunkSize;
    report["world"]["chunkHeight"] = directcraft::voxel::ChunkHeight;
    report["world"]["loadRadius"] = world.loadRadius();
    report["world"]["renderRadius"] = world.renderRadius();
    report["metrics"]["frameTimeMs"]["p50"] = percentile(0.50);
    report["metrics"]["frameTimeMs"]["p95"] = percentile(0.95);
    report["metrics"]["frameTimeMs"]["p99"] = percentile(0.99);
    report["metrics"]["frameTimeMs"]["max"] = frameMilliseconds.empty() ? 0.0 : frameMilliseconds.back();
    report["metrics"]["frameTimeMs"]["average"] = frames > 0 ? totalMilliseconds / static_cast<double>(frames) : 0.0;
    report["metrics"]["averageVertices"] = meshSamples > 0 ? static_cast<double>(totalVertices) / meshSamples : 0.0;
    report["metrics"]["averageIndices"] = meshSamples > 0 ? static_cast<double>(totalIndices) / meshSamples : 0.0;
    report["metrics"]["meshVertices"] = static_cast<int>(latestVertices);
    report["metrics"]["meshIndices"] = static_cast<int>(latestIndices);
    report["metrics"]["loadedChunks"] = static_cast<int>(world.stats().loadedChunks);
    report["metrics"]["visibleChunks"] = static_cast<int>(world.stats().visibleChunks);
    report["metrics"]["distanceCulledChunks"] = static_cast<int>(world.stats().distanceCulledChunks);
    report["metrics"]["frustumCulledChunks"] = static_cast<int>(world.stats().frustumCulledChunks);
    std::filesystem::create_directories(output.parent_path());
    std::ofstream jsonOutput(output);
    jsonOutput << report.dump() << std::endl;
    auto csv = output;
    csv.replace_extension(".csv");
    std::ofstream csvOutput(csv);
    csvOutput << "frame,frame_time_ms\n";
    for (std::size_t index = 0; index < frameMilliseconds.size(); ++index) csvOutput << index << "," << frameMilliseconds[index] << std::endl;
    return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        const std::wstring arguments = commandLine ? commandLine : L""; if (hasArgument(arguments, L"benchmark")) { const std::wstring output = commandLineValue(arguments, L"output"); const int seed = commandLineValue(arguments, L"seed").empty() ? 1337 : std::stoi(commandLineValue(arguments, L"seed")); const int frames = commandLineValue(arguments, L"frames").empty() ? 600 : std::max(1, std::stoi(commandLineValue(arguments, L"frames"))); const int result = runBenchmark(output.empty() ? std::filesystem::path(L"performance/benchmark-v1.1.0.json") : std::filesystem::path(output), seed, frames); CoUninitialize(); return result; } const bool renderTest = hasArgument(arguments, L"render-test"); const bool debugTools = hasArgument(arguments, L"debug-tools"); std::wstring pipeName = commandLineValue(arguments, L"pipe-name"); if (pipeName.empty()) pipeName = L"DirectCraftPP." + std::to_wstring(GetCurrentProcessId()); const std::wstring output = commandLineValue(arguments, L"render-test"); const std::filesystem::path screenshot = output.empty() ? L"directcraft_smoke.bmp" : std::filesystem::path(output); Game game(instance, debugTools, renderTest, pipeName); const int result = game.run(screenshot); CoUninitialize(); return result;
    } catch (const std::exception& error) { std::ofstream log("directcraft_error.log", std::ios::app); log << error.what() << '\n'; OutputDebugStringA(error.what()); OutputDebugStringA("\n"); MessageBoxA(nullptr, error.what(), "DirectCraft++ error", MB_ICONERROR | MB_OK); CoUninitialize(); return 1; }
}
