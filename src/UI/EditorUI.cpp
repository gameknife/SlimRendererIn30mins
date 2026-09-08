#include "UI/EditorUI.hpp"

#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_win32.h>

#include <commdlg.h>

#include <ctime>
#include <iomanip>
#include <sstream>

namespace SlimRender {

namespace {

constexpr ImVec4 kBlenderOrange{ 0.91f, 0.49f, 0.05f, 1.00f };
constexpr float kMenuBarHeight = 24.0f;

// ImGuizmo hands back a full matrix; the Properties panel needs it as editable TRS.
void DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotationDegrees, glm::vec3& scale) {
    translation = glm::vec3(transform[3]);
    scale = { glm::length(glm::vec3(transform[0])),
              glm::length(glm::vec3(transform[1])),
              glm::length(glm::vec3(transform[2])) };

    // Dividing the basis vectors by their length leaves a pure rotation matrix.
    glm::mat3 rotation(
        glm::vec3(transform[0]) / std::max(scale.x, 1e-6f),
        glm::vec3(transform[1]) / std::max(scale.y, 1e-6f),
        glm::vec3(transform[2]) / std::max(scale.z, 1e-6f));
    rotationDegrees = glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
}

glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale) {
    return glm::translate(glm::mat4(1.0f), translation) *
           glm::mat4_cast(glm::quat(glm::radians(rotationDegrees))) *
           glm::scale(glm::mat4(1.0f), scale);
}

std::string MakeTimestampedRenderPath() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
    localtime_s(&localTime, &now);

    std::ostringstream path;
    path << "renders/render_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".png";
    return path.str();
}

// Moving an object invalidates the instance buffer, the TLAS and every sample gathered.
void CommitTransformEdit(GltfScene& scene, PathTracer& pathTracer) {
    scene.UpdateInstanceBufferAndTLAS();
    pathTracer.UpdateTLASDescriptor(); // the rebuilt TLAS has a new handle
    pathTracer.ResetAccumulation();
}

std::string ShowOpenGltfDialog(HWND owner) {
    wchar_t selectedPath[MAX_PATH] = L"";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = static_cast<DWORD>(std::size(selectedPath));
    dialog.lpstrFilter = L"glTF / GLB Files (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    return GetOpenFileNameW(&dialog) ? WideToUtf8(selectedPath) : std::string{};
}

} // namespace

EditorUI::EditorUI(const VulkanContext& context, const Window& window, const VulkanSwapchain& swapchain)
    : context_(&context) {

    // ImGui allocates one descriptor per texture it draws; this pool is its own budget.
    std::array<VkDescriptorPoolSize, 1> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VK_CHECK(vkCreateDescriptorPool(context.GetDevice(), &poolInfo, nullptr, &imguiPool_));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    SetupBlenderTheme();

    ImGui_ImplWin32_Init(window.GetHWND());

    VkFormat swapchainFormat = swapchain.GetFormat();
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context.GetInstance();
    initInfo.PhysicalDevice = context.GetPhysicalDevice();
    initInfo.Device = context.GetDevice();
    initInfo.QueueFamily = context.GetGraphicsQueueFamily();
    initInfo.Queue = context.GetGraphicsQueue();
    initInfo.DescriptorPool = imguiPool_;
    initInfo.MinImageCount = VulkanSwapchain::MAX_FRAMES_IN_FLIGHT;
    initInfo.ImageCount = swapchain.GetImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // Dynamic rendering: no VkRenderPass, just the format of what we draw onto.
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

    ImGui_ImplVulkan_Init(&initInfo);
}

EditorUI::~EditorUI() {
    vkDeviceWaitIdle(context_->GetDevice()); // ImGui still owns in-flight buffers
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(context_->GetDevice(), imguiPool_, nullptr);
}

void EditorUI::SetupBlenderTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Slate greys with Blender's signature orange (#E87D0D) on anything interactive.
    colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]             = ImVec4(0.14f, 0.14f, 0.14f, 0.94f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.12f, 0.12f, 0.12f, 0.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.16f, 0.16f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]               = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.12f, 0.12f, 0.12f, 0.75f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.48f, 0.48f, 0.48f, 1.00f);
    colors[ImGuiCol_CheckMark]            = kBlenderOrange;
    colors[ImGuiCol_SliderGrab]           = kBlenderOrange;
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = kBlenderOrange;
    colors[ImGuiCol_Header]               = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = kBlenderOrange;
    colors[ImGuiCol_Separator]            = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = kBlenderOrange;
    colors[ImGuiCol_SeparatorActive]      = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.24f, 0.24f, 0.24f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.91f, 0.49f, 0.05f, 0.80f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 3.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(6.0f, 6.0f);
}

void EditorUI::BeginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void EditorUI::Draw(Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer,
                    float deltaTime, float aspect) {
    notificationTimer_ -= deltaTime;

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        uiVisible_ = !uiVisible_;
    }

    // A progressive export just waits for the accumulation to reach the target.
    if (isRenderingExport_ && pathTracer.GetAccumulatedFrames() >= static_cast<uint32_t>(targetSPP_)) {
        std::string outputPath = MakeTimestampedRenderPath();
        ShowNotification(pathTracer.SaveRenderToFile(outputPath) ? "Saved: " + outputPath
                                                                 : "Failed to save rendered image!",
                         5.0f);
        isRenderingExport_ = false;
    }

    if (uiVisible_) {
        DrawMainMenuBar(window, scene, camera, pathTracer);
        DrawViewportOverlay(window);
        DrawOutliner(scene);
        DrawProperties(scene, pathTracer);
        DrawStatusBar(pathTracer, deltaTime);
    }
    DrawGizmo(scene, camera, pathTracer, aspect);

    // Tell the camera to stand down while the pointer is over the UI or on a gizmo.
    const ImGuiIO& io = ImGui::GetIO();
    window.SetInputCaptured(io.WantCaptureMouse || io.WantCaptureKeyboard ||
                            ImGuizmo::IsUsing() || ImGuizmo::IsOver());
}

void EditorUI::RequestFileOpen(const Window& window) {
    std::string path = ShowOpenGltfDialog(window.GetHWND());
    if (!path.empty()) {
        pendingLoadFile_ = std::move(path);
        hasPendingLoadFile_ = true;
    }
}

void EditorUI::DrawMainMenuBar(const Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open glTF / GLB...", "Ctrl+O")) {
            RequestFileOpen(window);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Render (PNG)", "Ctrl+S")) {
            std::string outputPath = MakeTimestampedRenderPath();
            ShowNotification(pathTracer.SaveRenderToFile(outputPath) ? "Saved: " + outputPath
                                                                     : "Failed to save image!",
                             5.0f);
        }
        if (ImGui::MenuItem("Reset All Object Transforms")) {
            for (size_t i = 0; i < scene.GetObjects().size(); ++i) {
                scene.ResetInstanceTransform(static_cast<uint32_t>(i));
            }
            CommitTransformEdit(scene, pathTracer);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
            PostQuitMessage(0);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Render")) {
        if (ImGui::MenuItem("Reset Accumulation")) {
            pathTracer.ResetAccumulation();
        }
        ImGui::Separator();
        for (int spp : { 128, 256, 512, 1024, 2048 }) {
            std::string label = "Target SPP: " + std::to_string(spp);
            if (ImGui::MenuItem(label.c_str(), nullptr, targetSPP_ == spp)) {
                targetSPP_ = spp;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Start Progressive Render to Image")) {
            pathTracer.ResetAccumulation();
            isRenderingExport_ = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Camera")) {
            FrameCameraToScene(camera, scene);
        }
        ImGui::MenuItem("Show 3D Gizmo", nullptr, &showGizmo_);
        ImGui::Separator();
        if (ImGui::MenuItem("Translate Gizmo", "W", gizmoOperation_ == ImGuizmo::TRANSLATE)) gizmoOperation_ = ImGuizmo::TRANSLATE;
        if (ImGui::MenuItem("Rotate Gizmo", "E", gizmoOperation_ == ImGuizmo::ROTATE)) gizmoOperation_ = ImGuizmo::ROTATE;
        if (ImGui::MenuItem("Scale Gizmo", "R", gizmoOperation_ == ImGuizmo::SCALE)) gizmoOperation_ = ImGuizmo::SCALE;
        ImGui::Separator();
        if (ImGui::MenuItem("World Coordinates", nullptr, gizmoMode_ == ImGuizmo::WORLD)) gizmoMode_ = ImGuizmo::WORLD;
        if (ImGui::MenuItem("Local Coordinates", nullptr, gizmoMode_ == ImGuizmo::LOCAL)) gizmoMode_ = ImGuizmo::LOCAL;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::BulletText("Right Mouse Drag: Rotate Camera");
        ImGui::BulletText("Middle Drag / Shift+Right: Pan Camera");
        ImGui::BulletText("Mouse Wheel: Zoom / Dolly");
        ImGui::BulletText("W/A/S/D + Q/E: Fly Camera");
        ImGui::BulletText("W/E/R: Switch Move/Rotate/Scale Gizmo");
        ImGui::BulletText("F1: Toggle UI overlay");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorUI::DrawViewportOverlay(const Window& window) {
    ImGuiIO& io = ImGui::GetIO();
    constexpr ImGuiWindowFlags kOverlayFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowPos(ImVec2(16.0f, kMenuBarHeight + 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    if (ImGui::Begin("##ViewportOverlayTools", nullptr, kOverlayFlags)) {
        if (ImGui::RadioButton("Move (W)", gizmoOperation_ == ImGuizmo::TRANSLATE)) gizmoOperation_ = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", gizmoOperation_ == ImGuizmo::ROTATE)) gizmoOperation_ = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", gizmoOperation_ == ImGuizmo::SCALE)) gizmoOperation_ = ImGuizmo::SCALE;
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::RadioButton("World", gizmoMode_ == ImGuizmo::WORLD)) gizmoMode_ = ImGuizmo::WORLD;
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", gizmoMode_ == ImGuizmo::LOCAL)) gizmoMode_ = ImGuizmo::LOCAL;
    }
    ImGui::End();

    // Shortcuts, but not while a text field has focus.
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmoOperation_ = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmoOperation_ = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoOperation_ = ImGuizmo::SCALE;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
            RequestFileOpen(window);
        }
    }

    if (notificationTimer_ > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 180.0f, 40.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f));
        ImGui::Begin("##NotificationToast", nullptr, kOverlayFlags);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", notification_.c_str());
        ImGui::End();
    }
}

void EditorUI::DrawGizmo(GltfScene& scene, const Camera& camera, PathTracer& pathTracer, float aspect) {
    if (!showGizmo_ || selectedObjectIndex_ < 0 || selectedObjectIndex_ >= static_cast<int>(scene.GetObjects().size())) {
        return;
    }

    const SceneObject& object = scene.GetObjects()[selectedObjectIndex_];
    glm::mat4 transform = scene.GetInstanceTransform(object.instanceIndex);

    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList()); // on top of the traced image
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::PushID(selectedObjectIndex_);

    // ImGuizmo works in the OpenGL convention, hence the non-Y-flipped projection.
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 projection = camera.GetGizmoProjectionMatrix(aspect);

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                             gizmoOperation_, gizmoMode_, glm::value_ptr(transform))) {
        scene.SetInstanceTransform(object.instanceIndex, transform);
        CommitTransformEdit(scene, pathTracer);
    }

    ImGuizmo::PopID();
}

void EditorUI::DrawOutliner(const GltfScene& scene) {
    const ImGuiIO& io = ImGui::GetIO();
    float height = (io.DisplaySize.y - kMenuBarHeight - bottomBarHeight_) * 0.45f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - rightPanelWidth_, kMenuBarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth_, height), ImGuiCond_Always);

    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Outliner", nullptr, kPanelFlags) &&
        ImGui::TreeNodeEx("glTF Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& objects = scene.GetObjects();
        for (size_t i = 0; i < objects.size(); ++i) {
            std::string label = "[" + std::to_string(i) + "] " + objects[i].name;
            if (ImGui::Selectable(label.c_str(), selectedObjectIndex_ == static_cast<int>(i))) {
                selectedObjectIndex_ = static_cast<int>(i);
            }
        }
        ImGui::TreePop();
    }
    ImGui::End();
}

void EditorUI::DrawProperties(GltfScene& scene, PathTracer& pathTracer) {
    const ImGuiIO& io = ImGui::GetIO();
    float topY = kMenuBarHeight + (io.DisplaySize.y - kMenuBarHeight - bottomBarHeight_) * 0.45f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - rightPanelWidth_, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth_, io.DisplaySize.y - topY - bottomBarHeight_), ImGuiCond_Always);

    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    bool hasSelection = selectedObjectIndex_ >= 0 &&
                        selectedObjectIndex_ < static_cast<int>(scene.GetObjects().size());

    if (ImGui::Begin("Properties", nullptr, kPanelFlags) && ImGui::BeginTabBar("PropertyTabs")) {
        if (ImGui::BeginTabItem("Transform")) {
            if (!hasSelection) {
                ImGui::TextDisabled("Select an object from the Outliner.");
            } else {
                const SceneObject& object = scene.GetObjects()[selectedObjectIndex_];

                glm::vec3 translation, rotationDegrees, scale;
                DecomposeTransform(scene.GetInstanceTransform(object.instanceIndex),
                                   translation, rotationDegrees, scale);

                ImGui::TextColored(kBlenderOrange, "Object: %s", object.name.c_str());
                ImGui::Separator();

                bool changed = false;
                ImGui::Text("Location");
                changed |= ImGui::DragFloat3("##Location", glm::value_ptr(translation), 0.05f);
                ImGui::Text("Rotation (deg)");
                changed |= ImGui::DragFloat3("##Rotation", glm::value_ptr(rotationDegrees), 0.5f);
                ImGui::Text("Scale");
                changed |= ImGui::DragFloat3("##Scale", glm::value_ptr(scale), 0.01f, 0.001f, 100.0f);

                if (changed) {
                    scene.SetInstanceTransform(object.instanceIndex,
                                               ComposeTransform(translation, rotationDegrees, scale));
                    CommitTransformEdit(scene, pathTracer);
                }

                ImGui::Spacing();
                if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
                    scene.ResetInstanceTransform(object.instanceIndex);
                    CommitTransformEdit(scene, pathTracer);
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Material")) {
            uint32_t materialIndex = hasSelection ? scene.GetObjects()[selectedObjectIndex_].materialIndex : 0;
            if (!hasSelection || materialIndex >= scene.GetMaterialCount()) {
                ImGui::TextDisabled("Select an object to inspect its material.");
            } else {
                // Edit a copy, then push the whole material back if anything moved.
                Material material = scene.GetMaterial(materialIndex);
                bool changed = false;

                ImGui::TextColored(kBlenderOrange, "Material #%u", materialIndex);
                ImGui::Separator();
                ImGui::Text("Base Color");
                changed |= ImGui::ColorEdit4("##BaseColor", glm::value_ptr(material.baseColorFactor));
                ImGui::Text("Metallic");
                changed |= ImGui::SliderFloat("##Metallic", &material.metallicFactor, 0.0f, 1.0f);
                ImGui::Text("Roughness");
                changed |= ImGui::SliderFloat("##Roughness", &material.roughnessFactor, 0.04f, 1.0f);
                ImGui::Text("Emissive Color");
                changed |= ImGui::ColorEdit3("##Emissive", glm::value_ptr(material.emissiveFactor));

                if (changed) {
                    scene.SetMaterial(materialIndex, material);
                    scene.UpdateMaterialBuffer();
                    pathTracer.ResetAccumulation();
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Render")) {
            ImGui::TextColored(kBlenderOrange, "Path Tracing");
            ImGui::Separator();
            int bounces = static_cast<int>(pathTracer.GetMaxBounces());
            if (ImGui::SliderInt("Max Bounces", &bounces, 1, 16)) {
                pathTracer.SetMaxBounces(static_cast<uint32_t>(bounces));
            }

            ImGui::Spacing();
            ImGui::TextColored(kBlenderOrange, "Sun & Sky");
            ImGui::Separator();

            glm::vec3 sunDirection = pathTracer.GetSunDirection();
            if (ImGui::DragFloat3("Sun Direction", glm::value_ptr(sunDirection), 0.02f, -1.0f, 1.0f)) {
                pathTracer.SetSunDirection(sunDirection);
            }
            float sunIntensity = pathTracer.GetSunIntensity();
            if (ImGui::SliderFloat("Sun Intensity", &sunIntensity, 0.0f, 20.0f)) {
                pathTracer.SetSunIntensity(sunIntensity);
            }
            glm::vec3 sunColor = pathTracer.GetSunColor();
            if (ImGui::ColorEdit3("Sun Color", glm::value_ptr(sunColor))) {
                pathTracer.SetSunColor(sunColor);
            }
            float skyIntensity = pathTracer.GetSkyIntensity();
            if (ImGui::SliderFloat("Sky Intensity", &skyIntensity, 0.0f, 5.0f)) {
                pathTracer.SetSkyIntensity(skyIntensity);
            }

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Reset Accumulation", ImVec2(-1, 0))) {
                pathTracer.ResetAccumulation();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorUI::DrawStatusBar(PathTracer& pathTracer, float deltaTime) {
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - bottomBarHeight_), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, bottomBarHeight_), ImGuiCond_Always);

    constexpr ImGuiWindowFlags kStatusFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##StatusBar", nullptr, kStatusFlags)) {
        uint32_t currentSPP = pathTracer.GetAccumulatedFrames();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("SPP: %u / %d", currentSPP, targetSPP_);
        ImGui::SameLine();
        ImGui::ProgressBar(std::clamp(static_cast<float>(currentSPP) / targetSPP_, 0.0f, 1.0f), ImVec2(160.0f, 0.0f));
        ImGui::SameLine();
        ImGui::Text("%.1f FPS (%.2f ms)", deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f, deltaTime * 1000.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("| %ux%u", pathTracer.GetWidth(), pathTracer.GetHeight());

        constexpr float kButtonWidth = 170.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(io.DisplaySize.x - kButtonWidth - 16.0f);

        ImGui::PushStyleColor(ImGuiCol_Button,
                              isRenderingExport_ ? ImVec4(0.85f, 0.2f, 0.2f, 1.0f) : kBlenderOrange);
        if (ImGui::Button(isRenderingExport_ ? "Cancel Render" : "Render to Image (PNG)", ImVec2(kButtonWidth, 0.0f))) {
            if (!isRenderingExport_) {
                pathTracer.ResetAccumulation();
            }
            isRenderingExport_ = !isRenderingExport_;
        }
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

void EditorUI::EndFrameAndRender(VkCommandBuffer cmd, const VulkanSwapchain& swapchain, uint32_t imageIndex) {
    ImGui::Render();

    // Dynamic rendering: begin a render "pass" by naming the attachment inline. LOAD_OP_LOAD
    // is essential here - it keeps the path traced blit that is already in the image.
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain.GetImageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = swapchain.GetExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain.GetImage(imageIndex);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace SlimRender
