# =============================================================================
#  Compiler settings shared by every target we own.
#
#  Applied per target (playground_configure_target), not globally, so that
#  third-party code we build or include is never held to our warning level.
#  Sanitizers are the exception: every translation unit in the process has to
#  agree on them, so they are set globally.
# =============================================================================

if(PLAYGROUND_SANITIZERS)
  if(MSVC)
    message(WARNING "PLAYGROUND_SANITIZERS is only wired up for GCC and Clang; ignoring it.")
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

function(playground_configure_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4            # high warning level
      /permissive-   # standard conformance
      /utf-8         # source and execution charset; spdlog/fmt require it
      /Zc:__cplusplus)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(PLAYGROUND_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wshadow                 # a local hiding a member is how half of all
                               # "the value never changes" bugs start
      -Wconversion             # implicit narrowing, e.g. double -> float
      -Wnon-virtual-dtor       # deleting through a base pointer must be safe
      -Wold-style-cast
      -Wnull-dereference
      # Vulkan is used with C++20 designated initialisers that name only the
      # fields that matter: `VkApplicationInfo{.sType = ..., .apiVersion = ...}`.
      # The standard zero-initialises every field left out, which is exactly
      # what Vulkan expects, but GCC 13 still warns about each omitted field
      # (GCC 14 no longer does for designated initialisers). Turned off so
      # those warnings do not bury real ones.
      -Wno-missing-field-initializers)
    if(PLAYGROUND_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
