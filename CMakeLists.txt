cmake_minimum_required(VERSION 3.22)
project(SlimRender LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Vulkan SDK
find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan: ${Vulkan_INCLUDE_DIRS}")

# GLM Math Library (Try find_package first, fallback to FetchContent)
find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    message(STATUS "GLM not found locally, fetching via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
endif()

# Find glslc for shader compilation
find_program(GLSLC_EXECUTABLE glslc HINTS "${Vulkan_GLSLC_EXECUTABLE}" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please ensure Vulkan SDK bin is in PATH or VULKAN_SDK is set.")
endif()
message(STATUS "Using glslc: ${GLSLC_EXECUTABLE}")

# Shader compilation target
set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pathtrace.comp")
set(SHADER_SPV "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pathtrace.comp.spv")

add_custom_command(
    OUTPUT "${SHADER_SPV}"
    COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 -o "${SHADER_SPV}" "${SHADER_SRC}"
    DEPENDS "${SHADER_SRC}"
    COMMENT "Compiling ${SHADER_SRC} -> ${SHADER_SPV}"
)
add_custom_target(compile_shaders ALL DEPENDS "${SHADER_SPV}")

# SlimRender sources
set(SLIMRENDER_SOURCES
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
    # Third party ImGui & ImGuizmo
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
    third_party/ImGuizmo/ImGuizmo.cpp
)

add_executable(SlimRender ${SLIMRENDER_SOURCES})
add_dependencies(SlimRender compile_shaders)

target_include_directories(SlimRender PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo
    ${Vulkan_INCLUDE_DIRS}
)

target_compile_definitions(SlimRender PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(SlimRender PRIVATE
    Vulkan::Vulkan
    glm::glm
    shell32
    comdlg32
)

if(MSVC)
    target_compile_options(SlimRender PRIVATE /W3 /permissive- /utf-8)
    # Enable Release optimizations
    target_compile_options(SlimRender PRIVATE $<$<CONFIG:Release>:/O2 /Oi /Ot>)
endif()

# Post-build copy of assets to binary directory
add_custom_command(TARGET SlimRender POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:SlimRender>/assets"
    COMMENT "Copying assets to output directory..."
)
