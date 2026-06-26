#include "Viewport.hpp"

#include <threepp/cameras/PerspectiveCamera.hpp>
#include <threepp/helpers/AxesHelper.hpp>
#include <threepp/helpers/GridHelper.hpp>
#include <threepp/lights/AmbientLight.hpp>
#include <threepp/lights/DirectionalLight.hpp>
#include <threepp/math/Color.hpp>
#include <threepp/math/MathUtils.hpp>
#include <threepp/renderers/GLRenderer.hpp>
#include <threepp/renderers/RenderTarget.hpp>
#include <threepp/scenes/Scene.hpp>

#include <algorithm>
#include <cmath>

namespace utsyn {

Viewport::Viewport() {
    using namespace threepp;

    scene_ = Scene::create();
    scene_->background = Color(0x0a0a0a);

    camera_ = PerspectiveCamera::create(60.0f, 1.0f, 0.1f, 1000.0f);

    // Ground grid on the XY plane. GridHelper is built in the XZ plane (Y-up),
    // so rotate it a quarter turn about X to lie flat in our Z-up world.
    auto grid = GridHelper::create(10, 10, Color(0x335533), Color(0x224422));
    grid->rotateX(math::PI / 2.0f);
    scene_->add(grid);

    // Axes: X red (forward), Y green (left), Z blue (up).
    scene_->add(AxesHelper::create(2.0f));

    // The scene is intentionally empty of content beyond the grid/axes reference —
    // plugins (e.g. robot_description) add their objects via SceneManager.
    scene_->add(AmbientLight::create(Color(0xffffff), 0.4f));
    auto sun = DirectionalLight::create(Color(0xffffff), 0.8f);
    sun->position.set(5, -5, 10);
    scene_->add(sun);

    updateCamera();
}

Viewport::~Viewport() = default;

void Viewport::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width == width_ && height == height_) {
        return;
    }
    width_ = width;
    height_ = height;

    // Recreate the target at the new size rather than calling setSize().
    // setSize() updates width/height but leaves the viewport/scissor rectangles
    // stale, and GLRenderer drives the GL viewport from renderTarget.viewport —
    // so a resized target renders the scene into the old rectangle and the
    // displayed image ends up stretched/clipped. The constructor sets texture,
    // viewport, and scissor consistently, so recreating is the reliable path.
    renderTarget_ = threepp::RenderTarget::create(
            static_cast<unsigned int>(width),
            static_cast<unsigned int>(height),
            threepp::RenderTarget::Options{});

    camera_->aspect = static_cast<float>(width) / static_cast<float>(height);
    camera_->updateProjectionMatrix();
}

void Viewport::update(float /*deltaTime*/) {
    // Per-frame viewport hook. The scene's content is owned by plugins (added via
    // SceneManager); nothing to animate here at the viewport level.
}

threepp::Scene& Viewport::scene() noexcept {
    return *scene_;
}

void Viewport::orbit(float dAzimuth, float dElevation) {
    azimuth_ += dAzimuth;
    elevation_ += dElevation;
    updateCamera();
}

void Viewport::pan(float dxScreen, float dyScreen) {
    using namespace threepp;

    // Build the camera's screen-space basis from the current view direction.
    Vector3 forward;
    forward.subVectors(cameraTarget_, camera_->position).normalize();
    const Vector3 worldUp(0, 0, 1);
    Vector3 right;
    right.crossVectors(forward, worldUp).normalize();
    Vector3 up;
    up.crossVectors(right, forward).normalize();

    // Scale pan by distance so it feels consistent at any zoom.
    const float scale = radius_ * 0.0015f;
    right.multiplyScalar(-dxScreen * scale); // drag right -> scene moves right
    up.multiplyScalar(dyScreen * scale);     // drag down -> scene moves down
    cameraTarget_.add(right).add(up);
    updateCamera();
}

void Viewport::dolly(float wheelTicks) {
    // Each tick zooms by 10%; scrolling up (positive) moves closer.
    radius_ *= std::pow(0.9f, wheelTicks);
    updateCamera();
}

void Viewport::updateCamera() {
    using namespace threepp;

    // Clamp so we never look straight down the up-axis (degenerate lookAt).
    const float limit = math::PI / 2.0f - 0.05f;
    elevation_ = std::clamp(elevation_, -limit, limit);
    radius_ = std::clamp(radius_, 0.5f, 200.0f);

    const float ce = std::cos(elevation_);
    const Vector3 offset(
            radius_ * ce * std::cos(azimuth_),
            radius_ * ce * std::sin(azimuth_),
            radius_ * std::sin(elevation_));

    camera_->position.copy(cameraTarget_).add(offset);
    camera_->up.set(0, 0, 1); // Z-up
    camera_->lookAt(cameraTarget_);
}

void Viewport::render(threepp::GLRenderer& renderer) {
    if (!renderTarget_) {
        return;
    }
    renderer.setRenderTarget(renderTarget_.get());
    renderer.render(*scene_, *camera_);
    renderer.setRenderTarget(nullptr); // restore the default framebuffer
}

unsigned int Viewport::textureId(threepp::GLRenderer& renderer) const {
    if (!renderTarget_ || !renderTarget_->texture) {
        return 0;
    }
    return renderer.getGlTextureId(*renderTarget_->texture).value_or(0);
}

} // namespace utsyn
