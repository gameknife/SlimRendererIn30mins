#pragma once

#include "Core/Common.hpp"
#include "Vulkan/VulkanContext.hpp"

namespace SlimRender {

// One VkBuffer with its own VkDeviceMemory allocation.
//
// A real engine sub-allocates from a few large blocks (see VulkanMemoryAllocator);
// this renderer allocates per buffer, which is slower but keeps the ownership obvious.
class VulkanBuffer {
public:
    VulkanBuffer(
        const VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties);
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    VkBuffer GetHandle() const { return buffer_; }
    // The 64-bit pointer the shader dereferences; requires SHADER_DEVICE_ADDRESS usage.
    VkDeviceAddress GetDeviceAddress() const;

    void* Map();
    void Unmap();
    void Upload(const void* data, VkDeviceSize size);

    // Uploads through a staging buffer into device-local (VRAM) memory, which is where
    // geometry the GPU reads every ray belongs.
    static std::unique_ptr<VulkanBuffer> CreateDeviceLocal(
        const VulkanContext& context,
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags additionalUsage = 0);

private:
    const VulkanContext* context_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

} // namespace SlimRender
