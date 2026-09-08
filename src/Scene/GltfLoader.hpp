#pragma once

#include "Core/Common.hpp"
#include "Scene/SceneTypes.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanImage.hpp"
#include "Vulkan/AccelerationStructure.hpp"

#include <string>
#include <vector>
#include <memory>

namespace SlimRender {

struct SceneObject {
    std::string name;
    uint32_t instanceIndex = 0;
    uint32_t materialIndex = 0;
    glm::mat4 initialTransform{ 1.0f };
};

class GltfScene {
public:
    GltfScene(const VulkanContext& context, const std::string& filepath);
    ~GltfScene() = default;

    VkDeviceAddress GetVertexBufferAddress() const { return vertexBuffer_->GetDeviceAddress(); }
    VkDeviceAddress GetIndexBufferAddress() const { return indexBuffer_->GetDeviceAddress(); }
    VkDeviceAddress GetMaterialBufferAddress() const { return materialBuffer_->GetDeviceAddress(); }
    VkDeviceAddress GetInstanceBufferAddress() const { return instanceBuffer_->GetDeviceAddress(); }
    VkAccelerationStructureKHR GetTLASHandle() const { return tlas_->GetHandle(); }

    const std::vector<std::unique_ptr<VulkanImage>>& GetTextures() const { return textures_; }
    const VulkanImage* GetDefaultTexture() const { return defaultTexture_.get(); }

    uint32_t GetVertexCount() const { return static_cast<uint32_t>(vertices_.size()); }
    uint32_t GetIndexCount() const { return static_cast<uint32_t>(indices_.size()); }
    uint32_t GetInstanceCount() const { return static_cast<uint32_t>(instances_.size()); }

    glm::vec3 GetSceneCenter() const { return (aabbMin_ + aabbMax_) * 0.5f; }
    float GetSceneRadius() const { return glm::length(aabbMax_ - aabbMin_) * 0.5f; }

    bool HasCamera() const { return hasCamera_; }
    glm::vec3 GetCameraPosition() const { return cameraPosition_; }
    glm::vec3 GetCameraTarget() const { return cameraTarget_; }

    // Interactive Object & Material access
    const std::vector<SceneObject>& GetObjects() const { return objects_; }
    const glm::mat4& GetInstanceTransform(uint32_t instanceIndex) const { return instances_[instanceIndex].transform; }
    void SetInstanceTransform(uint32_t instanceIndex, const glm::mat4& transform);
    void ResetInstanceTransform(uint32_t instanceIndex);

    const std::vector<Material>& GetMaterials() const { return materials_; }
    Material& GetMaterial(uint32_t index) { return materials_[index]; }
    void SetMaterial(uint32_t index, const Material& mat);

    void UpdateInstanceBufferAndTLAS();
    void UpdateMaterialBuffer();

private:
    void LoadGltf(const std::string& filepath);
    void CreateDefaultTexture();

    const VulkanContext* context_ = nullptr;

    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Material> materials_;
    std::vector<InstanceData> instances_;
    std::vector<GeometryInstance> geometryInstances_;
    std::vector<SceneObject> objects_;

    std::unique_ptr<VulkanBuffer> vertexBuffer_;
    std::unique_ptr<VulkanBuffer> indexBuffer_;
    std::unique_ptr<VulkanBuffer> materialBuffer_;
    std::unique_ptr<VulkanBuffer> instanceBuffer_;

    std::vector<std::unique_ptr<BottomLevelAS>> blasList_;
    std::unique_ptr<TopLevelAS> tlas_;

    std::vector<std::unique_ptr<VulkanImage>> textures_;
    std::unique_ptr<VulkanImage> defaultTexture_;

    glm::vec3 aabbMin_{ 1e30f };
    glm::vec3 aabbMax_{ -1e30f };

    bool hasCamera_ = false;
    glm::vec3 cameraPosition_{ 0.0f };
    glm::vec3 cameraTarget_{ 0.0f };
};

} // namespace SlimRender
