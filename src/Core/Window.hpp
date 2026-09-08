#pragma once

#include "Core/Common.hpp"

namespace SlimRender {

class Window {
public:
    Window(uint32_t width, uint32_t height, const std::string& title);
    ~Window();

    void PollEvents();
    bool ShouldClose() const { return shouldClose_; }
    
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    bool IsResized() const { return resized_; }
    void ResetResized() { resized_ = false; }

    HWND GetHWND() const { return hwnd_; }
    HINSTANCE GetHINSTANCE() const { return hinstance_; }

    // Input state
    bool IsKeyDown(int vkey) const;
    bool IsMouseButtonDown(int button) const; // 0 = left, 1 = right, 2 = middle
    void GetMouseDelta(float& dx, float& dy);
    float GetMouseWheelDelta();

    void SetInputCaptured(bool captured) { inputCaptured_ = captured; }
    bool IsInputCaptured() const { return inputCaptured_; }

    // Drag and drop file support
    bool HasDroppedFile() const { return hasDroppedFile_; }
    std::string GetDroppedFile() {
        hasDroppedFile_ = false;
        return droppedFile_;
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool shouldClose_ = false;
    bool resized_ = false;
    bool inputCaptured_ = false;

    std::string droppedFile_;
    bool hasDroppedFile_ = false;

    bool keys_[256] = { false };
    bool mouseButtons_[3] = { false };
    float lastMouseX_ = 0.0f;
    float lastMouseY_ = 0.0f;
    float mouseDeltaX_ = 0.0f;
    float mouseDeltaY_ = 0.0f;
    float mouseWheelDelta_ = 0.0f;
    bool firstMouse_ = true;
};

} // namespace SlimRender
