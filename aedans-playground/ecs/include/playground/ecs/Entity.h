#pragma once
// =============================================================================
//  playground/ecs/Entity.h — the handle for one game object.
//
//  An entity is not an object. It is a number that the World uses to look up
//  components: an index (which slot) and a generation (which occupant of that
//  slot).
//
//  WHY THE GENERATION
//    When an entity is destroyed, its slot is reused for the next one created.
//    Without a generation, an old handle to the dead entity would quietly
//    address the new one: a homing missile retargets onto a door, with no
//    crash and no error. Each destroy bumps the slot's generation, so an old
//    handle no longer matches and World::alive() says false.
//
//  32 bits of generation wraps after four billion destroys of one slot,
//  far longer than any stale handle lives.
// =============================================================================

#include "playground/core/Types.h"

#include <cstddef>
#include <functional>
#include <limits>

namespace playground::ecs {

struct Entity {
  static constexpr u32 kInvalidIndex = std::numeric_limits<u32>::max();

  u32 index = kInvalidIndex;
  u32 generation = 0;

  // "Not the null handle". Whether it is still alive only the World knows.
  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidIndex; }
  friend constexpr bool operator==(const Entity&, const Entity&) = default;
};

inline constexpr Entity kNullEntity{};

}  // namespace playground::ecs

template <>
struct std::hash<playground::ecs::Entity> {
  std::size_t operator()(const playground::ecs::Entity& e) const noexcept {
    return std::hash<playground::u64>{}(static_cast<playground::u64>(e.generation) << 32 | e.index);
  }
};
