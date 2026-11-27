# =============================================================================
#  Aedan's Playground — root build file.
#
#  One static library per layer, each with its own public include folder, so a
#  wrong-way #include is a build error rather than a code-review comment:
#
#    playground_core      logging, asserts, Result, frame clock   (no deps up)
#    playground_platform  GLFW window, events, Ctrl+C handling    -> core
#    playground_gpu       Vulkan instance, surface, GPU choice,
#                         device, queues, swapchain              -> platform
#    playground_ecs       entities, component pools, queries     -> core
#    playground_render    Renderer interface + the Vulkan
#                         renderer (Vulkan stays private)        -> gpu
#    playground_engine    Application (loop, renderer slot),
#                         components and systems           -> render, ecs
#    playground_app       the executable                         -> engine
# =============================================================================
cmake_minimum_required(VERSION 3.24)
project(aedans_playground VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)       # -std=c++20, not gnu++20

# Single-config generators (Ninja, Makefiles) default to an empty build type,
# which means "no optimisation and no debug info" — the worst of both.
get_property(PLAYGROUND_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT PLAYGROUND_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(PLAYGROUND_WARNINGS_AS_ERRORS "Treat compiler warnings in our own code as errors" ON)
option(PLAYGROUND_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(PLAYGROUND_TSAN "Build with ThreadSanitizer (GCC/Clang); cannot be combined with PLAYGROUND_SANITIZERS" OFF)
option(PLAYGROUND_BUILD_TESTS "Build the test executables" ON)

include(cmake/ProjectOptions.cmake)
include(cmake/Shaders.cmake)

# --- Third-party -------------------------------------------------------------
# Found as CMake config packages: from system packages on Linux, from the
# vcpkg manifest (vcpkg.json) on Windows. Nothing is downloaded at configure
# time, so a configure never depends on the network.
find_package(Threads REQUIRED)  # std::thread (JobSystem)
find_package(glfw3 3.3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
# Vulkan headers and loader: the Vulkan SDK on Windows (VULKAN_SDK is set by
# its installer), libvulkan-dev on Ubuntu.
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)   # glslc compiles the shaders

# tl::expected is a single vendored header (see third_party/tl/README.md).
# SYSTEM, so its internals are not held to our warning flags.
# Vulkan Memory Allocator: vcpkg's package when there is one (Windows),
# otherwise the vendored single header (Ubuntu does not package it; see
# third_party/vma/README.md). Its Vulkan functions are taken straight from
# the loader we link, so VMA loads none itself.
find_package(VulkanMemoryAllocator CONFIG QUIET)
add_library(playground_vma INTERFACE)
if(TARGET GPUOpen::VulkanMemoryAllocator)
  target_link_libraries(playground_vma INTERFACE GPUOpen::VulkanMemoryAllocator)
  message(STATUS "Vulkan Memory Allocator: package")
else()
  target_include_directories(playground_vma SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma)
  message(STATUS "Vulkan Memory Allocator: vendored v3.3.0")
endif()
target_compile_definitions(playground_vma INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=1 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)

# glTF 2.0 and images. tinygltf (with nlohmann/json) parses glTF; stb_image
# decodes PNG and JPEG. Both are used header-only: their implementations are
# compiled once, in engine/src/GltfImplementation.cpp. Ubuntu packages:
# libtinygltf-dev, nlohmann-json3-dev, libstb-dev (which puts the stb headers
# in include/stb/); vcpkg ports: tinygltf (pulls in nlohmann-json) and stb.
# The TINYGLTF_ definitions change tinygltf's declarations, so every file
# that includes it must see the same ones: they are set here, on the target.
# tinygltf must not use its own stb_image call (it would keep the image's
# own channel count); engine/src/Gltf.cpp gives it one that always decodes
# to RGBA8.
find_path(PLAYGROUND_TINYGLTF_INCLUDE_DIR tiny_gltf.h REQUIRED)
find_path(PLAYGROUND_STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb REQUIRED)
find_package(nlohmann_json 3.9 CONFIG REQUIRED)
add_library(playground_gltf INTERFACE)
target_include_directories(playground_gltf SYSTEM INTERFACE ${PLAYGROUND_TINYGLTF_INCLUDE_DIR} ${PLAYGROUND_STB_INCLUDE_DIR})
target_link_libraries(playground_gltf INTERFACE nlohmann_json::nlohmann_json)
target_compile_definitions(playground_gltf INTERFACE TINYGLTF_NO_STB_IMAGE TINYGLTF_NO_STB_IMAGE_WRITE)

# ENet: reliable UDP, behind net/ (net/src/Host.cpp is the only file that
# includes it). vcpkg's port provides unofficial::enet::enet; Ubuntu's
# libenet-dev ships only a header and a library, found directly.
find_package(unofficial-enet CONFIG QUIET)
add_library(playground_enet INTERFACE)
if(TARGET unofficial::enet::enet)
  target_link_libraries(playground_enet INTERFACE unofficial::enet::enet)
else()
  find_path(PLAYGROUND_ENET_INCLUDE_DIR enet/enet.h REQUIRED)
  find_library(PLAYGROUND_ENET_LIBRARY enet REQUIRED)
  target_include_directories(playground_enet SYSTEM INTERFACE ${PLAYGROUND_ENET_INCLUDE_DIR})
  target_link_libraries(playground_enet INTERFACE ${PLAYGROUND_ENET_LIBRARY})
  if(WIN32)
    target_link_libraries(playground_enet INTERFACE ws2_32 winmm)
  endif()
endif()

add_library(playground_tl_expected INTERFACE)
target_include_directories(playground_tl_expected SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# --- Layers ------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(net)
add_subdirectory(platform)
add_subdirectory(ecs)
add_subdirectory(gpu)
add_subdirectory(render)
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(app)

if(PLAYGROUND_BUILD_TESTS)
  enable_testing()
  find_package(doctest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
