#include "debug/debug_server.h"

#include <algorithm>

namespace directcraft::debug {
namespace {
std::wstring fullPipeName(const std::wstring& name) { return name.rfind(L"\\\\.\\pipe\\", 0) == 0 ? name : L"\\\\.\\pipe\\" + name; }
}

DebugServer::DebugServer(std::wstring pipeName) : pipeName_(std::move(pipeName)) {}
DebugServer::~DebugServer() { stop(); }
void DebugServer::start() { if (!running_.exchange(true)) thread_ = std::thread(&DebugServer::worker, this); }
void DebugServer::stop() {
    if (!running_.exchange(false)) return;
    HANDLE wake = CreateFileW(fullPipeName(pipeName_).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    if (thread_.joinable()) thread_.join();
}
std::vector<PendingRequest> DebugServer::takeRequests() { std::lock_guard<std::mutex> lock(mutex_); auto result = std::move(requests_); requests_.clear(); return result; }
void DebugServer::worker() {
    const std::wstring name = fullPipeName(pipeName_);
    while (running_) {
        HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return;
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) { CloseHandle(pipe); continue; }
        std::string buffer; char chunk[4096]; DWORD bytesRead = 0;
        while (running_ && ReadFile(pipe, chunk, sizeof(chunk), &bytesRead, nullptr) && bytesRead > 0) {
            buffer.append(chunk, chunk + bytesRead);
            std::size_t newline = 0;
            while ((newline = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, newline); buffer.erase(0, newline + 1); if (!line.empty() && line.back() == '\r') line.pop_back();
                auto promise = std::make_shared<std::promise<std::string>>(); auto future = promise->get_future();
                { std::lock_guard<std::mutex> lock(mutex_); requests_.push_back({line, promise}); }
                const std::string response = future.get() + "\n"; DWORD written = 0; if (!WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr)) break;
            }
        }
        FlushFileBuffers(pipe); DisconnectNamedPipe(pipe); CloseHandle(pipe);
    }
}
} // namespace directcraft::debug
