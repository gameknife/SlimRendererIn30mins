#pragma once

#include "Core/Common.hpp"
#include "Scene/SceneTypes.hpp"
#include "Vulkan/AccelerationStructure.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanImage.hpp"

namespace SlimRender {

class Camera;

// One editable entry in the Outliner. It maps a glTF node/primitive pair onto the flat
// instance array the shader indexes into.
struct SceneObject {
    std::string name;
    uint32_t instanceIndex = 0;
    uint32_t materialIndex = 0;
    glm::mat4 initialTransform{ 1.0f }; // kept so "Reset Transform" can restore it
};

// A glTF file flattened into what the tracer needs: one shared vertex buffer, one shared
// index buffer, one BLAS per primitive, and a TLAS instancing them into the world.
//
// Node hierarchy and animation are baked away at load time; only the resulting world
// transforms survive, which is what keeps the runtime side so small.
class GltfScene {
public:
    GltfScene(const VulkanContext& context, const std::string& filepath);

    GltfScene(const GltfScene&) = delete;
    GltfScene& operator=(const GltfScene&) = delete;

    // Addresses change whenever a buffer is reuploaded, so callers must not cache them.
    VkDeviceAddress GetVertexBufferAddress() const { return vertexBuffer_->GetDeviceAddress(); }
    VkDeviceAddress GetIndexBufferAddress() const { return indexBuffer_->GetDeviceAddress(); }
    VkDeviceAddress GetMaterialBufferAddress() const { return materialBuffer_->GetDeviceAddress(); }
    VkDeviceAddress GetInstanceBufferAddress() const { return instanceBuffer_->GetDeviceAddress(); }
    VkAccelerationStructureKHR GetTLASHandle() const { return tlas_->GetHandle(); }

    const std::vector<std::unique_ptr<VulkanImage>>& GetTextures() const { return textures_; }
    const VulkanImage* GetDefaultTexture() const { return defaultTexture_.get(); }

    glm::vec3 GetSceneCenter() const { return (aabbMin_ + aabbMax_) * 0.5f; }
    float GetSceneRadius() const { return glm::length(aabbMax_ - aabbMin_) * 0.5f; }

    bool HasCamera() const { return hasCamera_; }
    glm::vec3 GetCameraPosition() const { return cameraPosition_; }
    glm::vec3 GetCameraTarget() const { return cameraTarget_; }

    // Editing: change the CPU-side copy, then push it to the GPU with one of the
    // Update* calls below.
    const std::vector<SceneObject>& GetObjects() const { return objects_; }
    const glm::mat4& GetInstanceTransform(uint32_t instanceIndex) const { return instances_[instanceIndex].transform; }
    void SetInstanceTransform(uint32_t instanceIndex, const glm::mat4& transform);
    void ResetInstanceTransform(uint32_t instanceIndex);

    size_t GetMaterialCount() const { return materials_.size(); }
    const Material& GetMaterial(uint32_t index) const { return materials_[index]; }
    void SetMaterial(uint32_t index, const Material& material);

    // Reuploads the instance buffer and rebuilds the TLAS. The BLASes are untouched:
    // only the instance transforms changed.
    void UpdateInstanceBufferAndTLAS();
    void UpdateMaterialBuffer();

private:
    void LoadGltf(const std::string& filepath);

    const VulkanContext* context_ = nullptr;

    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Material> materials_;
    std::vector<InstanceData> instances_;
    std::vector<GeometryInstance> geometryInstances_; // same order as instances_
    std::vector<SceneObject> objects_;

    std::unique_ptr<VulkanBuffer> vertexBuffer_;
    std::unique_ptr<VulkanBuffer> indexBuffer_;
    std::unique_ptr<VulkanBuffer> materialBuffer_;
    std::unique_ptr<VulkanBuffer> instanceBuffer_;

    std::vector<std::unique_ptr<BottomLevelAS>> blasList_;
    std::unique_ptr<TopLevelAS> tlas_;

    std::vector<std::unique_ptr<VulkanImage>> textures_;
    std::unique_ptr<VulkanImage> defaultTexture_; // 1x1 white, for scenes with no textures

    glm::vec3 aabbMin_{ std::numeric_limits<float>::max() };
    glm::vec3 aabbMax_{ std::numeric_limits<float>::lowest() };

    bool hasCamera_ = false;
    glm::vec3 cameraPosition_{ 0.0f };
    glm::vec3 cameraTarget_{ 0.0f };
};

// Uses the camera authored in the file if there is one, otherwise frames the bounding
// sphere from a three-quarter view.
void FrameCameraToScene(Camera& camera, const GltfScene& scene);

} // namespace SlimRender
