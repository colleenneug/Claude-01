#pragma once
// =============================================================================
//  erebus/ecs/ComponentStorage.h — sparse-set component pools.
//
//  THE LAYOUT
//    sparse[entityIndex] -> denseIndex          (u32, one per possible entity)
//    dense[denseIndex]   -> entity              (who owns slot n)
//    data [denseIndex]   -> T                   (packed, no holes)
//
//  Iterating a system is then a straight walk over `data`: contiguous,
//  prefetcher-friendly, no indirection and no branch for absent components.
//  That is the whole point of the design — an array of GameObject* with
//  virtual Update() spends most of its time waiting on cache misses and
//  indirect branch mispredictions, and no amount of micro-optimisation inside
//  Update() recovers it.
//
//  REMOVAL IS SWAP-AND-POP. The last dense element is moved into the removed
//  slot and the sparse entries are patched. This is O(1) but it *reorders*
//  the array, so no system may hold an index across a structural change. Hold
//  an Entity instead, or defer structural changes to a command buffer applied
//  at a sync point (see World::flush).
//
//  This is a sparse set, not an archetype/chunk model. The trade:
//    - sparse set: O(1) add/remove, cheap random access, but a multi-component
//      query walks the smallest pool and probes the others.
//    - archetype: perfectly packed multi-component iteration, but structural
//      changes move whole rows between chunks.
//  For a shooter, where most structural churn is spawn/despawn of projectiles
//  and most queries are one or two components wide, the sparse set wins on
//  simplicity and loses nothing measurable. Revisit if you grow queries that
//  join four or more pools in a hot path.
// =============================================================================

#include "erebus/core/Types.h"
#include "erebus/ecs/Entity.h"

#include <cassert>
#include <vector>

namespace erebus::ecs {

// Type-erased base so World can own heterogeneous pools and still destroy an
// entity's components without knowing their types.
class IComponentPool {
 public:
  virtual ~IComponentPool() = default;
  virtual void remove(Entity e) noexcept = 0;
  [[nodiscard]] virtual bool contains(Entity e) const noexcept = 0;
  [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

template <Component T>
class ComponentPool final : public IComponentPool {
 public:
  static constexpr u32 kEmpty = Entity::kInvalidIndex;

  // The contract from core/Types.h, restated where the diagnostic is useful.
  static_assert(std::is_trivially_copyable_v<T>,
                "Components are relocated by memcpy on swap-and-pop; a "
                "non-trivially-copyable component would be silently sliced.");

  T& add(Entity e, const T& value = T{}) {
    const u32 idx = e.index();
    if (idx >= sparse_.size()) sparse_.resize(idx + 1, kEmpty);
    if (sparse_[idx] != kEmpty) {                  // already present: overwrite
      data_[sparse_[idx]] = value;
      return data_[sparse_[idx]];
    }
    sparse_[idx] = static_cast<u32>(dense_.size());
    dense_.push_back(e);
    data_.push_back(value);
    return data_.back();
  }

  void remove(Entity e) noexcept override {
    const u32 idx = e.index();
    if (idx >= sparse_.size() || sparse_[idx] == kEmpty) return;

    const u32 slot = sparse_[idx];
    const u32 last = static_cast<u32>(dense_.size() - 1);
    if (slot != last) {
      // Swap-and-pop, then patch the sparse entry of whoever was moved.
      dense_[slot] = dense_[last];
      data_[slot]  = data_[last];
      sparse_[dense_[slot].index()] = slot;
    }
    dense_.pop_back();
    data_.pop_back();
    sparse_[idx] = kEmpty;
  }

  [[nodiscard]] bool contains(Entity e) const noexcept override {
    const u32 idx = e.index();
    return idx < sparse_.size() && sparse_[idx] != kEmpty;
  }

  // Null when absent. A reference-returning get() would need an assert and a
  // dummy object on the failure path; a pointer lets the caller branch once.
  [[nodiscard]] T* tryGet(Entity e) noexcept {
    const u32 idx = e.index();
    if (idx >= sparse_.size() || sparse_[idx] == kEmpty) return nullptr;
    return &data_[sparse_[idx]];
  }
  [[nodiscard]] const T* tryGet(Entity e) const noexcept {
    return const_cast<ComponentPool*>(this)->tryGet(e);
  }

  // The packed arrays, for systems that want to walk them directly. `data()`
  // and `entities()` are index-parallel: data()[i] belongs to entities()[i].
  [[nodiscard]] T* data() noexcept { return data_.data(); }
  [[nodiscard]] const T* data() const noexcept { return data_.data(); }
  [[nodiscard]] const Entity* entities() const noexcept { return dense_.data(); }
  [[nodiscard]] std::size_t size() const noexcept override { return data_.size(); }

  // Range-for over the values.
  [[nodiscard]] auto begin() noexcept { return data_.begin(); }
  [[nodiscard]] auto end() noexcept { return data_.end(); }

 private:
  std::vector<u32> sparse_;                                // entity index -> dense slot
  std::vector<Entity> dense_;                              // dense slot -> entity
  std::vector<T, AlignedAllocator<T>> data_;               // dense slot -> component
};

}  // namespace erebus::ecs
