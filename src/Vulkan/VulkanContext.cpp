#include "Vulkan/VulkanContext.hpp"

namespace SlimRender {

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[Vulkan Validation Layer] " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

VulkanContext::VulkanContext(const Window& window) {
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface(window.GetHWND(), window.GetHINSTANCE());
    PickPhysicalDevice();
    CreateLogicalDevice();
    LoadRayTracingFunctions();
    CreateCommandPool();
}

VulkanContext::~VulkanContext() {
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (debugMessenger_ != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(instance_, debugMessenger_, nullptr);
        }
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void VulkanContext::CreateInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SlimRender";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "SlimRender";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME
    };

    std::vector<const char*> instanceLayers;
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& layer : availableLayers) {
        if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
            std::cout << "[SlimRender] Enabled Vulkan Validation Layer." << std::endl;
            break;
        }
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
    createInfo.ppEnabledLayerNames = instanceLayers.data();

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_));
}

void VulkanContext::SetupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (func) {
        func(instance_, &createInfo, nullptr, &debugMessenger_);
    }
}

void VulkanContext::CreateSurface(HWND hwnd, HINSTANCE hinstance) {
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = hwnd;
    createInfo.hinstance = hinstance;

    VK_CHECK(vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_));
}

void VulkanContext::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        // Check for queue family with graphics, compute, and presentation support
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int foundQueue = -1;
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);

            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                presentSupport) {
                foundQueue = static_cast<int>(i);
                break;
            }
        }

        if (foundQueue < 0) continue;

        // Prefer discrete GPU
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physicalDevice_ = device;
            graphicsQueueFamily_ = static_cast<uint32_t>(foundQueue);
            std::cout << "[SlimRender] Selected Discrete GPU: " << props.deviceName << std::endl;
            break;
        } else if (physicalDevice_ == VK_NULL_HANDLE) {
            physicalDevice_ = device;
            graphicsQueueFamily_ = static_cast<uint32_t>(foundQueue);
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU with Ray Tracing / Ray Query support!");
    }
}

void VulkanContext::CreateLogicalDevice() {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily_;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };

    // Enable Ray Query Features
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.rayQuery = VK_TRUE;

    // Enable Acceleration Structure Features
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeatures.accelerationStructure = VK_TRUE;
    asFeatures.pNext = &rayQueryFeatures;

    // Enable Vulkan 1.2 Features (Buffer Device Address, Scalar Block Layout)
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.scalarBlockLayout = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features12.pNext = &asFeatures;

    // Enable Vulkan 1.3 Features (Synchronization2, Dynamic Rendering)
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.synchronization2 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;
    features13.pNext = &features12;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.shaderInt64 = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.pNext = &features13;

    VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
}

void VulkanContext::LoadRayTracingFunctions() {
    vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)
        vkGetDeviceProcAddr(device_, "vkCreateAccelerationStructureKHR");
    vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)
        vkGetDeviceProcAddr(device_, "vkDestroyAccelerationStructureKHR");
    vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)
        vkGetDeviceProcAddr(device_, "vkGetAccelerationStructureBuildSizesKHR");
    vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)
        vkGetDeviceProcAddr(device_, "vkCmdBuildAccelerationStructuresKHR");
    vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
        vkGetDeviceProcAddr(device_, "vkGetAccelerationStructureDeviceAddressKHR");

    if (!vkCreateAccelerationStructureKHR || !vkDestroyAccelerationStructureKHR ||
        !vkGetAccelerationStructureBuildSizesKHR || !vkCmdBuildAccelerationStructuresKHR ||
        !vkGetAccelerationStructureDeviceAddressKHR) {
        throw std::runtime_error("Failed to load required Vulkan Acceleration Structure KHR functions!");
    }
}

void VulkanContext::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_));
}

uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

VkCommandBuffer VulkanContext::BeginSingleTimeCommands() const {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer) const {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

} // namespace SlimRender
