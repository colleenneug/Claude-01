// ============================================================
//  Logging.
//
//  One ring buffer that the editor's output panel, the packaged game
//  and the console all read from, so a shipped build still has a log
//  you can inspect. Script "Print" nodes land here too.
// ============================================================
#pragma once

#include <cstdarg>
#include <functional>
#include <string>
#include <vector>

namespace forge {

enum class LogLevel { Verbose, Info, Warning, Error, Script };

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
    double time = 0.0;
    int repeats = 1;
};

class Log {
public:
    static Log& get();

    void write(LogLevel level, const char* category, const std::string& message);
    void printf(LogLevel level, const char* category, const char* fmt, ...);

    const std::vector<LogEntry>& entries() const { return entries_; }
    void clear() { entries_.clear(); }
    void setEchoToConsole(bool v) { echo_ = v; }
    // The editor subscribes so its log panel can scroll to the newest line.
    void onEntry(std::function<void(const LogEntry&)> fn) { listener_ = std::move(fn); }

    static const char* levelName(LogLevel l);

private:
    Log() = default;
    std::vector<LogEntry> entries_;
    std::function<void(const LogEntry&)> listener_;
    bool echo_ = true;
    size_t maxEntries_ = 2000;
};

#define FORGE_LOG(...)   ::forge::Log::get().printf(::forge::LogLevel::Info, "Forge", __VA_ARGS__)
#define FORGE_WARN(...)  ::forge::Log::get().printf(::forge::LogLevel::Warning, "Forge", __VA_ARGS__)
#define FORGE_ERROR(...) ::forge::Log::get().printf(::forge::LogLevel::Error, "Forge", __VA_ARGS__)

} // namespace forge
