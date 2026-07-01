#pragma once

#include <memory>
#include <string>
#include <vector>

namespace utsyn {

// A registered UI panel: an ImGui window name, a group label (for the View menu), and
// its open/closed state.
struct Panel {
    std::string name;  // ImGui window name (unique)
    std::string group; // "Core" or the contributing plugin's name — groups the View menu
    bool        open = true;
};

// Registry of dockable UI panels (core + plugin-contributed) so a View menu can
// enumerate and toggle every panel uniformly. The app owns one instance and exposes it
// to plugins via PluginContext. A registered Panel's `open` bool is both what each panel
// gates its ImGui::Begin on and what the View menu toggles:
//
//   Panel* p = ctx.panels.add("My Panel", name());   // in initialize()
//   if (ImGui::Begin("My Panel", &p->open)) { ... }   // in onImGui()
//   ImGui::End();
class PanelRegistry {
public:
    // Register a panel, or return the existing one if `name` is already registered
    // (idempotent). The returned pointer is stable for the registry's lifetime.
    Panel* add(std::string name, std::string group, bool open = true);

    [[nodiscard]] const std::vector<std::unique_ptr<Panel>>& panels() const { return panels_; }

private:
    std::vector<std::unique_ptr<Panel>> panels_;
};

} // namespace utsyn
