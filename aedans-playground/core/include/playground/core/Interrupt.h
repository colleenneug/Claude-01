#pragma once
// =============================================================================
//  playground/core/Interrupt.h — Ctrl+C as a request to stop, not a kill.
//
//  By default Ctrl+C (SIGINT) or `kill` (SIGTERM) ends a program on the
//  spot: no "goodbye" to connected players, no flushed log. While an
//  InterruptGuard exists, those signals only raise a flag; the main loop
//  checks interruptRequested() and shuts down in order.
//
//  In core, not platform, so a headless dedicated server (no window, no
//  GLFW) gets the same behaviour as the windowed game. platform::Platform
//  holds one for the game.
//
//  The handler only stores to a lock-free atomic: the one thing a signal
//  handler may safely do. One guard at a time (the handlers are global).
// =============================================================================

namespace playground {

class InterruptGuard {
 public:
  // Installs the handlers and clears the flag.
  InterruptGuard() noexcept;
  // Puts back whatever handlers were there before.
  ~InterruptGuard();
  InterruptGuard(const InterruptGuard&) = delete;
  InterruptGuard& operator=(const InterruptGuard&) = delete;

  // Set by Ctrl+C or SIGTERM while a guard exists. Stays set.
  [[nodiscard]] static bool interruptRequested() noexcept;

 private:
  using Handler = void (*)(int);
  Handler previousInterrupt_ = nullptr;
  Handler previousTerminate_ = nullptr;
};

}  // namespace playground
