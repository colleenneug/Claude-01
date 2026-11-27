#pragma once
// =============================================================================
//  playground/platform/Input.h — keyboard and mouse, as per-frame state.
//
//  THREE PIECES
//
//    InputCollector  Fed by the window's GLFW callbacks (Window.cpp) as events
//                    arrive. Turns them into held/pressed/released, and hands
//                    out one snapshot per frame.
//
//    InputState      That snapshot: plain, copyable data. For each key and
//                    mouse button:
//                      down      held right now
//                      pressed   went down since the last snapshot
//                      released  went up since the last snapshot
//                    A tap that starts and ends within one frame shows as
//                    pressed AND released (and not down), so it is never lost.
//
//    InputLatch      Hands input to the fixed simulation steps. Every step in
//                    a frame sees the same `down` state, but each press or
//                    release is delivered to exactly ONE step:
//                      - a slow frame that runs 4 steps does not fire 4 times;
//                      - a fast frame that runs 0 steps does not lose the press:
//                        it waits for the next step that does run.
//
//  FOCUS LOSS
//    When the window loses focus (Alt+Tab), the OS stops sending key events
//    to it, so a key held during the switch never gets its "up". Without a
//    guard the player keeps walking forever. focusLost() releases every held
//    key and button: they report `released` once, then nothing is down.
//
//  No GLFW here: keys have the engine's own names, and Window.cpp translates.
// =============================================================================

#include "playground/core/Types.h"

#include <bitset>
#include <string_view>

namespace playground::platform {

enum class Key : u16 {
  Unknown,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  Space, Enter, Escape, Tab, Backspace,
  Left, Right, Up, Down,
  LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  Count  // not a key: the number of keys
};

enum class MouseButton : u8 { Left, Right, Middle, Count };

inline constexpr usize kKeyCount = static_cast<usize>(Key::Count);
inline constexpr usize kMouseButtonCount = static_cast<usize>(MouseButton::Count);

// "W", "Space", "Left"... for logs and (later) settings files.
[[nodiscard]] std::string_view describe(Key key) noexcept;

class InputState {
 public:
  [[nodiscard]] bool down(Key key) const noexcept { return keys_.down.test(index(key)); }
  [[nodiscard]] bool pressed(Key key) const noexcept { return keys_.pressed.test(index(key)); }
  [[nodiscard]] bool released(Key key) const noexcept { return keys_.released.test(index(key)); }

  [[nodiscard]] bool down(MouseButton button) const noexcept { return mouse_.down.test(index(button)); }
  [[nodiscard]] bool pressed(MouseButton button) const noexcept { return mouse_.pressed.test(index(button)); }
  [[nodiscard]] bool released(MouseButton button) const noexcept { return mouse_.released.test(index(button)); }

  // Cursor position in window coordinates (pixels from the top-left).
  [[nodiscard]] f64 mouseX() const noexcept { return mouseX_; }
  [[nodiscard]] f64 mouseY() const noexcept { return mouseY_; }

  // Mouse wheel turned since the last snapshot, in "clicks" (a touchpad gives
  // fractions): positive = away from the user (scroll up). Like a press, it is
  // delivered to one fixed step only.
  [[nodiscard]] f64 scroll() const noexcept { return scroll_; }

  // True if any key or button was pressed or released.
  [[nodiscard]] bool hasEdges() const noexcept;

 private:
  friend class InputCollector;
  friend class InputLatch;

  template <usize N>
  struct Buttons {
    std::bitset<N> down, pressed, released;
  };

  static constexpr usize index(Key key) noexcept {
    const auto i = static_cast<usize>(key);
    return i < kKeyCount ? i : 0;  // out of range reads as Key::Unknown, never down
  }
  static constexpr usize index(MouseButton button) noexcept {
    const auto i = static_cast<usize>(button);
    return i < kMouseButtonCount ? i : 0;
  }

  Buttons<kKeyCount> keys_;
  Buttons<kMouseButtonCount> mouse_;
  f64 mouseX_ = 0.0;
  f64 mouseY_ = 0.0;
  f64 scroll_ = 0.0;
};

class InputCollector {
 public:
  // From the window's callbacks. Key repeats are not events: a held key is
  // simply down. Key::Unknown is ignored.
  void keyEvent(Key key, bool isDown) noexcept;
  void mouseButtonEvent(MouseButton button, bool isDown) noexcept;
  void cursorMoved(f64 x, f64 y) noexcept;
  void scrolled(f64 clicks) noexcept;  // vertical wheel; added up until the next snapshot

  // Releases everything held (each reports `released` in the next snapshot).
  void focusLost() noexcept;

  // Everything since the previous call: current held state plus the edges
  // that happened in between. Clears the edges.
  [[nodiscard]] InputState takeSnapshot() noexcept;

  // Held state right now, without taking a snapshot (for diagnostics).
  [[nodiscard]] const InputState& peek() const noexcept { return state_; }

 private:
  InputState state_;
};

class InputLatch {
 public:
  // Once per frame, with that frame's snapshot. Presses and releases not yet
  // delivered to a step are kept and added to the new ones.
  void beginFrame(const InputState& frame) noexcept;

  // What the next fixed step sees.
  [[nodiscard]] const InputState& current() const noexcept { return state_; }

  // After each fixed step: its edges are delivered, so the following steps
  // see only what is held.
  void consumeEdges() noexcept;

 private:
  InputState state_;
};

}  // namespace playground::platform
