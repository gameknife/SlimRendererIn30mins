#pragma once

#include "Core/Common.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanContext.hpp"

namespace SlimRender {

// Hardware ray tracing uses a two-level BVH:
//
//   TLAS ── instance ──> BLAS (triangles, in object space)
//        └─ instance ──> BLAS
//
// A BLAS is built once per mesh primitive and stays in object space, so moving an
// object only means rewriting its 3x4 instance transform and rebuilding the (cheap)
// TLAS - the expensive triangle BVH is reused.

// One TLAS entry: which BLAS to instantiate, and where to place it.
struct GeometryInstance {
    glm::mat4 transform{ 1.0f };
    uint32_t customIndex = 0; // surfaces in the shader as gl_InstanceCustomIndex
    VkDeviceAddress blasAddress = 0;
};

class BottomLevelAS {
public:
    explicit BottomLevelAS(const VulkanContext& context) : context_(&context) {}
    ~BottomLevelAS();

    BottomLevelAS(const BottomLevelAS&) = delete;
    BottomLevelAS& operator=(const BottomLevelAS&) = delete;

    // The addresses may point into the middle of the shared vertex/index buffers.
    void Build(
        VkDeviceAddress vertexBufferAddress,
        uint32_t vertexCount,
        VkDeviceSize vertexStride,
        VkDeviceAddress indexBufferAddress,
        uint32_t triangleCount);

    VkDeviceAddress GetDeviceAddress() const { return deviceAddress_; }

private:
    const VulkanContext* context_ = nullptr;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress_ = 0;
    std::unique_ptr<VulkanBuffer> buffer_;
};

class TopLevelAS {
public:
    explicit TopLevelAS(const VulkanContext& context) : context_(&context) {}
    ~TopLevelAS();

    TopLevelAS(const TopLevelAS&) = delete;
    TopLevelAS& operator=(const TopLevelAS&) = delete;

    // May be called again to rebuild after an instance transform changed.
    void Build(const std::vector<GeometryInstance>& instances);

    VkAccelerationStructureKHR GetHandle() const { return handle_; }

private:
    const VulkanContext* context_ = nullptr;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    std::unique_ptr<VulkanBuffer> buffer_;
    std::unique_ptr<VulkanBuffer> instanceBuffer_;
};

} // namespace SlimRender
