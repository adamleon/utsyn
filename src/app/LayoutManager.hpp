#pragma once

#include <string>

namespace utsyn {

// Owns where the UI layout persists. Currently it points ImGui's ini file (docking
// layout, window positions/sizes, open/closed state) at a stable per-user config dir so
// the layout survives restarts — instead of ImGui's default CWD-relative "imgui.ini",
// which lands wherever the process happens to be launched from.
//
// NOTE: utsyn's own higher-level layout.json (named layouts, plugin panel descriptors)
// is a separate, still-open design decision (whether it unifies with or wraps the ImGui
// ini — see CLAUDE.md) and is intentionally NOT implemented here.
class LayoutManager {
public:
    // Resolve + create the config dir and point ImGui's io.IniFilename at
    // <configDir>/imgui.ini. Call once per ImGui context, after CreateContext() and
    // before the first frame. The owned path string outlives the call — ImGui stores
    // the pointer, it does not copy — so this object must outlive the ImGui context.
    void applyImGuiIniPath();

    // Flush the current layout to disk now. ImGui saves periodically (IniSavingRate);
    // call this on shutdown so a change made in the last few seconds isn't lost.
    void saveNow() const;

    // Platform config dir: %APPDATA%\utsyn (Windows) / $XDG_CONFIG_HOME|~/.config /utsyn
    // (Linux). Empty if it could not be resolved.
    [[nodiscard]] const std::string& configDir() const { return configDir_; }

private:
    std::string configDir_;
    std::string iniPath_; // backing store for io.IniFilename (must outlive the context)
};

} // namespace utsyn
