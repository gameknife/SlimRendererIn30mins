#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <array>
#include <span>

#define VK_CHECK(result) \
    do { \
        VkResult r_ = (result); \
        if (r_ != VK_SUCCESS) { \
            char buf_[256]; \
            snprintf(buf_, sizeof(buf_), "Vulkan error: %d at %s:%d", static_cast<int>(r_), __FILE__, __LINE__); \
            std::cerr << "[VULKAN ERROR] " << buf_ << std::endl; \
            throw std::runtime_error(buf_); \
        } \
    } while (0)
