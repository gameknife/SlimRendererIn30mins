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

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context_->GetDevice(), buffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context_->FindMemoryType(memRequirements.memoryTypeBits, properties);

    VkMemoryAllocateFlagsInfo flagsInfo{};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &flagsInfo;
    }

    VK_CHECK(vkAllocateMemory(context_->GetDevice(), &allocInfo, nullptr, &memory_));
    VK_CHECK(vkBindBufferMemory(context_->GetDevice(), buffer_, memory_, 0));
}

VulkanBuffer::~VulkanBuffer() {
    if (mapped_) {
        Unmap();
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->GetDevice(), buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_->GetDevice(), memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : context_(other.context_),
      buffer_(other.buffer_),
      memory_(other.memory_),
      size_(other.size_),
      mapped_(other.mapped_) {
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.mapped_ = nullptr;
    other.size_ = 0;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        if (mapped_) Unmap();
        if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(context_->GetDevice(), buffer_, nullptr);
        if (memory_ != VK_NULL_HANDLE) vkFreeMemory(context_->GetDevice(), memory_, nullptr);

        context_ = other.context_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_ = other.size_;
        mapped_ = other.mapped_;

        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.mapped_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

VkDeviceAddress VulkanBuffer::GetDeviceAddress() const {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer_;
    return vkGetBufferDeviceAddress(context_->GetDevice(), &info);
}

void* VulkanBuffer::Map(VkDeviceSize offset, VkDeviceSize size) {
    if (!mapped_) {
        VK_CHECK(vkMapMemory(context_->GetDevice(), memory_, offset, size, 0, &mapped_));
    }
    return mapped_;
}

void VulkanBuffer::Unmap() {
    if (mapped_) {
        vkUnmapMemory(context_->GetDevice(), memory_);
        mapped_ = nullptr;
    }
}

void VulkanBuffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    void* ptr = Map(offset, size);
    std::memcpy(ptr, data, static_cast<size_t>(size));
    Unmap();
}

std::unique_ptr<VulkanBuffer> VulkanBuffer::CreateDeviceLocal(
    const VulkanContext& context,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags additionalUsage) {

    // Staging buffer
    VulkanBuffer staging(
        context,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    staging.Upload(data, size);

    // Device local buffer
    auto deviceBuffer = std::make_unique<VulkanBuffer>(
        context,
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | additionalUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    // Transfer via single-time command buffer
    VkCommandBuffer cmd = context.BeginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.GetHandle(), deviceBuffer->GetHandle(), 1, &copyRegion);
    context.EndSingleTimeCommands(cmd);

    return deviceBuffer;
}

} // namespace SlimRender
