#include "Scene/Camera.hpp"

namespace SlimRender {

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
    pitch_ = glm::degrees(asin(std::clamp(dir.y, -0.999f, 0.999f)));
    yaw_ = glm::degrees(atan2(dir.z, dir.x));
    hasMoved_ = true;
    UpdateVectors();
}

void Camera::SetPerspective(float fovYDegrees, float nearZ, float farZ) {
    fovY_ = fovYDegrees;
    nearZ_ = nearZ;
    farZ_ = farZ;
    hasMoved_ = true;
}

void Camera::UpdateVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front.y = sin(glm::radians(pitch_));
    front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    forward_ = glm::normalize(front);
    right_ = glm::normalize(glm::cross(forward_, glm::vec3(0.0f, 1.0f, 0.0f)));
    up_ = glm::normalize(glm::cross(right_, forward_));
}

void Camera::Update(float dt, Window& window) {
    if (window.IsInputCaptured()) {
        return;
    }

    float dx = 0.0f, dy = 0.0f;
    window.GetMouseDelta(dx, dy);
    float wheel = window.GetMouseWheelDelta();

    // Right mouse button to rotate
    if (window.IsMouseButtonDown(1)) {
        if (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f) {
            yaw_ += dx * mouseSensitivity_;
            pitch_ -= dy * mouseSensitivity_;
            pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
            UpdateVectors();
            hasMoved_ = true;
        }
    }

    // Middle mouse button or Shift+Right to pan
    if (window.IsMouseButtonDown(2) || (window.IsMouseButtonDown(1) && window.IsKeyDown(VK_SHIFT))) {
        if (std::abs(dx) > 0.001f || std::abs(dy) > 0.001f) {
            float panSpeed = 0.005f * glm::length(position_ - target_);
            position_ -= right_ * (dx * panSpeed);
            position_ += up_ * (dy * panSpeed);
            target_ -= right_ * (dx * panSpeed);
            target_ += up_ * (dy * panSpeed);
            hasMoved_ = true;
        }
    }

    // Mouse wheel zoom
    if (std::abs(wheel) > 0.001f) {
        position_ += forward_ * (wheel * 0.5f);
        hasMoved_ = true;
    }

    // WASD keyboard movement
    float currentSpeed = moveSpeed_;
    if (window.IsKeyDown(VK_SHIFT)) {
        currentSpeed *= 3.0f;
    }
    if (window.IsKeyDown(VK_CONTROL)) {
        currentSpeed *= 0.3f;
    }

    glm::vec3 moveDir(0.0f);
    if (window.IsKeyDown('W')) moveDir += forward_;
    if (window.IsKeyDown('S')) moveDir -= forward_;
    if (window.IsKeyDown('A')) moveDir -= right_;
    if (window.IsKeyDown('D')) moveDir += right_;
    if (window.IsKeyDown('E')) moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    if (window.IsKeyDown('Q')) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

    if (glm::length2(moveDir) > 0.0001f) {
        position_ += glm::normalize(moveDir) * (currentSpeed * dt);
        hasMoved_ = true;
    }
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(position_, position_ + forward_, up_);
}

glm::mat4 Camera::GetProjectionMatrix(float aspect) const {
    glm::mat4 proj = glm::perspective(glm::radians(fovY_), aspect, nearZ_, farZ_);
    proj[1][1] *= -1.0f; // Vulkan Y-flip
    return proj;
}

glm::mat4 Camera::GetStandardProjectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fovY_), aspect, nearZ_, farZ_);
}

glm::mat4 Camera::GetInvViewMatrix() const {
    return glm::inverse(GetViewMatrix());
}

glm::mat4 Camera::GetInvProjectionMatrix(float aspect) const {
    return glm::inverse(GetProjectionMatrix(aspect));
}

CameraUBO Camera::GetUBO(float aspect) const {
    CameraUBO ubo{};
    ubo.invView = GetInvViewMatrix();
    ubo.invProj = GetInvProjectionMatrix(aspect);
    ubo.cameraPos = glm::vec4(position_, 1.0f);
    return ubo;
}

} // namespace SlimRender
