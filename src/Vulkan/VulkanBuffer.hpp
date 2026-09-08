#pragma once

#include "Core/Common.hpp"
#include "Vulkan/VulkanContext.hpp"

namespace SlimRender {

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
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    VkBuffer GetHandle() const { return buffer_; }
    VkDeviceMemory GetMemory() const { return memory_; }
    VkDeviceSize GetSize() const { return size_; }
    VkDeviceAddress GetDeviceAddress() const;

    void* Map(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void Unmap();
    void Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

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
