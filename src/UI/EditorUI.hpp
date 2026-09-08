#pragma once

#include "Core/Common.hpp"
#include "Core/Window.hpp"
#include "Scene/Camera.hpp"
#include "Scene/GltfLoader.hpp"
#include "Vulkan/PathTracer.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

#include <imgui.h>
#include <ImGuizmo.h> // must come after imgui.h: it uses ImVec2/ImDrawList without including them

namespace SlimRender {

// Blender-flavoured editor built on Dear ImGui: an Outliner, a Properties panel, a
// viewport gizmo and a status bar, drawn on top of the path traced image.
//
// The UI never renders the scene itself; it edits GltfScene and PathTracer state and
// relies on the accumulation restarting whenever something changes.
class EditorUI {
public:
    EditorUI(const VulkanContext& context, const Window& window, const VulkanSwapchain& swapchain);
    ~EditorUI();

    EditorUI(const EditorUI&) = delete;
    EditorUI& operator=(const EditorUI&) = delete;

    void BeginFrame();
    void Draw(Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer,
              float deltaTime, float aspect);
    // Records the ImGui draw lists and leaves the swapchain image ready to present.
    void EndFrameAndRender(VkCommandBuffer cmd, const VulkanSwapchain& swapchain, uint32_t imageIndex);
    // Closes a frame that will not be submitted, e.g. when the swapchain went stale.
    void DiscardFrame() { ImGui::EndFrame(); }

    void SetSelectedObjectIndex(int index) { selectedObjectIndex_ = index; }

    void ShowNotification(const std::string& message, float durationSeconds = 3.0f) {
        notification_ = message;
        notificationTimer_ = durationSeconds;
    }

    // Set by File > Open; main() picks the path up and swaps the scene.
    bool HasPendingLoadFile() const { return hasPendingLoadFile_; }
    std::string TakePendingLoadFile() {
        hasPendingLoadFile_ = false;
        return std::move(pendingLoadFile_);
    }

private:
    void SetupBlenderTheme();
    void DrawMainMenuBar(const Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer);
    void DrawViewportOverlay(const Window& window);
    void DrawGizmo(GltfScene& scene, const Camera& camera, PathTracer& pathTracer, float aspect);
    void DrawOutliner(const GltfScene& scene);
    void DrawProperties(GltfScene& scene, PathTracer& pathTracer);
    void DrawStatusBar(PathTracer& pathTracer, float deltaTime);
    void RequestFileOpen(const Window& window);

    const VulkanContext* context_ = nullptr;
    VkDescriptorPool imguiPool_ = VK_NULL_HANDLE;

    int selectedObjectIndex_ = 0; // -1 when nothing is selected
    ImGuizmo::OPERATION gizmoOperation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE gizmoMode_ = ImGuizmo::WORLD;
    bool showGizmo_ = true;
    bool uiVisible_ = true;

    std::string pendingLoadFile_;
    bool hasPendingLoadFile_ = false;

    // Progressive export: keep rendering until targetSPP_ samples are in, then write a PNG.
    int targetSPP_ = 512;
    bool isRenderingExport_ = false;
    std::string notification_;
    float notificationTimer_ = 0.0f;

    float rightPanelWidth_ = 360.0f;
    float bottomBarHeight_ = 36.0f;
};

} // namespace SlimRender
