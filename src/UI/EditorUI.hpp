#pragma once

#include "Core/Common.hpp"
#include "Core/Window.hpp"
#include "Scene/Camera.hpp"
#include "Scene/GltfLoader.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/PathTracer.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

#include <string>
#include <vector>

namespace SlimRender {

class EditorUI {
public:
    EditorUI(const VulkanContext& context, const Window& window, const VulkanSwapchain& swapchain);
    ~EditorUI();

    void BeginFrame();

    void Draw(
        Window& window,
        GltfScene& scene,
        Camera& camera,
        PathTracer& pathTracer,
        float deltaTime,
        float aspect);

    void EndFrameAndRender(
        VkCommandBuffer cmd,
        const VulkanSwapchain& swapchain,
        uint32_t imageIndex);

    bool IsCapturingInput() const;
    int GetSelectedObjectIndex() const { return selectedObjectIndex_; }
    void SetSelectedObjectIndex(int index) { selectedObjectIndex_ = index; }

    void ShowNotification(const std::string& msg, float durationSeconds = 3.0f) {
        exportNotification_ = msg;
        exportNotificationTimer_ = durationSeconds;
    }

    bool HasPendingLoadFile() const { return hasPendingLoadFile_; }
    std::string GetPendingLoadFile() {
        hasPendingLoadFile_ = false;
        return pendingLoadFile_;
    }

private:
    void SetupBlenderTheme();
    void DrawMainMenuBar(Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer);
    void DrawViewportOverlay(Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer);
    void DrawGizmo(GltfScene& scene, const Camera& camera, PathTracer& pathTracer, float aspect);
    void DrawOutliner(GltfScene& scene, PathTracer& pathTracer);
    void DrawProperties(GltfScene& scene, Camera& camera, PathTracer& pathTracer);
    void DrawStatusBar(PathTracer& pathTracer, float deltaTime);

    const VulkanContext* context_ = nullptr;
    VkDescriptorPool imguiPool_ = VK_NULL_HANDLE;

    int selectedObjectIndex_ = 0; // Selected object in Outliner
    ImGuizmo::OPERATION currentGizmoOperation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE currentGizmoMode_ = ImGuizmo::WORLD;
    bool showGizmo_ = true;
    bool uiVisible_ = true;

    // File open request
    std::string pendingLoadFile_;
    bool hasPendingLoadFile_ = false;

    // Render export state
    int targetSPP_ = 512;
    bool isRenderingExport_ = false;
    std::string exportNotification_;
    float exportNotificationTimer_ = 0.0f;

    // Layout
    float rightPanelWidth_ = 360.0f;
    float bottomBarHeight_ = 36.0f;
};

} // namespace SlimRender
