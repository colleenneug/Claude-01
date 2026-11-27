#pragma once
// =============================================================================
//  render/src/ResourcePool.h — GPU objects behind generational handles (internal).
//
//  The renderer keeps every mesh and texture in a pool: a slot array plus a
//  generation per slot. add() returns {index, generation}; get() checks the
//  generation, so a handle to a destroyed object finds nothing.
//
//  DEFERRED DESTRUCTION
//    A frame the CPU recorded a moment ago may still be on the GPU, reading
//    the mesh the game just destroyed. So retire() does not free it: the
//    handle goes stale at once, but the object and its slot wait in a list,
//    tagged with how many frames had been submitted. release(completed)
//    frees those all of whose frames have finished. Slots are only reused
//    after that, which also matters for textures: a slot is a place in the
//    bindless array, and an in-flight frame may still read the old one.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace playground::render {

template <class T, class Handle>
class ResourcePool {
 public:
  ResourcePool(u32 capacity, std::string_view kind) noexcept : capacity_(capacity), kind_(kind) {}

  // Errc::InvalidState when every slot is taken (live or waiting to be freed).
  [[nodiscard]] Result<Handle> add(T value) {
    u32 index = 0;
    if (!free_.empty()) {
      index = free_.back();
      free_.pop_back();
    } else if (slots_.size() < capacity_) {
      index = static_cast<u32>(slots_.size());
      slots_.emplace_back();
    } else {
      return fail(Errc::InvalidState, "the renderer's {} limit of {} is reached", kind_, capacity_);
    }
    Slot& slot = slots_[index];
    slot.value.emplace(std::move(value));
    return Handle{.index = index, .generation = slot.generation};
  }

  [[nodiscard]] T* get(Handle handle) noexcept {
    if (handle.index >= slots_.size()) return nullptr;
    Slot& slot = slots_[handle.index];
    return slot.generation == handle.generation && slot.value ? &*slot.value : nullptr;
  }

  // The handle goes stale now; the object is freed by a later release().
  // `framesSubmitted`: frames handed to the GPU so far (any of them may use it).
  // False for a stale or invalid handle.
  bool retire(Handle handle, u64 framesSubmitted) {
    if (get(handle) == nullptr) return false;
    Slot& slot = slots_[handle.index];
    retired_.push_back({.framesSubmitted = framesSubmitted, .index = handle.index, .value = std::move(*slot.value)});
    slot.value.reset();
    ++slot.generation;
    return true;
  }

  // Frees what was retired when no more than `framesCompleted` frames had
  // been submitted: every frame that could use it has finished. Returns how
  // many were freed.
  usize release(u64 framesCompleted) {
    usize freed = 0;
    for (usize i = 0; i < retired_.size();) {
      if (retired_[i].framesSubmitted <= framesCompleted) {
        free_.push_back(retired_[i].index);
        retired_[i] = std::move(retired_.back());  // order does not matter
        retired_.pop_back();
        ++freed;
      } else {
        ++i;
      }
    }
    return freed;
  }

  [[nodiscard]] usize liveCount() const noexcept {
    usize live = 0;
    for (const Slot& slot : slots_) live += slot.value ? 1 : 0;
    return live;
  }
  [[nodiscard]] usize retiredCount() const noexcept { return retired_.size(); }

 private:
  struct Slot {
    std::optional<T> value;
    u32 generation = 0;
  };
  struct Retired {
    u64 framesSubmitted = 0;
    u32 index = 0;
    T value;
  };

  u32 capacity_;
  std::string_view kind_;  // "mesh", "texture": for the error message
  std::vector<Slot> slots_;
  std::vector<u32> free_;
  std::vector<Retired> retired_;
};

}  // namespace playground::render
