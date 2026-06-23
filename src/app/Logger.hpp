#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace utsyn {

enum class LogLevel { Info, Warn, Error };

struct LogEntry {
    LogLevel    level;
    double      monotonicSeconds;  // steady_clock seconds since logger start
    std::string message;
};

// Thread-safe singleton logger. The only sanctioned global mutable state. Lives
// in utsyn_core (SHARED) so the app and every plugin DLL share one instance. Both
// the ROS thread and the render thread log through it; the mutex is the only lock
// and never touches the render hot path beyond the occasional log line.
class Logger {
public:
    static Logger& instance() noexcept;

    void info(std::string_view msg) noexcept;
    void warn(std::string_view msg) noexcept;
    void error(std::string_view msg) noexcept;

    // Snapshot of up to the last `n` entries (oldest first). For a future log panel.
    [[nodiscard]] std::vector<LogEntry> recent(std::size_t n) const;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    void log(LogLevel level, std::string_view msg) noexcept;

    mutable std::mutex                    mu_;       // guards entries_
    std::deque<LogEntry>                  entries_;
    std::chrono::steady_clock::time_point start_;
    static constexpr std::size_t          kMaxEntries = 512;
};

} // namespace utsyn
