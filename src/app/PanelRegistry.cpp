#include "PanelRegistry.hpp"

namespace utsyn {

Panel* PanelRegistry::add(std::string name, std::string group, bool open) {
    for (const auto& p : panels_) {
        if (p->name == name) {
            return p.get();
        }
    }
    panels_.push_back(std::make_unique<Panel>(Panel{std::move(name), std::move(group), open}));
    return panels_.back().get();
}

} // namespace utsyn
