#pragma once

#include "Core/Common.hpp"
#include "Vulkan/VulkanContext.hpp"

namespace SlimRender {

class VulkanImage {
public:
    VulkanImage(
        const VulkanContext& context,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);
    ~VulkanImage();

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&& other) noexcept;
    VulkanImage& operator=(VulkanImage&& other) noexcept;

    VkImage GetHandle() const { return image_; }
    VkImageView GetView() const { return view_; }
    VkSampler GetSampler() const { return sampler_; }
    VkFormat GetFormat() const { return format_; }
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    VkImageLayout GetLayout() const { return currentLayout_; }

    void TransitionLayout(
        VkCommandBuffer cmd,
        VkImageLayout newLayout,
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    void CreateSampler(VkFilter filter = VK_FILTER_LINEAR, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

    static std::unique_ptr<VulkanImage> CreateStorageImage(
        const VulkanContext& context,
        uint32_t width,
        uint32_t height,
        VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT);

    static std::unique_ptr<VulkanImage> CreateTexture2D(
        const VulkanContext& context,
        uint32_t width,
        uint32_t height,
        const void* pixels,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

private:
    const VulkanContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspectFlags_ = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace SlimRender
