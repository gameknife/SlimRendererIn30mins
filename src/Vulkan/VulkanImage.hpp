#pragma once

#include "Core/Common.hpp"
#include "Vulkan/VulkanContext.hpp"

namespace SlimRender {

// A 2D image with its memory, view and (for textures) sampler, plus the current layout.
//
// Tracking the layout inside the object is what lets callers write
// `image->TransitionLayout(cmd, newLayout)` without repeating the old one - convenient
// here because every image is only ever touched by the single queue.
class VulkanImage {
public:
    VulkanImage(
        const VulkanContext& context,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties);
    ~VulkanImage();

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

    VkImage GetHandle() const { return image_; }
    VkImageView GetView() const { return view_; }
    VkSampler GetSampler() const { return sampler_; }

    void TransitionLayout(
        VkCommandBuffer cmd,
        VkImageLayout newLayout,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage);

    // Read-write target for the compute shader, left in VK_IMAGE_LAYOUT_GENERAL.
    static std::unique_ptr<VulkanImage> CreateStorageImage(
        const VulkanContext& context,
        uint32_t width,
        uint32_t height,
        VkFormat format);

    // Sampled texture uploaded from CPU pixels, left in SHADER_READ_ONLY_OPTIMAL.
    static std::unique_ptr<VulkanImage> CreateTexture2D(
        const VulkanContext& context,
        uint32_t width,
        uint32_t height,
        const void* pixels,
        VkFormat format);

private:
    void CreateSampler();

    const VulkanContext* context_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkImageLayout currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace SlimRender
