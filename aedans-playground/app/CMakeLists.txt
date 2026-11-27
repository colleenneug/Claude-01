add_executable(playground_app main.cpp ModelScene.cpp SpriteScene.cpp)
target_link_libraries(playground_app PRIVATE playground::engine)
playground_configure_target(playground_app)

# Game files: text and glTF models (app/assets/models, made by
# tools/make_demo_assets.py). Copied next to the executable after each build,
# so the game finds them from any working directory and they ship with it.
add_custom_command(TARGET playground_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:playground_app>/assets
  COMMENT "Copying game assets next to playground_app")
