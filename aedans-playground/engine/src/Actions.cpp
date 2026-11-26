#include "playground/engine/Actions.h"

#include "playground/core/Assert.h"

#include <algorithm>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace playground::engine {

using platform::Key;
using platform::MouseButton;

ActionMap ActionMap::defaults() {
  ActionMap map;
  map.bindAxis2D(actions::kMove, AxisKeys{.left = Key::A, .right = Key::D, .down = Key::S, .up = Key::W});
  map.bindAxis2D(actions::kMove, AxisKeys{.left = Key::Left, .right = Key::Right, .down = Key::Down, .up = Key::Up});
  map.bindButton(actions::kFire, Key::Space);
  map.bindButton(actions::kFire, MouseButton::Left);
  return map;
}

void ActionMap::bindButton(std::string_view action, InputSource source) {
  auto it = buttons_.find(action);
  if (it == buttons_.end()) it = buttons_.emplace(std::string(action), std::vector<InputSource>{}).first;
  it->second.push_back(source);
}

void ActionMap::bindAxis2D(std::string_view action, AxisKeys keys) {
  auto it = axes_.find(action);
  if (it == axes_.end()) it = axes_.emplace(std::string(action), std::vector<AxisKeys>{}).first;
  it->second.push_back(keys);
}

const std::vector<InputSource>* ActionMap::findButton(std::string_view action) const {
  const auto it = buttons_.find(action);
  PG_ASSERT(it != buttons_.end(), "query for a button action that was never bound (a typo?)");
  return it != buttons_.end() ? &it->second : nullptr;
}

namespace {

// Calls the matching InputState query for a key or a mouse button.
template <class Query>
bool anySource(const std::vector<InputSource>* sources, Query&& query) {
  if (sources == nullptr) return false;
  return std::ranges::any_of(*sources, [&](const InputSource& source) {
    return std::visit([&](auto which) { return query(which); }, source);
  });
}

}  // namespace

bool ActionMap::down(const platform::InputState& input, std::string_view action) const {
  return anySource(findButton(action), [&](auto which) { return input.down(which); });
}

bool ActionMap::pressed(const platform::InputState& input, std::string_view action) const {
  return anySource(findButton(action), [&](auto which) { return input.pressed(which); });
}

bool ActionMap::released(const platform::InputState& input, std::string_view action) const {
  return anySource(findButton(action), [&](auto which) { return input.released(which); });
}

glm::vec2 ActionMap::axis2D(const platform::InputState& input, std::string_view action) const {
  const auto it = axes_.find(action);
  PG_ASSERT(it != axes_.end(), "query for an axis action that was never bound (a typo?)");
  if (it == axes_.end()) return glm::vec2{0.0f};

  glm::vec2 value{0.0f};
  for (const AxisKeys& keys : it->second) {
    value.x += (input.down(keys.right) ? 1.0f : 0.0f) - (input.down(keys.left) ? 1.0f : 0.0f);
    value.y += (input.down(keys.up) ? 1.0f : 0.0f) - (input.down(keys.down) ? 1.0f : 0.0f);
  }
  // D held twice over (D and Right) is still full speed, not double.
  value = glm::clamp(value, glm::vec2{-1.0f}, glm::vec2{1.0f});
  // Diagonals: (1, 1) has length 1.41; scale it back to 1.
  const float length = glm::length(value);
  return length > 1.0f ? value / length : value;
}

}  // namespace playground::engine
