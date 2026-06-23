#pragma once

#include <memory>

namespace threepp {
class Canvas;
class GLRenderer;
} // namespace threepp

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
    void run();

private:
    void initImGui();
    void applyTerminalStyle();
    void frame();      // one render-loop iteration
    void renderUi();   // build the ImGui frame (dockspace, panels, plugin UI)
    void shutdown() noexcept; // orderly teardown: stop ROS -> clear broker -> unload plugins

    std::unique_ptr<threepp::Canvas>      canvas_;
    std::unique_ptr<threepp::GLRenderer>  renderer_;

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
