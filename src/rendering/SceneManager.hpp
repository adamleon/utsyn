#pragma once

#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace threepp {
class Scene;
class Object3D;
} // namespace threepp

namespace utsyn {

// Opaque handle to a plugin-added scene object. {slot id, generation}; gen 0 is
// invalid. Same id+generation pattern as the subscription registry's TopicHandle,
// so a stale handle (after remove) never resolves to a reused slot.
struct SceneHandle {
    std::uint32_t id  = 0;
    std::uint32_t gen = 0;

    explicit operator bool() const noexcept { return gen != 0; }
    bool operator==(const SceneHandle&) const = default;
};

// v1 has exactly one viewport; the id is keyed in now so multi-viewport routing is
// an additive change rather than a signature break.
using ViewportId = std::uint32_t;
inline constexpr ViewportId kPrimaryViewport = 0;

// The single, handle-based scene-injection API for all visual plugins (robot
// model, and later TF / markers / pointclouds). SceneManager owns the threepp
// Object3Ds a plugin adds and forwards add/remove to a Viewport's Scene, which
// Application binds in once at startup. Plugins reach it through PluginContext as
// ctx.scene and only ever hold a SceneHandle.
//
// Render-thread-only: add/remove/get/clear/bindScene must run on the render (UI)
// thread (asserted), the same thread that renders the scene — so there are no
// locks and no cross-thread scene mutation.
class SceneManager {
public:
    SceneManager() noexcept;
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // Wired once by Application after a Viewport exists. Stores a non-owning Scene*.
    void bindScene(ViewportId vp, threepp::Scene& scene);
    [[nodiscard]] bool hasScene(ViewportId vp = kPrimaryViewport) const noexcept;

    // Add a plugin-owned Object3D (e.g. a threepp::Robot) to a bound viewport's
    // scene. SceneManager takes shared ownership for the object's scene lifetime.
    // Returns an invalid handle (logged, never asserts) if obj is null or the
    // viewport is unbound.
    SceneHandle add(const std::shared_ptr<threepp::Object3D>& obj,
                    ViewportId vp = kPrimaryViewport);

    // Detach from the scene and drop ownership. No-op on an invalid/stale/double
    // remove (generation check).
    void remove(SceneHandle handle) noexcept;

    // Borrow the live object, or nullptr if the handle is stale. This is the
    // per-frame access path: callers re-resolve each frame rather than caching a
    // raw pointer that a remove() could dangle.
    [[nodiscard]] threepp::Object3D* get(SceneHandle handle) const noexcept;
    [[nodiscard]] bool valid(SceneHandle handle) const noexcept;

    // Detach everything from every bound scene while the scenes are still alive.
    // Application::shutdown() calls this BEFORE the viewports are destroyed.
    void clear() noexcept;

private:
    struct Slot {
        std::shared_ptr<threepp::Object3D> obj;
        threepp::Scene*                    scene = nullptr; // the scene it was added to
        std::uint32_t                      gen   = 0;
        bool                               live  = false;
    };

    void assertRenderThread() const noexcept;
    [[nodiscard]] const Slot* resolve(SceneHandle handle) const noexcept;
    [[nodiscard]] Slot*       resolve(SceneHandle handle) noexcept;

    threepp::Scene*            primary_ = nullptr; // v1: the single bound scene
    std::vector<Slot>          slots_;
    std::vector<std::uint32_t> freeList_;
    std::thread::id            renderThread_;
};

} // namespace utsyn
