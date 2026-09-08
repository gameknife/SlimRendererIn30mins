#pragma once

#include "Core/Common.hpp"
#include "Core/Window.hpp"
#include "Scene/SceneTypes.hpp"

namespace SlimRender {

// Free-flying viewport camera: right mouse looks, middle mouse (or Shift+right) pans,
// wheel dollies, WASD/QE flies. `target_` is only used to scale the pan speed with the
// distance to whatever the camera was last framed on, the way a DCC viewport does.
class Camera {
public:
    Camera();

    void SetPosition(const glm::vec3& position);
    void SetTarget(const glm::vec3& target);

    void Update(float dt, const Window& window);

    glm::mat4 GetViewMatrix() const;
    // Vulkan convention: clip space Y points down, so row 1 is negated.
    glm::mat4 GetProjectionMatrix(float aspect) const;
    // OpenGL convention (Y up), which is what ImGuizmo expects.
    glm::mat4 GetGizmoProjectionMatrix(float aspect) const;

    // The path tracer restarts its accumulation whenever the view changes.
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

    bool hasMoved_ = true;
};

} // namespace SlimRender
