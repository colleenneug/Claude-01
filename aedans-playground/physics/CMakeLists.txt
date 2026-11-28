# physics: rigid bodies, characters and ray queries (physics::PhysicsWorld)
# by Jolt Physics. Jolt is PRIVATE: only physics/src includes it, and nothing
# that includes physics/ needs Jolt's headers or its compile definitions.
add_library(playground_physics STATIC
  src/PhysicsWorld.cpp)
add_library(playground::physics ALIAS playground_physics)
add_library(engine_physics ALIAS playground_physics)  # the name in the physics plan

target_include_directories(playground_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(playground_physics
  PUBLIC  playground::core playground::ecs glm::glm
  PRIVATE playground_jolt)

playground_configure_target(playground_physics)
