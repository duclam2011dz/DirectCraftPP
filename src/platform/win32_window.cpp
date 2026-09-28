#include "platform/win32_window.h"
#include <windowsx.h>
#include <stdexcept>

namespace directcraft::platform {
namespace { constexpr wchar_t WindowClassName[] = L"DirectCraftWindowClass"; }

Win32Window::Win32Window(HINSTANCE instance, int width, int height, bool visible) : instance_(instance) {
    WNDCLASSEXW windowClass{}; windowClass.cbSize = sizeof(windowClass); windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &Win32Window::windowProc; windowClass.lpszClassName = WindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW); windowClass.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&windowClass);
    RECT rectangle{0, 0, width, height}; AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    window_ = CreateWindowExW(0, WindowClassName, L"DirectCraft++ v1.0.0", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        nullptr, nullptr, instance_, this);
    if (!window_) throw std::runtime_error("Could not create the DirectCraft Win32 window.");
    if (visible) ShowWindow(window_, SW_SHOW);
}
Win32Window::~Win32Window() { if (window_) DestroyWindow(window_); UnregisterClassW(WindowClassName, instance_); }
bool Win32Window::pumpMessages() {
    mouseDelta_ = {}; MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false; TranslateMessage(&message); DispatchMessageW(&message);
    }
    return true;
}
bool Win32Window::keyDown(int virtualKey) const { return virtualKey >= 0 && virtualKey < 256 && keys_[virtualKey]; }
bool Win32Window::mouseButtonDown(bool right) const { return right ? rightMouse_ : leftMouse_; }
POINT Win32Window::consumeMouseDelta() { const POINT result = mouseDelta_; mouseDelta_ = {}; return result; }
void Win32Window::setMouseCaptured(bool captured) {
    mouseCaptured_ = captured; if (captured) { SetCapture(window_); ShowCursor(FALSE); }
    else { ReleaseCapture(); ShowCursor(TRUE); }
}
LRESULT CALLBACK Win32Window::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Win32Window*>(create->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->window_ = window; }
    return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}
LRESULT Win32Window::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_KEYDOWN: if (wParam < 256) keys_[wParam] = true; return 0;
        case WM_KEYUP: if (wParam < 256) keys_[wParam] = false; return 0;
        case WM_LBUTTONDOWN: leftMouse_ = true; setMouseCaptured(true); return 0;
        case WM_LBUTTONUP: leftMouse_ = false; return 0;
        case WM_RBUTTONDOWN: rightMouse_ = true; setMouseCaptured(true); return 0;
        case WM_RBUTTONUP: rightMouse_ = false; return 0;
        case WM_MOUSEMOVE: if (mouseCaptured_) { mouseDelta_.x += GET_X_LPARAM(lParam); mouseDelta_.y += GET_Y_LPARAM(lParam); } return 0;
        case WM_KILLFOCUS: leftMouse_ = rightMouse_ = false; setMouseCaptured(false); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window_, message, wParam, lParam);
    }
}
} // namespace directcraft::platform


