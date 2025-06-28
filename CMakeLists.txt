cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME VulkanCube)
project(${PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# ----------------- 依赖项管理 -----------------

# 1. Vulkan
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Please install it.")
endif()

find_package(glslang REQUIRED COMPONENTS
    glslang
    SPIRV
    OSDependent
    HLSL
)
#find_package(directx-dxc CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(spdlog CONFIG REQUIRED)

#find_package(tinyobjloader CONFIG REQUIRED)
#find_package(Stb REQUIRED)

# ----------------- 编译目标 -----------------
add_executable(${PROJECT_NAME} main.cpp VulkanCube.hpp VulkanCube.cpp ShaderCompiler.hpp ShaderCompiler.cpp)

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_compile_definitions(${PROJECT_NAME} 
        PRIVATE
            GLSLANG_ENABLE_KHRONOS_EXTENSIONS
            GLSLANG_TARGET_SPIRV
            VK_USE_PLATFORM_WIN32_KHR
            NOMINMAX
    )
endif()

# 链接所有依赖库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    spdlog::spdlog
    glslang::glslang
    glslang::SPIRV
    glslang::SPVRemapper
    glslang::OSDependent
    glslang::glslang-default-resource-limits
    #Microsoft::DirectXShaderCompiler
    glm::glm
    GPUOpen::VulkanMemoryAllocator
    #tinyobjloader::tinyobjloader
)

# 添加头文件搜索路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${glm_SOURCE_DIR}
    # ${Stb_INCLUDE_DIR}
    # FetchContent 会自动处理 spdlog, glfw, glslang 的 include 目录
)

# 确保 shaders 目录在运行时能被找到
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()