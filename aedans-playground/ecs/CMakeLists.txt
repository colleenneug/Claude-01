# ecs: entities, component pools, queries. Depends only on core: it knows
# nothing about rendering, windows or any particular component.
add_library(playground_ecs STATIC
  src/World.cpp)
add_library(playground::ecs ALIAS playground_ecs)

target_include_directories(playground_ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(playground_ecs PUBLIC playground::core)

playground_configure_target(playground_ecs)
