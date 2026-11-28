#include "playground/ecs/World.h"

#include <atomic>

namespace playground::ecs {

namespace detail {
u32 nextComponentTypeId() noexcept {
  static std::atomic<u32> next{0};
  return next.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace detail

Entity World::create() {
  u32 index = 0;
  if (!freeIndices_.empty()) {
    // Reuse the most recently freed slot: its memory is likely still in cache.
    index = freeIndices_.back();
    freeIndices_.pop_back();
  } else {
    index = static_cast<u32>(slots_.size());
    PG_VERIFY(index != Entity::kInvalidIndex, "entity index space exhausted");
    slots_.push_back({});
  }
  slots_[index].inUse = true;
  ++liveCount_;
  return Entity{.index = index, .generation = slots_[index].generation};
}

void World::destroy(Entity e) {
  if (alive(e)) pendingDestroy_.push_back(e);
}

void World::flush() {
  PG_ASSERT(iterating_ == 0, "flush() during each(): call it after the systems have run");
  for (const Entity e : pendingDestroy_) {
    if (!alive(e)) continue;  // queued twice: already gone
    for (const auto& p : pools_) {
      if (p) p->remove(e);
    }
    Slot& slot = slots_[e.index];
    slot.inUse = false;
    ++slot.generation;  // every old handle to this slot now fails alive()
    freeIndices_.push_back(e.index);
    --liveCount_;
  }
  pendingDestroy_.clear();
}

std::vector<Entity> World::entities() const {
  std::vector<Entity> live;
  live.reserve(liveCount_);
  for (u32 i = 0; i < slots_.size(); ++i) {
    if (slots_[i].inUse) live.push_back({.index = i, .generation = slots_[i].generation});
  }
  return live;
}

void World::clear() {
  PG_ASSERT(iterating_ == 0, "clear() during each()");
  pendingDestroy_ = entities();
  flush();
}

}  // namespace playground::ecs
