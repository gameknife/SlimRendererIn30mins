cmake_minimum_required(VERSION 3.22)
project(SlimRender LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------------------
# Dependencies: only the Vulkan SDK has to be installed. GLM is fetched if it is missing,
# and everything else is vendored in third_party/ as single headers or plain sources.
# ---------------------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    message(STATUS "GLM not found locally, fetching it...")
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
endif()

find_program(GLSLC_EXECUTABLE glslc HINTS "${Vulkan_GLSLC_EXECUTABLE}" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Put the Vulkan SDK bin directory on PATH, or set VULKAN_SDK.")
endif()

# ---------------------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------------------
add_executable(SlimRender
    src/main.cpp
    src/Core/Common.hpp
    src/Core/Window.hpp
    src/Core/Window.cpp
    src/Scene/SceneTypes.hpp
    src/Scene/Camera.hpp
    src/Scene/Camera.cpp
    src/Scene/GltfLoader.hpp
    src/Scene/GltfLoader.cpp
    src/Vulkan/VulkanContext.hpp
    src/Vulkan/VulkanContext.cpp
    src/Vulkan/VulkanBuffer.hpp
    src/Vulkan/VulkanBuffer.cpp
    src/Vulkan/VulkanImage.hpp
    src/Vulkan/VulkanImage.cpp
    src/Vulkan/VulkanSwapchain.hpp
    src/Vulkan/VulkanSwapchain.cpp
    src/Vulkan/AccelerationStructure.hpp
    src/Vulkan/AccelerationStructure.cpp
    src/Vulkan/PathTracer.hpp
    src/Vulkan/PathTracer.cpp
    src/UI/EditorUI.hpp
    src/UI/EditorUI.cpp
    # Vendored Dear ImGui and ImGuizmo
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
    third_party/ImGuizmo/ImGuizmo.cpp
)

target_include_directories(SlimRender PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo
)

target_compile_definitions(SlimRender PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(SlimRender PRIVATE
    Vulkan::Vulkan
    glm::glm
    shell32   # DragQueryFile, for drag-and-drop
    comdlg32  # GetOpenFileName, for File > Open
)

if(MSVC)
    target_compile_options(SlimRender PRIVATE /W3 /permissive- /utf-8 /MP)
endif()

# ---------------------------------------------------------------------------------------
# Assets. Both steps run after every link so the output directory always matches the
# sources; compiling the shader is cheap enough not to bother tracking dependencies.
# ---------------------------------------------------------------------------------------
set(ASSET_DIR "$<TARGET_FILE_DIR:SlimRender>/assets")

add_custom_command(TARGET SlimRender POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${ASSET_DIR}"
    COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 -O
            -o "${ASSET_DIR}/shaders/pathtrace.comp.spv"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pathtrace.comp"
    COMMENT "Copying assets and compiling shaders"
    VERBATIM
)
