#include "playground/core/Log.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace playground::log {
namespace {

constexpr usize kCategoryCount = static_cast<usize>(Category::Count);

constexpr std::array<std::string_view, kCategoryCount> kCategoryNames{
    "core", "platform", "gpu", "render", "engine", "net", "game",
};

// [12:04:31.207] [warning ] [platform] message
constexpr const char* kPattern = "[%H:%M:%S.%e] [%^%-8l%$] [%-8n] %v";

struct State {
  std::mutex lifecycleMutex;  // serialises init and shutdown
  std::mutex fallbackMutex;   // keeps stderr lines whole when logging is down
  std::array<std::shared_ptr<spdlog::logger>, kCategoryCount> loggers;
  std::atomic<bool> initialized{false};
  std::atomic<Level> level{Level::Info};
};

// A function-local static is built on first use, so a line logged during
// another file's static initialisation still has somewhere to go.
State& state() noexcept {
  static State instance;
  return instance;
}

spdlog::level::level_enum toSpdlog(Level level) noexcept {
  switch (level) {
    case Level::Trace:    return spdlog::level::trace;
    case Level::Debug:    return spdlog::level::debug;
    case Level::Info:     return spdlog::level::info;
    case Level::Warn:     return spdlog::level::warn;
    case Level::Error:    return spdlog::level::err;
    case Level::Critical: return spdlog::level::critical;
    case Level::Off:      return spdlog::level::off;
  }
  return spdlog::level::info;
}

const char* levelName(Level level) noexcept {
  switch (level) {
    case Level::Trace:    return "trace";
    case Level::Debug:    return "debug";
    case Level::Info:     return "info";
    case Level::Warn:     return "warning";
    case Level::Error:    return "error";
    case Level::Critical: return "critical";
    case Level::Off:      return "off";
  }
  return "info";
}

void shutdownLogging() noexcept {
  State& s = state();
  std::scoped_lock lock(s.lifecycleMutex);
  if (!s.initialized.exchange(false)) return;
  for (auto& logger : s.loggers) {
    if (!logger) continue;
    try {
      logger->flush();
    } catch (...) {
      // Nothing useful can be done with a flush failure at shutdown.
    }
    logger.reset();  // last owner of the sinks: closes the file
  }
}

}  // namespace

// --- Session -----------------------------------------------------------------

Session::Session(Session&& other) noexcept : active_(std::exchange(other.active_, false)) {}

Session& Session::operator=(Session&& other) noexcept {
  if (this != &other) {
    if (active_) shutdownLogging();
    active_ = std::exchange(other.active_, false);
  }
  return *this;
}

Session::~Session() {
  if (active_) shutdownLogging();
}

// --- Lifecycle ---------------------------------------------------------------

Result<Session> init(const Config& config) {
  State& s = state();
  std::scoped_lock lock(s.lifecycleMutex);

  if (s.initialized.load()) {
    return fail(Errc::AlreadyInitialized, "logging is already initialised; only one log::Session may exist at a time");
  }

  std::array<std::shared_ptr<spdlog::logger>, kCategoryCount> loggers;
  try {
    std::vector<spdlog::sink_ptr> sinks;
    if (config.console) {
      sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    if (!config.filePath.empty()) {
      if (config.filePath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(config.filePath.parent_path(), ec);
        if (ec) {
          return fail(Errc::IoFailure, "could not create the log folder '{}': {}",
                      config.filePath.parent_path().string(), ec.message());
        }
      }
      sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.filePath.string(), /*truncate=*/true));
    }

    // One logger per category, all sharing the same sinks. The logger's own
    // level is left at trace: filtering is done once, in enabled(), before
    // any formatting happens.
    for (usize i = 0; i < kCategoryCount; ++i) {
      auto logger = std::make_shared<spdlog::logger>(std::string(kCategoryNames[i]), sinks.begin(), sinks.end());
      logger->set_pattern(kPattern);
      logger->set_level(spdlog::level::trace);
      logger->flush_on(toSpdlog(config.flushLevel));
      loggers[i] = std::move(logger);
    }
  } catch (const std::exception& e) {
    return fail(Errc::IoFailure, "could not start logging to '{}': {}", config.filePath.string(), e.what());
  }

  s.loggers = std::move(loggers);
  s.level.store(config.level);
  s.initialized.store(true, std::memory_order_release);

  Session session;
  session.active_ = true;
  return session;
}

bool isInitialized() noexcept { return state().initialized.load(std::memory_order_acquire); }

bool enabled(Level level) noexcept {
  return level != Level::Off && level >= state().level.load(std::memory_order_relaxed);
}

void setLevel(Level level) noexcept { state().level.store(level); }

std::string_view describe(Category category) noexcept {
  const auto index = static_cast<usize>(category);
  return index < kCategoryCount ? kCategoryNames[index] : std::string_view{"unknown"};
}

// --- Output ------------------------------------------------------------------

void write(Category category, Level level, std::string_view message) noexcept {
  State& s = state();
  const auto index = static_cast<usize>(category);
  if (index >= kCategoryCount || level == Level::Off) return;

  try {
    if (s.initialized.load(std::memory_order_acquire)) {
      s.loggers[index]->log(toSpdlog(level), spdlog::string_view_t(message.data(), message.size()));
      return;
    }
    // Logging is not running: stderr, so the line is never lost.
    std::scoped_lock lock(s.fallbackMutex);
    std::fprintf(stderr, "[%s] [%s] %.*s\n", levelName(level), kCategoryNames[index].data(),
                 static_cast<int>(message.size()), message.data());
  } catch (...) {
    // A logger must never be the reason the program dies.
  }
}

void flush() noexcept {
  State& s = state();
  if (s.initialized.load(std::memory_order_acquire)) {
    for (auto& logger : s.loggers) {
      try {
        if (logger) logger->flush();
      } catch (...) {
      }
    }
  }
  std::fflush(stderr);
  std::fflush(stdout);
}

}  // namespace playground::log
