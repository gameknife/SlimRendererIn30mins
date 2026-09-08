#pragma once

#include "Core/Common.hpp"
#include "Scene/Camera.hpp"
#include "Scene/GltfLoader.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanImage.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

namespace SlimRender {

// The renderer itself: one compute pipeline that traces the whole image.
//
// Each dispatch adds one sample per pixel into a float accumulation image and writes a
// tonemapped copy for display. The accumulation restarts whenever the camera, the
// lighting or the scene changes, which is what makes the viewport progressive.
class PathTracer {
public:
    PathTracer(
        const VulkanContext& context,
        const GltfScene& scene,
        uint32_t width,
        uint32_t height,
        const std::string& shaderSpvPath = "assets/shaders/pathtrace.comp.spv");
    ~PathTracer();

    PathTracer(const PathTracer&) = delete;
    PathTracer& operator=(const PathTracer&) = delete;

    // Traces one more sample per pixel into the accumulation image.
    void RenderCompute(VkCommandBuffer cmd, Camera& camera, float aspect);
    // Stretches the tonemapped result onto the swapchain image, ready for the UI pass.
    void BlitToSwapchain(VkCommandBuffer cmd, const VulkanSwapchain& swapchain, uint32_t imageIndex);

    // Must be called after the scene rebuilds its TLAS: the handle changes.
    void UpdateTLASDescriptor();
    bool SaveRenderToFile(const std::string& filepath);

    void OnResize(uint32_t width, uint32_t height);
    void ResetAccumulation() { frameIndex_ = 0; }
    uint32_t GetAccumulatedFrames() const { return frameIndex_; }

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }

    // Every setter restarts the accumulation, since the samples already gathered were
    // drawn from a different image.
    uint32_t GetMaxBounces() const { return maxBounces_; }
    void SetMaxBounces(uint32_t bounces) { maxBounces_ = bounces; ResetAccumulation(); }

    const glm::vec3& GetSunDirection() const { return sunDirection_; }
    void SetSunDirection(const glm::vec3& direction) { sunDirection_ = glm::normalize(direction); ResetAccumulation(); }

    float GetSunIntensity() const { return sunIntensity_; }
    void SetSunIntensity(float intensity) { sunIntensity_ = intensity; ResetAccumulation(); }

    const glm::vec3& GetSunColor() const { return sunColor_; }
    void SetSunColor(const glm::vec3& color) { sunColor_ = color; ResetAccumulation(); }

    float GetSkyIntensity() const { return skyIntensity_; }
    void SetSkyIntensity(float intensity) { skyIntensity_ = intensity; ResetAccumulation(); }

private:
    void CreateImages(uint32_t width, uint32_t height);
    void CreateDescriptors(const GltfScene& scene);
    void CreatePipeline(const std::string& shaderSpvPath);
    void WriteStorageImageDescriptors();

    const VulkanContext* context_ = nullptr;
    const GltfScene* scene_ = nullptr;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t frameIndex_ = 0; // samples accumulated so far
    uint32_t maxBounces_ = 4;

    glm::vec3 sunDirection_ = glm::normalize(glm::vec3(0.4f, 0.8f, 0.5f));
    float sunIntensity_ = 3.5f;
    glm::vec3 sunColor_{ 1.0f, 0.95f, 0.88f };
    float skyIntensity_ = 1.0f;

    std::unique_ptr<VulkanImage> accumulationImage_; // rgba32f running sum of all samples
    std::unique_ptr<VulkanImage> outputImage_;       // rgba8 tonemapped result
    std::unique_ptr<VulkanBuffer> cameraBuffer_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline computePipeline_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
};

} // namespace SlimRender
