#include "playground/platform/Input.h"

#include <array>

namespace playground::platform {
namespace {

// Indexed by Key; must list every key in enum order (checked below).
constexpr std::array<std::string_view, kKeyCount> kKeyNames{
    "Unknown",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "Space", "Enter", "Escape", "Tab", "Backspace",
    "Left", "Right", "Up", "Down",
    "LeftShift", "RightShift", "LeftControl", "RightControl", "LeftAlt", "RightAlt",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
};
static_assert(kKeyNames.back() == "F12", "kKeyNames is out of step with enum Key");

// One button changing: sets the edge only if the state really changed, so a
// duplicate "down" (or a stray "up" for a key never seen down) is harmless.
template <usize N>
void apply(std::bitset<N>& down, std::bitset<N>& pressed, std::bitset<N>& released, usize i, bool isDown) noexcept {
  if (isDown && !down.test(i)) {
    down.set(i);
    pressed.set(i);
  } else if (!isDown && down.test(i)) {
    down.reset(i);
    released.set(i);
  }
}

}  // namespace

std::string_view describe(Key key) noexcept {
  const auto i = static_cast<usize>(key);
  return i < kKeyCount ? kKeyNames[i] : std::string_view{"Unknown"};
}

bool InputState::hasEdges() const noexcept {
  return keys_.pressed.any() || keys_.released.any() || mouse_.pressed.any() || mouse_.released.any();
}

// --- InputCollector ---------------------------------------------------------

void InputCollector::keyEvent(Key key, bool isDown) noexcept {
  if (key == Key::Unknown || static_cast<usize>(key) >= kKeyCount) return;
  auto& k = state_.keys_;
  apply(k.down, k.pressed, k.released, static_cast<usize>(key), isDown);
}

void InputCollector::mouseButtonEvent(MouseButton button, bool isDown) noexcept {
  if (static_cast<usize>(button) >= kMouseButtonCount) return;
  auto& m = state_.mouse_;
  apply(m.down, m.pressed, m.released, static_cast<usize>(button), isDown);
}

void InputCollector::cursorMoved(f64 x, f64 y) noexcept {
  state_.mouseX_ = x;
  state_.mouseY_ = y;
}

void InputCollector::focusLost() noexcept {
  // Everything held is released: the game sees one `released` for each, so
  // "stop running" or "let go of the charge" logic still runs.
  auto& k = state_.keys_;
  k.released |= k.down;
  k.down.reset();
  auto& m = state_.mouse_;
  m.released |= m.down;
  m.down.reset();
}

InputState InputCollector::takeSnapshot() noexcept {
  InputState snapshot = state_;
  state_.keys_.pressed.reset();
  state_.keys_.released.reset();
  state_.mouse_.pressed.reset();
  state_.mouse_.released.reset();
  return snapshot;
}

// --- InputLatch ---------------------------------------------------------------

void InputLatch::beginFrame(const InputState& frame) noexcept {
  // Held state and cursor: always the newest. Edges: the newest plus any
  // the previous frame did not get to deliver (it ran no fixed step).
  InputState next = frame;
  next.keys_.pressed |= state_.keys_.pressed;
  next.keys_.released |= state_.keys_.released;
  next.mouse_.pressed |= state_.mouse_.pressed;
  next.mouse_.released |= state_.mouse_.released;
  state_ = next;
}

void InputLatch::consumeEdges() noexcept {
  state_.keys_.pressed.reset();
  state_.keys_.released.reset();
  state_.mouse_.pressed.reset();
  state_.mouse_.released.reset();
}

}  // namespace playground::platform
