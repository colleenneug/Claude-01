#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// What the player is pressing, this frame, in the game's own terms. The
// platform layer (PlatformInput.cpp, GLFW today) fills one of these once a
// frame; nothing in gameplay reads the window or a GLFW key code. Moving the
// game onto another engine means filling this struct from that engine's
// events and nothing else — see TRANSFER.md.
enum class Key : uint8_t {
  W, A, S, D,
  Up, Down, Left, Right,
  Space, LeftShift, LeftControl, RightControl,
  C, E, F, G, I, J, O, Q, R, X, Y, N,
  Num1, Num2, Num3,
  Tab, Enter, KeypadEnter, Escape, Backspace,
  Count,
  None = Count,
};

struct Input {
  std::array<bool, (size_t)Key::Count> down{};
  std::string typed;     // characters typed since the last frame (text fields)
  double time = 0.0;     // seconds since start, for anything that blinks

  bool held(Key k) const { return k != Key::None && down[(size_t)k]; }
  bool held(Key a, Key b) const { return held(a) || held(b); }

  // Nothing held: for a headless run, or a screen that should not react.
  static const Input& none() {
    static const Input nothing{};
    return nothing;
  }
};
