# =============================================================================
#  Aedan's Playground — root build file.
#
#  One static library per layer, each with its own public include folder, so a
#  wrong-way #include is a build error rather than a code-review comment:
#
#    playground_core      logging, asserts, Result, frame clock   (no deps up)
#    playground_platform  GLFW window, events, Ctrl+C handling    -> core
#    playground_gpu       Vulkan instance, surface, GPU choice,
#                         logical device and queues              -> platform
#    playground_engine    Application: owns the loop and the
#                         renderer slot                           -> platform
#    playground_app       the executable                    -> engine, gpu
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
option(PLAYGROUND_BUILD_TESTS "Build the test executables" ON)

include(cmake/ProjectOptions.cmake)

# --- Third-party -------------------------------------------------------------
# Found as CMake config packages: from system packages on Linux, from the
# vcpkg manifest (vcpkg.json) on Windows. Nothing is downloaded at configure
# time, so a configure never depends on the network.
find_package(glfw3 3.3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
# Vulkan headers and loader: the Vulkan SDK on Windows (VULKAN_SDK is set by
# its installer), libvulkan-dev on Ubuntu.
find_package(Vulkan 1.3 REQUIRED)

# tl::expected is a single vendored header (see third_party/tl/README.md).
# SYSTEM, so its internals are not held to our warning flags.
add_library(playground_tl_expected INTERFACE)
target_include_directories(playground_tl_expected SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# --- Layers ------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(gpu)
add_subdirectory(engine)
add_subdirectory(app)

if(PLAYGROUND_BUILD_TESTS)
  enable_testing()
  find_package(doctest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
