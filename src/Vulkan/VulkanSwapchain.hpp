#pragma once

#include "Core/Common.hpp"
#include "Vulkan/VulkanContext.hpp"

namespace SlimRender {

class VulkanSwapchain {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    VulkanSwapchain(const VulkanContext& context, uint32_t width, uint32_t height);
    ~VulkanSwapchain();

    VkSwapchainKHR GetHandle() const { return swapchain_; }
    VkFormat GetFormat() const { return imageFormat_; }
    VkExtent2D GetExtent() const { return extent_; }
    uint32_t GetImageCount() const { return static_cast<uint32_t>(images_.size()); }
    VkImage GetImage(uint32_t index) const { return images_[index]; }
    VkImageView GetImageView(uint32_t index) const { return imageViews_[index]; }

    VkResult AcquireNextImage(uint32_t currentFrame, uint32_t& outImageIndex);
    VkResult Present(uint32_t currentFrame, uint32_t imageIndex);

    void Recreate(uint32_t width, uint32_t height);

    VkSemaphore GetImageAvailableSemaphore(uint32_t frame) const { return imageAvailableSemaphores_[frame]; }
    VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const { return renderFinishedSemaphores_[imageIndex]; }
    VkFence GetInFlightFence(uint32_t frame) const { return inFlightFences_[frame]; }

private:
    void CreateSwapchain(uint32_t width, uint32_t height);
    void CreateImageViews();
    void CreateSyncObjects();
    void CleanupSwapchain();

    const VulkanContext* context_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat imageFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_{ 0, 0 };

    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores_{};
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences_{};
};

} // namespace SlimRender
