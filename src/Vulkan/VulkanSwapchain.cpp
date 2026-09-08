#include "Vulkan/VulkanSwapchain.hpp"

namespace SlimRender {

VulkanSwapchain::VulkanSwapchain(const VulkanContext& context, uint32_t width, uint32_t height)
    : context_(&context) {
    CreateSwapchain(width, height);
    CreateImageViews();
    CreateSyncObjects();
}

VulkanSwapchain::~VulkanSwapchain() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(context_->GetDevice(), imageAvailableSemaphores_[i], nullptr);
        }
        if (inFlightFences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(context_->GetDevice(), inFlightFences_[i], nullptr);
        }
    }
    for (auto sem : renderFinishedSemaphores_) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(context_->GetDevice(), sem, nullptr);
        }
    }
    renderFinishedSemaphores_.clear();
    CleanupSwapchain();
}

void VulkanSwapchain::CreateSwapchain(uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &formatCount, formats.data());

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(context_->GetPhysicalDevice(), context_->GetSurface(), &presentModeCount, presentModes.data());

    // Preferred format: B8G8R8A8_UNORM or R8G8B8A8_UNORM
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = f;
            break;
        }
    }
    imageFormat_ = surfaceFormat.format;

    // Preferred present mode: Mailbox (triple buffering) or FIFO (vsync)
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& pm : presentModes) {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = pm;
            break;
        }
    }

    // Extent
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent_.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context_->GetSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(context_->GetDevice(), &createInfo, nullptr, &swapchain_));

    vkGetSwapchainImagesKHR(context_->GetDevice(), swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(context_->GetDevice(), swapchain_, &imageCount, images_.data());
}

void VulkanSwapchain::CreateImageViews() {
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = images_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = imageFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(context_->GetDevice(), &createInfo, nullptr, &imageViews_[i]));
    }
}

void VulkanSwapchain::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(context_->GetDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]));
        VK_CHECK(vkCreateFence(context_->GetDevice(), &fenceInfo, nullptr, &inFlightFences_[i]));
    }

    renderFinishedSemaphores_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VK_CHECK(vkCreateSemaphore(context_->GetDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]));
    }
}

void VulkanSwapchain::CleanupSwapchain() {
    for (auto sem : renderFinishedSemaphores_) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(context_->GetDevice(), sem, nullptr);
        }
    }
    renderFinishedSemaphores_.clear();

    for (auto imageView : imageViews_) {
        vkDestroyImageView(context_->GetDevice(), imageView, nullptr);
    }
    imageViews_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_->GetDevice(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanSwapchain::Recreate(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(context_->GetDevice());
    CleanupSwapchain();
    CreateSwapchain(width, height);
    CreateImageViews();
    CreateSyncObjects();
}

VkResult VulkanSwapchain::AcquireNextImage(uint32_t currentFrame, uint32_t& outImageIndex) {
    vkWaitForFences(context_->GetDevice(), 1, &inFlightFences_[currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        context_->GetDevice(),
        swapchain_,
        UINT64_MAX,
        imageAvailableSemaphores_[currentFrame],
        VK_NULL_HANDLE,
        &outImageIndex
    );

    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        vkResetFences(context_->GetDevice(), 1, &inFlightFences_[currentFrame]);
    }
    return result;
}

VkResult VulkanSwapchain::Present(uint32_t currentFrame, uint32_t imageIndex) {
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
