cmake_minimum_required(VERSION 3.16)
project(erebus_cradle_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(GLEW REQUIRED glew)
find_package(glm QUIET)

add_executable(erebus_native
  src/main.cpp
  src/Shader.cpp
  src/Camera.cpp
  src/Mesh.cpp
  src/Framebuffer.cpp
  src/CascadedShadowMap.cpp
  src/Bloom.cpp
  src/IBL.cpp
  src/Scene.cpp
  src/Renderer.cpp
)

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
if(TARGET glm::glm)
  target_link_libraries(erebus_native PRIVATE glm::glm)
endif()
target_compile_options(erebus_native PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Ship the shader source tree next to the binary so the executable can find
# it at runtime with a path relative to argv[0] — no install step needed to
# just build and run.
add_custom_command(TARGET erebus_native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:erebus_native>/shaders)
