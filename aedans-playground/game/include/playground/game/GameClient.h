#pragma once
// =============================================================================
//  playground/game/GameClient.h — playing on a GameServer.
//
//    connect     Connecting -> (UDP connected) send Hello -> Handshaking
//                -> Welcome: Playing, or Reject: Rejected. No Welcome
//                within connectTimeoutMs: Disconnected.
//    each tick   step(input): the input is QUANTIZED exactly as the server
//                will see it, applied at once to the local player
//                (prediction: no waiting for the server), remembered, and
//                sent with the last few unacknowledged ones (unreliable).
//    snapshots   older than the newest already seen: dropped. A delta is
//                rebuilt on the snapshot it names, from the last
//                kSnapshotHistory kept (each Input tells the server the
//                newest we have, so it only names ones we kept). Then the
//                local player is RECONCILED (Prediction.h) and the snapshot
//                buffered for drawing everything else a little in the past,
//                smoothly (Interpolation.h).
//
//  Drawing the local player: blended between the last two ticks (alpha),
//  and when a correction moves it, the jump is spread over a few ticks
//  (a visual offset that fades) rather than shown as a snap.
//
//  One thread; the caller owns the loop and the clock: pumpNetwork() as
//  often as it likes, step() once per 1/120 s.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/game/Interpolation.h"
#include "playground/game/Prediction.h"
#include "playground/game/Protocol.h"
#include "playground/net/Host.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

namespace playground::game {

struct ClientConfig {
  std::string host = "127.0.0.1";
  u16 port = protocol::kDefaultPort;
  std::string name = "player";
  u16 protocolVersion = protocol::kVersion;  // what Hello claims: tests send another to be rejected
  u32 connectTimeoutMs = 5000;               // connected AND welcomed by then, or give up
  net::NetworkConditions network;            // simulated latency and loss on what the client sends
};

enum class ClientState : u8 { Connecting, Handshaking, Playing, Rejected, Disconnected };
[[nodiscard]] std::string_view describe(ClientState state) noexcept;

// Something else in the world, as this client draws it now.
struct RemoteEntity {
  u32 id = 0;
  EntityKind kind = EntityKind::Ball;
  glm::vec2 position{0.0f};
  f32 rotation = 0.0f;
};

// A cutscene the server scheduled for everyone, in server ticks.
struct CutsceneCue {
  u32 sequenceId = 0;
  u32 startTick = 0;
  u32 endTick = 0;         // start + duration, or earlier once everyone skipped
  bool endedEarly = false;
};

struct ClientStats {
  u64 snapshots = 0;
  u64 outOfOrder = 0;  // older than one already seen: dropped
  u64 deltaSnapshots = 0;   // of `snapshots`, how many came as deltas
  u64 missingBaseline = 0;  // deltas against a snapshot we did not have: dropped (should stay 0)
  u64 malformed = 0;
  u64 inputPackets = 0;
  u64 spawned = 0;
  u64 despawned = 0;
  PredictionStats prediction;
};

class GameClient {
 public:
  [[nodiscard]] static Result<std::unique_ptr<GameClient>> connect(const ClientConfig& config);
  ~GameClient();
  GameClient(const GameClient&) = delete;
  GameClient& operator=(const GameClient&) = delete;

  void pumpNetwork(u32 waitMs = 0);
  // One 1/120 s tick with this input (ignored until Playing).
  void step(const PlayerInput& input);
  // Goodbye, waiting up to `waitMs` for the server to answer.
  void disconnect(u32 waitMs = 500);

  [[nodiscard]] ClientState state() const noexcept;
  // Why it was Rejected or Disconnected (empty for a goodbye of our own).
  [[nodiscard]] const std::string& failure() const noexcept;
  [[nodiscard]] u32 playerId() const noexcept;  // valid once Playing
  [[nodiscard]] glm::vec2 predictedPosition() const noexcept;
  // Where the newest snapshot said the local player was (after lastAckedInputSeq()).
  [[nodiscard]] glm::vec2 serverPosition() const noexcept;
  // Where to draw the local player, `alpha` of the way from the previous tick to this one.
  [[nodiscard]] glm::vec2 renderPosition(f32 alpha) const noexcept;
  // Everything else the server has told this client about, interpolated.
  [[nodiscard]] std::vector<RemoteEntity> remoteEntities(f32 alpha = 1.0f) const;
  [[nodiscard]] usize knownEntityCount() const noexcept;
  [[nodiscard]] u32 lastServerTick() const noexcept;
  // The server tick others are drawn at (kInterpolationDelayTicks behind, roughly).
  [[nodiscard]] f64 interpolationTick() const noexcept;
  [[nodiscard]] u32 inputSeq() const noexcept;
  [[nodiscard]] u32 lastAckedInputSeq() const noexcept;
  // How long, smoothed, an input takes to come back acknowledged: the
  // round trip plus the server's queue and snapshot spacing.
  [[nodiscard]] f64 ackDelayMs() const noexcept;
  // The server's tick now, as this client estimates it (cutscenes run on it).
  [[nodiscard]] f64 serverTickNow() const noexcept;
  // The latest cutscene the server announced (it may be over), and whether
  // movement is locked by it right now (step() then sends no movement).
  [[nodiscard]] std::optional<CutsceneCue> cutscene() const noexcept;
  [[nodiscard]] bool cutsceneLocked() const noexcept;
  // ENet's smoothed round trip to the server (without simulated delay).
  [[nodiscard]] f64 roundTripMs() const;
  [[nodiscard]] ClientStats stats() const noexcept;

 private:
  struct Impl;
  explicit GameClient(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace playground::game
