#include "Vulkan/PathTracer.hpp"
#include <filesystem>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace SlimRender {

static std::vector<char> ReadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

PathTracer::PathTracer(
    const VulkanContext& context,
    const GltfScene& scene,
    uint32_t width,
    uint32_t height,
    const std::string& shaderSpvPath)
    : context_(&context), scene_(&scene), width_(width), height_(height) {

    cameraBuffer_ = std::make_unique<VulkanBuffer>(
        *context_,
        sizeof(CameraUBO),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    CreateImages(width, height);
    CreateDescriptors(scene);
    CreatePipeline(shaderSpvPath);
}

PathTracer::~PathTracer() {
    if (computePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->GetDevice(), computePipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_->GetDevice(), pipelineLayout_, nullptr);
    }
    if (shaderModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(context_->GetDevice(), shaderModule_, nullptr);
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_->GetDevice(), descriptorPool_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_->GetDevice(), descriptorSetLayout_, nullptr);
    }
}

void PathTracer::CreateImages(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;

    accumulationImage_ = VulkanImage::CreateStorageImage(
        *context_, width, height, VK_FORMAT_R32G32B32A32_SFLOAT);

    outputImage_ = VulkanImage::CreateStorageImage(
        *context_, width, height, VK_FORMAT_R8G8B8A8_UNORM);
}

void PathTracer::CreateDescriptors(const GltfScene& scene) {
    uint32_t textureCount = static_cast<uint32_t>(scene.GetTextures().size());
    if (textureCount == 0) {
        textureCount = 1; // Default white texture
    }

    // 1. Descriptor Set Layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // Binding 0: TLAS
        { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        // Binding 1: Accumulation image
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        // Binding 2: Output image
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        // Binding 3: Textures array
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(context_->GetDevice(), &layoutInfo, nullptr, &descriptorSetLayout_));

    // 2. Descriptor Pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    VK_CHECK(vkCreateDescriptorPool(context_->GetDevice(), &poolInfo, nullptr, &descriptorPool_));

    // 3. Allocate Descriptor Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;

    VK_CHECK(vkAllocateDescriptorSets(context_->GetDevice(), &allocInfo, &descriptorSet_));

    // 4. Update Descriptor Set
    // Write 0: TLAS
    VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    VkAccelerationStructureKHR tlasHandle = scene.GetTLASHandle();
    asInfo.pAccelerationStructures = &tlasHandle;

    VkWriteDescriptorSet asWrite{};
    asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    asWrite.pNext = &asInfo;
    asWrite.dstSet = descriptorSet_;
    asWrite.dstBinding = 0;
    asWrite.dstArrayElement = 0;
    asWrite.descriptorCount = 1;
    asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    // Write 1: Accumulation Image
    VkDescriptorImageInfo accumInfo{};
    accumInfo.imageView = accumulationImage_->GetView();
    accumInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet accumWrite{};
    accumWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accumWrite.dstSet = descriptorSet_;
    accumWrite.dstBinding = 1;
    accumWrite.dstArrayElement = 0;
    accumWrite.descriptorCount = 1;
    accumWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumWrite.pImageInfo = &accumInfo;

    // Write 2: Output Image
    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageView = outputImage_->GetView();
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet outputWrite{};
    outputWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    outputWrite.dstSet = descriptorSet_;
    outputWrite.dstBinding = 2;
    outputWrite.dstArrayElement = 0;
    outputWrite.descriptorCount = 1;
    outputWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputWrite.pImageInfo = &outputInfo;

    // Write 3: Textures
    std::vector<VkDescriptorImageInfo> textureInfos(textureCount);
    if (scene.GetTextures().empty()) {
        textureInfos[0].sampler = scene.GetDefaultTexture()->GetSampler();
        textureInfos[0].imageView = scene.GetDefaultTexture()->GetView();
        textureInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else {
        for (size_t i = 0; i < scene.GetTextures().size(); ++i) {
            textureInfos[i].sampler = scene.GetTextures()[i]->GetSampler();
            textureInfos[i].imageView = scene.GetTextures()[i]->GetView();
            textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = descriptorSet_;
    textureWrite.dstBinding = 3;
    textureWrite.dstArrayElement = 0;
    textureWrite.descriptorCount = textureCount;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureWrite.pImageInfo = textureInfos.data();

    std::array<VkWriteDescriptorSet, 4> writes = { asWrite, accumWrite, outputWrite, textureWrite };
    vkUpdateDescriptorSets(context_->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void PathTracer::CreatePipeline(const std::string& shaderSpvPath) {
    auto code = ReadFile(shaderSpvPath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VK_CHECK(vkCreateShaderModule(context_->GetDevice(), &createInfo, nullptr, &shaderModule_));

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(context_->GetDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout_));

    VkComputePipelineCreateInfo computeInfo{};
    computeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeInfo.stage.module = shaderModule_;
    computeInfo.stage.pName = "main";
    computeInfo.layout = pipelineLayout_;

    VK_CHECK(vkCreateComputePipelines(context_->GetDevice(), VK_NULL_HANDLE, 1, &computeInfo, nullptr, &computePipeline_));
}

void PathTracer::OnResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == width_ && height == height_)) {
        return;
    }

    vkDeviceWaitIdle(context_->GetDevice());

    CreateImages(width, height);

    // Update descriptor set with new image views
    VkDescriptorImageInfo accumInfo{};
    accumInfo.imageView = accumulationImage_->GetView();
    accumInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet accumWrite{};
    accumWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accumWrite.dstSet = descriptorSet_;
    accumWrite.dstBinding = 1;
    accumWrite.dstArrayElement = 0;
    accumWrite.descriptorCount = 1;
    accumWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accumWrite.pImageInfo = &accumInfo;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageView = outputImage_->GetView();
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet outputWrite{};
    outputWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    outputWrite.dstSet = descriptorSet_;
    outputWrite.dstBinding = 2;
    outputWrite.dstArrayElement = 0;
    outputWrite.descriptorCount = 1;
    outputWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputWrite.pImageInfo = &outputInfo;

    std::array<VkWriteDescriptorSet, 2> writes = { accumWrite, outputWrite };
    vkUpdateDescriptorSets(context_->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    ResetAccumulation();
}

void PathTracer::RenderCompute(VkCommandBuffer cmd, Camera& camera, float aspect) {
    if (camera.HasMoved()) {
        ResetAccumulation();
        camera.ResetMoved();
    }

    // 1. Update Camera UBO
    CameraUBO ubo = camera.GetUBO(aspect);
    cameraBuffer_->Upload(&ubo, sizeof(CameraUBO));

    // 2. Set Push Constants
    PushConstants pc{};
    pc.cameraAddress = cameraBuffer_->GetDeviceAddress();
    pc.vertexAddress = scene_->GetVertexBufferAddress();
    pc.indexAddress = scene_->GetIndexBufferAddress();
    pc.materialAddress = scene_->GetMaterialBufferAddress();
    pc.instanceAddress = scene_->GetInstanceBufferAddress();
    pc.tlasHandle = 0; // Accessed via binding 0
    pc.frameIndex = frameIndex_;
    pc.maxBounces = maxBounces_;
    pc.numSamples = 1;
    pc.width = width_;
    pc.height = height_;
    pc.pad0 = 0;
    pc.sunDirectionAndIntensity = glm::vec4(sunDirection_, sunIntensity_);
    pc.sunColorAndSky = glm::vec4(sunColor_, skyIntensity_);

    // 3. Dispatch Compute
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    uint32_t groupX = (width_ + 15) / 16;
    uint32_t groupY = (height_ + 15) / 16;
    vkCmdDispatch(cmd, groupX, groupY, 1);

    // 4. Memory Barrier: Compute writes to outputImage -> Transfer reads outputImage
    outputImage_->TransitionLayout(
        cmd,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    frameIndex_++;
}

void PathTracer::BlitToSwapchain(VkCommandBuffer cmd, const VulkanSwapchain& swapchain, uint32_t imageIndex) {
    // 1. Swapchain Image Transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain.GetImage(imageIndex);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );

    // 2. Blit outputImage to Swapchain Image
    VkImageBlit blit{};
    blit.srcOffsets[0] = { 0, 0, 0 };
    blit.srcOffsets[1] = { static_cast<int32_t>(width_), static_cast<int32_t>(height_), 1 };
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;

    VkExtent2D scExtent = swapchain.GetExtent();
    blit.dstOffsets[0] = { 0, 0, 0 };
    blit.dstOffsets[1] = { static_cast<int32_t>(scExtent.width), static_cast<int32_t>(scExtent.height), 1 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(
        cmd,
        outputImage_->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchain.GetImage(imageIndex), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_LINEAR
    );

    // 3. Swapchain Image Transition: TRANSFER_DST_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL (for ImGui / rendering)
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );

    // 4. Transition outputImage back to GENERAL for next frame compute
    outputImage_->TransitionLayout(
        cmd,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );
}

void PathTracer::Render(
    VkCommandBuffer cmd,
    const VulkanSwapchain& swapchain,
    uint32_t imageIndex,
    Camera& camera,
    float aspect) {

    RenderCompute(cmd, camera, aspect);
    BlitToSwapchain(cmd, swapchain, imageIndex);

    // Swapchain Image Transition: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain.GetImage(imageIndex);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );
}

void PathTracer::UpdateTLASDescriptor() {
    VkAccelerationStructureKHR tlasHandle = scene_->GetTLASHandle();
    VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
    asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &tlasHandle;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &asWrite;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    vkUpdateDescriptorSets(context_->GetDevice(), 1, &write, 0, nullptr);
}

bool PathTracer::SaveRenderToFile(const std::string& filepath) {
    vkDeviceWaitIdle(context_->GetDevice());

    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width_) * height_ * 4;
    VulkanBuffer stagingBuffer(
        *context_,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    VkCommandBuffer cmd = context_->BeginSingleTimeCommands();

    outputImage_->TransitionLayout(
        cmd,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width_, height_, 1 };

    vkCmdCopyImageToBuffer(
        cmd,
        outputImage_->GetHandle(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer.GetHandle(),
        1,
        &region
    );

    outputImage_->TransitionLayout(
        cmd,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );

    context_->EndSingleTimeCommands(cmd);

    void* mapped = nullptr;
    vkMapMemory(context_->GetDevice(), stagingBuffer.GetMemory(), 0, bufferSize, 0, &mapped);
    if (!mapped) return false;

    // Create parent directories if needed
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    int success = stbi_write_png(filepath.c_str(), width_, height_, 4, mapped, width_ * 4);
    vkUnmapMemory(context_->GetDevice(), stagingBuffer.GetMemory());

    if (success) {
        std::cout << "[SlimRender] Saved rendered image to: " << filepath << std::endl;
    }
    return success != 0;
}

} // namespace SlimRender
