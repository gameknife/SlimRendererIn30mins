#include "UI/EditorUI.hpp"
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/type_ptr.hpp>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace SlimRender {

static void DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotationDegrees, glm::vec3& scale) {
    translation = glm::vec3(transform[3]);
    scale.x = glm::length(glm::vec3(transform[0]));
    scale.y = glm::length(glm::vec3(transform[1]));
    scale.z = glm::length(glm::vec3(transform[2]));

    glm::mat3 rotMat(
        glm::vec3(transform[0]) / (scale.x > 1e-6f ? scale.x : 1.0f),
        glm::vec3(transform[1]) / (scale.y > 1e-6f ? scale.y : 1.0f),
        glm::vec3(transform[2]) / (scale.z > 1e-6f ? scale.z : 1.0f)
    );
    glm::quat q = glm::quat_cast(rotMat);
    glm::vec3 euler = glm::eulerAngles(q);
    rotationDegrees = glm::degrees(euler);
}

static glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale) {
    glm::mat4 t = glm::translate(glm::mat4(1.0f), translation);
    glm::quat q = glm::quat(glm::radians(rotationDegrees));
    glm::mat4 r = glm::mat4_cast(q);
    glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
    return t * r * s;
}

static std::string GetTimestampString() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    std::tm tm{};
    localtime_s(&tm, &in_time_t);
    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ss.str();
}

EditorUI::EditorUI(const VulkanContext& context, const Window& window, const VulkanSwapchain& swapchain)
    : context_(&context) {

    // 1. Create Descriptor Pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 }
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 400;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(context.GetDevice(), &poolInfo, nullptr, &imguiPool_));

    // 2. Initialize ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 3. Setup Blender Dark Theme
    SetupBlenderTheme();

    // 4. Initialize Platform and Renderer Backends
    ImGui_ImplWin32_Init(window.GetHWND());

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context.GetInstance();
    initInfo.PhysicalDevice = context.GetPhysicalDevice();
    initInfo.Device = context.GetDevice();
    initInfo.QueueFamily = context.GetGraphicsQueueFamily();
    initInfo.Queue = context.GetGraphicsQueue();
    initInfo.DescriptorPool = imguiPool_;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchain.GetImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    VkFormat swapchainFormat = swapchain.GetFormat();
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

    ImGui_ImplVulkan_Init(&initInfo);
}

EditorUI::~EditorUI() {
    if (context_) {
        vkDeviceWaitIdle(context_->GetDevice());
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (imguiPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(context_->GetDevice(), imguiPool_, nullptr);
            imguiPool_ = VK_NULL_HANDLE;
        }
    }
}

void EditorUI::SetupBlenderTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Blender Classic Dark slate color palette
    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.14f, 0.14f, 0.14f, 0.94f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.12f, 0.12f, 0.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.16f, 0.16f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.12f, 0.12f, 0.12f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.48f, 0.48f, 0.48f, 1.00f);
    
    // Blender Orange Accents (#E87D0D)
    colors[ImGuiCol_CheckMark]             = ImVec4(0.91f, 0.49f, 0.05f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.91f, 0.49f, 0.05f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.91f, 0.49f, 0.05f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.91f, 0.49f, 0.05f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.91f, 0.49f, 0.05f, 1.00f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.24f, 0.24f, 0.24f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.91f, 0.49f, 0.05f, 0.80f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

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

void EditorUI::Draw(
    Window& window,
    GltfScene& scene,
    Camera& camera,
    PathTracer& pathTracer,
    float deltaTime,
    float aspect) {

    // Notification timer
    if (exportNotificationTimer_ > 0.0f) {
        exportNotificationTimer_ -= deltaTime;
    }

    // Toggle UI with F1
    if (window.IsKeyDown(VK_F1)) {
        static bool f1WasDown = false;
        if (!f1WasDown) {
            uiVisible_ = !uiVisible_;
            f1WasDown = true;
        }
    } else {
        static bool f1WasDown = false;
        f1WasDown = false;
    }

    // Render export logic
    if (isRenderingExport_) {
        if (pathTracer.GetAccumulatedFrames() >= static_cast<uint32_t>(targetSPP_)) {
            std::string outFilename = "renders/render_" + GetTimestampString() + ".png";
            bool success = pathTracer.SaveRenderToFile(outFilename);
            if (success) {
                exportNotification_ = "Saved: " + outFilename;
            } else {
                exportNotification_ = "Failed to save rendered image!";
            }
            exportNotificationTimer_ = 5.0f;
            isRenderingExport_ = false;
        }
    }

    if (uiVisible_) {
        DrawMainMenuBar(window, scene, camera, pathTracer);
        DrawViewportOverlay(window, scene, camera, pathTracer);
        DrawOutliner(scene, pathTracer);
        DrawProperties(scene, camera, pathTracer);
        DrawStatusBar(pathTracer, deltaTime);
    }

    // 3D Gizmo is always drawn when active
    DrawGizmo(scene, camera, pathTracer, aspect);

    // Update window input captured state
    bool imguiWantsInput = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
    bool gizmoActive = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
    window.SetInputCaptured(imguiWantsInput || gizmoActive);
}

void EditorUI::DrawMainMenuBar(Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open glTF / GLB...", "Ctrl+O")) {
                OPENFILENAMEW ofn{};
                wchar_t szFile[MAX_PATH] = L"";
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = window.GetHWND();
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
                ofn.lpstrFilter = L"glTF / GLB Files (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, nullptr, 0, nullptr, nullptr);
                    if (sizeNeeded > 1) {
                        std::string filePath(sizeNeeded - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, szFile, -1, filePath.data(), sizeNeeded, nullptr, nullptr);
                        pendingLoadFile_ = filePath;
                        hasPendingLoadFile_ = true;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Render (PNG)", "Ctrl+S")) {
                std::string outFilename = "renders/render_" + GetTimestampString() + ".png";
                if (pathTracer.SaveRenderToFile(outFilename)) {
                    exportNotification_ = "Saved: " + outFilename;
                } else {
                    exportNotification_ = "Failed to save image!";
                }
                exportNotificationTimer_ = 5.0f;
            }
            if (ImGui::MenuItem("Reset All Object Transforms")) {
                for (size_t i = 0; i < scene.GetObjects().size(); ++i) {
                    scene.ResetInstanceTransform(static_cast<uint32_t>(i));
                }
                scene.UpdateInstanceBufferAndTLAS();
                pathTracer.UpdateTLASDescriptor();
                pathTracer.ResetAccumulation();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Render")) {
            if (ImGui::MenuItem("Reset Accumulation", "R")) {
                pathTracer.ResetAccumulation();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Target SPP: 128", nullptr, targetSPP_ == 128)) targetSPP_ = 128;
            if (ImGui::MenuItem("Target SPP: 256", nullptr, targetSPP_ == 256)) targetSPP_ = 256;
            if (ImGui::MenuItem("Target SPP: 512", nullptr, targetSPP_ == 512)) targetSPP_ = 512;
            if (ImGui::MenuItem("Target SPP: 1024", nullptr, targetSPP_ == 1024)) targetSPP_ = 1024;
            if (ImGui::MenuItem("Target SPP: 2048", nullptr, targetSPP_ == 2048)) targetSPP_ = 2048;
            ImGui::Separator();
            if (ImGui::MenuItem("Start Progressive Render to Image")) {
                pathTracer.ResetAccumulation();
                isRenderingExport_ = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Camera")) {
                glm::vec3 c = scene.GetSceneCenter();
                float r = std::max(scene.GetSceneRadius(), 1.0f);
                camera.SetTarget(c);
                camera.SetPosition(c + glm::vec3(0.0f, r * 0.4f, r * 1.6f));
                pathTracer.ResetAccumulation();
            }
            ImGui::MenuItem("Show 3D Gizmo", nullptr, &showGizmo_);
            ImGui::Separator();
            if (ImGui::MenuItem("Translate Gizmo", "W", currentGizmoOperation_ == ImGuizmo::TRANSLATE)) {
                currentGizmoOperation_ = ImGuizmo::TRANSLATE;
            }
            if (ImGui::MenuItem("Rotate Gizmo", "E", currentGizmoOperation_ == ImGuizmo::ROTATE)) {
                currentGizmoOperation_ = ImGuizmo::ROTATE;
            }
            if (ImGui::MenuItem("Scale Gizmo", "R", currentGizmoOperation_ == ImGuizmo::SCALE)) {
                currentGizmoOperation_ = ImGuizmo::SCALE;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("World Coordinates", nullptr, currentGizmoMode_ == ImGuizmo::WORLD)) {
                currentGizmoMode_ = ImGuizmo::WORLD;
            }
            if (ImGui::MenuItem("Local Coordinates", nullptr, currentGizmoMode_ == ImGuizmo::LOCAL)) {
                currentGizmoMode_ = ImGuizmo::LOCAL;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::Text("SlimRender - Blender Style Editor");
            ImGui::BulletText("Right Mouse Drag: Rotate Camera");
            ImGui::BulletText("Middle Drag / Shift+Right: Pan Camera");
            ImGui::BulletText("Mouse Wheel: Zoom / Dolly");
            ImGui::BulletText("W/E/R: Switch Move/Rotate/Scale Gizmo");
            ImGui::BulletText("F1: Toggle UI overlay");
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorUI::DrawViewportOverlay(Window& window, GltfScene& scene, Camera& camera, PathTracer& pathTracer) {
    ImGuiIO& io = ImGui::GetIO();

    // Viewport tools bar on top-left
    ImGui::SetNextWindowPos(ImVec2(16.0f, 32.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##ViewportOverlayTools", nullptr, flags)) {
        if (ImGui::RadioButton("Move (W)", currentGizmoOperation_ == ImGuizmo::TRANSLATE)) {
            currentGizmoOperation_ = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", currentGizmoOperation_ == ImGuizmo::ROTATE)) {
            currentGizmoOperation_ = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", currentGizmoOperation_ == ImGuizmo::SCALE)) {
            currentGizmoOperation_ = ImGuizmo::SCALE;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::RadioButton("World", currentGizmoMode_ == ImGuizmo::WORLD)) {
            currentGizmoMode_ = ImGuizmo::WORLD;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", currentGizmoMode_ == ImGuizmo::LOCAL)) {
            currentGizmoMode_ = ImGuizmo::LOCAL;
        }
        ImGui::End();
    }

    // Keyboard shortcuts for gizmo modes (when not typing in text inputs)
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) currentGizmoOperation_ = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation_ = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation_ = ImGuizmo::SCALE;

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
            OPENFILENAMEW ofn{};
            wchar_t szFile[MAX_PATH] = L"";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = window.GetHWND();
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = L"glTF / GLB Files (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, nullptr, 0, nullptr, nullptr);
                if (sizeNeeded > 1) {
                    std::string filePath(sizeNeeded - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, szFile, -1, filePath.data(), sizeNeeded, nullptr, nullptr);
                    pendingLoadFile_ = filePath;
                    hasPendingLoadFile_ = true;
                }
            }
        }
    }

    // Notification toast if active
    if (exportNotificationTimer_ > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 180.0f, 40.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f));
        ImGui::Begin("##NotificationToast", nullptr, flags);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", exportNotification_.c_str());
        ImGui::End();
    }
}

void EditorUI::DrawGizmo(GltfScene& scene, const Camera& camera, PathTracer& pathTracer, float aspect) {
    if (!showGizmo_ || selectedObjectIndex_ < 0 || selectedObjectIndex_ >= static_cast<int>(scene.GetObjects().size())) {
        return;
    }

    const auto& obj = scene.GetObjects()[selectedObjectIndex_];
    glm::mat4 matrix = scene.GetInstanceTransform(obj.instanceIndex);

    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::PushID(selectedObjectIndex_);

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = camera.GetStandardProjectionMatrix(aspect);

    if (ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        currentGizmoOperation_,
        currentGizmoMode_,
        glm::value_ptr(matrix),
        nullptr, nullptr)) {

        scene.SetInstanceTransform(obj.instanceIndex, matrix);
        scene.UpdateInstanceBufferAndTLAS();
        pathTracer.UpdateTLASDescriptor();
        pathTracer.ResetAccumulation();
    }

    ImGuizmo::PopID();
}

void EditorUI::DrawOutliner(GltfScene& scene, PathTracer& pathTracer) {
    ImGuiIO& io = ImGui::GetIO();
    float height = (io.DisplaySize.y - 24.0f - bottomBarHeight_) * 0.45f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - rightPanelWidth_, 24.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth_, height), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Outliner (大纲视图)", nullptr, flags)) {
        if (ImGui::TreeNodeEx("glTF Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& objects = scene.GetObjects();
            for (size_t i = 0; i < objects.size(); ++i) {
                const auto& obj = objects[i];
                bool isSelected = (selectedObjectIndex_ == static_cast<int>(i));

                std::string label = "[" + std::to_string(i) + "] " + obj.name;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    selectedObjectIndex_ = static_cast<int>(i);
                }
            }
            ImGui::TreePop();
        }
        ImGui::End();
    }
}

void EditorUI::DrawProperties(GltfScene& scene, Camera& camera, PathTracer& pathTracer) {
    ImGuiIO& io = ImGui::GetIO();
    float topY = 24.0f + (io.DisplaySize.y - 24.0f - bottomBarHeight_) * 0.45f;
    float height = io.DisplaySize.y - topY - bottomBarHeight_;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - rightPanelWidth_, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightPanelWidth_, height), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Properties (属性面板)", nullptr, flags)) {
        if (ImGui::BeginTabBar("PropertyTabs")) {
            
            // 1. Transform Tab
            if (ImGui::BeginTabItem("Transform")) {
                if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(scene.GetObjects().size())) {
                    const auto& obj = scene.GetObjects()[selectedObjectIndex_];
                    glm::mat4 matrix = scene.GetInstanceTransform(obj.instanceIndex);

                    glm::vec3 translation(0.0f);
                    glm::vec3 rotationDegrees(0.0f);
                    glm::vec3 scale(1.0f);
                    DecomposeTransform(matrix, translation, rotationDegrees, scale);

                    bool changed = false;

                    ImGui::TextColored(ImVec4(0.91f, 0.49f, 0.05f, 1.0f), "Object: %s", obj.name.c_str());
                    ImGui::Separator();

                    ImGui::Text("Location");
                    if (ImGui::DragFloat3("##Location", glm::value_ptr(translation), 0.05f)) {
                        changed = true;
                    }

                    ImGui::Text("Rotation (deg)");
                    if (ImGui::DragFloat3("##Rotation", glm::value_ptr(rotationDegrees), 0.5f)) {
                        changed = true;
                    }

                    ImGui::Text("Scale");
                    if (ImGui::DragFloat3("##Scale", glm::value_ptr(scale), 0.01f, 0.001f, 100.0f)) {
                        changed = true;
                    }

                    ImGui::Spacing();
                    if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
                        scene.ResetInstanceTransform(obj.instanceIndex);
                        scene.UpdateInstanceBufferAndTLAS();
                        pathTracer.UpdateTLASDescriptor();
                        pathTracer.ResetAccumulation();
                    }

                    if (changed) {
                        glm::mat4 newTransform = ComposeTransform(translation, rotationDegrees, scale);
                        scene.SetInstanceTransform(obj.instanceIndex, newTransform);
                        scene.UpdateInstanceBufferAndTLAS();
                        pathTracer.UpdateTLASDescriptor();
                        pathTracer.ResetAccumulation();
                    }
                } else {
                    ImGui::TextDisabled("Select an object from the Outliner.");
                }
                ImGui::EndTabItem();
            }

            // 2. Material Tab
            if (ImGui::BeginTabItem("Material")) {
                if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(scene.GetObjects().size())) {
                    const auto& obj = scene.GetObjects()[selectedObjectIndex_];
                    uint32_t matIdx = obj.materialIndex;

                    if (matIdx < scene.GetMaterials().size()) {
                        Material mat = scene.GetMaterial(matIdx);
                        bool matChanged = false;

                        ImGui::Text("Material #%d", matIdx);
                        ImGui::Separator();

                        ImGui::Text("Base Color");
                        if (ImGui::ColorEdit4("##BaseColor", glm::value_ptr(mat.baseColorFactor))) {
                            matChanged = true;
                        }

                        ImGui::Text("Metallic");
                        if (ImGui::SliderFloat("##Metallic", &mat.metallicFactor, 0.0f, 1.0f)) {
                            matChanged = true;
                        }

                        ImGui::Text("Roughness");
                        if (ImGui::SliderFloat("##Roughness", &mat.roughnessFactor, 0.04f, 1.0f)) {
                            matChanged = true;
                        }

                        ImGui::Text("Emissive Color");
                        if (ImGui::ColorEdit3("##Emissive", glm::value_ptr(mat.emissiveFactor))) {
                            matChanged = true;
                        }

                        if (matChanged) {
                            scene.SetMaterial(matIdx, mat);
                            scene.UpdateMaterialBuffer();
                            pathTracer.ResetAccumulation();
                        }
                    }
                } else {
                    ImGui::TextDisabled("Select an object to inspect material.");
                }
                ImGui::EndTabItem();
            }

            // 3. Render Settings Tab
            if (ImGui::BeginTabItem("Render")) {
                ImGui::TextColored(ImVec4(0.91f, 0.49f, 0.05f, 1.0f), "Path Tracing Settings");
                ImGui::Separator();

                int bounces = static_cast<int>(pathTracer.GetMaxBounces());
                if (ImGui::SliderInt("Max Bounces", &bounces, 1, 16)) {
                    pathTracer.SetMaxBounces(static_cast<uint32_t>(bounces));
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.91f, 0.49f, 0.05f, 1.0f), "Sun & Sky Lighting");
                ImGui::Separator();

                glm::vec3 sunDir = pathTracer.GetSunDirection();
                if (ImGui::DragFloat3("Sun Direction", glm::value_ptr(sunDir), 0.02f, -1.0f, 1.0f)) {
                    pathTracer.SetSunDirection(sunDir);
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
                if (ImGui::Button("Reset All Samples", ImVec2(-1, 0))) {
                    pathTracer.ResetAccumulation();
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ImGui::End();
    }
}

void EditorUI::DrawStatusBar(PathTracer& pathTracer, float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();
    float width = io.DisplaySize.x;

    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - bottomBarHeight_), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, bottomBarHeight_), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        uint32_t currentSPP = pathTracer.GetAccumulatedFrames();
        float progress = std::clamp(static_cast<float>(currentSPP) / targetSPP_, 0.0f, 1.0f);

        // SPP info
        ImGui::AlignTextToFramePadding();
        ImGui::Text("SPP: %d / %d", currentSPP, targetSPP_);
        ImGui::SameLine();

        // Progress bar
        ImGui::ProgressBar(progress, ImVec2(160.0f, 0.0f));
        ImGui::SameLine();

        // FPS info
        float fps = (deltaTime > 0.0001f) ? (1.0f / deltaTime) : 0.0f;
        ImGui::Text("%.1f FPS (%.2f ms)", fps, deltaTime * 1000.0f);
        ImGui::SameLine();

        // Resolution
        ImGui::TextDisabled("| %dx%d", pathTracer.GetWidth(), pathTracer.GetHeight());
        ImGui::SameLine();

        // Push button to right side
        float buttonWidth = 170.0f;
        ImGui::SetCursorPosX(width - buttonWidth - 16.0f);

        if (isRenderingExport_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Cancel Render", ImVec2(buttonWidth, 0.0f))) {
                isRenderingExport_ = false;
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.91f, 0.49f, 0.05f, 1.0f));
            if (ImGui::Button("Render to Image (PNG)", ImVec2(buttonWidth, 0.0f))) {
                pathTracer.ResetAccumulation();
                isRenderingExport_ = true;
            }
            ImGui::PopStyleColor();
        }

        ImGui::End();
    }
}

void EditorUI::EndFrameAndRender(
    VkCommandBuffer cmd,
    const VulkanSwapchain& swapchain,
    uint32_t imageIndex) {

    ImGui::Render();

    // Render ImGui draw data on top of swapchain image using Vulkan Dynamic Rendering
    VkRenderingAttachmentInfoKHR colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    colorAttachment.imageView = swapchain.GetImageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve the path traced blit!
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfoKHR renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = swapchain.GetExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);

    // Transition swapchain image from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
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

} // namespace SlimRender
