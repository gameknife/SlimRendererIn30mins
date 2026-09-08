#pragma once

#include "Core/Common.hpp"

namespace SlimRender {

// Every struct below is mirrored one-to-one in assets/shaders/pathtrace.comp.
// The device enables scalarBlockLayout, so GLSL lays these out with plain C rules:
// a member sits at the next offset that satisfies its own alignment, nothing more.
// Keep the two definitions in sync, or the shader will read garbage.

// Packing the UVs into the unused w components keeps a vertex at exactly 32 bytes,
// which matters because the BLAS build strides over this same array.
struct Vertex {
    glm::vec4 pos;    // xyz = position, w = uv.x
    glm::vec4 normal; // xyz = normal,   w = uv.y
};

struct Material {
    glm::vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    int32_t baseColorTextureIndex = -1; // -1 = untextured, otherwise an index into textures[]
};

// Per instance data the shader looks up with gl_InstanceCustomIndex after a hit.
struct InstanceData {
    glm::mat4 transform;
    glm::mat4 invTransform; // its transpose is the normal matrix
    uint32_t vertexOffset;  // where this primitive starts in the shared vertex buffer
    uint32_t indexOffset;
    uint32_t materialIndex;
};

// The shader reconstructs a world-space ray per pixel from these inverses.
struct CameraUBO {
    glm::mat4 invView;
    glm::mat4 invProj;
    glm::vec4 cameraPos;
};

// Push constants: 88 bytes, comfortably inside the 128 bytes Vulkan guarantees.
// The vec4s come first on purpose - their 16-byte alignment would otherwise force
// padding between the scalars and them.
struct PushConstants {
    glm::vec4 sunDirectionAndIntensity{ 0.4f, 0.8f, 0.5f, 3.5f };
    glm::vec4 sunColorAndSky{ 1.0f, 0.95f, 0.88f, 1.0f };
    // Buffer device addresses: the shader casts these back into pointers, so no
    // descriptor set is needed for any of the scene data.
    uint64_t cameraAddress = 0;
    uint64_t vertexAddress = 0;
    uint64_t indexAddress = 0;
    uint64_t materialAddress = 0;
    uint64_t instanceAddress = 0;
    uint32_t frameIndex = 0; // doubles as the accumulated sample count and the RNG seed
    uint32_t maxBounces = 4;
    uint32_t width = 0;
    uint32_t height = 0;
};
static_assert(sizeof(PushConstants) == 88, "PushConstants must match the shader block layout");

} // namespace SlimRender
