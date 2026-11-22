# --- Unit tests: no window, no GPU ------------------------------------------
add_executable(playground_core_tests core_tests.cpp)
target_link_libraries(playground_core_tests PRIVATE playground::core doctest::doctest)
playground_configure_target(playground_core_tests)
add_test(NAME core_tests COMMAND playground_core_tests)

# --- Application tests: open real windows -----------------------------------
add_executable(playground_app_tests app_tests.cpp)
target_link_libraries(playground_app_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_app_tests)
add_test(NAME app_tests COMMAND playground_app_tests)
# Exit code 77 means "no display here": reported as skipped, not failed.
set_tests_properties(app_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS display)

# --- Vulkan bring-up tests: need a display and a Vulkan driver --------------
add_executable(playground_gpu_tests gpu_tests.cpp)
target_link_libraries(playground_gpu_tests PRIVATE playground::gpu doctest::doctest)
playground_configure_target(playground_gpu_tests)
add_test(NAME gpu_tests COMMAND playground_gpu_tests)
set_tests_properties(gpu_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;gpu")

# --- Renderer tests: frames, resize, teardown. White-box: sees render/src. --
add_executable(playground_render_tests render_tests.cpp)
target_link_libraries(playground_render_tests PRIVATE playground::engine playground::gpu doctest::doctest)
target_include_directories(playground_render_tests PRIVATE ${PROJECT_SOURCE_DIR}/render/src)
playground_configure_target(playground_render_tests)
add_test(NAME render_tests COMMAND playground_render_tests)
set_tests_properties(render_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;gpu")

# --- The real executable -----------------------------------------------------
# Opens a window, runs 120 frames, exits 0.
add_test(NAME app_smoke COMMAND playground_app --frames 120 --verbose --log-file app_smoke.log)
set_tests_properties(app_smoke PROPERTIES
  LABELS display
  # GLFW's own words for "there is no display": skip rather than fail.
  SKIP_REGULAR_EXPRESSION "DISPLAY environment variable is missing;Failed to open display;Failed to detect any supported platform")

# --- Sanitizer builds and Mesa's software Vulkan driver ----------------------
# Mesa's presentation thread keeps its allocations reachable only from the
# driver's own memory. When vkDestroyInstance makes the Vulkan loader unload
# the driver, those references vanish and LeakSanitizer reports ~112 bytes as
# leaked, with "<unknown module>" frames, so no suppression can name them.
# Verified: with the driver kept loaded for the whole process, the report
# disappears, so it is not a leak in our code. Preloading the driver (after
# ASan's runtime, which must come first) keeps it loaded without turning leak
# detection off. GCC only: Clang links its ASan runtime statically.
if(PLAYGROUND_SANITIZERS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND UNIX AND NOT APPLE)
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libasan.so
                  OUTPUT_VARIABLE playgroundAsanRuntime OUTPUT_STRIP_TRAILING_WHITESPACE)
  find_file(PLAYGROUND_MESA_LAVAPIPE libvulkan_lvp.so
            PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE} /usr/lib64 /usr/lib NO_DEFAULT_PATH)
  if(EXISTS "${playgroundAsanRuntime}" AND PLAYGROUND_MESA_LAVAPIPE)
    set_tests_properties(gpu_tests render_tests app_smoke PROPERTIES
      ENVIRONMENT "LD_PRELOAD=${playgroundAsanRuntime}:${PLAYGROUND_MESA_LAVAPIPE}")
  endif()
endif()

# With no display at all, start-up must fail with a clear reason, not crash.
if(UNIX AND NOT APPLE)
  add_test(NAME app_no_display COMMAND playground_app --frames 1 --log-file app_no_display.log)
  set_tests_properties(app_no_display PROPERTIES
    ENVIRONMENT_MODIFICATION "DISPLAY=unset:;WAYLAND_DISPLAY=unset:"
    PASS_REGULAR_EXPRESSION "start-up failed \\(platform failure\\): GLFW could not initialise")
endif()
