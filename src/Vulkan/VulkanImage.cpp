#include "Vulkan/VulkanImage.hpp"
#include "Vulkan/VulkanBuffer.hpp"

namespace SlimRender {

VulkanImage::VulkanImage(
    const VulkanContext& context,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImageAspectFlags aspectFlags)
    : context_(&context), width_(width), height_(height), format_(format), aspectFlags_(aspectFlags) {

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width_;
    imageInfo.extent.height = height_;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(context_->GetDevice(), &imageInfo, nullptr, &image_));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context_->GetDevice(), image_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = context_->FindMemoryType(memRequirements.memoryTypeBits, properties);

    VK_CHECK(vkAllocateMemory(context_->GetDevice(), &allocInfo, nullptr, &memory_));
    VK_CHECK(vkBindImageMemory(context_->GetDevice(), image_, memory_, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = aspectFlags_;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(context_->GetDevice(), &viewInfo, nullptr, &view_));
}

VulkanImage::~VulkanImage() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(context_->GetDevice(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_->GetDevice(), view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(context_->GetDevice(), image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_->GetDevice(), memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

VulkanImage::VulkanImage(VulkanImage&& other) noexcept
    : context_(other.context_),
      image_(other.image_),
      memory_(other.memory_),
      view_(other.view_),
      sampler_(other.sampler_),
      width_(other.width_),
      height_(other.height_),
      format_(other.format_),
      aspectFlags_(other.aspectFlags_),
      currentLayout_(other.currentLayout_) {
    other.image_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.view_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept {
    if (this != &other) {
        if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(context_->GetDevice(), sampler_, nullptr);
        if (view_ != VK_NULL_HANDLE) vkDestroyImageView(context_->GetDevice(), view_, nullptr);
        if (image_ != VK_NULL_HANDLE) vkDestroyImage(context_->GetDevice(), image_, nullptr);
        if (memory_ != VK_NULL_HANDLE) vkFreeMemory(context_->GetDevice(), memory_, nullptr);

        context_ = other.context_;
        image_ = other.image_;
        memory_ = other.memory_;
        view_ = other.view_;
        sampler_ = other.sampler_;
        width_ = other.width_;
        height_ = other.height_;
        format_ = other.format_;
        aspectFlags_ = other.aspectFlags_;
        currentLayout_ = other.currentLayout_;

        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
    }
    return *this;
}

void VulkanImage::TransitionLayout(
    VkCommandBuffer cmd,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage) {

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = currentLayout_;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = aspectFlags_;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // Source access mask
    switch (currentLayout_) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        barrier.srcAccessMask = 0;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        break;
    default:
        barrier.srcAccessMask = 0;
        break;
    }

    // Destination access mask
    switch (newLayout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        barrier.dstAccessMask = 0;
        break;
    default:
        barrier.dstAccessMask = 0;
        break;
    }

    vkCmdPipelineBarrier(
        cmd,
        srcStage, dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    currentLayout_ = newLayout;
}

void VulkanImage::CreateSampler(VkFilter filter, VkSamplerAddressMode addressMode) {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(context_->GetDevice(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VK_CHECK(vkCreateSampler(context_->GetDevice(), &samplerInfo, nullptr, &sampler_));
}

std::unique_ptr<VulkanImage> VulkanImage::CreateStorageImage(
    const VulkanContext& context,
    uint32_t width,
    uint32_t height,
    VkFormat format) {

    auto image = std::make_unique<VulkanImage>(
        context,
        width,
        height,
        format,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VkCommandBuffer cmd = context.BeginSingleTimeCommands();
    image->TransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    context.EndSingleTimeCommands(cmd);

    return image;
}

std::unique_ptr<VulkanImage> VulkanImage::CreateTexture2D(
    const VulkanContext& context,
    uint32_t width,
    uint32_t height,
    const void* pixels,
    VkFormat format) {

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    VulkanBuffer staging(
        context,
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    staging.Upload(pixels, imageSize);

    auto image = std::make_unique<VulkanImage>(
        context,
        width,
        height,
        format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VkCommandBuffer cmd = context.BeginSingleTimeCommands();
    image->TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(cmd, staging.GetHandle(), image->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    image->TransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    context.EndSingleTimeCommands(cmd);

    image->CreateSampler();
    return image;
}

} // namespace SlimRender
