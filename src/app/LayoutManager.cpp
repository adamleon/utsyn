#include "LayoutManager.hpp"

#include "Logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <system_error>

#include <imgui.h>

namespace utsyn {

namespace {

// Read an environment variable. Uses _dupenv_s on Windows because std::getenv trips
// MSVC's C4996 ("may be unsafe"), which is fatal under /WX. (Same pattern as
// PackageResolver's getEnv.)
std::optional<std::string> getEnv(const char* name) {
#ifdef _WIN32
    char*  buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
        return std::nullopt;
    }
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* v = std::getenv(name);
    if (v == nullptr) {
        return std::nullopt;
    }
    return std::string(v);
#endif
}

// Per-user config dir for utsyn, platform-appropriate. Returns empty on failure.
std::string resolveConfigDir() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    if (const auto appdata = getEnv("APPDATA"); appdata && !appdata->empty()) {
        return (fs::path(*appdata) / "utsyn").string();
    }
#else
    if (const auto xdg = getEnv("XDG_CONFIG_HOME"); xdg && !xdg->empty()) {
        return (fs::path(*xdg) / "utsyn").string();
    }
    if (const auto home = getEnv("HOME"); home && !home->empty()) {
        return (fs::path(*home) / ".config" / "utsyn").string();
    }
#endif
    return {};
}

} // namespace

void LayoutManager::applyImGuiIniPath() {
    if (iniPath_.empty()) {
        configDir_ = resolveConfigDir();
        if (configDir_.empty()) {
            Logger::instance().warn(
                    "LayoutManager: no config dir (APPDATA/HOME unset); ImGui layout "
                    "stays at the default CWD-relative imgui.ini");
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(configDir_, ec);
        if (ec) {
            Logger::instance().warn("LayoutManager: could not create " + configDir_ + ": " +
                                    ec.message());
            return;
        }
        iniPath_ = (std::filesystem::path(configDir_) / "imgui.ini").string();
    }
    // ImGui stores the pointer (does not copy), so iniPath_ must outlive the context.
    ImGui::GetIO().IniFilename = iniPath_.c_str();
    Logger::instance().info("LayoutManager: ImGui layout -> " + iniPath_);
}

void LayoutManager::saveNow() const {
    if (!iniPath_.empty() && ImGui::GetCurrentContext() != nullptr) {
        ImGui::SaveIniSettingsToDisk(iniPath_.c_str());
    }
}

} // namespace utsyn
