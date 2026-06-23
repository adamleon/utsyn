#pragma once

namespace utsyn {

// Thread-safe singleton logger. The only sanctioned global mutable state.
class Logger {
public:
    static Logger& instance();

    // TODO: info(), warn(), error()

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
};

} // namespace utsyn
