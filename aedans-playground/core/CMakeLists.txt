# core: the layer everything stands on. Depends on nothing of ours.
add_library(playground_core STATIC
  src/Assert.cpp
  src/FixedStep.cpp
  src/FrameClock.cpp
  src/Interrupt.cpp
  src/JobSystem.cpp
  src/Log.cpp)
add_library(playground::core ALIAS playground_core)

target_include_directories(playground_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# spdlog is PRIVATE: no header of ours includes it (see Log.h).
target_link_libraries(playground_core
  PUBLIC  playground_tl_expected Threads::Threads
  PRIVATE spdlog::spdlog)

playground_configure_target(playground_core)
