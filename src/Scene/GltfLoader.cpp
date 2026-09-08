#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <functional>
#include <filesystem>
#include <iostream>
#include <cstring>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Scene/GltfLoader.hpp"

namespace SlimRender {

GltfScene::GltfScene(const VulkanContext& context, const std::string& filepath)
    : context_(&context) {
    CreateDefaultTexture();
    LoadGltf(filepath);
}

void GltfScene::CreateDefaultTexture() {
    uint32_t whitePixel = 0xFFFFFFFF;
    defaultTexture_ = VulkanImage::CreateTexture2D(*context_, 1, 1, &whitePixel, VK_FORMAT_R8G8B8A8_UNORM);
}

void GltfScene::LoadGltf(const std::string& filepath) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
    if (result != cgltf_result_success) {
        throw std::runtime_error("Failed to parse glTF file: " + filepath);
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("Failed to load glTF buffers: " + filepath);
    }

    // Check for unsupported extensions
    for (size_t i = 0; i < data->extensions_required_count; ++i) {
        if (data->extensions_required[i]) {
            if (strcmp(data->extensions_required[i], "KHR_draco_mesh_compression") == 0) {
                std::cerr << "[SlimRender WARNING] File '" << filepath
                          << "' requires 'KHR_draco_mesh_compression', which is not supported by cgltf. "
                          << "Mesh vertices will fail to decode (read as 0). Please decompress the glTF file (e.g. using @gltf-transform/cli or gltf-pipeline) before loading." << std::endl;
            }
            if (strcmp(data->extensions_required[i], "EXT_texture_webp") == 0) {
                std::cerr << "[SlimRender WARNING] File '" << filepath
                          << "' requires 'EXT_texture_webp', which is not supported by stb_image. "
                          << "WebP textures will fallback to solid white." << std::endl;
            }
        }
    }

    std::filesystem::path modelDir = std::filesystem::path(filepath).parent_path();

    // 1. Load Textures
    for (size_t i = 0; i < data->images_count; ++i) {
        const auto& img = data->images[i];
        int w = 0, h = 0, comp = 0;
        stbi_uc* pixels = nullptr;

        if (img.buffer_view) {
            const uint8_t* bufferData = static_cast<const uint8_t*>(img.buffer_view->buffer->data) + img.buffer_view->offset;
            pixels = stbi_load_from_memory(bufferData, static_cast<int>(img.buffer_view->size), &w, &h, &comp, 4);
        } else if (img.uri) {
            std::filesystem::path fullPath = modelDir / img.uri;
            pixels = stbi_load(fullPath.string().c_str(), &w, &h, &comp, 4);
        }

        if (pixels && w > 0 && h > 0) {
            textures_.push_back(VulkanImage::CreateTexture2D(*context_, w, h, pixels, VK_FORMAT_R8G8B8A8_UNORM));
            stbi_image_free(pixels);
        } else {
            // Fallback to 1x1 white
            uint32_t white = 0xFFFFFFFF;
            textures_.push_back(VulkanImage::CreateTexture2D(*context_, 1, 1, &white, VK_FORMAT_R8G8B8A8_UNORM));
        }
    }

    // 2. Load Materials
    if (data->materials_count == 0) {
        Material defaultMat{};
        defaultMat.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        defaultMat.metallicFactor = 0.0f;
        defaultMat.roughnessFactor = 0.5f;
        materials_.push_back(defaultMat);
    } else {
        for (size_t i = 0; i < data->materials_count; ++i) {
            const auto& mat = data->materials[i];
            Material m{};

            if (mat.has_pbr_metallic_roughness) {
                m.baseColorFactor = glm::make_vec4(mat.pbr_metallic_roughness.base_color_factor);
                m.metallicFactor = mat.pbr_metallic_roughness.metallic_factor;
                m.roughnessFactor = mat.pbr_metallic_roughness.roughness_factor;

                if (mat.pbr_metallic_roughness.base_color_texture.texture &&
                    mat.pbr_metallic_roughness.base_color_texture.texture->image) {
                    m.baseColorTextureIndex = static_cast<int32_t>(
                        mat.pbr_metallic_roughness.base_color_texture.texture->image - data->images
                    );
                }
            }

            m.emissiveFactor = glm::vec4(glm::make_vec3(mat.emissive_factor), 1.0f);
            materials_.push_back(m);
        }
    }

    // Struct to hold primitive info before building BLAS
    struct PrimRange {
        uint32_t firstVertex;
        uint32_t vertexCount;
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t materialIndex;
        glm::mat4 worldTransform;
        std::string nodeName;
    };
    std::vector<PrimRange> primRanges;

    // Helper lambda for node traversal
    std::function<void(cgltf_node*, const glm::mat4&)> traverseNode = [&](cgltf_node* node, const glm::mat4& parentTransform) {
        glm::mat4 localTransform(1.0f);
        if (node->has_matrix) {
            localTransform = glm::make_mat4(node->matrix);
        } else {
            glm::vec3 translation(0.0f);
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 scale(1.0f);
            if (node->has_translation) translation = glm::make_vec3(node->translation);
            if (node->has_rotation) rotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
            if (node->has_scale) scale = glm::make_vec3(node->scale);
            localTransform = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
        }

        glm::mat4 worldTransform = parentTransform * localTransform;

        if (node->camera && !hasCamera_) {
            hasCamera_ = true;
            cameraPosition_ = glm::vec3(worldTransform[3]);
            glm::vec3 forward = -glm::normalize(glm::vec3(worldTransform[2]));
            cameraTarget_ = cameraPosition_ + forward;
            std::cout << "[SlimRender] glTF embedded camera found: '" 
                      << (node->name ? node->name : "Camera") << "' at ("
                      << cameraPosition_.x << ", " << cameraPosition_.y << ", " << cameraPosition_.z << ")" << std::endl;
        }

        if (node->mesh) {
            for (size_t p = 0; p < node->mesh->primitives_count; ++p) {
                const auto& prim = node->mesh->primitives[p];
                if (prim.type != cgltf_primitive_type_triangles) continue;

                // Find attributes
                const cgltf_accessor* posAccessor = nullptr;
                const cgltf_accessor* normAccessor = nullptr;
                const cgltf_accessor* uvAccessor = nullptr;

                for (size_t a = 0; a < prim.attributes_count; ++a) {
                    if (prim.attributes[a].type == cgltf_attribute_type_position) {
                        posAccessor = prim.attributes[a].data;
                    } else if (prim.attributes[a].type == cgltf_attribute_type_normal) {
                        normAccessor = prim.attributes[a].data;
                    } else if (prim.attributes[a].type == cgltf_attribute_type_texcoord) {
                        uvAccessor = prim.attributes[a].data;
                    }
                }

                if (!posAccessor || !posAccessor->buffer_view) {
                    if (posAccessor && !posAccessor->buffer_view) {
                        std::cerr << "[SlimRender WARNING] Primitive position accessor has null buffer_view (likely Draco compressed). Skipping primitive." << std::endl;
                    }
                    continue;
                }

                uint32_t firstVertex = static_cast<uint32_t>(vertices_.size());
                uint32_t vertexCount = static_cast<uint32_t>(posAccessor->count);

                for (size_t v = 0; v < posAccessor->count; ++v) {
                    Vertex vert{};
                    float pval[3] = { 0.0f, 0.0f, 0.0f };
                    cgltf_accessor_read_float(posAccessor, v, pval, 3);
                    vert.pos = glm::vec4(pval[0], pval[1], pval[2], 0.0f);

                    // AABB calculation in world space
                    glm::vec4 worldPos = worldTransform * glm::vec4(vert.pos.x, vert.pos.y, vert.pos.z, 1.0f);
                    aabbMin_ = glm::min(aabbMin_, glm::vec3(worldPos));
                    aabbMax_ = glm::max(aabbMax_, glm::vec3(worldPos));

                    if (normAccessor) {
                        float nval[3] = { 0.0f, 1.0f, 0.0f };
                        cgltf_accessor_read_float(normAccessor, v, nval, 3);
                        vert.normal = glm::vec4(nval[0], nval[1], nval[2], 0.0f);
                    } else {
                        vert.normal = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
                    }

                    if (uvAccessor) {
                        float uvval[2] = { 0.0f, 0.0f };
                        cgltf_accessor_read_float(uvAccessor, v, uvval, 2);
                        vert.pos.w = uvval[0];
                        vert.normal.w = uvval[1];
                    }

                    vertices_.push_back(vert);
                }

                uint32_t firstIndex = static_cast<uint32_t>(indices_.size());
                uint32_t indexCount = 0;

                if (prim.indices) {
                    indexCount = static_cast<uint32_t>(prim.indices->count);
                    for (size_t idx = 0; idx < prim.indices->count; ++idx) {
                        uint32_t i = 0;
                        cgltf_accessor_read_uint(prim.indices, idx, &i, 1);
                        indices_.push_back(i);
                    }
                } else {
                    indexCount = vertexCount;
                    for (uint32_t idx = 0; idx < vertexCount; ++idx) {
                        indices_.push_back(idx);
                    }
                }

                uint32_t matIdx = 0;
                if (prim.material && data->materials) {
                    matIdx = static_cast<uint32_t>(prim.material - data->materials);
                }

                std::string nName = (node->name && strlen(node->name) > 0) ? node->name : ("Node_" + std::to_string(primRanges.size()));
                if (node->mesh && node->mesh->name && strlen(node->mesh->name) > 0 && nName != node->mesh->name) {
                    nName += " [" + std::string(node->mesh->name) + "]";
                }
                primRanges.push_back({ firstVertex, vertexCount, firstIndex, indexCount, matIdx, worldTransform, nName });
            }
        }

        for (size_t c = 0; c < node->children_count; ++c) {
            traverseNode(node->children[c], worldTransform);
        }
    };

    // Traverse all root nodes in scene or file
    if (data->scene) {
        for (size_t i = 0; i < data->scene->nodes_count; ++i) {
            traverseNode(data->scene->nodes[i], glm::mat4(1.0f));
        }
    } else {
        for (size_t i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr) {
                traverseNode(&data->nodes[i], glm::mat4(1.0f));
            }
        }
    }

    cgltf_free(data);

    if (vertices_.empty() || indices_.empty()) {
        throw std::runtime_error("No valid geometry found in glTF file: " + filepath);
    }

    std::cout << "[SlimRender] glTF loaded: " << filepath << std::endl;
    std::cout << "  Vertices: " << vertices_.size() << ", Indices: " << indices_.size()
              << ", Primitives: " << primRanges.size() << ", Textures: " << textures_.size() << std::endl;
    std::cout << "  AABB min: (" << aabbMin_.x << ", " << aabbMin_.y << ", " << aabbMin_.z << ")" << std::endl;
    std::cout << "  AABB max: (" << aabbMax_.x << ", " << aabbMax_.y << ", " << aabbMax_.z << ")" << std::endl;
    std::cout << "  Scene Center: (" << GetSceneCenter().x << ", " << GetSceneCenter().y << ", " << GetSceneCenter().z << "), Radius: " << GetSceneRadius() << std::endl;
    for (size_t i = 0; i < std::min<size_t>(3, primRanges.size()); ++i) {
        std::cout << "  Prim " << i << ": firstV=" << primRanges[i].firstVertex << " countV=" << primRanges[i].vertexCount
                  << " firstI=" << primRanges[i].firstIndex << " countI=" << primRanges[i].indexCount
                  << " trans=(" << primRanges[i].worldTransform[3][0] << ", " << primRanges[i].worldTransform[3][1] << ", " << primRanges[i].worldTransform[3][2] << ")"
                  << std::endl;
        std::cout << "    V0: (" << vertices_[primRanges[i].firstVertex].pos.x << ", " << vertices_[primRanges[i].firstVertex].pos.y << ", " << vertices_[primRanges[i].firstVertex].pos.z << ")" << std::endl;
    }

    // 3. Create and upload GPU Buffers
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices_.size();
    vertexBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        vertices_.data(),
        vertexBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
    );

    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices_.size();
    indexBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        indices_.data(),
        indexBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
    );

    VkDeviceSize materialBufferSize = sizeof(Material) * materials_.size();
    materialBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        materials_.data(),
        materialBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );

    // 4. Build BLAS for each primitive and record InstanceData
    blasList_.reserve(primRanges.size());
    instances_.reserve(primRanges.size());
    geometryInstances_.reserve(primRanges.size());

    for (size_t i = 0; i < primRanges.size(); ++i) {
        const auto& pr = primRanges[i];

        auto blas = std::make_unique<BottomLevelAS>(*context_);
        // Pointing into the global vertex and index buffers
        VkDeviceAddress vertAddr = vertexBuffer_->GetDeviceAddress() + pr.firstVertex * sizeof(Vertex);
        VkDeviceAddress idxAddr = indexBuffer_->GetDeviceAddress() + pr.firstIndex * sizeof(uint32_t);

        blas->Build(
            vertAddr,
            pr.vertexCount,
            sizeof(Vertex),
            idxAddr,
            pr.indexCount / 3
        );

        InstanceData instData{};
        instData.transform = pr.worldTransform;
        instData.invTransform = glm::inverse(pr.worldTransform);
        instData.vertexOffset = pr.firstVertex;
        instData.indexOffset = pr.firstIndex;
        instData.materialIndex = pr.materialIndex;
        instances_.push_back(instData);

        GeometryInstance geomInst{};
        geomInst.transform = pr.worldTransform;
        geomInst.customIndex = static_cast<uint32_t>(i);
        geomInst.mask = 0xFF;
        geomInst.blasAddress = blas->GetDeviceAddress();
        geometryInstances_.push_back(geomInst);

        SceneObject obj{};
        obj.name = pr.nodeName;
        obj.instanceIndex = static_cast<uint32_t>(i);
        obj.materialIndex = pr.materialIndex;
        obj.initialTransform = pr.worldTransform;
        objects_.push_back(obj);

        blasList_.push_back(std::move(blas));
    }

    // 5. Upload Instance Buffer
    VkDeviceSize instanceBufferSize = sizeof(InstanceData) * instances_.size();
    instanceBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        instances_.data(),
        instanceBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );

    // 6. Build TLAS
    tlas_ = std::make_unique<TopLevelAS>(*context_);
    tlas_->Build(geometryInstances_);
    std::cout << "[SlimRender] TLAS built successfully with " << geometryInstances_.size() << " instances." << std::endl;
}

void GltfScene::SetInstanceTransform(uint32_t instanceIndex, const glm::mat4& transform) {
    if (instanceIndex < instances_.size()) {
        instances_[instanceIndex].transform = transform;
        instances_[instanceIndex].invTransform = glm::inverse(transform);
        geometryInstances_[instanceIndex].transform = transform;
    }
}

void GltfScene::ResetInstanceTransform(uint32_t instanceIndex) {
    if (instanceIndex < objects_.size()) {
        SetInstanceTransform(instanceIndex, objects_[instanceIndex].initialTransform);
    }
}

void GltfScene::SetMaterial(uint32_t index, const Material& mat) {
    if (index < materials_.size()) {
        materials_[index] = mat;
    }
}

void GltfScene::UpdateInstanceBufferAndTLAS() {
    VkDeviceSize instanceBufferSize = sizeof(InstanceData) * instances_.size();
    instanceBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        instances_.data(),
        instanceBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );
    tlas_->Build(geometryInstances_);
}

void GltfScene::UpdateMaterialBuffer() {
    VkDeviceSize materialBufferSize = sizeof(Material) * materials_.size();
    materialBuffer_ = VulkanBuffer::CreateDeviceLocal(
        *context_,
        materials_.data(),
        materialBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );
}

} // namespace SlimRender
