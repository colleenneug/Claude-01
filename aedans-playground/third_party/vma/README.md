# Vulkan Memory Allocator (VMA)

Vendored single header, used by `gpu::Allocator` and `gpu::Buffer`. Ubuntu
24.04 does not package it; on Windows the vcpkg port `vulkan-memory-allocator`
is used instead when present (see the root `CMakeLists.txt`).

| | |
|---|---|
| Source | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator |
| Version | v3.3.0 (`include/vk_mem_alloc.h`, unmodified) |
| SHA-256 | `90ce12fc4a2466235a09ae02905dd0c13aee80c1bbf11b331ab61230c2ceb112` |
| Licence | MIT (the copyright and permission notice are at the top of the header) |

The implementation is compiled once, in `gpu/src/VmaImplementation.cpp`.
