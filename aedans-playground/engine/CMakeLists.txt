# engine: the Application (window + main loop + renderer slot). Depends on
# render for the Renderer interface (and, through it, on platform).
add_library(playground_engine STATIC
  src/Application.cpp)
add_library(playground::engine ALIAS playground_engine)

target_include_directories(playground_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(playground_engine PUBLIC playground::render)

# Logged at start-up. $<CONFIG> is right for single- and multi-config generators.
target_compile_definitions(playground_engine PRIVATE PLAYGROUND_BUILD_CONFIG="$<CONFIG>")

playground_configure_target(playground_engine)
