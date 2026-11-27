# =============================================================================
#  playground_compile_shader(<target> <shader>)
#
#  Compiles a GLSL shader to SPIR-V with glslc at build time, written as a
#  comma-separated list of 32-bit words (-mfmt=num) that C++ includes into a
#  `const u32[]`:
#
#      constexpr u32 kTriangleVertSpirv[] = {
#      #include "triangle.vert.spv.inc"
#      };
#
#  So the shaders are inside the executable: nothing to find on disk at run
#  time. A shader error is a build error (-Werror), and editing a shader
#  rebuilds only what includes it. Shaders may #include shared files
#  (relative to the shader); glslc writes a dependency file (-MD), so editing
#  an included file rebuilds every shader that uses it.
# =============================================================================

function(playground_compile_shader target shader)
  get_filename_component(name ${shader} NAME)
  set(outputDir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
  set(output ${outputDir}/${name}.spv.inc)

  add_custom_command(
    OUTPUT ${output}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${outputDir}
    COMMAND Vulkan::glslc --target-env=vulkan1.3 -Werror -O -mfmt=num -MD -MF ${output}.d -o ${output}
            ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
    DEPFILE ${output}.d
    COMMENT "Compiling shader ${name}"
    VERBATIM)

  target_sources(${target} PRIVATE ${output})
  target_include_directories(${target} PRIVATE ${outputDir})
endfunction()
