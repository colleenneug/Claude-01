#include "playground/core/Assert.h"

#include "playground/core/Log.h"

#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace playground {
namespace {

void breakIntoDebugger() noexcept {
  if (!debuggerAttached()) return;
#if defined(_WIN32)
  __debugbreak();
#else
  std::raise(SIGTRAP);
#endif
}

// Logging can itself fail (out of memory while formatting). The last resort
// is a raw write to stderr, which needs no allocation.
void report(std::string_view text) noexcept {
  try {
    log::critical(log::Category::Core, "{}", text);
  } catch (...) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(text.size()), text.data());
  }
  log::flush();
}

[[noreturn]] void stop() noexcept {
  breakIntoDebugger();
  std::abort();
}

}  // namespace

bool debuggerAttached() noexcept {
#if defined(_WIN32)
  return IsDebuggerPresent() != 0;
#elif defined(__linux__)
  // Linux has no API for this; the kernel reports the tracing process here.
  try {
    std::ifstream status("/proc/self/status");
    std::string line;
    constexpr std::string_view kKey = "TracerPid:";
    while (std::getline(status, line)) {
      if (!line.starts_with(kKey)) continue;
      const char* first = line.data() + kKey.size();
      const char* last = line.data() + line.size();
      while (first != last && (*first == ' ' || *first == '\t')) ++first;
      int pid = 0;
      std::from_chars(first, last, pid);
      return pid != 0;
    }
  } catch (...) {
  }
  return false;
#else
  return false;
#endif
}

void fatal(std::string_view message, std::source_location where) noexcept {
  try {
    report(std::format("FATAL: {} [{}:{} in {}]", message, where.file_name(), where.line(), where.function_name()));
  } catch (...) {
    report(message);
  }
  stop();
}

namespace detail {

void assertionFailed(const char* expression, std::string_view message, std::source_location where) noexcept {
  try {
    report(std::format("assertion failed: ({}) {} [{}:{} in {}]", expression, message, where.file_name(),
                       where.line(), where.function_name()));
  } catch (...) {
    report(message);
  }
  stop();
}

}  // namespace detail
}  // namespace playground
