# platform: window, events, Ctrl+C. Depends on core.
add_library(playground_platform STATIC
  src/Input.cpp
  src/Platform.cpp
  src/Window.cpp)
add_library(playground::platform ALIAS playground_platform)

target_include_directories(playground_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# GLFW is PRIVATE: Window.h forward-declares GLFWwindow and nothing more.
target_link_libraries(playground_platform
  PUBLIC  playground::core
  PRIVATE glfw)

playground_configure_target(playground_platform)
