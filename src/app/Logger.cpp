#include "Logger.hpp"

namespace utsyn {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

} // namespace utsyn
