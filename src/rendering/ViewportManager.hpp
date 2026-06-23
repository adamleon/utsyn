#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace utsyn {

class Viewport;

// Owns and manages all Viewport instances. The app creates viewports here;
// whether plugins may also create viewports is an open decision (see
// ARCHITECTURE.md) and is not supported yet.
class ViewportManager {
public:
    ViewportManager();
    ~ViewportManager();

    ViewportManager(const ViewportManager&) = delete;
    ViewportManager& operator=(const ViewportManager&) = delete;

    // Create a new viewport and return a reference to it. The manager retains
    // ownership; the reference stays valid until the manager is destroyed.
    Viewport& create();

    [[nodiscard]] std::size_t count() const { return viewports_.size(); }
    [[nodiscard]] Viewport& at(std::size_t index);

private:
    std::vector<std::unique_ptr<Viewport>> viewports_;
};

} // namespace utsyn
