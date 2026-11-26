#pragma once
// =============================================================================
//  playground/ecs/ComponentPool.h — where one component type's data lives.
//
//  A SPARSE SET, three arrays:
//
//    sparse[entity.index] -> slot         which slot holds this entity's T
//    dense[slot]          -> Entity       who owns slot n (with generation)
//    data[slot]           -> T            the components, packed, no gaps
//
//  A system walks `data` front to back: contiguous memory, no pointers to
//  chase, no checks for missing components. That is the whole reason for an
//  ECS. A list of GameObject* with a virtual Update() spends most of its time
//  waiting for memory; this spends it doing work.
//
//  REMOVAL IS SWAP-AND-POP: the last element moves into the hole. O(1), but
//  it reorders the arrays, so nothing may keep a slot number or a T& across
//  an add or remove. Keep the Entity and look the component up again.
//
//  GENERATION CHECK: contains() compares the whole Entity stored in `dense`,
//  not just the index. A stale handle (same slot, older generation) does not
//  see the new owner's component. (The earlier engine/ blueprint checked
//  only the index, and had exactly that bug.)
// =============================================================================

#include "playground/core/Types.h"
#include "playground/ecs/Entity.h"

#include <concepts>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace playground::ecs {

// What a component must be: a plain value that can be moved without
// throwing (swap-and-pop moves them). No base class, no virtual functions.
template <class T>
concept Component = std::is_object_v<T> && !std::is_const_v<T> && std::is_nothrow_move_constructible_v<T> &&
                    std::is_nothrow_move_assignable_v<T>;

// Lets the World own pools of different types and remove an entity from all
// of them without knowing the types.
class PoolBase {
 public:
  virtual ~PoolBase() = default;
  virtual void remove(Entity e) noexcept = 0;
  [[nodiscard]] virtual bool contains(Entity e) const noexcept = 0;
  [[nodiscard]] virtual std::span<const Entity> entities() const noexcept = 0;
  [[nodiscard]] usize size() const noexcept { return entities().size(); }
};

template <Component T>
class ComponentPool final : public PoolBase {
 public:
  static constexpr u32 kNoSlot = Entity::kInvalidIndex;

  // Adds T to `e`, or replaces the one it has. The reference is valid until
  // the next add or remove on this pool.
  template <class... Args>
  T& emplace(Entity e, Args&&... args) {
    if (e.index >= sparse_.size()) sparse_.resize(static_cast<usize>(e.index) + 1, kNoSlot);
    const u32 slot = sparse_[e.index];
    if (slot != kNoSlot && dense_[slot] == e) {
      data_[slot] = T{std::forward<Args>(args)...};
      return data_[slot];
    }
    sparse_[e.index] = static_cast<u32>(dense_.size());
    dense_.push_back(e);
    return data_.emplace_back(std::forward<Args>(args)...);
  }

  void remove(Entity e) noexcept override {
    if (!contains(e)) return;
    const u32 slot = sparse_[e.index];
    const u32 last = static_cast<u32>(dense_.size() - 1);
    if (slot != last) {
      // Move the last element into the hole and repoint its sparse entry.
      dense_[slot] = dense_[last];
      data_[slot] = std::move(data_[last]);
      sparse_[dense_[slot].index] = slot;
    }
    dense_.pop_back();
    data_.pop_back();
    sparse_[e.index] = kNoSlot;
  }

  [[nodiscard]] bool contains(Entity e) const noexcept override {
    if (e.index >= sparse_.size()) return false;
    const u32 slot = sparse_[e.index];
    return slot != kNoSlot && dense_[slot] == e;  // the generation must match too
  }

  [[nodiscard]] T* tryGet(Entity e) noexcept { return contains(e) ? &data_[sparse_[e.index]] : nullptr; }
  [[nodiscard]] const T* tryGet(Entity e) const noexcept {
    return contains(e) ? &data_[sparse_[e.index]] : nullptr;
  }

  // Only after contains(e) said yes: no checks.
  [[nodiscard]] T& getUnchecked(Entity e) noexcept { return data_[sparse_[e.index]]; }

  // The packed arrays, index-parallel: components()[i] belongs to entities()[i].
  [[nodiscard]] std::span<const Entity> entities() const noexcept override { return dense_; }
  [[nodiscard]] std::span<T> components() noexcept { return data_; }
  [[nodiscard]] std::span<const T> components() const noexcept { return data_; }

 private:
  std::vector<u32> sparse_;
  std::vector<Entity> dense_;
  std::vector<T> data_;
};

}  // namespace playground::ecs
