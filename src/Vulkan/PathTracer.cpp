#include "Vulkan/PathTracer.hpp"

#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace SlimRender {

namespace {

constexpr uint32_t kWorkgroupSize = 16; // must match local_size_x/y in the shader

// Descriptor bindings, mirrored at the top of assets/shaders/pathtrace.comp.
enum Binding : uint32_t {
    kBindingTLAS = 0,
    kBindingAccumulationImage = 1,
    kBindingOutputImage = 2,
    kBindingTextures = 3,
};

std::vector<char> ReadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    std::vector<char> buffer(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return buffer;
}

VkWriteDescriptorSet MakeImageWrite(VkDescriptorSet set, uint32_t binding, const VkDescriptorImageInfo* info) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = info;
    return write;
}

} // namespace

PathTracer::PathTracer(
    const VulkanContext& context,
    const GltfScene& scene,
    uint32_t width,
    uint32_t height,
    const std::string& shaderSpvPath)
    : context_(&context), scene_(&scene) {

    // Host-visible so the camera can be rewritten every frame without a staging copy.
    cameraBuffer_ = std::make_unique<VulkanBuffer>(
        *context_,
        sizeof(CameraUBO),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    CreateImages(width, height);
    CreateDescriptors(scene);
    CreatePipeline(shaderSpvPath);
}

PathTracer::~PathTracer() {
    vkDestroyPipeline(context_->GetDevice(), computePipeline_, nullptr);
    vkDestroyPipelineLayout(context_->GetDevice(), pipelineLayout_, nullptr);
    vkDestroyShaderModule(context_->GetDevice(), shaderModule_, nullptr);
    vkDestroyDescriptorPool(context_->GetDevice(), descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(context_->GetDevice(), descriptorSetLayout_, nullptr);
}

void PathTracer::CreateImages(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;

    // Full float precision: the running sum grows without bound as samples accumulate.
    accumulationImage_ = VulkanImage::CreateStorageImage(*context_, width, height, VK_FORMAT_R32G32B32A32_SFLOAT);
    outputImage_ = VulkanImage::CreateStorageImage(*context_, width, height, VK_FORMAT_R8G8B8A8_UNORM);
}

void PathTracer::CreateDescriptors(const GltfScene& scene) {
    // Only the TLAS, the two images and the textures need descriptors; all the geometry
    // reaches the shader as buffer device addresses in the push constants instead.
    const auto& textures = scene.GetTextures();
    uint32_t textureCount = std::max<uint32_t>(1, static_cast<uint32_t>(textures.size()));

    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {{
        { kBindingTLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { kBindingAccumulationImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { kBindingOutputImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { kBindingTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    }};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(context_->GetDevice(), &layoutInfo, nullptr, &descriptorSetLayout_));

    std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount },
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    VK_CHECK(vkCreateDescriptorPool(context_->GetDevice(), &poolInfo, nullptr, &descriptorPool_));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;
    VK_CHECK(vkAllocateDescriptorSets(context_->GetDevice(), &allocInfo, &descriptorSet_));

    // A scene without textures still binds one entry, so the shader array is never empty.
    std::vector<VkDescriptorImageInfo> textureInfos(textureCount);
    for (uint32_t i = 0; i < textureCount; ++i) {
        const VulkanImage* texture = textures.empty() ? scene.GetDefaultTexture() : textures[i].get();
        textureInfos[i].sampler = texture->GetSampler();
        textureInfos[i].imageView = texture->GetView();
        textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = descriptorSet_;
    textureWrite.dstBinding = kBindingTextures;
    textureWrite.descriptorCount = textureCount;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureWrite.pImageInfo = textureInfos.data();
    vkUpdateDescriptorSets(context_->GetDevice(), 1, &textureWrite, 0, nullptr);

    UpdateTLASDescriptor();
    WriteStorageImageDescriptors();
}

void PathTracer::WriteStorageImageDescriptors() {
    VkDescriptorImageInfo accumInfo{};
    accumInfo.imageView = accumulationImage_->GetView();
    accumInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageView = outputImage_->GetView();
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array writes = {
        MakeImageWrite(descriptorSet_, kBindingAccumulationImage, &accumInfo),
        MakeImageWrite(descriptorSet_, kBindingOutputImage, &outputInfo),
    };
    vkUpdateDescriptorSets(context_->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void PathTracer::UpdateTLASDescriptor() {
    VkAccelerationStructureKHR tlas = scene_->GetTLASHandle();

    VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures = &tlas;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &asInfo;
    write.dstSet = descriptorSet_;
    write.dstBinding = kBindingTLAS;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    vkUpdateDescriptorSets(context_->GetDevice(), 1, &write, 0, nullptr);
}

void PathTracer::CreatePipeline(const std::string& shaderSpvPath) {
    std::vector<char> code = ReadFile(shaderSpvPath);

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VK_CHECK(vkCreateShaderModule(context_->GetDevice(), &moduleInfo, nullptr, &shaderModule_));

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(context_->GetDevice(), &layoutInfo, nullptr, &pipelineLayout_));

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule_;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout_;
    VK_CHECK(vkCreateComputePipelines(context_->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline_));
}

void PathTracer::OnResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == width_ && height == height_)) {
        return;
    }

    vkDeviceWaitIdle(context_->GetDevice()); // the old images may still be in flight
    CreateImages(width, height);
    WriteStorageImageDescriptors();
    ResetAccumulation();
}

void PathTracer::RenderCompute(VkCommandBuffer cmd, Camera& camera, float aspect) {
    if (camera.HasMoved()) {
        ResetAccumulation();
        camera.ResetMoved();
    }

    CameraUBO ubo = camera.GetUBO(aspect);
    cameraBuffer_->Upload(&ubo, sizeof(ubo));

    // The scene reuploads its buffers when the UI edits it, so re-read the addresses
    // every frame rather than caching them.
    PushConstants pc{};
    pc.sunDirectionAndIntensity = glm::vec4(sunDirection_, sunIntensity_);
    pc.sunColorAndSky = glm::vec4(sunColor_, skyIntensity_);
    pc.cameraAddress = cameraBuffer_->GetDeviceAddress();
    pc.vertexAddress = scene_->GetVertexBufferAddress();
    pc.indexAddress = scene_->GetIndexBufferAddress();
    pc.materialAddress = scene_->GetMaterialBufferAddress();
    pc.instanceAddress = scene_->GetInstanceBufferAddress();
    pc.frameIndex = frameIndex_;
    pc.maxBounces = maxBounces_;
    pc.width = width_;
    pc.height = height_;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    // One invocation per pixel, rounded up to whole workgroups; the shader discards the
    // invocations that fall outside the image.
    vkCmdDispatch(cmd,
                  (width_ + kWorkgroupSize - 1) / kWorkgroupSize,
                  (height_ + kWorkgroupSize - 1) / kWorkgroupSize,
                  1);

    // Doubles as the barrier that orders the compute writes before the blit reads.
    outputImage_->TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    ++frameIndex_;
}

void PathTracer::BlitToSwapchain(VkCommandBuffer cmd, const VulkanSwapchain& swapchain, uint32_t imageIndex) {
    // The swapchain image is entirely overwritten, so its previous contents are
    // irrelevant and UNDEFINED is the cheapest old layout to declare.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain.GetImage(imageIndex);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // A blit rather than a copy: it rescales while the window is being resized, before
    // the render target catches up with the new swapchain extent.
    VkExtent2D extent = swapchain.GetExtent();
    VkImageBlit blit{};
    blit.srcOffsets[1] = { static_cast<int32_t>(width_), static_cast<int32_t>(height_), 1 };
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[1] = { static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(cmd,
                   outputImage_->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain.GetImage(imageIndex), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    // Hand the swapchain image to the UI pass, which draws on top of the blit.
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    outputImage_->TransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

bool PathTracer::SaveRenderToFile(const std::string& filepath) {
    vkDeviceWaitIdle(context_->GetDevice());

    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width_) * height_ * 4;
    VulkanBuffer stagingBuffer(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkCommandBuffer cmd = context_->BeginSingleTimeCommands();
    outputImage_->TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width_, height_, 1 };
    vkCmdCopyImageToBuffer(cmd, outputImage_->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer.GetHandle(), 1, &region);

    outputImage_->TransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    context_->EndSingleTimeCommands(cmd);

    std::filesystem::path path(filepath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    // outputImage_ already holds tonemapped, gamma-encoded 8-bit pixels.
    int written = stbi_write_png(filepath.c_str(), static_cast<int>(width_), static_cast<int>(height_),
                                 4, stagingBuffer.Map(), static_cast<int>(width_) * 4);
    stagingBuffer.Unmap();

    if (written) {
        std::cout << "[SlimRender] Saved rendered image to: " << filepath << std::endl;
    } else {
        std::cerr << "[SlimRender] Failed to write PNG: " << filepath << std::endl;
    }
    return written != 0;
}

} // namespace SlimRender
