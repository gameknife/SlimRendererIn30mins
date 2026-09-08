#pragma once

#include "Core/Common.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanBuffer.hpp"

namespace SlimRender {

struct GeometryInstance {
    glm::mat4 transform{ 1.0f };
    uint32_t customIndex = 0; // Instance custom index (maps to primitive / mesh ID)
    uint32_t mask = 0xFF;
    VkDeviceAddress blasAddress = 0;
};

class BottomLevelAS {
public:
    BottomLevelAS(const VulkanContext& context);
    ~BottomLevelAS();

    void Build(
        VkDeviceAddress vertexBufferAddress,
        uint32_t vertexCount,
        VkDeviceSize vertexStride,
        VkDeviceAddress indexBufferAddress,
        uint32_t triangleCount);

    VkAccelerationStructureKHR GetHandle() const { return handle_; }
    VkDeviceAddress GetDeviceAddress() const { return deviceAddress_; }

private:
    const VulkanContext* context_ = nullptr;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress_ = 0;
    std::unique_ptr<VulkanBuffer> buffer_;
};

class TopLevelAS {
public:
    TopLevelAS(const VulkanContext& context);
    ~TopLevelAS();

    void Build(const std::vector<GeometryInstance>& instances);

    VkAccelerationStructureKHR GetHandle() const { return handle_; }
    VkDeviceAddress GetDeviceAddress() const { return deviceAddress_; }

private:
    const VulkanContext* context_ = nullptr;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress_ = 0;
    std::unique_ptr<VulkanBuffer> asBuffer_;
    std::unique_ptr<VulkanBuffer> instanceBuffer_;
};

} // namespace SlimRender
