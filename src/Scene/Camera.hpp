#pragma once

#include "Core/Common.hpp"
#include "Core/Window.hpp"
#include "Scene/SceneTypes.hpp"

namespace SlimRender {

class Camera {
public:
    Camera();

    void SetPosition(const glm::vec3& position);
    void SetTarget(const glm::vec3& target);
    void SetPerspective(float fovYDegrees, float nearZ, float farZ);

    void Update(float dt, Window& window);

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspect) const;
    glm::mat4 GetStandardProjectionMatrix(float aspect) const;
    glm::mat4 GetInvViewMatrix() const;
    glm::mat4 GetInvProjectionMatrix(float aspect) const;
    glm::vec3 GetPosition() const { return position_; }

    bool HasMoved() const { return hasMoved_; }
    void ResetMoved() { hasMoved_ = false; }

    CameraUBO GetUBO(float aspect) const;

private:
    void UpdateVectors();

    glm::vec3 position_{ 0.0f, 2.0f, 5.0f };
    glm::vec3 target_{ 0.0f, 1.0f, 0.0f };
    glm::vec3 forward_{ 0.0f, 0.0f, -1.0f };
    glm::vec3 right_{ 1.0f, 0.0f, 0.0f };
    glm::vec3 up_{ 0.0f, 1.0f, 0.0f };

    float yaw_ = -90.0f;
    float pitch_ = -15.0f;
    float fovY_ = 45.0f;
    float nearZ_ = 0.01f;
    float farZ_ = 1000.0f;

    float moveSpeed_ = 5.0f;
    float mouseSensitivity_ = 0.15f;

    bool hasMoved_ = true;
};

} // namespace SlimRender
