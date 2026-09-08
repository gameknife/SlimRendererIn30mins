#include "Scene/GltfLoader.hpp"

#include "Scene/Camera.hpp"

#include <filesystem>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace SlimRender {

namespace {

constexpr VkBufferUsageFlags kStorageBufferUsage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

// Uploads a std::vector into a device-local buffer the shader can reach by address.
template <typename T>
std::unique_ptr<VulkanBuffer> CreateStorageBuffer(
    const VulkanContext& context,
    const std::vector<T>& data,
    VkBufferUsageFlags additionalUsage = 0) {
    return VulkanBuffer::CreateDeviceLocal(
        context, data.data(), sizeof(T) * data.size(), kStorageBufferUsage | additionalUsage);
}

// glTF stores TRS or a matrix per node; both collapse to one local transform.
glm::mat4 NodeLocalTransform(const cgltf_node& node) {
    if (node.has_matrix) {
        return glm::make_mat4(node.matrix);
    }
    glm::vec3 translation = node.has_translation ? glm::make_vec3(node.translation) : glm::vec3(0.0f);
    glm::vec3 scale = node.has_scale ? glm::make_vec3(node.scale) : glm::vec3(1.0f);
    // glTF stores quaternions as xyzw, glm::quat takes wxyz.
    glm::quat rotation = node.has_rotation
        ? glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])
        : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

void WarnAboutUnsupportedExtensions(const cgltf_data& data, const std::string& filepath) {
    for (size_t i = 0; i < data.extensions_required_count; ++i) {
        const char* extension = data.extensions_required[i];
        if (!extension) {
            continue;
        }
        if (strcmp(extension, "KHR_draco_mesh_compression") == 0) {
            std::cerr << "[SlimRender WARNING] " << filepath << " requires KHR_draco_mesh_compression, "
                      << "which cgltf cannot decode. Decompress it first (gltf-transform or gltf-pipeline)."
                      << std::endl;
        } else if (strcmp(extension, "EXT_texture_webp") == 0) {
            std::cerr << "[SlimRender WARNING] " << filepath << " requires EXT_texture_webp, "
                      << "which stb_image cannot decode. Those textures fall back to white." << std::endl;
        }
    }
}

// A material may declare a texture that failed to decode, so textures_ always has one
// entry per glTF image and this only ever returns a valid pointer.
std::unique_ptr<VulkanImage> LoadTexture(
    const VulkanContext& context,
    const cgltf_image& image,
    const std::filesystem::path& modelDir) {

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = nullptr;

    if (image.buffer_view) { // .glb embeds its images in the binary chunk
        const auto* encoded = static_cast<const uint8_t*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
        pixels = stbi_load_from_memory(encoded, static_cast<int>(image.buffer_view->size), &width, &height, &channels, 4);
    } else if (image.uri) {
        pixels = stbi_load((modelDir / image.uri).string().c_str(), &width, &height, &channels, 4);
    }

    // Base colour textures are authored in sRGB. Declaring the format as _SRGB makes the
    // sampler linearise them, which is what the lighting maths downstream assumes.
    if (pixels && width > 0 && height > 0) {
        auto texture = VulkanImage::CreateTexture2D(context, width, height, pixels, VK_FORMAT_R8G8B8A8_SRGB);
        stbi_image_free(pixels);
        return texture;
    }

    uint32_t white = 0xFFFFFFFF;
    return VulkanImage::CreateTexture2D(context, 1, 1, &white, VK_FORMAT_R8G8B8A8_SRGB);
}

Material ConvertMaterial(const cgltf_material& source, const cgltf_data& data) {
    Material material{};
    if (source.has_pbr_metallic_roughness) {
        const auto& pbr = source.pbr_metallic_roughness;
        material.baseColorFactor = glm::make_vec4(pbr.base_color_factor);
        material.metallicFactor = pbr.metallic_factor;
        material.roughnessFactor = pbr.roughness_factor;
        if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
            // cgltf keeps everything in flat arrays, so pointer arithmetic gives the index.
            material.baseColorTextureIndex =
                static_cast<int32_t>(pbr.base_color_texture.texture->image - data.images);
        }
    }
    material.emissiveFactor = glm::vec4(glm::make_vec3(source.emissive_factor), 1.0f);
    return material;
}

} // namespace

GltfScene::GltfScene(const VulkanContext& context, const std::string& filepath)
    : context_(&context) {
    uint32_t whitePixel = 0xFFFFFFFF;
    defaultTexture_ = VulkanImage::CreateTexture2D(*context_, 1, 1, &whitePixel, VK_FORMAT_R8G8B8A8_SRGB);
    LoadGltf(filepath);
}

void GltfScene::LoadGltf(const std::string& filepath) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, filepath.c_str(), &data) != cgltf_result_success) {
        throw std::runtime_error("Failed to parse glTF file: " + filepath);
    }
    // cgltf_data owns raw allocations, so guard the rest of this function against throws.
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> scopedData(data, &cgltf_free);

    if (cgltf_load_buffers(&options, data, filepath.c_str()) != cgltf_result_success) {
        throw std::runtime_error("Failed to load glTF buffers: " + filepath);
    }
    WarnAboutUnsupportedExtensions(*data, filepath);

    // 1. Textures, in glTF image order so material indices stay valid.
    std::filesystem::path modelDir = std::filesystem::path(filepath).parent_path();
    for (size_t i = 0; i < data->images_count; ++i) {
        textures_.push_back(LoadTexture(*context_, data->images[i], modelDir));
    }

    // 2. Materials. A file without any still needs one entry for the primitives to point at.
    if (data->materials_count == 0) {
        Material fallback{};
        fallback.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        fallback.metallicFactor = 0.0f;
        fallback.roughnessFactor = 0.5f;
        materials_.push_back(fallback);
    } else {
        for (size_t i = 0; i < data->materials_count; ++i) {
            materials_.push_back(ConvertMaterial(data->materials[i], *data));
        }
    }

    // 3. Flatten the node hierarchy. Every triangle primitive becomes one range inside
    //    the shared vertex/index buffers plus the world transform to place it with.
    struct PrimitiveRange {
        uint32_t firstVertex;
        uint32_t vertexCount;
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t materialIndex;
        glm::mat4 worldTransform;
        std::string name;
    };
    std::vector<PrimitiveRange> primitiveRanges;

    auto appendPrimitive = [&](const cgltf_primitive& primitive, const cgltf_node& node,
                               const glm::mat4& worldTransform) {
        const cgltf_accessor* positions = nullptr;
        const cgltf_accessor* normals = nullptr;
        const cgltf_accessor* uvs = nullptr;
        for (size_t a = 0; a < primitive.attributes_count; ++a) {
            switch (primitive.attributes[a].type) {
            case cgltf_attribute_type_position: positions = primitive.attributes[a].data; break;
            case cgltf_attribute_type_normal:   normals = primitive.attributes[a].data; break;
            case cgltf_attribute_type_texcoord: uvs = primitive.attributes[a].data; break;
            default: break;
            }
        }

        if (!positions || !positions->buffer_view) {
            // A position accessor without data means the mesh is still Draco compressed.
            std::cerr << "[SlimRender WARNING] Skipping primitive with undecodable positions." << std::endl;
            return;
        }

        PrimitiveRange range{};
        range.firstVertex = static_cast<uint32_t>(vertices_.size());
        range.vertexCount = static_cast<uint32_t>(positions->count);
        range.firstIndex = static_cast<uint32_t>(indices_.size());
        range.worldTransform = worldTransform;
        range.materialIndex = primitive.material && data->materials
            ? static_cast<uint32_t>(primitive.material - data->materials)
            : 0;

        for (size_t v = 0; v < positions->count; ++v) {
            Vertex vertex{};

            float position[3] = { 0.0f, 0.0f, 0.0f };
            cgltf_accessor_read_float(positions, v, position, 3);
            vertex.pos = glm::vec4(position[0], position[1], position[2], 0.0f);

            float normal[3] = { 0.0f, 1.0f, 0.0f };
            if (normals) {
                cgltf_accessor_read_float(normals, v, normal, 3);
            }
            vertex.normal = glm::vec4(normal[0], normal[1], normal[2], 0.0f);

            if (uvs) {
                float uv[2] = { 0.0f, 0.0f };
                cgltf_accessor_read_float(uvs, v, uv, 2);
                vertex.pos.w = uv[0];
                vertex.normal.w = uv[1];
            }

            // Vertices stay in object space (that is what the BLAS wants), so the bounds
            // have to be accumulated in world space separately.
            glm::vec3 worldPos = glm::vec3(worldTransform * glm::vec4(glm::vec3(vertex.pos), 1.0f));
            aabbMin_ = glm::min(aabbMin_, worldPos);
            aabbMax_ = glm::max(aabbMax_, worldPos);

            vertices_.push_back(vertex);
        }

        if (primitive.indices) {
            range.indexCount = static_cast<uint32_t>(primitive.indices->count);
            for (size_t i = 0; i < primitive.indices->count; ++i) {
                uint32_t index = 0;
                cgltf_accessor_read_uint(primitive.indices, i, &index, 1);
                indices_.push_back(index);
            }
        } else { // non-indexed primitive: synthesise a trivial index buffer
            range.indexCount = range.vertexCount;
            for (uint32_t i = 0; i < range.vertexCount; ++i) {
                indices_.push_back(i);
            }
        }

        range.name = node.name && node.name[0] ? node.name : "Node_" + std::to_string(primitiveRanges.size());
        if (node.mesh->name && node.mesh->name[0] && range.name != node.mesh->name) {
            range.name += " [" + std::string(node.mesh->name) + "]";
        }
        primitiveRanges.push_back(std::move(range));
    };

    auto traverseNode = [&](auto&& self, const cgltf_node& node, const glm::mat4& parentTransform) -> void {
        glm::mat4 worldTransform = parentTransform * NodeLocalTransform(node);

        if (node.camera && !hasCamera_) {
            hasCamera_ = true;
            cameraPosition_ = glm::vec3(worldTransform[3]);
            // glTF cameras look down their local -Z axis.
            cameraTarget_ = cameraPosition_ - glm::normalize(glm::vec3(worldTransform[2]));
            std::cout << "[SlimRender] Using camera from file: " << (node.name ? node.name : "Camera") << std::endl;
        }

        if (node.mesh) {
            for (size_t p = 0; p < node.mesh->primitives_count; ++p) {
                if (node.mesh->primitives[p].type == cgltf_primitive_type_triangles) {
                    appendPrimitive(node.mesh->primitives[p], node, worldTransform);
                }
            }
        }

        for (size_t c = 0; c < node.children_count; ++c) {
            self(self, *node.children[c], worldTransform);
        }
    };

    if (data->scene) {
        for (size_t i = 0; i < data->scene->nodes_count; ++i) {
            traverseNode(traverseNode, *data->scene->nodes[i], glm::mat4(1.0f));
        }
    } else { // no default scene: walk every root node in the file
        for (size_t i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr) {
                traverseNode(traverseNode, data->nodes[i], glm::mat4(1.0f));
            }
        }
    }

    if (vertices_.empty() || indices_.empty()) {
        throw std::runtime_error("No triangle geometry found in glTF file: " + filepath);
    }

    // 4. Upload the geometry once. Every BLAS then points into these two buffers, and the
    //    shader reads them by address after a hit.
    constexpr VkBufferUsageFlags kBlasInput = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    vertexBuffer_ = CreateStorageBuffer(*context_, vertices_, kBlasInput);
    indexBuffer_ = CreateStorageBuffer(*context_, indices_, kBlasInput);
    materialBuffer_ = CreateStorageBuffer(*context_, materials_);

    // 5. One BLAS per primitive, plus the instance record that places it in the world.
    blasList_.reserve(primitiveRanges.size());
    instances_.reserve(primitiveRanges.size());
    geometryInstances_.reserve(primitiveRanges.size());
    objects_.reserve(primitiveRanges.size());

    for (size_t i = 0; i < primitiveRanges.size(); ++i) {
        const PrimitiveRange& range = primitiveRanges[i];

        auto blas = std::make_unique<BottomLevelAS>(*context_);
        blas->Build(
            vertexBuffer_->GetDeviceAddress() + range.firstVertex * sizeof(Vertex),
            range.vertexCount,
            sizeof(Vertex),
            indexBuffer_->GetDeviceAddress() + range.firstIndex * sizeof(uint32_t),
            range.indexCount / 3);

        InstanceData instance{};
        instance.transform = range.worldTransform;
        instance.invTransform = glm::inverse(range.worldTransform);
        instance.vertexOffset = range.firstVertex;
        instance.indexOffset = range.firstIndex;
        instance.materialIndex = range.materialIndex;
        instances_.push_back(instance);

        GeometryInstance geometryInstance{};
        geometryInstance.transform = range.worldTransform;
        geometryInstance.customIndex = static_cast<uint32_t>(i); // indexes instances_ in the shader
        geometryInstance.blasAddress = blas->GetDeviceAddress();
        geometryInstances_.push_back(geometryInstance);

        SceneObject object{};
        object.name = range.name;
        object.instanceIndex = static_cast<uint32_t>(i);
        object.materialIndex = range.materialIndex;
        object.initialTransform = range.worldTransform;
        objects_.push_back(std::move(object));

        blasList_.push_back(std::move(blas));
    }

    instanceBuffer_ = CreateStorageBuffer(*context_, instances_);

    tlas_ = std::make_unique<TopLevelAS>(*context_);
    tlas_->Build(geometryInstances_);

    std::cout << "[SlimRender] Loaded " << filepath << ": "
              << vertices_.size() << " vertices, " << indices_.size() / 3 << " triangles, "
              << primitiveRanges.size() << " instances, " << textures_.size() << " textures."
              << std::endl;
}

void GltfScene::SetInstanceTransform(uint32_t instanceIndex, const glm::mat4& transform) {
    if (instanceIndex >= instances_.size()) {
        return;
    }
    instances_[instanceIndex].transform = transform;
    instances_[instanceIndex].invTransform = glm::inverse(transform);
    geometryInstances_[instanceIndex].transform = transform;
}

void GltfScene::ResetInstanceTransform(uint32_t instanceIndex) {
    if (instanceIndex < objects_.size()) {
        SetInstanceTransform(instanceIndex, objects_[instanceIndex].initialTransform);
    }
}

void GltfScene::SetMaterial(uint32_t index, const Material& material) {
    if (index < materials_.size()) {
        materials_[index] = material;
    }
}

void GltfScene::UpdateInstanceBufferAndTLAS() {
    instanceBuffer_ = CreateStorageBuffer(*context_, instances_);
    tlas_->Build(geometryInstances_);
}

void GltfScene::UpdateMaterialBuffer() {
    materialBuffer_ = CreateStorageBuffer(*context_, materials_);
}

void FrameCameraToScene(Camera& camera, const GltfScene& scene) {
    if (scene.HasCamera()) {
        camera.SetPosition(scene.GetCameraPosition());
        camera.SetTarget(scene.GetCameraTarget());
        return;
    }

    glm::vec3 center = scene.GetSceneCenter();
    float radius = std::max(scene.GetSceneRadius(), 1.0f);
    camera.SetPosition(center + glm::vec3(0.0f, radius * 0.4f, radius * 1.6f));
    camera.SetTarget(center);
}

} // namespace SlimRender
