#include "playground/game/InputQueue.h"

#include <algorithm>

namespace playground::game {

void InputQueue::receive(u32 newestSeq, std::span<const PlayerInput> inputs) {
  if (inputs.empty() || newestSeq < inputs.size()) return;  // sequence numbers start at 1
  u32 seq = newestSeq - static_cast<u32>(inputs.size()) + 1;
  for (const PlayerInput& input : inputs) {
    const u32 thisSeq = seq++;
    if (thisSeq <= lastApplied_) {
      ++stats_.duplicate;
      continue;
    }
    // Sorted insert; most arrive in order, so this is nearly always at the back.
    const auto at = std::lower_bound(queue_.begin(), queue_.end(), thisSeq,
                                     [](const Entry& e, u32 s) { return e.seq < s; });
    if (at != queue_.end() && at->seq == thisSeq) {
      ++stats_.duplicate;
      continue;
    }
    queue_.insert(at, Entry{.seq = thisSeq, .input = protocol::quantize(input)});
    ++stats_.received;
  }
  // Too far ahead: drop the oldest. Applying them later would only add lag
  // (or, for a cheater, speed).
  while (queue_.size() > kMaxQueued) {
    lastApplied_ = std::max(lastApplied_, queue_.front().seq);  // never accept it (or older) again
    queue_.pop_front();
    ++stats_.dropped;
  }
}

u32 InputQueue::tick(const std::function<void(u32 seq, const PlayerInput& input)>& apply) {
  tokens_ = std::min(tokens_ + 1, kBurst);
  u32 applied = 0;
  while (tokens_ > 0 && !queue_.empty()) {
    const Entry next = queue_.front();
    queue_.pop_front();
    stats_.skipped += next.seq - lastApplied_ - 1;  // every copy of these was lost
    apply(next.seq, next.input);
    lastApplied_ = next.seq;
    --tokens_;
    ++applied;
    ++stats_.applied;
  }
  return applied;
}

}  // namespace playground::game
