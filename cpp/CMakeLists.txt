cmake_minimum_required(VERSION 3.16)
project(erebus_cradle_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(glm QUIET)

# Two different worlds for finding glfw3/glew: on Linux/macOS they're
# typically installed as system packages that ship pkg-config .pc files
# (apt/Homebrew), but on Windows via vcpkg they ship CMake config packages
# instead and pkg-config usually isn't even installed — so branch on it
# rather than pick one and break the other platform.
if(WIN32)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(GLEW REQUIRED)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLFW REQUIRED glfw3)
  pkg_check_modules(GLEW REQUIRED glew)
endif()

add_executable(erebus_native
  src/main.cpp
  src/Shader.cpp
  src/Camera.cpp
  src/Mesh.cpp
  src/Framebuffer.cpp
  src/CascadedShadowMap.cpp
  src/Bloom.cpp
  src/IBL.cpp
  src/Level.cpp
  src/Hostile.cpp
  src/Player.cpp
  src/Weapon.cpp
  src/Hud.cpp
  src/Content.cpp
  src/Profile.cpp
  src/Hub.cpp
  src/Space.cpp
    src/Station.cpp
    src/Crew.cpp
    src/Sites.cpp
    src/Cutscene.cpp
    src/Loadout.cpp
  src/Game.cpp
  src/Renderer.cpp
)

if(WIN32)
  # vcpkg's config packages give proper imported targets — no manual
  # include/library path plumbing needed.
  target_link_libraries(erebus_native PRIVATE glfw GLEW::GLEW OpenGL::GL)
else()
  target_include_directories(erebus_native PRIVATE
    ${GLFW_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
  )
  target_link_directories(erebus_native PRIVATE ${GLFW_LIBRARY_DIRS} ${GLEW_LIBRARY_DIRS})
  target_link_libraries(erebus_native PRIVATE
    ${GLFW_LIBRARIES}
    ${GLEW_LIBRARIES}
    OpenGL::GL
    m
  )
endif()
if(TARGET glm::glm)
  target_link_libraries(erebus_native PRIVATE glm::glm)
endif()
# -Wall/-Wextra are GCC/Clang flags; MSVC doesn't understand them and would
# just error out on an unrecognized option.
if(NOT MSVC)
  target_compile_options(erebus_native PRIVATE -Wall -Wextra -Wno-unused-parameter)
else()
  # MSVC's <cmath> only defines M_PI (and friends) when this is set before
  # it's included — glibc/Apple's <cmath> always define it, so this never
  # came up building on Linux.
  target_compile_definitions(erebus_native PRIVATE _USE_MATH_DEFINES)
endif()

# Ship the shader source tree and the data-driven content (enemy/mission
# .cfg files) next to the binary so the executable can find both at runtime
# relative to argv[0] — no install step needed to just build and run. This
# is also exactly how a monthly content drop reaches players: replace or
# add files under content/, rebuild (or, once packaged, just ship the
# updated content/ folder on its own — nothing in it requires recompiling).
add_custom_command(TARGET erebus_native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:erebus_native>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/content $<TARGET_FILE_DIR:erebus_native>/content)
