#pragma once
// =============================================================================
//  app/Server.h — the headless modes: a dedicated server, and a test client.
//
//    --server [--port N]       runs the 120 Hz simulation with no window and
//                              no GPU (nothing of GLFW or Vulkan is even
//                              started), accepting clients over UDP and
//                              sending them snapshots (app/Protocol.h).
//                              Ctrl+C or SIGTERM: clients are told goodbye,
//                              then it exits 0.
//    --connect HOST[:PORT]     connects to such a server with no window,
//                              listens for --frames N ticks' worth of time
//                              (N / 120 seconds), and logs what it received.
//                              For the end-to-end test (tests/e2e/server.sh)
//                              and for poking a server by hand.
// =============================================================================

#include "Protocol.h"

#include "playground/core/Types.h"

#include <string>

namespace playground::app {

struct ServerOptions {
  u16 port = protocol::kDefaultPort;
  u32 maxClients = 16;
  u32 entities = 64;
  u64 maxTicks = 0;  // 0: until Ctrl+C
};

struct ClientOptions {
  std::string host = "127.0.0.1";
  u16 port = protocol::kDefaultPort;
  u64 ticks = 240;  // how long to listen, in 1/120 s ticks (0: until Ctrl+C)
};

// Exit codes: 0 clean stop, 1 could not start (or, for the client, never
// connected or never welcomed).
[[nodiscard]] int runServer(const ServerOptions& options);
[[nodiscard]] int runClient(const ClientOptions& options);

}  // namespace playground::app
