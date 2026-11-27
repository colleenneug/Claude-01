# gpu: Vulkan bring-up (instance, surface, GPU selection, device, queues)
# and the swapchain.
# Depends on platform for the window. Vulkan types appear in this layer's
# public headers by design; nothing above the renderer should include them.
add_library(playground_gpu STATIC
  src/Allocator.cpp
  src/Buffer.cpp
  src/Context.cpp
  src/Device.cpp
  src/Image.cpp
  src/Instance.cpp
  src/PhysicalDevice.cpp
  src/Sampler.cpp
  src/Surface.cpp
  src/Swapchain.cpp
  src/VmaImplementation.cpp
  src/VulkanCheck.cpp)
add_library(playground::gpu ALIAS playground_gpu)

target_include_directories(playground_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(playground_gpu
  PUBLIC  playground::platform Vulkan::Vulkan playground_vma
  PRIVATE glfw)   # only for glfwCreateWindowSurface, in Surface.cpp

playground_configure_target(playground_gpu)

# VMA is third-party: its implementation is built without our warning flags.
if(MSVC)
  set_source_files_properties(src/VmaImplementation.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(src/VmaImplementation.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
