# net: networking. Bit-level serialization (BitStream, Quantize) and the
# transport (Host, Peer, Packet) over ENet. Depends on core only; ENet is
# PRIVATE, so no ENet or socket header reaches anything that uses this.
add_library(playground_net STATIC
  src/BitStream.cpp
  src/Host.cpp
  src/Quantize.cpp)
add_library(playground::net ALIAS playground_net)
add_library(engine_net ALIAS playground_net)  # the name in the networking plan

target_include_directories(playground_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(playground_net
  PUBLIC  playground::core glm::glm   # Quantize.h takes glm vectors and quaternions
  PRIVATE playground_enet)

playground_configure_target(playground_net)
