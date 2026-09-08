#include "Core/Window.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <imgui.h>

#pragma comment(lib, "shell32.lib")

// Forward declare message handler from imgui_impl_win32.cpp in global namespace
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace SlimRender {

static const wchar_t* WINDOW_CLASS_NAME = L"SlimRenderWindowClass";

Window::Window(uint32_t width, uint32_t height, const std::string& title)
    : width_(width), height_(height) {
    hinstance_ = GetModuleHandle(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinstance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    RegisterClassExW(&wc);

    RECT wr = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    std::wstring wtitle(title.begin(), title.end());

    hwnd_ = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        wtitle.c_str(),
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

    // Allow drag-drop even if elevated (UIPI)
    typedef BOOL(WINAPI* PFN_ChangeWindowMessageFilter)(UINT, DWORD);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        PFN_ChangeWindowMessageFilter pChangeWindowMessageFilter =
            reinterpret_cast<PFN_ChangeWindowMessageFilter>(GetProcAddress(user32, "ChangeWindowMessageFilter"));
        if (pChangeWindowMessageFilter) {
            pChangeWindowMessageFilter(WM_DROPFILES, 1 /* MSGFLT_ADD */);
            pChangeWindowMessageFilter(WM_COPYDATA, 1 /* MSGFLT_ADD */);
            pChangeWindowMessageFilter(0x0049 /* WM_COPYGLOBALDATA */, 1 /* MSGFLT_ADD */);
        }
    }

    DragAcceptFiles(hwnd_, TRUE);
}

Window::~Window() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(WINDOW_CLASS_NAME, hinstance_);
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

void Window::GetMouseDelta(float& dx, float& dy) {
    dx = mouseDeltaX_;
    dy = mouseDeltaY_;
}

float Window::GetMouseWheelDelta() {
    return mouseWheelDelta_;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (::ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
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
        HDROP hDrop = reinterpret_cast<HDROP>(wparam);
        UINT charCount = DragQueryFileW(hDrop, 0, nullptr, 0);
        if (charCount > 0) {
            std::vector<wchar_t> buffer(charCount + 1);
            if (DragQueryFileW(hDrop, 0, buffer.data(), charCount + 1)) {
                int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), -1, nullptr, 0, nullptr, nullptr);
                if (sizeNeeded > 1) {
                    std::string filePath(sizeNeeded - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, buffer.data(), -1, filePath.data(), sizeNeeded, nullptr, nullptr);
                    window->droppedFile_ = filePath;
                    window->hasDroppedFile_ = true;
                }
            }
        }
        DragFinish(hDrop);
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
