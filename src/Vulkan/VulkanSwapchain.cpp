#include "Vulkan/VulkanSwapchain.hpp"

namespace SlimRender {

VulkanSwapchain::VulkanSwapchain(const VulkanContext& context, uint32_t width, uint32_t height)
    : context_(&context) {
    CreateFrameSyncObjects();
    CreateSwapchain(width, height);
    CreateImageViews();
    CreatePerImageSemaphores();
}

VulkanSwapchain::~VulkanSwapchain() {
    DestroySwapchain();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(context_->GetDevice(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(context_->GetDevice(), inFlightFences_[i], nullptr);
    }
}

void VulkanSwapchain::CreateSwapchain(uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &formatCount, formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &presentModeCount, presentModes.data());

    // The compute shader already tonemaps and gamma-encodes, so an UNORM (non-sRGB)
    // surface is what we want: the presentation engine must not convert again.
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        bool isPreferred = format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM;
        if (isPreferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }
    imageFormat_ = surfaceFormat.format;

    // Mailbox never blocks the renderer; FIFO (vsync) is the guaranteed fallback.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    // A currentExtent of UINT32_MAX means the surface lets us choose the size.
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent_.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1; // one spare so the driver never stalls
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context_->GetSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    // TRANSFER_DST for the path tracer blit, COLOR_ATTACHMENT for the ImGui pass on top.
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(context_->GetDevice(), &createInfo, nullptr, &swapchain_));

    vkGetSwapchainImagesKHR(context_->GetDevice(), swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(context_->GetDevice(), swapchain_, &imageCount, images_.data());
}

void VulkanSwapchain::CreateImageViews() {
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = images_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = imageFormat_;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(context_->GetDevice(), &createInfo, nullptr, &imageViews_[i]));
    }
}

void VulkanSwapchain::CreateFrameSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // so the very first wait returns immediately

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(context_->GetDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]));
        VK_CHECK(vkCreateFence(context_->GetDevice(), &fenceInfo, nullptr, &inFlightFences_[i]));
    }
}

void VulkanSwapchain::CreatePerImageSemaphores() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    renderFinishedSemaphores_.resize(images_.size());
    for (VkSemaphore& semaphore : renderFinishedSemaphores_) {
        VK_CHECK(vkCreateSemaphore(context_->GetDevice(), &semaphoreInfo, nullptr, &semaphore));
    }
}

void VulkanSwapchain::DestroySwapchain() {
    for (VkSemaphore semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(context_->GetDevice(), semaphore, nullptr);
    }
    renderFinishedSemaphores_.clear();

    for (VkImageView imageView : imageViews_) {
        vkDestroyImageView(context_->GetDevice(), imageView, nullptr);
    }
    imageViews_.clear();

    vkDestroySwapchainKHR(context_->GetDevice(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanSwapchain::Recreate(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(context_->GetDevice()); // nothing may still reference the old images
    DestroySwapchain();
    CreateSwapchain(width, height);
    CreateImageViews();
    CreatePerImageSemaphores();
}

VkResult VulkanSwapchain::AcquireNextImage(uint32_t currentFrame, uint32_t& outImageIndex) {
    vkWaitForFences(context_->GetDevice(), 1, &inFlightFences_[currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        context_->GetDevice(),
        swapchain_,
        UINT64_MAX,
        imageAvailableSemaphores_[currentFrame],
        VK_NULL_HANDLE,
        &outImageIndex);

    // Only reset once we know a submit will follow, or the next wait would deadlock.
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        vkResetFences(context_->GetDevice(), 1, &inFlightFences_[currentFrame]);
    }
    return result;
}

VkResult VulkanSwapchain::Present(uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(context_->GetGraphicsQueue(), &presentInfo);
}

} // namespace SlimRender
