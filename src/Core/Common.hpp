#pragma once

// Single place where the platform, Vulkan and math headers are pulled in, so that
// every other translation unit only has to include "Core/Common.hpp".

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
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Vulkan clip space is [0, 1], not OpenGL's [-1, 1]
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Every Vulkan call that can fail goes through this: it turns a VkResult into an
// exception carrying the call site, which main() reports in a message box.
#define VK_CHECK(result)                                                                  \
    do {                                                                                  \
        VkResult vkCheckResult_ = (result);                                               \
        if (vkCheckResult_ != VK_SUCCESS) {                                               \
            char vkCheckMsg_[256];                                                        \
            snprintf(vkCheckMsg_, sizeof(vkCheckMsg_), "Vulkan error %d at %s:%d",        \
                     static_cast<int>(vkCheckResult_), __FILE__, __LINE__);               \
            std::cerr << "[VULKAN ERROR] " << vkCheckMsg_ << std::endl;                   \
            throw std::runtime_error(vkCheckMsg_);                                        \
        }                                                                                 \
    } while (0)

namespace SlimRender {

// Win32 hands out UTF-16 paths; everything above the platform layer speaks UTF-8.
inline std::string WideToUtf8(const wchar_t* wide) {
    int byteCount = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (byteCount <= 1) {
        return {};
    }
    std::string utf8(static_cast<size_t>(byteCount) - 1, '\0'); // byteCount includes the null terminator
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), byteCount, nullptr, nullptr);
    return utf8;
}

inline std::wstring Utf8ToWide(const std::string& utf8) {
    int charCount = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (charCount <= 1) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(charCount) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), charCount);
    return wide;
}

} // namespace SlimRender
