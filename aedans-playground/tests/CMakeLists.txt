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

# --- Shadow-map projection maths: no window, no GPU --------------------------
add_executable(playground_shadow_tests shadow_tests.cpp)
target_link_libraries(playground_shadow_tests PRIVATE playground::render doctest::doctest)
playground_configure_target(playground_shadow_tests)
add_test(NAME shadow_tests COMMAND playground_shadow_tests)

# --- Networking: bit packing, quantization, a real loopback connection --------
add_executable(playground_net_tests net_tests.cpp)
target_link_libraries(playground_net_tests PRIVATE playground::net doctest::doctest)
playground_configure_target(playground_net_tests)
add_test(NAME net_tests COMMAND playground_net_tests)
set_tests_properties(net_tests PROPERTIES LABELS net)

# --- The game's pieces: protocol, movement, input queue, prediction, interpolation
add_executable(playground_game_tests game_tests.cpp)
target_link_libraries(playground_game_tests PRIVATE playground::game doctest::doctest)
playground_configure_target(playground_game_tests)
add_test(NAME game_tests COMMAND playground_game_tests)
set_tests_properties(game_tests PROPERTIES LABELS net)

# --- A server and two clients in one process, over a bad simulated network --
add_executable(playground_netsync_tests netsync_tests.cpp)
target_link_libraries(playground_netsync_tests PRIVATE playground::game doctest::doctest)
playground_configure_target(playground_netsync_tests)
add_test(NAME netsync_tests COMMAND playground_netsync_tests)
set_tests_properties(netsync_tests PROPERTIES LABELS net TIMEOUT 60)

# --- Physics: Jolt behind physics::PhysicsWorld, and the ECS sync --------------
# Also under ThreadSanitizer (label "threads"): the job-system bridge.
add_executable(playground_physics_tests physics_tests.cpp)
target_link_libraries(playground_physics_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_physics_tests)
add_test(NAME physics_tests COMMAND playground_physics_tests)
set_tests_properties(physics_tests PROPERTIES LABELS "physics;threads")

# --- Scene files: JSON round trips of an ecs::World ---------------------------
add_executable(playground_scene_tests scene_tests.cpp)
target_link_libraries(playground_scene_tests PRIVATE playground::engine doctest::doctest)
playground_configure_target(playground_scene_tests)
add_test(NAME scene_tests COMMAND playground_scene_tests)

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

# --- Shadows and HDR post-processing: pixels read back. White-box. -------------
add_executable(playground_postfx_tests postfx_tests.cpp)
target_link_libraries(playground_postfx_tests PRIVATE playground::engine playground::gpu doctest::doctest)
target_include_directories(playground_postfx_tests PRIVATE ${PROJECT_SOURCE_DIR}/render/src)
playground_configure_target(playground_postfx_tests)
add_test(NAME postfx_tests COMMAND playground_postfx_tests)
set_tests_properties(postfx_tests PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;gpu")

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
  # Walk mode and hitscan shooting in the 3D scene's physics (tests/e2e/physics.sh).
  add_test(NAME physics_e2e
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/e2e/physics.sh $<TARGET_FILE:playground_app> physics_e2e.log)
  set_tests_properties(physics_e2e PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;input;physics" TIMEOUT 90)
  # The developer UI: F1, save and load the scene, F1, Escape (tests/e2e/ui.sh).
  add_test(NAME ui_e2e COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/e2e/ui.sh $<TARGET_FILE:playground_app> ui_e2e.log)
  set_tests_properties(ui_e2e PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;input" TIMEOUT 90)
endif()

# A dedicated server and a headless client talking over UDP, with no display
# at all; Ctrl+C stops the server (tests/e2e/server.sh).
if(UNIX)
  add_test(NAME server_e2e
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/e2e/server.sh $<TARGET_FILE:playground_app> server_e2e)
  set_tests_properties(server_e2e PROPERTIES LABELS net TIMEOUT 60)
endif()
# The windowed client (--connect) against a dedicated server: joins, draws
# the server's world for 240 frames, leaves (tests/e2e/netclient.sh).
if(UNIX AND NOT APPLE)
  add_test(NAME netclient_e2e
           COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/e2e/netclient.sh $<TARGET_FILE:playground_app> netclient_e2e)
  set_tests_properties(netclient_e2e PROPERTIES SKIP_RETURN_CODE 77 LABELS "display;net" TIMEOUT 90)
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
    set_tests_properties(gpu_tests render_tests mesh_tests environment_tests postfx_tests app_smoke PROPERTIES
      ENVIRONMENT "LD_PRELOAD=${playgroundAsanRuntime}:${PLAYGROUND_MESA_LAVAPIPE}")
    # The keyboard test is a shell script driving xdotool: only the game itself
    # may get the preload, so it is passed through a variable the script reads.
    if(TEST keyboard_e2e)
      set_tests_properties(keyboard_e2e camera_e2e physics_e2e ui_e2e netclient_e2e PROPERTIES
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
