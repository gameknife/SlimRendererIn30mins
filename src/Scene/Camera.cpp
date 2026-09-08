#include "Scene/Camera.hpp"

namespace SlimRender {

namespace {
constexpr float kMoveSpeed = 5.0f;        // world units per second
constexpr float kMouseSensitivity = 0.15f; // degrees per pixel
constexpr float kFastMultiplier = 3.0f;
constexpr float kSlowMultiplier = 0.3f;
constexpr float kPitchLimit = 89.0f;      // stop short of the poles to keep `up` stable
} // namespace

Camera::Camera() {
    UpdateVectors();
}

void Camera::SetPosition(const glm::vec3& position) {
    position_ = position;
    hasMoved_ = true;
    UpdateVectors();
}

void Camera::SetTarget(const glm::vec3& target) {
    target_ = target;
    glm::vec3 dir = glm::normalize(target_ - position_);
    pitch_ = glm::degrees(std::asin(std::clamp(dir.y, -0.999f, 0.999f)));
    yaw_ = glm::degrees(std::atan2(dir.z, dir.x));
    hasMoved_ = true;
    UpdateVectors();
}

void Camera::UpdateVectors() {
    forward_ = glm::normalize(glm::vec3(
        std::cos(glm::radians(yaw_)) * std::cos(glm::radians(pitch_)),
        std::sin(glm::radians(pitch_)),
        std::sin(glm::radians(yaw_)) * std::cos(glm::radians(pitch_))));
    right_ = glm::normalize(glm::cross(forward_, glm::vec3(0.0f, 1.0f, 0.0f)));
    up_ = glm::normalize(glm::cross(right_, forward_));
}

void Camera::Update(float dt, const Window& window) {
    if (window.IsInputCaptured()) {
        return; // the cursor is over a panel or dragging a gizmo
    }

    const glm::vec2 mouse = window.GetMouseDelta();
    const bool mouseMoved = glm::length2(mouse) > 1e-6f;
    const bool rotating = window.IsMouseButtonDown(1) && !window.IsKeyDown(VK_SHIFT);
    const bool panning = window.IsMouseButtonDown(2) ||
                         (window.IsMouseButtonDown(1) && window.IsKeyDown(VK_SHIFT));

    if (rotating && mouseMoved) {
        yaw_ += mouse.x * kMouseSensitivity;
        pitch_ = std::clamp(pitch_ - mouse.y * kMouseSensitivity, -kPitchLimit, kPitchLimit);
        UpdateVectors();
        hasMoved_ = true;
    }

    if (panning && mouseMoved) {
        // Scale panning with the viewing distance so it feels the same at any zoom level.
        const float panSpeed = 0.005f * glm::length(position_ - target_);
        const glm::vec3 offset = up_ * (mouse.y * panSpeed) - right_ * (mouse.x * panSpeed);
        position_ += offset;
        target_ += offset;
        hasMoved_ = true;
    }

    const float wheel = window.GetMouseWheelDelta();
    if (std::abs(wheel) > 0.001f) {
        position_ += forward_ * (wheel * 0.5f);
        hasMoved_ = true;
    }

    glm::vec3 moveDir(0.0f);
    if (window.IsKeyDown('W')) moveDir += forward_;
    if (window.IsKeyDown('S')) moveDir -= forward_;
    if (window.IsKeyDown('A')) moveDir -= right_;
    if (window.IsKeyDown('D')) moveDir += right_;
    if (window.IsKeyDown('E')) moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    if (window.IsKeyDown('Q')) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

    if (glm::length2(moveDir) > 0.0f) {
        float speed = kMoveSpeed;
        if (window.IsKeyDown(VK_SHIFT)) speed *= kFastMultiplier;
        if (window.IsKeyDown(VK_CONTROL)) speed *= kSlowMultiplier;
        position_ += glm::normalize(moveDir) * (speed * dt);
        hasMoved_ = true;
    }
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(position_, position_ + forward_, up_);
}

glm::mat4 Camera::GetProjectionMatrix(float aspect) const {
    glm::mat4 proj = GetGizmoProjectionMatrix(aspect);
    proj[1][1] *= -1.0f;
    return proj;
}

glm::mat4 Camera::GetGizmoProjectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fovY_), aspect, nearZ_, farZ_);
}

CameraUBO Camera::GetUBO(float aspect) const {
    // The shader turns pixel coordinates into a world-space ray, so it needs the
    // inverses rather than the forward matrices.
    CameraUBO ubo{};
    ubo.invView = glm::inverse(GetViewMatrix());
    ubo.invProj = glm::inverse(GetProjectionMatrix(aspect));
    ubo.cameraPos = glm::vec4(position_, 1.0f);
    return ubo;
}

} // namespace SlimRender
