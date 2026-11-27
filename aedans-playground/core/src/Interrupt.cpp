#include "playground/core/Interrupt.h"

#include <atomic>
#include <csignal>

namespace playground {
namespace {

// Written by a signal handler, so it must be lock-free.
std::atomic<bool> gInterruptRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free, "signal handlers may only touch lock-free atomics");

extern "C" void onInterruptSignal(int /*signal*/) { gInterruptRequested.store(true); }

}  // namespace

InterruptGuard::InterruptGuard() noexcept {
  gInterruptRequested.store(false);
  previousInterrupt_ = std::signal(SIGINT, &onInterruptSignal);
  previousTerminate_ = std::signal(SIGTERM, &onInterruptSignal);
}

InterruptGuard::~InterruptGuard() {
  // SIG_ERR means we never replaced it.
  if (previousInterrupt_ != SIG_ERR) std::signal(SIGINT, previousInterrupt_);
  if (previousTerminate_ != SIG_ERR) std::signal(SIGTERM, previousTerminate_);
}

bool InterruptGuard::interruptRequested() noexcept { return gInterruptRequested.load(); }

}  // namespace playground
