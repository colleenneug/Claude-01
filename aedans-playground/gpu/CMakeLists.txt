# gpu: Vulkan bring-up (instance, surface, GPU selection, device, queues).
# Depends on platform for the window. Vulkan types appear in this layer's
# public headers by design; nothing above the renderer should include them.
add_library(playground_gpu STATIC
  src/Context.cpp
  src/Device.cpp
  src/Instance.cpp
  src/PhysicalDevice.cpp
  src/Surface.cpp
  src/VulkanCheck.cpp)
add_library(playground::gpu ALIAS playground_gpu)

target_include_directories(playground_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(playground_gpu
  PUBLIC  playground::platform Vulkan::Vulkan
  PRIVATE glfw)   # only for glfwCreateWindowSurface, in Surface.cpp

playground_configure_target(playground_gpu)
