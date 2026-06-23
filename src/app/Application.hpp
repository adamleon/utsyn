#pragma once

#include <memory>

namespace threepp {
class Canvas;
class GLRenderer;
} // namespace threepp

namespace utsyn {

class ViewportManager;
class ViewportPanel;

// Top-level app: owns the window, the render loop, and DPI/ImGui setup.
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
    void renderUi();   // build the ImGui frame (dockspace, panels)

    std::unique_ptr<threepp::Canvas>     canvas_;
    std::unique_ptr<threepp::GLRenderer> renderer_;
    std::unique_ptr<ViewportManager>     viewports_;
    std::unique_ptr<ViewportPanel>       viewportPanel_;
    bool  imguiInitialized_ = false;
    float dpiScale_ = 1.0f;
};

} // namespace utsyn
