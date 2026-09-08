#include "Vulkan/AccelerationStructure.hpp"

namespace SlimRender {

namespace {

struct BuiltAccelerationStructure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    std::unique_ptr<VulkanBuffer> storage; // the AS lives inside this buffer
};

// Building a BLAS and a TLAS differ only in the geometry description, so the whole
// query-size / allocate / create / build dance lives here once.
BuiltAccelerationStructure BuildAccelerationStructure(
    const VulkanContext& context,
    VkAccelerationStructureTypeKHR type,
    const VkAccelerationStructureGeometryKHR& geometry,
    uint32_t primitiveCount) {

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = type;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    // 1. Ask the driver how much memory this build needs.
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    context.vkGetAccelerationStructureBuildSizesKHR(
        context.GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    // 2. Allocate the permanent storage plus a scratch buffer used only during the build.
    BuiltAccelerationStructure result;
    result.storage = std::make_unique<VulkanBuffer>(
        context,
        sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VulkanBuffer scratchBuffer(
        context,
        sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = result.storage->GetHandle();
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = type;
    VK_CHECK(context.vkCreateAccelerationStructureKHR(context.GetDevice(), &createInfo, nullptr, &result.handle));

    buildInfo.dstAccelerationStructure = result.handle;
    buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

    // 3. Build it on the GPU.
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* rangeInfoPtr = &rangeInfo;

    VkCommandBuffer cmd = context.BeginSingleTimeCommands();
    context.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &rangeInfoPtr);

    // The build writes the BVH; the TLAS build and the tracing shader both read it back.
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    context.EndSingleTimeCommands(cmd); // waits, so the scratch buffer may die here

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = result.handle;
    result.deviceAddress = context.vkGetAccelerationStructureDeviceAddressKHR(context.GetDevice(), &addressInfo);

    return result;
}

} // namespace

BottomLevelAS::~BottomLevelAS() {
    context_->vkDestroyAccelerationStructureKHR(context_->GetDevice(), handle_, nullptr);
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
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; // no any-hit shader, so traversal can stop early
    auto& triangles = geometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    // Only the position is read here; the stride skips over the rest of the Vertex struct.
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertexBufferAddress;
    triangles.vertexStride = vertexStride;
    triangles.maxVertex = vertexCount;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = indexBufferAddress;

    auto built = BuildAccelerationStructure(
        *context_, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, triangleCount);

    handle_ = built.handle;
    deviceAddress_ = built.deviceAddress;
    buffer_ = std::move(built.storage);
}

TopLevelAS::~TopLevelAS() {
    context_->vkDestroyAccelerationStructureKHR(context_->GetDevice(), handle_, nullptr);
}

void TopLevelAS::Build(const std::vector<GeometryInstance>& instances) {
    std::vector<VkAccelerationStructureInstanceKHR> gpuInstances(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        VkAccelerationStructureInstanceKHR& gpuInstance = gpuInstances[i];
        gpuInstance.instanceCustomIndex = instances[i].customIndex;
        gpuInstance.mask = 0xFF; // no ray masking in this renderer: every ray sees everything
        gpuInstance.instanceShaderBindingTableRecordOffset = 0;
        gpuInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        gpuInstance.accelerationStructureReference = instances[i].blasAddress;

        // glm::mat4 is column-major 4x4; VkTransformMatrixKHR is row-major 3x4.
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 4; ++column) {
                gpuInstance.transform.matrix[row][column] = instances[i].transform[column][row];
            }
        }
    }

    instanceBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        gpuInstances.data(),
        sizeof(VkAccelerationStructureInstanceKHR) * gpuInstances.size(),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instanceBuffer_->GetDeviceAddress();

    auto built = BuildAccelerationStructure(
        *context_,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        geometry,
        static_cast<uint32_t>(instances.size()));

    // Safe to drop the previous TLAS now: the build above ended with a queue wait.
    context_->vkDestroyAccelerationStructureKHR(context_->GetDevice(), handle_, nullptr);
    handle_ = built.handle;
    buffer_ = std::move(built.storage);
}

} // namespace SlimRender
