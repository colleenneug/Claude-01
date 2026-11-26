#pragma once
// =============================================================================
//  playground/engine/Actions.h — what the player means, not which key it was.
//
//  Gameplay asks "is Fire pressed?" or "which way is Move?", never "is Space
//  down?". The mapping from keys to actions is data, held in an ActionMap,
//  so rebinding keys (or adding a gamepad later) changes the map, not the
//  game code.
//
//    ActionMap actions = ActionMap::defaults();   // Move: WASD + arrows, Fire: Space + left mouse
//    glm::vec2 move = actions.axis2D(input, actions::kMove);
//    if (actions.pressed(input, actions::kFire)) spawnBullet();
//
//  Two kinds of action:
//    button  pressed / down / released, true if ANY bound key or mouse
//            button is (so Space and left-click both fire)
//    axis2D  a direction from four keys (left, right, down, up). Several key
//            sets can be bound (WASD and arrows); they add up. The result is
//            never longer than 1, so moving diagonally is not ~41% faster.
//
//  Asking for an action that was never bound is a bug (a typo in the name):
//  debug builds assert, release builds answer "not active".
// =============================================================================

#include "playground/platform/Input.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/vec2.hpp>

namespace playground::engine {

namespace actions {
inline constexpr std::string_view kMove = "Move";
inline constexpr std::string_view kFire = "Fire";
}  // namespace actions

// One key or one mouse button.
using InputSource = std::variant<platform::Key, platform::MouseButton>;

// Four keys making a 2D direction.
struct AxisKeys {
  platform::Key left = platform::Key::Unknown;
  platform::Key right = platform::Key::Unknown;
  platform::Key down = platform::Key::Unknown;
  platform::Key up = platform::Key::Unknown;
};

class ActionMap {
 public:
  // Move = WASD and the arrow keys; Fire = Space and the left mouse button.
  [[nodiscard]] static ActionMap defaults();

  // Adds a source to a button action (creating the action on first use).
  void bindButton(std::string_view action, InputSource source);
  // Adds a key set to a 2D axis action (creating it on first use).
  void bindAxis2D(std::string_view action, AxisKeys keys);

  [[nodiscard]] bool down(const platform::InputState& input, std::string_view action) const;
  [[nodiscard]] bool pressed(const platform::InputState& input, std::string_view action) const;
  [[nodiscard]] bool released(const platform::InputState& input, std::string_view action) const;
  [[nodiscard]] glm::vec2 axis2D(const platform::InputState& input, std::string_view action) const;

  [[nodiscard]] bool hasButton(std::string_view action) const { return buttons_.contains(action); }
  [[nodiscard]] bool hasAxis2D(std::string_view action) const { return axes_.contains(action); }

 private:
  // Lets the maps be searched with a string_view, without building a string.
  struct NameHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
  };
  template <class T>
  using ByName = std::unordered_map<std::string, T, NameHash, std::equal_to<>>;

  const std::vector<InputSource>* findButton(std::string_view action) const;

  ByName<std::vector<InputSource>> buttons_;
  ByName<std::vector<AxisKeys>> axes_;
};

}  // namespace playground::engine
