#include "Vulkan/VulkanBuffer.hpp"

namespace SlimRender {

VulkanBuffer::VulkanBuffer(
    const VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties)
    : context_(&context), size_(size) {

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size_;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(context_->GetDevice(), &bufferInfo, nullptr, &buffer_));

    // The buffer is only a description; memory is allocated and bound separately.
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context_->GetDevice(), buffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context_->FindMemoryType(memRequirements.memoryTypeBits, properties);

    // Taking a device address later requires opting in at allocation time.
    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        allocInfo.pNext = &flagsInfo;
    }

    VK_CHECK(vkAllocateMemory(context_->GetDevice(), &allocInfo, nullptr, &memory_));
    VK_CHECK(vkBindBufferMemory(context_->GetDevice(), buffer_, memory_, 0));
}

VulkanBuffer::~VulkanBuffer() {
    Unmap();
    vkDestroyBuffer(context_->GetDevice(), buffer_, nullptr);
    vkFreeMemory(context_->GetDevice(), memory_, nullptr);
}

VkDeviceAddress VulkanBuffer::GetDeviceAddress() const {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer_;
    return vkGetBufferDeviceAddress(context_->GetDevice(), &info);
}

void* VulkanBuffer::Map() {
    if (!mapped_) {
        VK_CHECK(vkMapMemory(context_->GetDevice(), memory_, 0, size_, 0, &mapped_));
    }
    return mapped_;
}

void VulkanBuffer::Unmap() {
    if (mapped_) {
        vkUnmapMemory(context_->GetDevice(), memory_);
        mapped_ = nullptr;
    }
}

void VulkanBuffer::Upload(const void* data, VkDeviceSize size) {
    std::memcpy(Map(), data, static_cast<size_t>(size));
    Unmap();
}

std::unique_ptr<VulkanBuffer> VulkanBuffer::CreateDeviceLocal(
    const VulkanContext& context,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags additionalUsage) {

    // Host-visible staging copy, which the CPU can write to directly...
    VulkanBuffer staging(
        context,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.Upload(data, size);

    // ...and the VRAM-resident copy the GPU actually reads from.
    auto deviceBuffer = std::make_unique<VulkanBuffer>(
        context,
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | additionalUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkCommandBuffer cmd = context.BeginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.GetHandle(), deviceBuffer->GetHandle(), 1, &copyRegion);
    context.EndSingleTimeCommands(cmd); // waits, so `staging` may be destroyed on return

    return deviceBuffer;
}

} // namespace SlimRender
