#include "Core/Common.hpp"
#include "Core/Window.hpp"
#include "Scene/Camera.hpp"
#include "Scene/GltfLoader.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanSwapchain.hpp"
#include "Vulkan/PathTracer.hpp"
#include "UI/EditorUI.hpp"

#include <filesystem>

int main(int argc, char* argv[]) {
    std::string modelPath = "assets/models/pbr.glb";
    std::string testOutput;
    uint32_t testSPP = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test-render" && i + 2 < argc) {
            testOutput = argv[i + 1];
            testSPP = static_cast<uint32_t>(std::stoi(argv[i + 2]));
            i += 2;
        } else if (arg[0] != '-') {
            modelPath = arg;
        }
    }

    if (!std::filesystem::exists(modelPath)) {
        if (std::filesystem::exists("assets/models/conf_room.glb")) {
            modelPath = "assets/models/conf_room.glb";
        }
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "  SlimRender: Minimal Vulkan Path Tracer (C++20)   " << std::endl;
    std::cout << "  Model: " << modelPath << std::endl;
    std::cout << "  Controls:" << std::endl;
    std::cout << "    - Right Mouse Drag : Rotate View (Yaw/Pitch)" << std::endl;
    std::cout << "    - Middle Mouse Drag / Shift+Right Drag : Pan" << std::endl;
    std::cout << "    - Mouse Wheel      : Zoom / Dolly" << std::endl;
    std::cout << "    - W / A / S / D    : Move Camera" << std::endl;
    std::cout << "    - Q / E            : Move Down / Up" << std::endl;
    std::cout << "    - Shift / Ctrl     : Faster / Slower Move" << std::endl;
    std::cout << "    - F1               : Toggle UI Overlay" << std::endl;
    std::cout << "    - ESC              : Exit" << std::endl;
    std::cout << "====================================================" << std::endl;

    try {
        const uint32_t width = 1280;
        const uint32_t height = 720;

        SlimRender::Window window(width, height, "SlimRender - Blender Style Path Tracer");
        SlimRender::VulkanContext context(window);
        SlimRender::VulkanSwapchain swapchain(context, width, height);

        std::cout << "[SlimRender] Loading glTF scene: " << modelPath << "..." << std::endl;
        auto scene = std::make_unique<SlimRender::GltfScene>(context, modelPath);

        SlimRender::Camera camera;
        auto frameCamera = [&camera](const SlimRender::GltfScene& s) {
            if (s.HasCamera()) {
                camera.SetPosition(s.GetCameraPosition());
                camera.SetTarget(s.GetCameraTarget());
            } else {
                glm::vec3 sceneCenter = s.GetSceneCenter();
                float sceneRadius = std::max(s.GetSceneRadius(), 1.0f);
                camera.SetPosition(sceneCenter + glm::vec3(0.0f, sceneRadius * 0.4f, sceneRadius * 1.6f));
                camera.SetTarget(sceneCenter);
            }
        };
        frameCamera(*scene);

        std::cout << "[SlimRender] Initializing Path Tracer..." << std::endl;
        auto pathTracer = std::make_unique<SlimRender::PathTracer>(context, *scene, width, height);

        std::cout << "[SlimRender] Initializing Blender Style Editor UI..." << std::endl;
        SlimRender::EditorUI editorUI(context, window, swapchain);

        // Command Buffers for frames in flight
        std::array<VkCommandBuffer, SlimRender::VulkanSwapchain::MAX_FRAMES_IN_FLIGHT> commandBuffers{};
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = context.GetCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        VK_CHECK(vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, commandBuffers.data()));

        uint32_t currentFrame = 0;
        auto lastTime = std::chrono::high_resolution_clock::now();
        int frameCounter = 0;
        double fpsTimer = 0.0;

        while (!window.ShouldClose()) {
            window.PollEvents();

            auto currentTime = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
            lastTime = currentTime;

            if (window.IsKeyDown(VK_ESCAPE)) {
                break;
            }

            // Handle file drag-and-drop or File->Open request
            std::string loadPath;
            if (window.HasDroppedFile()) {
                loadPath = window.GetDroppedFile();
            } else if (editorUI.HasPendingLoadFile()) {
                loadPath = editorUI.GetPendingLoadFile();
            }

            if (!loadPath.empty()) {
                std::filesystem::path p(loadPath);
                std::string ext = p.extension().string();
                for (char& c : ext) c = static_cast<char>(std::tolower(c));
                if (ext == ".gltf" || ext == ".glb") {
                    try {
                        std::cout << "[SlimRender] Loading scene: " << loadPath << "..." << std::endl;
                        vkDeviceWaitIdle(context.GetDevice());

                        auto newScene = std::make_unique<SlimRender::GltfScene>(context, loadPath);
                        auto newPathTracer = std::make_unique<SlimRender::PathTracer>(
                            context, *newScene, window.GetWidth(), window.GetHeight());

                        // Preserve lighting configuration
                        newPathTracer->SetSunDirection(pathTracer->GetSunDirection());
                        newPathTracer->SetSunIntensity(pathTracer->GetSunIntensity());
                        newPathTracer->SetSunColor(pathTracer->GetSunColor());
                        newPathTracer->SetSkyIntensity(pathTracer->GetSkyIntensity());

                        vkDeviceWaitIdle(context.GetDevice());

                        scene = std::move(newScene);
                        pathTracer = std::move(newPathTracer);

                        // Frame camera to new scene (either embedded camera or scene bounds)
                        frameCamera(*scene);

                        editorUI.SetSelectedObjectIndex(scene->GetObjects().empty() ? -1 : 0);
                        editorUI.ShowNotification("Loaded: " + p.filename().string());
                        std::cout << "[SlimRender] Scene loaded successfully: " << loadPath << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "[SlimRender] Failed to load scene: " << e.what() << std::endl;
                        editorUI.ShowNotification("Load failed: " + std::string(e.what()), 5.0f);
                    }
                } else {
                    editorUI.ShowNotification("Unsupported format (only .gltf/.glb): " + p.filename().string(), 4.0f);
                }
            }

            if (window.IsResized()) {
                uint32_t newW = window.GetWidth();
                uint32_t newH = window.GetHeight();
                if (newW > 0 && newH > 0) {
                    swapchain.Recreate(newW, newH);
                    pathTracer->OnResize(newW, newH);
                }
                window.ResetResized();
            }

            float aspect = static_cast<float>(window.GetWidth()) / static_cast<float>(window.GetHeight());

            // 1. Begin UI Frame and Draw
            editorUI.BeginFrame();
            editorUI.Draw(window, *scene, camera, *pathTracer, dt, aspect);

            // 2. Update Camera (respects UI/Gizmo input capture)
            camera.Update(dt, window);

            // 3. Acquire Swapchain Image
            uint32_t imageIndex = 0;
            VkResult acquireRes = swapchain.AcquireNextImage(currentFrame, imageIndex);
            if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
                swapchain.Recreate(window.GetWidth(), window.GetHeight());
                pathTracer->OnResize(window.GetWidth(), window.GetHeight());
                continue;
            }

            // 4. Record Command Buffer
            VkCommandBuffer cmd = commandBuffers[currentFrame];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

            // 4.1 Compute Path Tracing
            pathTracer->RenderCompute(cmd, camera, aspect);

            // 4.2 Blit compute result to Swapchain image (transitions to COLOR_ATTACHMENT_OPTIMAL)
            pathTracer->BlitToSwapchain(cmd, swapchain, imageIndex);

            // 4.3 Draw ImGui UI on top via Dynamic Rendering (transitions to PRESENT_SRC_KHR)
            editorUI.EndFrameAndRender(cmd, swapchain, imageIndex);

            VK_CHECK(vkEndCommandBuffer(cmd));

            // 5. Submit & Present
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkSemaphore waitSemaphores[] = { swapchain.GetImageAvailableSemaphore(currentFrame) };
            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;

            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;

            VkSemaphore signalSemaphores[] = { swapchain.GetRenderFinishedSemaphore(imageIndex) };
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            VK_CHECK(vkQueueSubmit(context.GetGraphicsQueue(), 1, &submitInfo, swapchain.GetInFlightFence(currentFrame)));

            VkResult presentRes = swapchain.Present(currentFrame, imageIndex);
            if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
                swapchain.Recreate(window.GetWidth(), window.GetHeight());
                pathTracer->OnResize(window.GetWidth(), window.GetHeight());
            }

            if (testSPP > 0 && pathTracer->GetAccumulatedFrames() >= testSPP) {
                std::cout << "[SlimRender] Test render finished with " << pathTracer->GetAccumulatedFrames()
                          << " SPP. Saving to " << testOutput << "..." << std::endl;
                pathTracer->SaveRenderToFile(testOutput);
                break;
            }

            currentFrame = (currentFrame + 1) % SlimRender::VulkanSwapchain::MAX_FRAMES_IN_FLIGHT;

            frameCounter++;
            fpsTimer += dt;
            if (fpsTimer >= 0.5) {
                double fps = frameCounter / fpsTimer;
                char titleBuf[256];
                snprintf(titleBuf, sizeof(titleBuf), "SlimRender | SPP: %u | FPS: %.1f",
                         pathTracer->GetAccumulatedFrames(), fps);
                SetWindowTextA(window.GetHWND(), titleBuf);
                frameCounter = 0;
                fpsTimer = 0.0;
            }
        }

        vkDeviceWaitIdle(context.GetDevice());
    } catch (const std::exception& e) {
        std::cerr << "[SlimRender FATAL] " << e.what() << std::endl;
        MessageBoxA(nullptr, e.what(), "SlimRender Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    return 0;
}
