# render: the Renderer interface and the Vulkan renderer.
#
# gpu (and so Vulkan) is PRIVATE: the public headers in include/ have no
# Vulkan types, so the engine and the game build without Vulkan headers and
# cannot start depending on them by accident.
add_library(playground_render STATIC
  src/VulkanRenderer.cpp)
add_library(playground::render ALIAS playground_render)

target_include_directories(playground_render
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(playground_render
  PUBLIC  playground::platform glm::glm   # FrameData uses GLM vectors
  PRIVATE playground::gpu)

playground_compile_shader(playground_render shaders/sprite.vert)
playground_compile_shader(playground_render shaders/sprite.frag)

playground_configure_target(playground_render)
