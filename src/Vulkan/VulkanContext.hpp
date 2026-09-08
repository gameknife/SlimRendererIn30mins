#pragma once

#include "Core/Common.hpp"
#include "Core/Window.hpp"

namespace SlimRender {

// Owns the Vulkan instance, the ray-query capable device and the one queue everything
// runs on. A single graphics+compute+present queue keeps submission logic trivial; a
// production renderer would use dedicated transfer and async-compute queues.
class VulkanContext {
public:
    explicit VulkanContext(const Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance GetInstance() const { return instance_; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice_; }
    VkDevice GetDevice() const { return device_; }
    VkSurfaceKHR GetSurface() const { return surface_; }
    VkQueue GetGraphicsQueue() const { return graphicsQueue_; }
    uint32_t GetGraphicsQueueFamily() const { return graphicsQueueFamily_; }
    VkCommandPool GetCommandPool() const { return commandPool_; }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    // Records, submits and waits for a one-shot command buffer. Simple but fully
    // synchronous, so it belongs in load/setup paths only, never in the frame loop.
    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    // Acceleration structure entry points live in an extension, so they are not exported
    // by the loader and have to be resolved through vkGetDeviceProcAddr.
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;

private:
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface(const Window& window);
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
