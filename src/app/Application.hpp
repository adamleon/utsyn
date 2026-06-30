#pragma once

#include <memory>

namespace threepp {
class Canvas;
class Renderer;
class GLRenderer;
class PerspectiveCamera;
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
class VulkanRenderer;
struct MouseListener;
#endif
} // namespace threepp

#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
class ImguiContext; // threepp's ImGui helper (global namespace, in extras/imgui)
#endif

namespace utsyn {

class ViewportManager;
class ViewportPanel;
class RosCore;
class SceneManager;
class SubscriptionRegistry;
class SubscriptionBroker;
class PluginLoader;
struct PluginContext;

// Top-level app: owns the window, the render loop, DPI/ImGui setup, the ROS/plugin
// backbone, and orderly teardown.
class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Open the window and run the render loop until the window is closed.
    // useVulkan selects the threepp Vulkan deferred-hybrid backend (fullscreen +
    // ImGui overlay); the default OpenGL path keeps the docked multi-viewport.
    void run(bool useVulkan = false);

private:
    void initImGui();
    void applyTerminalStyle();
    void frame();      // one render-loop iteration (dispatches GL vs Vulkan)
    void renderUi();   // build the ImGui frame (dockspace, panels, plugin UI)
    void shutdown() noexcept; // orderly teardown: stop ROS -> clear broker -> unload plugins
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    void frameVulkan(); // fullscreen scene render + ImGui overlay
    [[nodiscard]] bool mouseInCentral(float x, float y) const; // cursor over the scene area
#endif

    std::unique_ptr<threepp::Canvas>      canvas_;
    std::unique_ptr<threepp::Renderer>    renderer_;     // GLRenderer or VulkanRenderer
    threepp::GLRenderer*                  glRenderer_ = nullptr; // non-owning view, GL only
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    threepp::VulkanRenderer*              vkRenderer_ = nullptr; // non-owning view, Vulkan only
    std::unique_ptr<ImguiContext>         vkUi_;                 // Vulkan ImGui (threepp helper)
    std::unique_ptr<threepp::MouseListener> vkWheel_;           // scroll -> camera zoom
    bool                                  vkStyleApplied_ = false;
    // Central dockspace-node rect (window coords) under Vulkan; camera input is
    // active only when the cursor is over it (the scene area showing through the
    // passthrough central node), not over a docked/floating panel.
    float                                 vkCentralX_ = 0.0f, vkCentralY_ = 0.0f;
    float                                 vkCentralW_ = 0.0f, vkCentralH_ = 0.0f;
#endif
    bool                                  useVulkan_ = false;

    // ROS / plugin backbone. Declaration order matters for destruction: plugins
    // and the broker tear down before the registry, which tears down before
    // RosCore destroys the node. shutdown() performs the explicit ordered teardown
    // while the window is still alive; these are the destructor backstop.
    std::unique_ptr<RosCore>              rosCore_;
    std::unique_ptr<SubscriptionRegistry> registry_;
    std::unique_ptr<SubscriptionBroker>   broker_;
    std::unique_ptr<SceneManager>         scene_;
    std::unique_ptr<ViewportManager>      viewports_;
    std::unique_ptr<PluginLoader>         plugins_;
    std::unique_ptr<PluginContext>        ctx_;
    std::unique_ptr<ViewportPanel>        viewportPanel_;

    bool  imguiInitialized_ = false;
    bool  shutdownDone_ = false;
    float dpiScale_ = 1.0f;
};

} // namespace utsyn
