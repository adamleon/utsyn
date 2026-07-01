#pragma once

#include <memory>

#include "LayoutManager.hpp" // direct member (owns the ImGui ini path string)

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
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
struct VkScenePanel; // Vulkan scene-color -> ImGui descriptor cache (defined in Application.cpp)
#endif
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
    void loadFonts(); // add JetBrains Mono to the ImGui atlas (shared by GL + Vulkan)
    void applyTerminalStyle();
    void frame();      // one render-loop iteration (dispatches GL vs Vulkan)
    void renderUi();   // build the ImGui frame (dockspace, panels, plugin UI)
    void shutdown() noexcept; // orderly teardown: stop ROS -> clear broker -> unload plugins
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    void frameVulkan(); // fullscreen scene render + ImGui overlay
#endif

    std::unique_ptr<threepp::Canvas>      canvas_;
    std::unique_ptr<threepp::Renderer>    renderer_;     // GLRenderer or VulkanRenderer
    threepp::GLRenderer*                  glRenderer_ = nullptr; // non-owning view, GL only
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    threepp::VulkanRenderer*              vkRenderer_ = nullptr; // non-owning view, Vulkan only
    std::unique_ptr<ImguiContext>         vkUi_;                 // Vulkan ImGui (threepp helper)
    std::unique_ptr<threepp::MouseListener> vkWheel_;           // scroll -> camera zoom
    bool                                  vkStyleApplied_ = false;
    std::unique_ptr<VkScenePanel>         vkScenePanel_; // scene-color image -> ImGui::Image
    float                                 vkPanelW_ = 0.0f, vkPanelH_ = 0.0f; // 3D Viewport panel size -> camera aspect
    bool                                  vkPanelHovered_ = false;            // 3D Viewport panel hovered (wheel zoom)
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

    // Layout persistence (points ImGui's ini at a stable per-user path). Declared last
    // so it outlives the ImGui context, which is torn down in ~Application's body.
    LayoutManager layout_;
};

} // namespace utsyn
