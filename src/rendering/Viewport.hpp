#pragma once

#include <memory>

#include <threepp/math/Vector3.hpp>

namespace threepp {
class Scene;
class PerspectiveCamera;
class RenderTarget;
class GLRenderer;
} // namespace threepp

namespace utsyn {

// Wraps a threepp scene + camera, rendered offscreen into a RenderTarget so the
// result can be embedded in an ImGui panel as a texture.
//
// Right-handed, Z-up coordinate system (X forward, Y left, Z up) per CLAUDE.md.
class Viewport {
public:
    Viewport();
    ~Viewport();

    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    // Resize the offscreen target and update camera aspect. No-op if unchanged
    // or if given a non-positive size.
    void resize(int width, int height);

    // Advance scene animation by deltaTime seconds.
    void update(float deltaTime);

    // --- Camera navigation (orbit/pan/zoom around a target) ---
    // Deltas are in screen pixels / mouse-wheel ticks; the panel feeds these
    // from ImGui input. The camera orbits the target on a sphere.
    void orbit(float dAzimuth, float dElevation);
    void pan(float dxScreen, float dyScreen);
    void dolly(float wheelTicks);

    // Render the scene into the offscreen target using the given renderer.
    void render(threepp::GLRenderer& renderer);

    // GL texture id of the rendered color attachment, or 0 if nothing has been
    // rendered yet.
    [[nodiscard]] unsigned int textureId(threepp::GLRenderer& renderer) const;

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    // The threepp scene this viewport renders. SceneManager binds this so plugins
    // can add/remove objects (the Viewport keeps ownership of the scene).
    [[nodiscard]] threepp::Scene& scene() noexcept;

    // The viewport's orbit camera. Exposed so a fullscreen renderer (the Vulkan
    // backend, which has no offscreen render-target support) can render this
    // viewport's scene directly to the window instead of through the panel texture.
    [[nodiscard]] threepp::PerspectiveCamera& camera() noexcept;

private:
    // Recompute the camera position from the orbit parameters (target,
    // radius, azimuth, elevation) and point it at the target.
    void updateCamera();

    std::shared_ptr<threepp::Scene>             scene_;
    std::shared_ptr<threepp::PerspectiveCamera> camera_;
    std::unique_ptr<threepp::RenderTarget>      renderTarget_;
    int width_ = 0;
    int height_ = 0;

    // Orbit-camera state. Default framing is sized for a ~1 m robot at the origin
    // (e.g. a UR arm), not the old 12 m wide-shot that rendered it tiny.
    threepp::Vector3 cameraTarget_{0.0f, 0.0f, 0.4f};
    float radius_    = 3.0f;
    float azimuth_   = -0.785f; // ~ -45° : view from +X / -Y
    float elevation_ = 0.40f;   // radians above the XY plane
};

} // namespace utsyn
