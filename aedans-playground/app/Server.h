#pragma once
// =============================================================================
//  app/Server.h — the headless modes: a dedicated server, and a bot client.
//
//    --server [--port N]              the authoritative game (game/GameServer.h)
//                                     with no window and no GPU (nothing of GLFW
//                                     or Vulkan is even started), at 120 Hz.
//                                     Ctrl+C or SIGTERM: clients are told
//                                     goodbye, then it exits 0.
//    --connect HOST[:PORT] --headless a scripted bot (game/GameClient.h): joins,
//                                     walks in a circle for --frames N ticks,
//                                     stands still half a second so the server
//                                     catches up, then logs a summary,
//                                     including how far its prediction ended
//                                     from the server's answer. For the
//                                     end-to-end test (tests/e2e/server.sh).
//  (--connect without --headless is the windowed client: app/NetScene.h.)
//
//  --net-latency MS and --net-loss PERCENT make either end's outgoing
//  traffic slower and lossier (net::NetworkConditions), for testing.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/game/Protocol.h"
#include "playground/net/Host.h"

#include <string>
#include <string_view>

namespace playground::app {

struct ServerOptions {
  u16 port = game::protocol::kDefaultPort;
  u32 maxClients = 16;
  u32 balls = 64;
  u64 maxTicks = 0;  // 0: until Ctrl+C
  net::NetworkConditions network;
};

struct ClientOptions {
  std::string host = "127.0.0.1";
  u16 port = game::protocol::kDefaultPort;
  u64 ticks = 240;  // how long the bot walks, in 1/120 s ticks (0: until Ctrl+C)
  net::NetworkConditions network;
};

// "HOST" or "HOST:PORT" into `options` (the port stays as it is without one).
[[nodiscard]] Status parseAddress(std::string_view text, ClientOptions& options);

// Exit codes: 0 clean stop, 1 could not start (or, for the client, never
// welcomed, rejected, or dropped by the server).
[[nodiscard]] int runServer(const ServerOptions& options);
[[nodiscard]] int runClient(const ClientOptions& options);

}  // namespace playground::app
