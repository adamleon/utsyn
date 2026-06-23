#include "ViewportManager.hpp"

#include "Viewport.hpp"

#include <cassert>

namespace utsyn {

ViewportManager::ViewportManager() = default;
ViewportManager::~ViewportManager() = default;

Viewport& ViewportManager::create() {
    viewports_.push_back(std::make_unique<Viewport>());
    return *viewports_.back();
}

Viewport& ViewportManager::at(std::size_t index) {
    assert(index < viewports_.size());
    return *viewports_[index];
}

} // namespace utsyn
