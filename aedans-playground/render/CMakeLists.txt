# render: the Renderer interface and the Vulkan renderer.
#
# gpu (and so Vulkan) is PRIVATE: the public headers in include/ have no
# Vulkan types, so the engine and the game build without Vulkan headers and
# cannot start depending on them by accident.
add_library(playground_render STATIC
  src/BrdfLut.cpp
  src/IblBaker.cpp
  src/Mesh.cpp
  src/PostProcess.cpp
  src/Shadow.cpp
  src/ShadowMap.cpp
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
playground_compile_shader(playground_render shaders/pbr.vert)   # both #include mesh_common.glsl
playground_compile_shader(playground_render shaders/pbr.frag)
playground_compile_shader(playground_render shaders/shadow.vert)  # depth only, for the sun's shadow map
playground_compile_shader(playground_render shaders/sky.vert)
playground_compile_shader(playground_render shaders/sky.frag)
# Post-processing (render/src/PostProcess.cpp): bloom and the final composite.
playground_compile_shader(playground_render shaders/fullscreen.vert)
playground_compile_shader(playground_render shaders/bloom.frag)      # #include post_common.glsl
playground_compile_shader(playground_render shaders/tonemap.frag)
# Image-based lighting bakes (render/src/IblBaker.cpp); all #include bake_common.glsl.
playground_compile_shader(playground_render shaders/bake_sky.comp)
playground_compile_shader(playground_render shaders/bake_panorama.comp)
playground_compile_shader(playground_render shaders/bake_irradiance.comp)
playground_compile_shader(playground_render shaders/bake_specular.comp)

playground_configure_target(playground_render)
