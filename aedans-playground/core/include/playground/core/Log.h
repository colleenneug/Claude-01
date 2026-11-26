#pragma once
// =============================================================================
//  playground/core/Log.h — the engine's one logging interface.
//
//  WHAT IT GIVES
//    - Severity levels, and a category per subsystem so a line says where it
//      came from: [platform], [gpu], [engine]...
//    - Output to a coloured console and to a file overwritten each run.
//    - Lines are written to disk as they are logged (see Config::flushLevel),
//      so `tail -f playground.log` is live and a crash never loses the
//      lines that explain it.
//    - Lines logged before init() or after shutdown go to stderr, never
//      nowhere. Start-up is exactly when things fail.
//
//  WHY spdlog IS NOT IN THIS HEADER
//    Messages are formatted here with std::format and handed to Log.cpp as a
//    finished string, so no subsystem includes spdlog. Replacing it touches
//    one .cpp file. A message below the current level is rejected before it
//    is formatted, so disabled logging costs one comparison.
//
//  LIFETIME AND THREADS
//    init() returns a Session; logging lives exactly as long as it does.
//    Create it first in main() and let it die last. Logging itself is
//    thread-safe; init and shutdown must happen while no other thread logs.
//
//  USE
//    auto session = log::init({.filePath = "playground.log"});
//    log::info(log::Category::Platform, "window {}x{}", width, height);
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace playground::log {

enum class Level : u8 { Trace, Debug, Info, Warn, Error, Critical, Off };

// One per layer in the roadmap. Add a category when a subsystem is added,
// not before.
enum class Category : u8 { Core, Platform, Gpu, Render, Engine, Game, Count };

// Lines at or above this level are pushed to disk immediately. In debug
// builds that is every line; in release, info and above (trace and debug
// lines, if enabled, are buffered: they can come by the thousand, and a disk
// write per line would show up in the frame time).
#if defined(NDEBUG)
inline constexpr Level kDefaultFlushLevel = Level::Info;
#else
inline constexpr Level kDefaultFlushLevel = Level::Trace;
#endif

struct Config {
  std::filesystem::path filePath = "playground.log";  // empty: no log file
  Level level = Level::Info;                          // lines below this are dropped
  bool console = true;                                // also write to stdout
  Level flushLevel = kDefaultFlushLevel;              // lines at or above this are flushed at once
};

// RAII handle for the logging system. Move-only; destroying the active
// Session flushes and closes every sink.
class Session {
 public:
  Session(Session&& other) noexcept;
  Session& operator=(Session&& other) noexcept;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  ~Session();

 private:
  friend Result<Session> init(const Config& config);
  Session() = default;
  bool active_ = false;
};

// Fails with Errc::AlreadyInitialized if a Session is alive, or
// Errc::IoFailure if the log file cannot be created.
[[nodiscard]] Result<Session> init(const Config& config);

[[nodiscard]] bool isInitialized() noexcept;
[[nodiscard]] bool enabled(Level level) noexcept;
void setLevel(Level level) noexcept;
[[nodiscard]] std::string_view describe(Category category) noexcept;

// Writes one finished line. Prefer the typed helpers below.
void write(Category category, Level level, std::string_view message) noexcept;

// Pushes everything buffered to disk. Called by fatal() before aborting.
void flush() noexcept;

template <class... Args>
void print(Category category, Level level, std::format_string<Args...> format, Args&&... args) {
  if (!enabled(level)) return;
  write(category, level, std::format(format, std::forward<Args>(args)...));
}

template <class... Args>
void trace(Category c, std::format_string<Args...> f, Args&&... a) { print(c, Level::Trace, f, std::forward<Args>(a)...); }
template <class... Args>
void debug(Category c, std::format_string<Args...> f, Args&&... a) { print(c, Level::Debug, f, std::forward<Args>(a)...); }
template <class... Args>
void info(Category c, std::format_string<Args...> f, Args&&... a) { print(c, Level::Info, f, std::forward<Args>(a)...); }
template <class... Args>
void warn(Category c, std::format_string<Args...> f, Args&&... a) { print(c, Level::Warn, f, std::forward<Args>(a)...); }
template <class... Args>
void error(Category c, std::format_string<Args...> f, Args&&... a) { print(c, Level::Error, f, std::forward<Args>(a)...); }
template <class... Args>
void critical(Category c, std::format_string<Args...> f, Args&&... a) { print(c, Level::Critical, f, std::forward<Args>(a)...); }

}  // namespace playground::log
