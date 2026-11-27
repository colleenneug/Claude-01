#pragma once
// =============================================================================
//  playground/game/Prediction.h — moving the local player with no lag.
//
//  CLIENT-SIDE PREDICTION: each tick the client applies its own input at
//  once (stepPlayer, the server's own rule) instead of waiting a round trip
//  for the server to say where it is. It remembers what it did in a ring:
//  { input sequence number, the input, the state after it }.
//
//  RECONCILIATION: each snapshot says "I have applied your inputs up to
//  number A, and that left you at P". The client looks up what it predicted
//  for input A:
//    - the same (within epsilon): the prediction was right; forget A and
//      everything before it;
//    - different (an input was lost, capped, or the world disagreed): the
//      server wins. Take P as the state after A, then REPLAY every input
//      after A on top of it, so the player stays where its newer inputs
//      put it, now on the server's footing.
//
//  THE RING is fixed-size (kCapacity, 2 s at 120 Hz). With a huge ping the
//  client can get further ahead of the server than that: the oldest entry
//  is overwritten (counted in overflows). If the server then acknowledges
//  an input the ring no longer holds, the client cannot compare, so it
//  takes the server's state and replays what it still has. Nothing reads
//  outside the ring, whatever the numbers.
// =============================================================================

#include "playground/core/Types.h"
#include "playground/game/Protocol.h"
#include "playground/game/Simulation.h"

#include <array>
#include <vector>

namespace playground::game {

struct PredictionStats {
  u64 predicted = 0;
  u64 confirmed = 0;    // acknowledgements that matched
  u64 corrections = 0;  // acknowledgements that did not: rewound and replayed
  u64 replayed = 0;     // inputs re-applied during corrections
  u64 overflows = 0;    // ring entries overwritten before the server acknowledged them
  u64 stale = 0;        // acknowledgements older than one already handled: ignored
};

class Prediction {
 public:
  static constexpr u32 kCapacity = 256;
  static constexpr f32 kDefaultEpsilon = 0.01f;  // world units; the server sends the exact position

  Prediction() = default;
  // Starts over at `state`, with nothing predicted (after Welcome).
  void reset(const PlayerState& state) noexcept;

  // Applies input number `seq` (one more than the last) and returns the new state.
  const PlayerState& predict(u32 seq, const PlayerInput& input);

  // The server applied inputs up to `ackSeq`, which left it at `server`.
  // Returns true if that corrected the prediction.
  bool reconcile(u32 ackSeq, const PlayerState& server, f32 epsilon = kDefaultEpsilon);

  [[nodiscard]] const PlayerState& current() const noexcept { return current_; }
  [[nodiscard]] u32 newestSeq() const noexcept { return newest_; }
  [[nodiscard]] u32 lastAcked() const noexcept { return acked_; }
  // Predicted but not yet acknowledged (what an Input packet should repeat).
  [[nodiscard]] u32 unacknowledged() const noexcept { return newest_ - acked_; }
  // The inputs `first..newestSeq()`, oldest first (from the oldest the ring still holds).
  [[nodiscard]] std::vector<PlayerInput> inputsSince(u32 first) const;
  [[nodiscard]] const PredictionStats& stats() const noexcept { return stats_; }

 private:
  struct Entry {
    u32 seq = 0;  // 0: empty
    PlayerInput input;
    PlayerState state;  // after the input
  };
  [[nodiscard]] const Entry* find(u32 seq) const noexcept;
  [[nodiscard]] u32 oldestHeld() const noexcept;  // the oldest input the ring can still hold

  std::array<Entry, kCapacity> ring_{};  // input `seq` lives at seq % kCapacity
  PlayerState current_;
  u32 newest_ = 0;  // the last predicted input
  u32 acked_ = 0;   // the last acknowledged input
  PredictionStats stats_;
};

}  // namespace playground::game
