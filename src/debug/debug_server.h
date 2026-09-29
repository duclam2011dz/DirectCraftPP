#pragma once

#include <windows.h>

#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace directcraft::debug {

struct PendingRequest {
    std::string line;
    std::shared_ptr<std::promise<std::string>> response;
};

class DebugServer {
public:
    explicit DebugServer(std::wstring pipeName);
    ~DebugServer();
    void start();
    void stop();
    std::vector<PendingRequest> takeRequests();
    const std::wstring& pipeName() const { return pipeName_; }

private:
    void worker();
    std::wstring pipeName_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::vector<PendingRequest> requests_;
};

} // namespace directcraft::debug
