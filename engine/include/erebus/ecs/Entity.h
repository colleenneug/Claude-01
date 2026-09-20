#pragma once
// =============================================================================
//  erebus/ecs/Entity.h — generational entity handles.
//
//  An entity is a 32-bit index plus a 32-bit generation, packed into a u64.
//
//  WHY THE GENERATION. Index-only handles have a use-after-free that is
//  invisible: entity 41 dies, entity 41's slot is recycled for a new entity,
//  and a stale handle to the dead one now addresses a live, unrelated object.
//  No crash, no assert — a projectile homes onto a door. The generation
//  counter increments on every destroy, so a stale handle compares unequal to
//  whatever now occupies the slot and `alive()` returns false.
//
//  32 bits of generation at, say, 10,000 destroys a second wraps in about five
//  days of continuous play, which is comfortably past the point where a stale
//  handle would have caused a visible problem anyway.
// =============================================================================

#include "erebus/core/Types.h"

#include <functional>
#include <limits>

namespace erebus::ecs {

class Entity {
 public:
  static constexpr u32 kInvalidIndex = std::numeric_limits<u32>::max();

  constexpr Entity() noexcept = default;
  constexpr Entity(u32 index, u32 generation) noexcept
      : bits_(static_cast<u64>(generation) << 32 | index) {}

  [[nodiscard]] constexpr u32 index() const noexcept { return static_cast<u32>(bits_ & 0xffffffffull); }
  [[nodiscard]] constexpr u32 generation() const noexcept { return static_cast<u32>(bits_ >> 32); }
  [[nodiscard]] constexpr u64 bits() const noexcept { return bits_; }

  // "Not the null handle". Says nothing about whether the entity is still
  // alive — only World::alive() can answer that, because only the World holds
  // the authoritative generation for a slot.
  [[nodiscard]] constexpr bool valid() const noexcept { return index() != kInvalidIndex; }

  constexpr bool operator==(const Entity&) const noexcept = default;

 private:
  u64 bits_ = kInvalidIndex;
};

inline constexpr Entity kNullEntity{};

}  // namespace erebus::ecs

template <>
struct std::hash<erebus::ecs::Entity> {
  std::size_t operator()(const erebus::ecs::Entity& e) const noexcept {
    return std::hash<erebus::u64>{}(e.bits());
  }
};
