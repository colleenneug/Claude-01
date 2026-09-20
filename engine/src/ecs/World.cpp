#include "erebus/ecs/World.h"

#include <algorithm>

namespace erebus::ecs {

Entity World::create() {
  // Reuse a dead slot if there is one. Slots are recycled LIFO, which keeps
  // the live set dense and the sparse arrays in every pool short — an
  // ever-growing index space would make every pool's sparse array grow with
  // total entities ever created rather than with entities alive.
  if (!freeIndices_.empty()) {
    const u32 idx = freeIndices_.back();
    freeIndices_.pop_back();
    free_[idx] = false;
    return Entity{idx, generations_[idx]};
  }

  const u32 idx = static_cast<u32>(generations_.size());
  generations_.push_back(0);
  free_.push_back(false);
  return Entity{idx, 0};
}

void World::destroy(Entity e) {
  // Queued. Applying it here would reorder pools that a system may be
  // iterating right now — see the swap-and-pop note in ComponentStorage.h.
  if (!alive(e)) return;
  pendingDestroy_.push_back(e);
}

void World::flush() {
  if (pendingDestroy_.empty()) return;

  for (const Entity e : pendingDestroy_) {
    // A double-queued destroy (two systems both killed the same projectile in
    // the same step) is not an error; the second one finds it already dead.
    if (!alive(e)) continue;

    const u32 idx = e.index();
    for (IComponentPool* p : order_) p->remove(e);

    // Bumping the generation is what invalidates every outstanding handle.
    // Wrapping is harmless: it would take days of continuous churn, and a
    // handle stale for that long has long since stopped being dereferenced.
    ++generations_[idx];
    free_[idx] = true;
    freeIndices_.push_back(idx);
  }
  pendingDestroy_.clear();
}

}  // namespace erebus::ecs
