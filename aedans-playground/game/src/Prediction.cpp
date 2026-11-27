#include "playground/game/Prediction.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace playground::game {

void Prediction::reset(const PlayerState& state) noexcept {
  ring_.fill({});
  current_ = state;
  newest_ = 0;
  acked_ = 0;
}

const Prediction::Entry* Prediction::find(u32 seq) const noexcept {
  const Entry& entry = ring_[seq % kCapacity];
  return seq != 0 && entry.seq == seq ? &entry : nullptr;
}

const PlayerState& Prediction::predict(u32 seq, const PlayerInput& input) {
  Entry& slot = ring_[seq % kCapacity];
  if (slot.seq != 0 && slot.seq > acked_) ++stats_.overflows;  // still unacknowledged: lost from history
  current_ = stepPlayer(current_, input);
  slot = Entry{.seq = seq, .input = input, .state = current_};
  newest_ = seq;
  ++stats_.predicted;
  return current_;
}

bool Prediction::reconcile(u32 ackSeq, const PlayerState& server, f32 epsilon) {
  if (ackSeq == 0 || ackSeq <= acked_) {  // nothing applied yet, or old news
    if (ackSeq != 0 && ackSeq < acked_) ++stats_.stale;
    return false;
  }
  if (ackSeq > newest_) {
    // The server has applied inputs this client never predicted (it was
    // reset, or the server is confused): its word is all there is.
    current_ = server;
    acked_ = ackSeq;
    newest_ = ackSeq;
    ring_.fill({});
    ++stats_.corrections;
    return true;
  }
  acked_ = ackSeq;
  const Entry* predicted = find(ackSeq);
  if (predicted != nullptr && glm::length(predicted->state.position - server.position) <= epsilon) {
    ++stats_.confirmed;
    return false;
  }

  // Rewind to the server's state and replay every later input the ring
  // still holds. (If the ack's own entry was overwritten, the inputs
  // between it and the oldest held are gone: replay what is left.)
  PlayerState state = server;
  for (u32 seq = std::max(ackSeq + 1, oldestHeld()); seq <= newest_; ++seq) {
    Entry& entry = ring_[seq % kCapacity];
    if (entry.seq != seq) continue;
    state = stepPlayer(state, entry.input);
    entry.state = state;
    ++stats_.replayed;
  }
  current_ = state;
  ++stats_.corrections;
  return true;
}

u32 Prediction::oldestHeld() const noexcept { return newest_ < kCapacity ? 1 : newest_ - kCapacity + 1; }

std::vector<PlayerInput> Prediction::inputsSince(u32 first) const {
  std::vector<PlayerInput> inputs;
  for (u32 seq = std::max(first, oldestHeld()); seq <= newest_; ++seq) {
    const Entry* entry = find(seq);
    if (entry == nullptr) break;  // never: everything from oldestHeld() on is in the ring
    inputs.push_back(entry->input);
  }
  return inputs;
}

}  // namespace playground::game
