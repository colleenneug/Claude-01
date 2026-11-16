#pragma once
// =============================================================================
//  playground/core/Assert.h — checks for things that must never happen.
//
//  Result/Status is for failures the program should expect and handle (no
//  display, a missing file, an old GPU). Asserts are for bugs: a broken
//  invariant means the program's own state is wrong, and carrying on makes
//  the eventual crash harder to trace back.
//
//    PG_ASSERT(cond, "message")   checked in debug builds, compiled out in
//                                 release. The condition is not evaluated
//                                 in release, so it must have no side effects.
//    PG_VERIFY(cond, "message")   checked in every build. For invariants
//                                 cheap enough to keep, or too dangerous to lose.
//    playground::fatal("message") unconditional stop.
//
//  A failure logs the condition, message, file, line and function, flushes
//  the log, breaks into the debugger if one is attached, and aborts. It only
//  breaks when a debugger is attached because otherwise the break itself
//  would kill the process with a bare trap signal and no explanation.
// =============================================================================

#include <source_location>
#include <string_view>

namespace playground {

[[noreturn]] void fatal(std::string_view message,
                        std::source_location where = std::source_location::current()) noexcept;

[[nodiscard]] bool debuggerAttached() noexcept;

namespace detail {
[[noreturn]] void assertionFailed(const char* expression, std::string_view message,
                                  std::source_location where) noexcept;
}  // namespace detail

}  // namespace playground

#define PG_VERIFY(condition, message)                                                          \
  do {                                                                                         \
    if (!(condition)) [[unlikely]] {                                                           \
      ::playground::detail::assertionFailed(#condition, (message),                             \
                                            std::source_location::current());                  \
    }                                                                                          \
  } while (false)

#if defined(NDEBUG)
// sizeof keeps the expression type-checked (and its variables "used") without
// ever evaluating it.
#define PG_ASSERT(condition, message) \
  do {                                \
    (void)sizeof(!(condition));       \
  } while (false)
#else
#define PG_ASSERT(condition, message) PG_VERIFY(condition, message)
#endif
