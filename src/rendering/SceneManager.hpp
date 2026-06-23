#pragma once

namespace utsyn {

// Manages 3D scene objects added by plugins. Prefer handles/indices over
// shared_ptr for hot-path scene objects.
class SceneManager {
public:
    // TODO: add(), remove(), update()

private:
};

} // namespace utsyn
