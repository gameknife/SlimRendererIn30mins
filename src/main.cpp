#include "Core/Common.hpp"
#include "Core/Window.hpp"
#include "Scene/Camera.hpp"
#include "Scene/GltfLoader.hpp"
#include "UI/EditorUI.hpp"
#include "Vulkan/PathTracer.hpp"
#include "Vulkan/VulkanContext.hpp"
#include "Vulkan/VulkanSwapchain.hpp"

#include <filesystem>

namespace {

constexpr uint32_t kInitialWidth = 1280;
constexpr uint32_t kInitialHeight = 720;
constexpr const char* kDefaultModel = "assets/models/pbr.glb";

struct Options {
    std::string modelPath = kDefaultModel;
    // Headless-ish smoke test: render this many samples, write a PNG, exit.
    std::string testOutputPath;
    uint32_t testSPP = 0;
};

Options ParseArguments(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--test-render" && i + 2 < argc) {
            options.testOutputPath = argv[i + 1];
            options.testSPP = static_cast<uint32_t>(std::stoul(argv[i + 2]));
            i += 2;
        } else if (!argument.starts_with("-")) {
            options.modelPath = argument;
        }
    }
    return options;
}

bool IsGltfFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".gltf" || extension == ".glb";
}

void PrintBanner(const std::string& modelPath) {
    std::cout << "====================================================\n"
              << "  SlimRender: Minimal Vulkan Path Tracer (C++20)\n"
              << "  Model: " << modelPath << "\n"
              << "  Right drag: orbit | Middle drag: pan | Wheel: dolly\n"
              << "  WASD + Q/E: fly | Shift/Ctrl: faster/slower\n"
              << "  W/E/R: gizmo mode | F1: toggle UI | ESC: exit\n"
              << "  Drag a .gltf/.glb onto the window to load it\n"
              << "====================================================" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    Options options = ParseArguments(argc, argv);
    PrintBanner(options.modelPath);

    try {
        SlimRender::Window window(kInitialWidth, kInitialHeight, "SlimRender");
        SlimRender::VulkanContext context(window);
        SlimRender::VulkanSwapchain swapchain(context, kInitialWidth, kInitialHeight);

        auto scene = std::make_unique<SlimRender::GltfScene>(context, options.modelPath);
        auto pathTracer = std::make_unique<SlimRender::PathTracer>(
            context, *scene, kInitialWidth, kInitialHeight);

        SlimRender::Camera camera;
        FrameCameraToScene(camera, *scene);

        SlimRender::EditorUI editorUI(context, window, swapchain);

        // One command buffer per frame in flight, so the CPU can record frame N+1 while
        // the GPU is still executing frame N.
        std::array<VkCommandBuffer, SlimRender::VulkanSwapchain::MAX_FRAMES_IN_FLIGHT> commandBuffers{};
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = context.GetCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        VK_CHECK(vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, commandBuffers.data()));

        auto resizeToWindow = [&] {
            uint32_t width = window.GetWidth();
            uint32_t height = window.GetHeight();
            if (width > 0 && height > 0) {
                swapchain.Recreate(width, height);
                pathTracer->OnResize(width, height);
            }
        };

        // Swapping the scene tears down everything that references it, but keeps the
        // lighting the user has dialled in.
        auto loadScene = [&](const std::string& path) {
            std::filesystem::path filePath(path);
            if (!IsGltfFile(filePath)) {
                editorUI.ShowNotification("Only .gltf / .glb are supported: " + filePath.filename().string(), 4.0f);
                return;
            }

            try {
                vkDeviceWaitIdle(context.GetDevice());
                auto newScene = std::make_unique<SlimRender::GltfScene>(context, path);
                auto newPathTracer = std::make_unique<SlimRender::PathTracer>(
                    context, *newScene, window.GetWidth(), window.GetHeight());

                newPathTracer->SetSunDirection(pathTracer->GetSunDirection());
                newPathTracer->SetSunIntensity(pathTracer->GetSunIntensity());
                newPathTracer->SetSunColor(pathTracer->GetSunColor());
                newPathTracer->SetSkyIntensity(pathTracer->GetSkyIntensity());
                newPathTracer->SetMaxBounces(pathTracer->GetMaxBounces());

                scene = std::move(newScene);
                pathTracer = std::move(newPathTracer);

                FrameCameraToScene(camera, *scene);
                editorUI.SetSelectedObjectIndex(scene->GetObjects().empty() ? -1 : 0);
                editorUI.ShowNotification("Loaded: " + filePath.filename().string());
            } catch (const std::exception& error) {
                std::cerr << "[SlimRender] Failed to load scene: " << error.what() << std::endl;
                editorUI.ShowNotification("Load failed: " + std::string(error.what()), 5.0f);
            }
        };

        uint32_t currentFrame = 0;
        auto lastTime = std::chrono::high_resolution_clock::now();

        while (!window.ShouldClose()) {
            window.PollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            if (window.HasDroppedFile()) {
                loadScene(window.TakeDroppedFile());
            } else if (editorUI.HasPendingLoadFile()) {
                loadScene(editorUI.TakePendingLoadFile());
            }

            if (window.IsResized()) {
                resizeToWindow();
                window.ResetResized();
            }

            float aspect = static_cast<float>(window.GetWidth()) / static_cast<float>(window.GetHeight());

            // The UI runs first: it may edit the scene, and it decides whether the camera
            // is allowed to react to the mouse this frame.
            editorUI.BeginFrame();
            editorUI.Draw(window, *scene, camera, *pathTracer, deltaTime, aspect);
            camera.Update(deltaTime, window);

            uint32_t imageIndex = 0;
            if (swapchain.AcquireNextImage(currentFrame, imageIndex) == VK_ERROR_OUT_OF_DATE_KHR) {
                editorUI.DiscardFrame(); // nothing will be submitted, so close the ImGui frame
                resizeToWindow();
                continue;
            }

            VkCommandBuffer cmd = commandBuffers[currentFrame];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

            pathTracer->RenderCompute(cmd, camera, aspect);            // trace one sample
            pathTracer->BlitToSwapchain(cmd, swapchain, imageIndex);   // show it
            editorUI.EndFrameAndRender(cmd, swapchain, imageIndex);    // draw the editor on top

            VK_CHECK(vkEndCommandBuffer(cmd));

            VkSemaphore waitSemaphore = swapchain.GetImageAvailableSemaphore(currentFrame);
            VkSemaphore signalSemaphore = swapchain.GetRenderFinishedSemaphore(imageIndex);
            // The first thing this command buffer does to the acquired image is blit
            // into it, so that is the stage that has to wait for the acquire.
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &waitSemaphore;
            submitInfo.pWaitDstStageMask = &waitStage;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &signalSemaphore;

            VK_CHECK(vkQueueSubmit(context.GetGraphicsQueue(), 1, &submitInfo,
                                   swapchain.GetInFlightFence(currentFrame)));

            VkResult presentResult = swapchain.Present(imageIndex);
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
                resizeToWindow();
            }

            currentFrame = (currentFrame + 1) % SlimRender::VulkanSwapchain::MAX_FRAMES_IN_FLIGHT;

            if (options.testSPP > 0 && pathTracer->GetAccumulatedFrames() >= options.testSPP) {
                pathTracer->SaveRenderToFile(options.testOutputPath);
                break;
            }
        }

        vkDeviceWaitIdle(context.GetDevice()); // let every frame finish before teardown
    } catch (const std::exception& error) {
        std::cerr << "[SlimRender FATAL] " << error.what() << std::endl;
        MessageBoxA(nullptr, error.what(), "SlimRender Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    return 0;
}
