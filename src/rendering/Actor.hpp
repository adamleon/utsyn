#pragma once

#include <string>

namespace utsyn {

class SceneManager;

// Base class for a plugin-contributed 3D visualization with a lifecycle — utsyn's
// equivalent of an rviz "Display" (named Actor per the project's choice). An Actor
// owns its threepp scene objects (added/removed through SceneManager) and may draw
// an ImGui inspector for its own properties. Plugins subclass it: RobotActor, and
// later MarkerActor / TfActor / etc.
//
// Lifecycle (all on the render thread): onAttach() creates + adds the threepp
// objects; onUpdate(dt) mutates them each frame; onInspector() draws the actor's
// ImGui properties; onDetach() removes them. Today the owning plugin drives these;
// a later interaction layer (selection, transform gizmos, picking) will manage
// Actors uniformly — which is why the lifecycle lives in a base class rather than
// inline in each plugin.
class Actor {
public:
    explicit Actor(SceneManager& scene) noexcept : scene_(&scene) {}
    virtual ~Actor() = default;

    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    virtual void onAttach() {}
    virtual void onUpdate(float deltaTime) { (void)deltaTime; }
    virtual void onDetach() {}
    virtual void onInspector() {}

    [[nodiscard]] virtual std::string name() const = 0;

protected:
    [[nodiscard]] SceneManager& scene() noexcept { return *scene_; }

private:
    SceneManager* scene_;
};

} // namespace utsyn
