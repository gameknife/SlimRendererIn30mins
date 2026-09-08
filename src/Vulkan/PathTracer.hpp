#pragma once

#include "Core/Common.hpp"
#include "Scene/Camera.hpp"
#include "Scene/GltfLoader.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanImage.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

namespace SlimRender {

class PathTracer {
public:
    PathTracer(
        const VulkanContext& context,
        const GltfScene& scene,
        uint32_t width,
        uint32_t height,
        const std::string& shaderSpvPath = "assets/shaders/pathtrace.comp.spv");
    ~PathTracer();

    void Render(
        VkCommandBuffer cmd,
        const VulkanSwapchain& swapchain,
        uint32_t imageIndex,
        Camera& camera,
        float aspect);

    void RenderCompute(VkCommandBuffer cmd, Camera& camera, float aspect);
    void BlitToSwapchain(VkCommandBuffer cmd, const VulkanSwapchain& swapchain, uint32_t imageIndex);

    void UpdateTLASDescriptor();
    bool SaveRenderToFile(const std::string& filepath);

    void OnResize(uint32_t width, uint32_t height);
    void ResetAccumulation() { frameIndex_ = 0; }
    uint32_t GetAccumulatedFrames() const { return frameIndex_; }

    void SetMaxBounces(uint32_t bounces) { maxBounces_ = bounces; frameIndex_ = 0; }
    uint32_t GetMaxBounces() const { return maxBounces_; }

    // Lighting controls
    glm::vec3& GetSunDirection() { return sunDirection_; }
    const glm::vec3& GetSunDirection() const { return sunDirection_; }
    void SetSunDirection(const glm::vec3& dir) { sunDirection_ = glm::normalize(dir); frameIndex_ = 0; }

    float& GetSunIntensity() { return sunIntensity_; }
    float GetSunIntensity() const { return sunIntensity_; }
    void SetSunIntensity(float intensity) { sunIntensity_ = intensity; frameIndex_ = 0; }

    glm::vec3& GetSunColor() { return sunColor_; }
    const glm::vec3& GetSunColor() const { return sunColor_; }
    void SetSunColor(const glm::vec3& color) { sunColor_ = color; frameIndex_ = 0; }

    float& GetSkyIntensity() { return skyIntensity_; }
    float GetSkyIntensity() const { return skyIntensity_; }
    void SetSkyIntensity(float intensity) { skyIntensity_ = intensity; frameIndex_ = 0; }

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }

private:
    void CreateImages(uint32_t width, uint32_t height);
    void CreateDescriptors(const GltfScene& scene);
    void CreatePipeline(const std::string& shaderSpvPath);

    const VulkanContext* context_ = nullptr;
    const GltfScene* scene_ = nullptr;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t frameIndex_ = 0;
    uint32_t maxBounces_ = 4;

    glm::vec3 sunDirection_ = glm::normalize(glm::vec3(0.4f, 0.8f, 0.5f));
    float sunIntensity_ = 3.5f;
    glm::vec3 sunColor_ = glm::vec3(1.0f, 0.95f, 0.88f);
    float skyIntensity_ = 1.0f;

    std::unique_ptr<VulkanImage> accumulationImage_;
    std::unique_ptr<VulkanImage> outputImage_;
    std::unique_ptr<VulkanBuffer> cameraBuffer_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline computePipeline_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
};

} // namespace SlimRender
