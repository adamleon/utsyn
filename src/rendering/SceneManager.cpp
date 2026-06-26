#include "rendering/SceneManager.hpp"

#include "app/Logger.hpp"

#include <threepp/core/Object3D.hpp>
#include <threepp/scenes/Scene.hpp>

#include <cassert>

namespace utsyn {

SceneManager::SceneManager() noexcept : renderThread_(std::this_thread::get_id()) {}

SceneManager::~SceneManager() {
    // Do NOT touch scenes here: by destruction time the Viewport (and its Scene)
    // may already be gone. Application::shutdown() calls clear() while scenes are
    // still alive; this just drops any remaining shared_ptrs.
    slots_.clear();
    freeList_.clear();
}

void SceneManager::assertRenderThread() const noexcept {
    assert(std::this_thread::get_id() == renderThread_ &&
           "SceneManager used off the render thread");
}

void SceneManager::bindScene(ViewportId vp, threepp::Scene& scene) {
    assertRenderThread();
    // v1 honors only the primary viewport; other ids are accepted but routed to
    // the primary scene until multi-viewport targeting lands.
    (void)vp;
    primary_ = &scene;
}

bool SceneManager::hasScene(ViewportId vp) const noexcept {
    (void)vp;
    return primary_ != nullptr;
}

SceneManager::Slot* SceneManager::resolve(SceneHandle handle) noexcept {
    if (handle.gen == 0) {
        return nullptr;
    }
    if (handle.id >= slots_.size()) {
        return nullptr;
    }
    Slot& s = slots_[handle.id];
    if (!s.live || s.gen != handle.gen) {
        return nullptr;
    }
    return &s;
}

const SceneManager::Slot* SceneManager::resolve(SceneHandle handle) const noexcept {
    if (handle.gen == 0) {
        return nullptr;
    }
    if (handle.id >= slots_.size()) {
        return nullptr;
    }
    const Slot& s = slots_[handle.id];
    if (!s.live || s.gen != handle.gen) {
        return nullptr;
    }
    return &s;
}

SceneHandle SceneManager::add(const std::shared_ptr<threepp::Object3D>& obj, ViewportId vp) {
    assertRenderThread();
    if (!obj) {
        Logger::instance().error("SceneManager::add: null object");
        return SceneHandle{};
    }
    threepp::Scene* scene = primary_; // v1 routes everything to the primary scene
    (void)vp;
    if (!scene) {
        Logger::instance().error("SceneManager::add: no scene bound");
        return SceneHandle{};
    }

    std::size_t index;
    if (!freeList_.empty()) {
        index = static_cast<std::size_t>(freeList_.back());
        freeList_.pop_back();
    } else {
        index = slots_.size();
        slots_.emplace_back();
    }

    Slot& s = slots_[index];
    s.obj   = obj;
    s.scene = scene;
    s.gen += 1; // 0 -> 1 on first use; advances on every reuse so old handles die
    s.live  = true;

    scene->add(obj);
    return SceneHandle{static_cast<std::uint32_t>(index), s.gen};
}

void SceneManager::remove(SceneHandle handle) noexcept {
    assertRenderThread();
    Slot* s = resolve(handle);
    if (!s) {
        return;
    }
    if (s->scene && s->obj) {
        s->scene->remove(*s->obj);
    }
    s->obj.reset();
    s->scene = nullptr;
    s->live  = false;
    freeList_.push_back(handle.id);
}

threepp::Object3D* SceneManager::get(SceneHandle handle) const noexcept {
    assertRenderThread();
    const Slot* s = resolve(handle);
    return s ? s->obj.get() : nullptr;
}

bool SceneManager::valid(SceneHandle handle) const noexcept {
    return resolve(handle) != nullptr;
}

void SceneManager::clear() noexcept {
    assertRenderThread();
    for (Slot& s : slots_) {
        if (s.live && s.scene && s.obj) {
            s.scene->remove(*s.obj);
        }
        s.obj.reset();
        s.scene = nullptr;
        s.live  = false;
    }
    slots_.clear();
    freeList_.clear();
}

} // namespace utsyn
