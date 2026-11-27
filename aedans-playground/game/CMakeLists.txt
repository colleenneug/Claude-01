# game: the networked game itself. The protocol both sides speak, the rules
# both sides run (so the client can predict), and the authoritative server
# and the predicting, interpolating client. No window and no GPU: the
# dedicated server, the headless client and the tests all use it as is.
add_library(playground_game STATIC
  src/GameClient.cpp
  src/GameServer.cpp
  src/InputQueue.cpp
  src/Interpolation.cpp
  src/Prediction.cpp
  src/Protocol.cpp
  src/Simulation.cpp)
add_library(playground::game ALIAS playground_game)

target_include_directories(playground_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(playground_game PUBLIC playground::engine playground::net)

playground_configure_target(playground_game)
