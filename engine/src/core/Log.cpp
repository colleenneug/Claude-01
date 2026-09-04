#include "forge/core/Log.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace forge {

Log& Log::get() {
    static Log log;
    return log;
}

const char* Log::levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Verbose: return "verbose";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error:   return "error";
        case LogLevel::Script:  return "script";
    }
    return "info";
}

static double nowSeconds() {
    using clock = std::chrono::steady_clock;
    static clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

void Log::write(LogLevel level, const char* category, const std::string& message) {
    // Collapse an immediately repeated line. A script printing every
    // frame would otherwise bury everything else in the panel.
    if (!entries_.empty()) {
        LogEntry& back = entries_.back();
        if (back.level == level && back.message == message && back.category == category) {
            ++back.repeats;
            back.time = nowSeconds();
            if (listener_) listener_(back);
            return;
        }
    }

    LogEntry e;
    e.level = level;
    e.category = category ? category : "Forge";
    e.message = message;
    e.time = nowSeconds();
    entries_.push_back(e);
    if (entries_.size() > maxEntries_)
        entries_.erase(entries_.begin(), entries_.begin() + (long)(entries_.size() - maxEntries_));

    if (echo_) {
        FILE* out = (level == LogLevel::Error || level == LogLevel::Warning) ? stderr : stdout;
        std::fprintf(out, "[%s] %s: %s\n", levelName(level), e.category.c_str(), message.c_str());
    }
    if (listener_) listener_(entries_.back());
}

void Log::printf(LogLevel level, const char* category, const char* fmt, ...) {
    char stackBuf[1024];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof(stackBuf)) { write(level, category, std::string(stackBuf, (size_t)n)); return; }

    std::string big((size_t)n + 1, '\0');
    va_start(args, fmt);
    std::vsnprintf(&big[0], big.size(), fmt, args);
    va_end(args);
    big.resize((size_t)n);
    write(level, category, big);
}

} // namespace forge
