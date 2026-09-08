#include "Vulkan/AccelerationStructure.hpp"

namespace SlimRender {

BottomLevelAS::BottomLevelAS(const VulkanContext& context)
    : context_(&context) {
}

BottomLevelAS::~BottomLevelAS() {
    if (handle_ != VK_NULL_HANDLE) {
        context_->vkDestroyAccelerationStructureKHR(context_->GetDevice(), handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

void BottomLevelAS::Build(
    VkDeviceAddress vertexBufferAddress,
    uint32_t vertexCount,
    VkDeviceSize vertexStride,
    VkDeviceAddress indexBufferAddress,
    uint32_t triangleCount) {

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.vertexData.deviceAddress = vertexBufferAddress;
    geometry.geometry.triangles.vertexStride = vertexStride;
    geometry.geometry.triangles.maxVertex = vertexCount;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geometry.geometry.triangles.indexData.deviceAddress = indexBufferAddress;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    context_->vkGetAccelerationStructureBuildSizesKHR(
        context_->GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &triangleCount,
        &sizeInfo
    );

    buffer_ = std::make_unique<VulkanBuffer>(
        *context_,
        sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VulkanBuffer scratchBuffer(
        *context_,
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = buffer_->GetHandle();
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    VK_CHECK(context_->vkCreateAccelerationStructureKHR(context_->GetDevice(), &createInfo, nullptr, &handle_));

    buildInfo.dstAccelerationStructure = handle_;
    buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = triangleCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    VkCommandBuffer cmd = context_->BeginSingleTimeCommands();
    context_->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    context_->EndSingleTimeCommands(cmd);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = handle_;
    deviceAddress_ = context_->vkGetAccelerationStructureDeviceAddressKHR(context_->GetDevice(), &addressInfo);
}

TopLevelAS::TopLevelAS(const VulkanContext& context)
    : context_(&context) {
}

TopLevelAS::~TopLevelAS() {
    if (handle_ != VK_NULL_HANDLE) {
        context_->vkDestroyAccelerationStructureKHR(context_->GetDevice(), handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

void TopLevelAS::Build(const std::vector<GeometryInstance>& instances) {
    if (handle_ != VK_NULL_HANDLE) {
        context_->vkDestroyAccelerationStructureKHR(context_->GetDevice(), handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances(instances.size());
    for (size_t i = 0; i < instances.size(); i++) {
        vkInstances[i].instanceCustomIndex = instances[i].customIndex;
        vkInstances[i].mask = instances[i].mask;
        vkInstances[i].instanceShaderBindingTableRecordOffset = 0;
        vkInstances[i].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInstances[i].accelerationStructureReference = instances[i].blasAddress;

        // glm::mat4 is column-major; VkTransformMatrixKHR is 3x4 row-major
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                vkInstances[i].transform.matrix[r][c] = instances[i].transform[c][r];
            }
        }
    }

    VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size();
    instanceBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        vkInstances.data(),
        instanceBufferSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instanceBuffer_->GetDeviceAddress();

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    context_->vkGetAccelerationStructureBuildSizesKHR(
        context_->GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &sizeInfo
    );

    asBuffer_ = std::make_unique<VulkanBuffer>(
        *context_,
        sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VulkanBuffer scratchBuffer(
        *context_,
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = asBuffer_->GetHandle();
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VK_CHECK(context_->vkCreateAccelerationStructureKHR(context_->GetDevice(), &createInfo, nullptr, &handle_));

    buildInfo.dstAccelerationStructure = handle_;
    buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instanceCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    VkCommandBuffer cmd = context_->BeginSingleTimeCommands();
    context_->vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    context_->EndSingleTimeCommands(cmd);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = handle_;
    deviceAddress_ = context_->vkGetAccelerationStructureDeviceAddressKHR(context_->GetDevice(), &addressInfo);
}

} // namespace SlimRender
