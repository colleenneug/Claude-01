# --- Unit tests: no window, no GPU ------------------------------------------
add_executable(playground_core_tests core_tests.cpp)
target_link_libraries(playground_core_tests PRIVATE playground::core doctest::doctest)
playground_configure_target(playground_core_tests)
add_test(NAME core_tests COMMAND playground_core_tests)

# --- Job system: no window. Also run under ThreadSanitizer (label "threads"). --
add_executable(playground_job_tests job_tests.cpp)
target_link_libraries(playground_job_tests PRIVATE playground::core doctest::doctest)
playground_configure_target(playground_job_tests)
add_test(NAME job_tests COMMAND playground_job_tests)
set_tests_properties(job_tests PROPERTIES LABELS threads)

# --- Asset manager: no window, no GPU; also under ThreadSanitizer. -----------
add_executable(playground_asset_tests asset_tests.cpp)
target_link_libraries(playground_asset_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_asset_tests)
add_test(NAME asset_tests COMMAND playground_asset_tests)
set_tests_properties(asset_tests PROPERTIES LABELS threads)

# --- glTF loader: no window, no GPU (a recording fake renderer); also TSan. ---
add_executable(playground_gltf_tests gltf_tests.cpp)
target_link_libraries(playground_gltf_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_gltf_tests)
add_test(NAME gltf_tests COMMAND playground_gltf_tests)
set_tests_properties(gltf_tests PROPERTIES LABELS threads)

# --- Image-based lighting maths: no window, no GPU. White-box: render/src. ----
add_executable(playground_ibl_tests ibl_tests.cpp)
target_link_libraries(playground_ibl_tests PRIVATE playground::render doctest::doctest)
target_include_directories(playground_ibl_tests PRIVATE ${PROJECT_SOURCE_DIR}/render/src)
playground_configure_target(playground_ibl_tests)
add_test(NAME ibl_tests COMMAND playground_ibl_tests)

# --- ECS and systems: no window, no GPU --------------------------------------
add_executable(playground_ecs_tests ecs_tests.cpp)
target_link_libraries(playground_ecs_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_ecs_tests)
add_test(NAME ecs_tests COMMAND playground_ecs_tests)

# --- Input and actions: no window ---------------------------------------------
add_executable(playground_input_tests input_tests.cpp)
target_link_libraries(playground_input_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_input_tests)
add_test(NAME input_tests COMMAND playground_input_tests)

# --- Camera maths: no window ---------------------------------------------------
add_executable(playground_camera_tests camera_tests.cpp)
target_link_libraries(playground_camera_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_camera_tests)
add_test(NAME camera_tests COMMAND playground_camera_tests)

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

# --- Meshes and textures: pixels read back from the GPU. White-box. ----------
add_executable(playground_mesh_tests mesh_tests.cpp)
target_link_libraries(playground_mesh_tests PRIVATE playground::engine playground::gpu doctest::doctest)
target_include_directories(playground_mesh_tests PRIVATE ${PROJECT_SOURCE_DIR}/render/src)
target_compile_definitions(playground_mesh_tests PRIVATE PLAYGROUND_DEMO_ASSETS="${PROJECT_SOURCE_DIR}/app/assets")
playground_configure_target(playground_mesh_tests)
add_test(NAME mesh_tests COMMAND playground_mesh_tests)
set_tests_properties(mesh_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;gpu")

# --- Image-based lighting and the sky: pixels read back. White-box. ----------
add_executable(playground_environment_tests environment_tests.cpp)
target_link_libraries(playground_environment_tests PRIVATE playground::engine playground::gpu doctest::doctest)
target_include_directories(playground_environment_tests PRIVATE ${PROJECT_SOURCE_DIR}/render/src)
playground_configure_target(playground_environment_tests)
add_test(NAME environment_tests COMMAND playground_environment_tests)
set_tests_properties(environment_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;gpu")

# --- The real executable -----------------------------------------------------
# Opens a window, runs 120 frames, exits 0.
add_test(NAME app_smoke COMMAND playground_app --frames 120 --verbose --log-file app_smoke.log)
set_tests_properties(app_smoke PROPERTIES
  LABELS display
  # GLFW's own words for "there is no display": skip rather than fail.
  SKIP_REGULAR_EXPRESSION "DISPLAY environment variable is missing;Failed to open display;Failed to detect any supported platform")

# Real keystrokes through X11 and GLFW's callbacks (needs xdotool; skipped
# without it or without a display). See tests/e2e/keyboard.sh.
if(UNIX AND NOT APPLE)
  add_test(NAME keyboard_e2e
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/e2e/keyboard.sh $<TARGET_FILE:playground_app> keyboard_e2e.log)
  set_tests_properties(keyboard_e2e PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;input" TIMEOUT 90)
  # The same for the 3D scene's camera: Tab to fly, W to move (tests/e2e/camera.sh).
  add_test(NAME camera_e2e
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/e2e/camera.sh $<TARGET_FILE:playground_app> camera_e2e.log)
  set_tests_properties(camera_e2e PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;input" TIMEOUT 90)
endif()

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
    set_tests_properties(gpu_tests render_tests mesh_tests environment_tests app_smoke PROPERTIES
      ENVIRONMENT "LD_PRELOAD=${playgroundAsanRuntime}:${PLAYGROUND_MESA_LAVAPIPE}")
    # The keyboard test is a shell script driving xdotool: only the game itself
    # may get the preload, so it is passed through a variable the script reads.
    if(TEST keyboard_e2e)
      set_tests_properties(keyboard_e2e camera_e2e PROPERTIES
        ENVIRONMENT "PLAYGROUND_APP_PRELOAD=${playgroundAsanRuntime}:${PLAYGROUND_MESA_LAVAPIPE}")
    endif()
  endif()
endif()

# With no display at all, start-up must fail with a clear reason, not crash.
if(UNIX AND NOT APPLE)
  add_test(NAME app_no_display COMMAND playground_app --frames 1 --log-file app_no_display.log)
  set_tests_properties(app_no_display PROPERTIES
    ENVIRONMENT_MODIFICATION "DISPLAY=unset:;WAYLAND_DISPLAY=unset:"
    PASS_REGULAR_EXPRESSION "start-up failed \\(platform failure\\): GLFW could not initialise")
endif()
