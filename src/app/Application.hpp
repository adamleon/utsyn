#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "LayoutManager.hpp"  // direct member (owns the ImGui ini path string)
#include "PanelRegistry.hpp"  // direct member (owns panel open-state for the View menu)

namespace threepp {
class Canvas;
class Renderer;
class GLRenderer;
class PerspectiveCamera;
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
class VulkanRenderer;
class RenderTarget;
struct MouseListener;
#endif
} // namespace threepp

#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
class ImguiContext; // threepp's ImGui helper (global namespace, in extras/imgui)
#endif

namespace utsyn {

class ViewportManager;
class ViewportPanel;
class TfListener;
class TfTree;
class TopicPlot;
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
    // Multi-viewport (B-lite offscreen render targets): N cameras of the shared
    // scene (viewport 0's) render into N RenderTargets via VulkanRenderer::
    // setRenderTarget, each shown in its own docked panel. vkScenePanel_ caches
    // one ImGui descriptor per viewport (keyed by index); the RT views are stable
    // for a given panel size.
    //
    // The count is set once before run() creates the viewports. Default 2 to show
    // off multi-viewport; 1 is a single panel with no extra cost. Each viewport adds
    // a full path-trace + a fence-serialized offscreen submit per frame (see the
    // acquire-once model in VulkanRenderer), so cost scales ~linearly with N — fine
    // for a handful of panels, and a natural knob to expose in the UI later.
    std::size_t                                         numVkViewports_ = 2;
    std::unique_ptr<VkScenePanel>                       vkScenePanel_;       // per-viewport RT image -> ImGui::Image
    std::vector<std::unique_ptr<threepp::RenderTarget>> vkTargets_;          // offscreen target per viewport
    std::vector<Panel*>                                 vkViewportPanels_;   // View-menu entry per viewport ([0]=viewportEntry_)
    std::vector<float>                                  vkPanelW_, vkPanelH_; // per-viewport panel size -> camera aspect
    int                                                 vkHoveredViewport_ = -1; // panel under cursor (wheel zoom)
#endif
    bool                                  useVulkan_ = false;

    // UI panel registry (core + plugin panels) driving the View menu. Declared before
    // the backbone so it outlives ctx_ (which references it) and the plugins (which hold
    // Panel* into it).
    PanelRegistry panels_;
    Panel*        statusPanel_   = nullptr; // core "utsyn" status panel
    Panel*        viewportEntry_ = nullptr; // core "3D Viewport" panel
    Panel*        tfPanel_       = nullptr; // core "TF Tree" panel
    Panel*        plotPanel_     = nullptr; // core "Topic Plot" panel
    bool          plotLive_      = false;   // true: /joint_states feeds the plot (else demo)

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
    std::unique_ptr<TfListener>           tfListener_; // TF frame graph (demo offline; /tf later)
    std::unique_ptr<TfTree>               tfTree_;     // TF Tree panel widget
    std::unique_ptr<TopicPlot>            topicPlot_;  // real-time /joint_states plot widget

    bool  imguiInitialized_ = false;
    bool  implotInitialized_ = false;
    bool  shutdownDone_ = false;
    float dpiScale_ = 1.0f;

    // Layout persistence (points ImGui's ini at a stable per-user path). Declared last
    // so it outlives the ImGui context, which is torn down in ~Application's body.
    LayoutManager layout_;
};

} // namespace utsyn
