#pragma once
// =============================================================================
//  playground/game/GameServer.h — the authoritative game server.
//
//  It owns the truth: bouncing balls, and one player entity per client,
//  moved ONLY by that client's inputs, applied here (InputQueue.h: in order,
//  at most one per tick on average) with the same rule the client predicts
//  with (Simulation.h).
//
//  A CLIENT'S LIFE
//    connect      a UDP connection. Nothing more until it says Hello; if it
//                 says nothing within handshakeTimeoutMs (a half-open or
//                 broken client), it is disconnected.
//    Hello        another protocol version: Reject, then goodbye. Otherwise
//                 its player is spawned, and it gets Welcome (its player's
//                 entity id and exact start) and a Spawn of everything that
//                 exists; every other client gets a Spawn of the new player.
//                 All reliable.
//    playing      Input in (unreliable), a Snapshot out every 6 ticks,
//                 made for it: with its lastAckedInputSeq and exact position.
//    disconnect   its player is destroyed and every other client gets a
//                 Despawn (reliable).
//
//  One thread. The caller owns the loop and the clock (app/Server.cpp for
//  the dedicated server, the tests for an in-process one):
//      pumpNetwork(waitMs) as often as it likes, step() once per 1/120 s.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/game/Protocol.h"
#include "playground/game/Simulation.h"
#include "playground/net/Host.h"

#include <memory>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>

namespace playground::game {

struct ServerConfig {
  u16 port = protocol::kDefaultPort;  // 0: any free port (port() says which)
  u32 maxClients = 16;
  u32 balls = 64;
  u32 handshakeTimeoutMs = 3000;  // connected but no Hello by then: disconnected
  net::NetworkConditions network;  // simulated latency and loss on what the server sends
};

struct ServerClientStats {
  u32 peer = 0;
  u32 playerId = 0;
  u32 lastAckedInputSeq = 0;
  u64 inputsApplied = 0;
  u64 inputsDropped = 0;  // over the rate limit
  u64 inputsSkipped = 0;  // never arrived
  glm::vec2 position{0.0f};
};

struct ServerStats {
  u64 snapshotsSent = 0;
  u64 malformedPackets = 0;
  u64 handshakeTimeouts = 0;
  u64 rejected = 0;
  u64 joined = 0;
  u64 left = 0;
};

class GameServer {
 public:
  [[nodiscard]] static Result<std::unique_ptr<GameServer>> start(const ServerConfig& config);
  ~GameServer();
  GameServer(const GameServer&) = delete;
  GameServer& operator=(const GameServer&) = delete;

  // Handles what arrived (waiting up to `waitMs` for the first event), and
  // disconnects clients whose handshake took too long.
  void pumpNetwork(u32 waitMs = 0);
  // One 1/120 s tick: applies inputs, moves the balls, sends snapshots
  // every kSnapshotEveryTicks ticks, flushes.
  void step();
  // Goodbye to every client, waiting up to `waitMs` for them to answer.
  void shutdown(u32 waitMs = 500);

  [[nodiscard]] u32 tick() const noexcept;
  [[nodiscard]] u16 port() const noexcept;
  [[nodiscard]] usize playerCount() const noexcept;   // clients past the handshake
  [[nodiscard]] usize connectedCount() const noexcept;  // including those still handshaking
  [[nodiscard]] usize entityCount() const noexcept;
  [[nodiscard]] std::optional<glm::vec2> playerPosition(u32 playerId) const;
  [[nodiscard]] std::vector<ServerClientStats> clientStats() const;
  [[nodiscard]] const ServerStats& stats() const noexcept;

 private:
  struct Impl;
  explicit GameServer(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace playground::game
