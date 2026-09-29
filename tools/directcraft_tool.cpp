#include "debug/json.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
std::wstring widen(const std::string& value) { return std::wstring(value.begin(), value.end()); }
std::string readFile(const std::filesystem::path& path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
std::string argumentValue(int argc, char** argv, const std::string& name) { for (int index = 1; index + 1 < argc; ++index) if (argv[index] == name) return argv[index + 1]; return {}; }
HANDLE connectToPipe(const std::wstring& pipeName) {
    const std::wstring name = pipeName.rfind(L"\\\\.\\pipe\\", 0) == 0 ? pipeName : L"\\\\.\\pipe\\" + pipeName;
    for (int attempt = 0; attempt < 120; ++attempt) { HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); if (pipe != INVALID_HANDLE_VALUE) return pipe; std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    return INVALID_HANDLE_VALUE;
}
PROCESS_INFORMATION launchGame(const std::string& executable, const std::string& pipeName) {
    PROCESS_INFORMATION process{}; STARTUPINFOW startup{}; startup.cb = sizeof(startup); std::wstring command = L"\"" + widen(executable) + L"\" --debug-tools --pipe-name " + widen(pipeName); std::vector<wchar_t> commandLine(command.begin(), command.end()); commandLine.push_back(L'\0'); if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) throw std::runtime_error("Could not launch DirectCraft for the integration scenario."); CloseHandle(process.hThread); return process;
}
void verifyArtifactList(const directcraft::debug::Json& response, const char* key, const char* extension) {
    const auto* list = response.find(key); if (!list || !list->isArray()) throw std::runtime_error(std::string("response missing ") + key);
    for (const auto& item : list->values()) { const std::filesystem::path path(item.string()); if (!std::filesystem::exists(path)) throw std::runtime_error(std::string("missing artifact: ") + path.string()); if (extension && path.extension() != extension) throw std::runtime_error(std::string("unexpected artifact extension: ") + path.string()); }
}
void verifyState(const directcraft::debug::Json& response) {
    const auto* state = response.find("state"); if (!state || !state->isObject()) throw std::runtime_error("response missing state");
    for (const char* key : {"application", "input", "player", "camera", "world", "raycast", "mesh", "renderer", "trace"}) if (!state->find(key)) throw std::runtime_error(std::string("state missing ") + key);
    if (!state->at("camera").find("forward") || !state->at("camera").find("yaw") || !state->at("camera").find("pitch")) throw std::runtime_error("state missing camera orientation");
    if (!state->at("player").find("aabb") || !state->at("input").find("rawMouseDelta")) throw std::runtime_error("state missing physics/input diagnostics");
}
}

int main(int argc, char** argv) {
    try {
        const std::string pipe = argumentValue(argc, argv, "--pipe"); const std::string pipeName = pipe.empty() ? argumentValue(argc, argv, "--pipe-name") : pipe; if (pipeName.empty()) throw std::runtime_error("Missing --pipe or --pipe-name.");
        const std::string scenarioPath = argumentValue(argc, argv, "--scenario"); const std::string inlineJson = argumentValue(argc, argv, "--json"); const std::string outputPath = argumentValue(argc, argv, "--out"); const std::string launchPath = argumentValue(argc, argv, "--launch");
        const std::string request = !inlineJson.empty() ? inlineJson : (!scenarioPath.empty() ? readFile(scenarioPath) : "{\"commands\":[{\"op\":\"getState\"}]}"); PROCESS_INFORMATION process{}; if (!launchPath.empty()) process = launchGame(launchPath, pipeName);
        HANDLE pipeHandle = connectToPipe(widen(pipeName)); if (pipeHandle == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not connect to the DirectCraft debug pipe."); const std::string normalized = directcraft::debug::Json::parse(request).dump(); const std::string line = normalized + std::string(1, '\n'); DWORD written = 0; if (!WriteFile(pipeHandle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) throw std::runtime_error("Could not write the debug scenario.");
        std::string response; char buffer[4096]; DWORD read = 0; while (ReadFile(pipeHandle, buffer, sizeof(buffer), &read, nullptr) && read > 0) { response.append(buffer, buffer + read); const auto newline = response.find('\n'); if (newline != std::string::npos) { response.resize(newline); break; } }
        CloseHandle(pipeHandle); if (process.hProcess) { WaitForSingleObject(process.hProcess, 2000); if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 0); CloseHandle(process.hProcess); }
        if (!outputPath.empty()) { std::ofstream output(outputPath, std::ios::binary); output << response << '\n'; }
        std::cout << response << '\n'; const auto parsed = directcraft::debug::Json::parse(response); if (!parsed.find("ok") || !parsed.at("ok").boolean()) return 1; verifyState(parsed); verifyArtifactList(parsed, "screenshots", ".png"); verifyArtifactList(parsed, "traces", ".jsonl"); return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
