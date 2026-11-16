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

# --- The real executable -----------------------------------------------------
# Opens a window, runs 120 frames, exits 0.
add_test(NAME app_smoke COMMAND playground_app --frames 120 --verbose --log-file app_smoke.log)
set_tests_properties(app_smoke PROPERTIES
  LABELS display
  # GLFW's own words for "there is no display": skip rather than fail.
  SKIP_REGULAR_EXPRESSION "DISPLAY environment variable is missing;Failed to open display;Failed to detect any supported platform")

# With no display at all, start-up must fail with a clear reason, not crash.
if(UNIX AND NOT APPLE)
  add_test(NAME app_no_display COMMAND playground_app --frames 1 --log-file app_no_display.log)
  set_tests_properties(app_no_display PROPERTIES
    ENVIRONMENT_MODIFICATION "DISPLAY=unset:;WAYLAND_DISPLAY=unset:"
    PASS_REGULAR_EXPRESSION "start-up failed \\(platform failure\\): GLFW could not initialise")
endif()
