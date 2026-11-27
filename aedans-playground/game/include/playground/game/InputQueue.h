#pragma once
// =============================================================================
//  playground/game/InputQueue.h — one client's inputs, waiting on the server.
//
//  Inputs arrive unreliably, several copies of each (every packet repeats
//  the last few), out of order, in bursts after a hiccup. The queue keeps
//  each sequence number once, in order, and forgets anything at or before
//  the last one applied.
//
//  RATE LIMIT (no speed hacks): a client is allowed ONE input per server
//  tick, on average. Each tick adds one token to a small bucket (at most
//  kBurst); applying an input costs one. So a client that fell behind may
//  catch up a few inputs at once, but a client sending inputs twice as fast
//  as the tick rate moves no faster: the extras pile up and, past
//  kMaxQueued, the oldest are thrown away (counted as dropped). The client
//  then sees the server's answer differ from its prediction and corrects.
//
//  A missing input (every copy of it lost) is skipped, not waited for:
//  inputs are applied in order, and the next one present goes next.
// =============================================================================

#include "playground/core/Types.h"
#include "playground/game/Protocol.h"

#include <deque>
#include <functional>
#include <span>

namespace playground::game {

struct InputQueueStats {
  u64 received = 0;   // new inputs accepted
  u64 duplicate = 0;  // already queued, or already applied: ignored
  u64 applied = 0;
  u64 dropped = 0;    // thrown away because the queue was full (too many, too fast)
  u64 skipped = 0;    // sequence numbers that never arrived
};

class InputQueue {
 public:
  static constexpr u32 kBurst = 3;       // tokens: at most this many inputs in one tick
  static constexpr u32 kMaxQueued = 12;  // 100 ms of input; more is a client too far ahead (or cheating)

  // Inputs `newestSeq - inputs.size() + 1 .. newestSeq`, oldest first.
  void receive(u32 newestSeq, std::span<const PlayerInput> inputs);

  // One server tick: refills the bucket, then applies queued inputs in
  // order while tokens last. `apply(seq, input)` is called for each.
  // Returns how many were applied.
  u32 tick(const std::function<void(u32 seq, const PlayerInput& input)>& apply);

  [[nodiscard]] u32 lastApplied() const noexcept { return lastApplied_; }
  [[nodiscard]] usize queued() const noexcept { return queue_.size(); }
  [[nodiscard]] const InputQueueStats& stats() const noexcept { return stats_; }

 private:
  struct Entry {
    u32 seq = 0;
    PlayerInput input;
  };
  std::deque<Entry> queue_;  // sorted by seq, each once, all > lastApplied_
  u32 lastApplied_ = 0;
  u32 tokens_ = kBurst;
  InputQueueStats stats_;
};

}  // namespace playground::game
