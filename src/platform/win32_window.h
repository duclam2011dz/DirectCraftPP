#pragma once
#include <windows.h>

namespace directcraft::platform {
class Win32Window {
public:
    Win32Window(HINSTANCE instance, int width, int height, bool visible);
    ~Win32Window();
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    bool pumpMessages();
    HWND handle() const { return window_; }
    bool keyDown(int virtualKey) const;
    bool mouseButtonDown(bool right) const;
    POINT consumeMouseDelta();
    void setMouseCaptured(bool captured);
    bool mouseCaptured() const { return mouseCaptured_; }
private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    HINSTANCE instance_{}; HWND window_{}; bool keys_[256]{};
    bool leftMouse_{false}; bool rightMouse_{false}; bool mouseCaptured_{false}; POINT mouseDelta_{};
};
} // namespace directcraft::platform
