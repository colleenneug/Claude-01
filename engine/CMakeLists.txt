cmake_minimum_required(VERSION 3.20)
project(erebus_engine LANGUAGES CXX)

# C++20 for concepts, designated initialisers and <span>. C++23 would buy
# std::expected for the RHI error paths and mdspan for the froxel grid; it is
# not required because the compilers that ship with the Windows toolchain this
# project already uses do not all have it yet.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(EREBUS_ENGINE_BACKEND_VULKAN "Build the Vulkan RHI backend" ON)
option(EREBUS_ENGINE_BACKEND_D3D12  "Build the D3D12 RHI backend"  OFF)

# The engine core is deliberately backend-agnostic and has no third-party
# dependencies at all: it compiles and its logic is testable without a GPU,
# which is what lets movement and gunplay be tuned in a unit test rather than
# by launching the game.
add_library(erebus_engine STATIC
  src/core/GameLoop.cpp
  src/ecs/World.cpp
  src/game/PlayerController.cpp
  src/game/WeaponSystem.cpp
  src/render/ClusteredLighting.cpp
)

target_include_directories(erebus_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(EREBUS_ENGINE_BACKEND_VULKAN)
  target_sources(erebus_engine PRIVATE src/render/vulkan/VulkanDevice.cpp)
  target_compile_definitions(erebus_engine PUBLIC EREBUS_RHI_VULKAN=1)
  # find_package(Vulkan REQUIRED)
  # target_link_libraries(erebus_engine PUBLIC Vulkan::Vulkan)
endif()

if(NOT MSVC)
  target_compile_options(erebus_engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
else()
  target_compile_definitions(erebus_engine PRIVATE _USE_MATH_DEFINES NOMINMAX)
endif()

# A console harness that exercises the parts that need no GPU: the fixed
# timestep, the ECS, the movement integrator and the weapon state machine.
add_executable(erebus_engine_smoke tests/smoke.cpp)
target_link_libraries(erebus_engine_smoke PRIVATE erebus_engine)
