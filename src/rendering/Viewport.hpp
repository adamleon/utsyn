#pragma once

#include <memory>

#include <threepp/math/Vector3.hpp>

namespace threepp {
class Scene;
class PerspectiveCamera;
class RenderTarget;
class GLRenderer;
class Mesh;
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

private:
    // Recompute the camera position from the orbit parameters (target,
    // radius, azimuth, elevation) and point it at the target.
    void updateCamera();

    std::shared_ptr<threepp::Scene>             scene_;
    std::shared_ptr<threepp::PerspectiveCamera> camera_;
    std::unique_ptr<threepp::RenderTarget>      renderTarget_;
    std::shared_ptr<threepp::Mesh>              spinner_; // demo content
    int width_ = 0;
    int height_ = 0;

    // Orbit-camera state.
    threepp::Vector3 cameraTarget_{0, 0, 1};
    float radius_    = 12.0f;
    float azimuth_   = -0.785f; // ~ -45° : view from +X / -Y
    float elevation_ = 0.45f;   // radians above the XY plane
};

} // namespace utsyn
