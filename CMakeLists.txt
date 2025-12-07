cmake_minimum_required(VERSION 3.20)
project(MiEngine2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Global Definitions
add_compile_definitions(
    NOMINMAX 
    FBXSDK_SHARED 
    _CONSOLE
    UNICODE
    _UNICODE
)

# -----------------------------------------------------------------------------
# Dependencies (Paths taken from MiEngine2.vcxproj)
# -----------------------------------------------------------------------------
set(VULKAN_SDK_PATH "C:/VulkanSDK/1.4.304.1")
set(GLFW_PATH "C:/Users/13667/Downloads/glfw-3.4.bin.WIN64/glfw-3.4.bin.WIN64")
set(GLM_PATH "C:/Users/13667/Downloads/glm-1.0.1-light")
set(IMGUI_PATH "C:/Users/13667/imgui")
set(FBX_SDK_PATH "C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.7")

# -----------------------------------------------------------------------------
# Include Directories
# -----------------------------------------------------------------------------
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${VULKAN_SDK_PATH}/Include"
    "${GLFW_PATH}/include"
    "${GLM_PATH}"
    "${IMGUI_PATH}"
    "${IMGUI_PATH}/backends"
    "${FBX_SDK_PATH}/include"
)

# -----------------------------------------------------------------------------
# Library Directories
# -----------------------------------------------------------------------------
link_directories(
    "${VULKAN_SDK_PATH}/Lib"
    "${GLFW_PATH}/lib-vc2022"
    "${FBX_SDK_PATH}/lib/x64/$<IF:$<CONFIG:Debug>,debug,release>"
)

# -----------------------------------------------------------------------------
# Source Files
# -----------------------------------------------------------------------------
# Recursively find all source files in src and include
file(GLOB_RECURSE SRC_FILES "src/*.cpp")
file(GLOB_RECURSE INCLUDE_FILES "include/*.h" "include/*.cpp")

# Manually add root files and ImGui sources
set(PROJECT_SOURCES
    ${SRC_FILES}
    ${INCLUDE_FILES}
    "VulkanRenderer.cpp"
    "VulkanRenderer.h"
    
    # ImGui Sources
    "${IMGUI_PATH}/imgui.cpp"
    "${IMGUI_PATH}/imgui_demo.cpp"
    "${IMGUI_PATH}/imgui_draw.cpp"
    "${IMGUI_PATH}/imgui_tables.cpp"
    "${IMGUI_PATH}/imgui_widgets.cpp"
    "${IMGUI_PATH}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_PATH}/backends/imgui_impl_vulkan.cpp"
)

# -----------------------------------------------------------------------------
# Executable
# -----------------------------------------------------------------------------
add_executable(MiEngine2 ${PROJECT_SOURCES})

# -----------------------------------------------------------------------------
# Link Libraries
# -----------------------------------------------------------------------------
target_link_libraries(MiEngine2 PRIVATE
    vulkan-1.lib
    glfw3.lib
    libfbxsdk.lib
    libxml2.lib
    zlib.lib
    dwmapi.lib # Windows API for Dark Mode
)

# -----------------------------------------------------------------------------
# Post-Build Events
# -----------------------------------------------------------------------------

# 1. Copy FBX SDK DLL
add_custom_command(TARGET MiEngine2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${FBX_SDK_PATH}/lib/x64/$<IF:$<CONFIG:Debug>,debug,release>/libfbxsdk.dll"
    $<TARGET_FILE_DIR:MiEngine2>
    COMMENT "Copying FBX SDK DLL..."
)

# 2. Copy Shaders Folder
add_custom_command(TARGET MiEngine2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:MiEngine2>/shaders"
    COMMENT "Copying Shaders..."
)
