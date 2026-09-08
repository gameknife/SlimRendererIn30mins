#pragma once

#include "Core/Common.hpp"

namespace SlimRender {

struct Vertex {
    glm::vec4 pos;    // xyz = position, w = uv.x
    glm::vec4 normal; // xyz = normal,   w = uv.y
};

struct Material {
    glm::vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    int32_t baseColorTextureIndex = -1;
    int32_t pad0 = 0;
};

struct InstanceData {
    glm::mat4 transform;
    glm::mat4 invTransform;
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t materialIndex = 0;
    uint32_t pad = 0;
};

struct CameraUBO {
    glm::mat4 invView;
    glm::mat4 invProj;
    glm::vec4 cameraPos;
};

// Push constants sent to compute ray tracing shader
// Stays well under the standard 128 bytes guaranteed by Vulkan
struct PushConstants {
    uint64_t cameraAddress = 0;
    uint64_t vertexAddress = 0;
    uint64_t indexAddress = 0;
    uint64_t materialAddress = 0;
    uint64_t instanceAddress = 0;
    uint64_t tlasHandle = 0;
    uint32_t frameIndex = 0;
    uint32_t maxBounces = 4;
    uint32_t numSamples = 1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
    alignas(16) glm::vec4 sunDirectionAndIntensity{ 0.4f, 0.8f, 0.5f, 3.5f };
    alignas(16) glm::vec4 sunColorAndSky{ 1.0f, 0.95f, 0.88f, 1.0f };
};

} // namespace SlimRender
