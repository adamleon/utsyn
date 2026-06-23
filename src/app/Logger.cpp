#include "app/Logger.hpp"

#include <algorithm>
#include <cstdio>

namespace utsyn {

Logger& Logger::instance() noexcept {
    static Logger logger;
    return logger;
}

Logger::Logger() : start_(std::chrono::steady_clock::now()) {}

namespace {

const char* levelTag(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

} // namespace

void Logger::log(LogLevel level, std::string_view msg) noexcept {
    const double t = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start_)
                             .count();
    {
        std::lock_guard<std::mutex> lock(mu_);
        entries_.push_back(LogEntry{level, t, std::string(msg)});
        if (entries_.size() > kMaxEntries) {
            entries_.pop_front();
        }
    }
    // Mirror to stderr so logs from the ROS thread or a headless run are visible
    // immediately, independent of any UI log panel. Flush so a later crash cannot
    // swallow the most recent lines (stderr may be fully buffered when redirected).
    std::fprintf(stderr, "[%9.3f] %-5s %.*s\n", t, levelTag(level),
                 static_cast<int>(msg.size()), msg.data());
    std::fflush(stderr);
}

void Logger::info(std::string_view msg) noexcept { log(LogLevel::Info, msg); }
void Logger::warn(std::string_view msg) noexcept { log(LogLevel::Warn, msg); }
void Logger::error(std::string_view msg) noexcept { log(LogLevel::Error, msg); }

std::vector<LogEntry> Logger::recent(std::size_t n) const {
    std::lock_guard<std::mutex> lock(mu_);
    const std::size_t count = std::min(n, entries_.size());
    return std::vector<LogEntry>(
            entries_.end() - static_cast<std::ptrdiff_t>(count), entries_.end());
}

} // namespace utsyn
