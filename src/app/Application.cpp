#include "Application.hpp"

#include "app/Logger.hpp"
#include "plugins/IPlugin.hpp"
#include "plugins/PluginLoader.hpp"
#include "rendering/SceneManager.hpp"
#include "rendering/Viewport.hpp"
#include "rendering/ViewportManager.hpp"
#include "ros/RosCore.hpp"
#include "ros/SubscriptionBroker.hpp"
#include "ros/SubscriptionRegistry.hpp"
#include "widgets/ViewportPanel.hpp"

#include <threepp/canvas/Canvas.hpp>
#include <threepp/canvas/Monitor.hpp>
#include <threepp/renderers/GLRenderer.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <string>

namespace utsyn {

Application::Application() = default;

Application::~Application() {
    shutdown(); // backstop; normally already done at the end of run()
    if (imguiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
}

void Application::run() {
    using namespace threepp;

    canvas_ = std::make_unique<Canvas>(
            Canvas::Parameters()
                    .title("utsyn")
                    .size(1280, 800)
                    .vsync(true)
                    // We drive quitting through the UI, not the Escape key.
                    .exitOnKeyEscape(false));

    // Constructing the GL renderer with the canvas initialises the window's
    // OpenGL context, which ImGui's backend needs to exist first.
    renderer_ = std::make_unique<GLRenderer>(*canvas_);

    initImGui();
    applyTerminalStyle();

    // ROS / plugin backbone. RosCore comes up first (its node backs every
    // subscription); without ROS2 it is inert. The registry + broker provide the
    // SubscriptionBroker plugins see; SceneManager is still a stub but the
    // PluginContext requires it.
    rosCore_   = std::make_unique<RosCore>();
    registry_  = std::make_unique<SubscriptionRegistry>(*rosCore_);
    broker_    = std::make_unique<SubscriptionBroker>(*registry_);
    scene_     = std::make_unique<SceneManager>();

    // One 3D viewport, hosted in a dockable panel.
    viewports_ = std::make_unique<ViewportManager>();
    Viewport& vp = viewports_->create();
    viewportPanel_ = std::make_unique<ViewportPanel>("3D Viewport", vp);

    // One PluginContext shared by all plugins. Its references must outlive every
    // plugin, so it is built after the collaborators and torn down before them.
    ctx_ = std::make_unique<PluginContext>(
            PluginContext{*broker_, *scene_, *viewports_, Logger::instance()});

    plugins_ = std::make_unique<PluginLoader>();
    const std::size_t loaded =
            plugins_->loadDirectory(PluginLoader::executableDir(), *ctx_);
    Logger::instance().info("Application: " + std::to_string(loaded) + " plugin(s) loaded");

    canvas_->animate([this] { frame(); });

    // The render loop has returned (window closed): tear down in order while the
    // GL/ImGui context and the plugin DLLs are still valid.
    shutdown();
}

void Application::initImGui() {
    // DPI scale from the primary monitor — UI sizes derive from this rather
    // than hardcoded pixels.
    dpiScale_ = threepp::monitor::contentScale().first;
    if (dpiScale_ <= 0.0f) {
        dpiScale_ = 1.0f;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // TODO: load assets/fonts/JetBrainsMono-Regular.ttf at 16px * dpiScale once
    // the font asset is added (see CLAUDE.md). Until then use the default font,
    // scaled for DPI.
    io.FontGlobalScale = dpiScale_;

    ImGui_ImplGlfw_InitForOpenGL(
            static_cast<GLFWwindow*>(canvas_->windowPtr()), true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    imguiInitialized_ = true;
}

void Application::applyTerminalStyle() {
    // Terminal/ASCII aesthetic — applied once at startup, never per-widget.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;
    style.FramePadding      = ImVec2(6, 3);
    style.ItemSpacing       = ImVec2(8, 4);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]      = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
    colors[ImGuiCol_FrameBg]       = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
    colors[ImGuiCol_TitleBg]       = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.35f, 0.10f, 1.0f); // green accent
    colors[ImGuiCol_CheckMark]     = ImVec4(0.20f, 0.85f, 0.20f, 1.0f);
    colors[ImGuiCol_SliderGrab]    = ImVec4(0.20f, 0.75f, 0.20f, 1.0f);
    colors[ImGuiCol_Button]        = ImVec4(0.12f, 0.40f, 0.12f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.60f, 0.18f, 1.0f);
    colors[ImGuiCol_ButtonActive]  = ImVec4(0.10f, 0.30f, 0.10f, 1.0f);

    // Scale all metrics for the current DPI (sizes above are at 1x).
    style.ScaleAllSizes(dpiScale_);
}

void Application::frame() {
    renderer_->clear();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const float dt = ImGui::GetIO().DeltaTime;

    // Drain ROS->render payload queues into plugin callbacks BEFORE plugins build
    // their scene/UI, so both see one coherent frame of data. No-op without ROS2.
    broker_->pump();
    plugins_->onSceneUpdate(dt);

    // Advance scene animation before the panel renders it.
    for (std::size_t i = 0; i < viewports_->count(); ++i) {
        viewports_->at(i).update(dt);
    }

    renderUi();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::renderUi() {
    // Full-window dockspace so panels (built-in widgets, plugin panels) can
    // dock against the edges. Plugins will render into this in onImGui().
    ImGui::DockSpaceOverViewport();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Quit")) {
                canvas_->close();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (ImGui::Begin("utsyn")) {
        ImGui::TextUnformatted("utsyn - ROS2 visualizer / HMI");
        ImGui::Separator();
        ImGui::Text("%.1f FPS (%.2f ms/frame)",
                    static_cast<double>(ImGui::GetIO().Framerate),
                    1000.0 / static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::Text("DPI scale: %.2f", static_cast<double>(dpiScale_));
        ImGui::Text("ROS2: %s", rosCore_->rosEnabled() ? "enabled" : "disabled (UTSYN_ROS2 off)");
        ImGui::TextDisabled("Plugins loaded: %llu",
                            static_cast<unsigned long long>(plugins_->count()));
    }
    ImGui::End();

    viewportPanel_->onImGui(*renderer_);

    // Plugin panels render into the dockspace via their own ImGui::Begin/End.
    plugins_->onImGui();
}

void Application::shutdown() noexcept {
    if (shutdownDone_) {
        return;
    }
    shutdownDone_ = true;
    // Teardown order is load-bearing. Stop ROS callbacks first so nothing fires
    // into a plugin mid-teardown; run plugin shutdown(); clear the broker so the
    // closures/subscriptions that capture plugin message types are dropped while
    // the DLLs are still loaded; only then unload the DLLs.
    if (rosCore_) {
        rosCore_->stop();
    }
    if (plugins_) {
        plugins_->shutdownAll();
    }
    if (registry_) {
        registry_->clear();
    }
    if (plugins_) {
        plugins_->unloadAll();
    }
}

} // namespace utsyn
