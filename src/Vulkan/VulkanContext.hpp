#pragma once

#include "Core/Common.hpp"
#include "Core/Window.hpp"

namespace SlimRender {

class VulkanContext {
public:
    VulkanContext(const Window& window);
    ~VulkanContext();

    VkInstance GetInstance() const { return instance_; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice_; }
    VkDevice GetDevice() const { return device_; }
    VkSurfaceKHR GetSurface() const { return surface_; }
    VkQueue GetGraphicsQueue() const { return graphicsQueue_; }
    uint32_t GetGraphicsQueueFamily() const { return graphicsQueueFamily_; }
    VkCommandPool GetCommandPool() const { return commandPool_; }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    // Ray tracing extension function pointers (dynamically loaded via vkGetDeviceProcAddr)
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;

private:
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface(HWND hwnd, HINSTANCE hinstance);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateCommandPool();
    void LoadRayTracingFunctions();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
};

} // namespace SlimRender
