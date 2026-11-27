// The one translation unit that compiles VMA's implementation.
//
// VMA is a single-header library: every file includes the declarations, and
// exactly one defines VMA_IMPLEMENTATION before including it. It is
// third-party code, so it is built without our warning flags (see
// gpu/CMakeLists.txt). The configuration macros (static Vulkan functions,
// since we link the Vulkan loader directly) are set for the whole gpu target.

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
