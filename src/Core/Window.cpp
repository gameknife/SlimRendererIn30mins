#include "Core/Window.hpp"

#include <imgui.h>
#include <shellapi.h>
#include <windowsx.h>

// Declared here rather than included: imgui_impl_win32.h would pull in the whole backend.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace SlimRender {

constexpr const wchar_t* kWindowClassName = L"SlimRenderWindowClass";

Window::Window(uint32_t width, uint32_t height, const std::string& title)
    : width_(width), height_(height) {
    hinstance_ = GetModuleHandle(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinstance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    RegisterClassExW(&wc);

    RECT wr = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        Utf8ToWide(title).c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr,
        hinstance_,
        this
    );

    if (!hwnd_) {
        throw std::runtime_error("Failed to create Win32 window!");
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Explorer runs unelevated: without lifting the UIPI filter, a drop onto an
    // elevated SlimRender window would be silently discarded.
    for (UINT dropMessage : { UINT(WM_DROPFILES), UINT(WM_COPYDATA), UINT(0x0049 /* WM_COPYGLOBALDATA */) }) {
        ChangeWindowMessageFilterEx(hwnd_, dropMessage, MSGFLT_ALLOW, nullptr);
    }

    DragAcceptFiles(hwnd_, TRUE);
}

Window::~Window() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(kWindowClassName, hinstance_);
}

void Window::PollEvents() {
    mouseDeltaX_ = 0.0f;
    mouseDeltaY_ = 0.0f;
    mouseWheelDelta_ = 0.0f;

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool Window::IsKeyDown(int vkey) const {
    if (vkey >= 0 && vkey < 256) {
        return keys_[vkey];
    }
    return false;
}

bool Window::IsMouseButtonDown(int button) const {
    if (button >= 0 && button < 3) {
        return mouseButtons_[button];
    }
    return false;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // ImGui gets first look: while the cursor is over a panel the UI owns the event.
    if (::ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return 1;
    }

    Window* window = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = reinterpret_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!window) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        window->shouldClose_ = true;
        PostQuitMessage(0);
        return 0;

    case WM_SIZE: {
        uint32_t newWidth = LOWORD(lparam);
        uint32_t newHeight = HIWORD(lparam);
        if (newWidth > 0 && newHeight > 0 && (newWidth != window->width_ || newHeight != window->height_)) {
            window->width_ = newWidth;
            window->height_ = newHeight;
            window->resized_ = true;
        }
        return 0;
    }

    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        UINT charCount = DragQueryFileW(drop, 0, nullptr, 0); // excludes the null terminator
        if (charCount > 0) {
            std::vector<wchar_t> path(charCount + 1);
            if (DragQueryFileW(drop, 0, path.data(), charCount + 1)) {
                window->droppedFile_ = WideToUtf8(path.data());
                window->hasDroppedFile_ = !window->droppedFile_.empty();
            }
        }
        DragFinish(drop);
        return 0;
    }

    case WM_KEYDOWN:
        if (wparam < 256) {
            window->keys_[wparam] = true;
        }
        if (wparam == VK_ESCAPE) {
            window->shouldClose_ = true;
        }
        return 0;

    case WM_KEYUP:
        if (wparam < 256) {
            window->keys_[wparam] = false;
        }
        return 0;

    case WM_LBUTTONDOWN:
        window->mouseButtons_[0] = true;
        SetCapture(hwnd);
        return 0;

    case WM_LBUTTONUP:
        window->mouseButtons_[0] = false;
        ReleaseCapture();
        return 0;

    case WM_RBUTTONDOWN:
        window->mouseButtons_[1] = true;
        SetCapture(hwnd);
        return 0;

    case WM_RBUTTONUP:
        window->mouseButtons_[1] = false;
        ReleaseCapture();
        return 0;

    case WM_MBUTTONDOWN:
        window->mouseButtons_[2] = true;
        return 0;

    case WM_MBUTTONUP:
        window->mouseButtons_[2] = false;
        return 0;

    case WM_MOUSEMOVE: {
        float x = static_cast<float>(GET_X_LPARAM(lparam));
        float y = static_cast<float>(GET_Y_LPARAM(lparam));

        if (window->firstMouse_) {
            window->lastMouseX_ = x;
            window->lastMouseY_ = y;
            window->firstMouse_ = false;
        }

        window->mouseDeltaX_ += (x - window->lastMouseX_);
        window->mouseDeltaY_ += (y - window->lastMouseY_);
        window->lastMouseX_ = x;
        window->lastMouseY_ = y;
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wparam);
        window->mouseWheelDelta_ += static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace SlimRender
