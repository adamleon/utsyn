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
#include <threepp/renderers/Renderer.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
#  include <imgui_impl_vulkan.h> // ImGui_ImplVulkan_AddTexture/RemoveTexture (scene-color -> ImGui::Image)
#  include <threepp/cameras/PerspectiveCamera.hpp>
#  include <threepp/extras/imgui/ImguiContext.hpp>
#  include <threepp/input/MouseListener.hpp>
#  include <threepp/renderers/VulkanRenderer.hpp>
#  include <threepp/scenes/Scene.hpp> // complete type: Scene -> Object3D upcast for render()
#endif

#include <string>
#include <vector>

namespace utsyn {

#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
namespace {
// Bridges threepp's mouse-wheel events to a callback. Under the Vulkan canvas
// ImGui's io.MouseWheel doesn't receive the scroll, so the camera zoom is driven
// off threepp's own input system instead (the same path OrbitControls uses).
struct WheelListener final : threepp::MouseListener {
    std::function<void(float)> cb;
    void onMouseWheel(const threepp::Vector2& delta) override {
        if (cb) {
            cb(delta.y);
        }
    }
};
} // namespace

// Caches an ImGui_ImplVulkan descriptor per scene-color slot so the rendered scene
// can be drawn as an ImGui::Image. A slot's descriptor is (re)registered whenever its
// VkImageView changes — e.g. a window resize recreates the renderer's images.
struct VkScenePanel {
    VkDevice                     device  = VK_NULL_HANDLE;
    VkSampler                    sampler = VK_NULL_HANDLE;
    std::vector<VkImageView>     regView; // currently-registered view per slot
    std::vector<VkDescriptorSet> sets;    // ImGui descriptor per slot

    VkScenePanel(VkDevice dev, uint32_t slots)
        : device(dev), regView(slots, VK_NULL_HANDLE), sets(slots, VK_NULL_HANDLE) {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod       = 1.0f;
        vkCreateSampler(device, &si, nullptr, &sampler);
    }
    ~VkScenePanel() {
        if (!device) {
            return;
        }
        vkDeviceWaitIdle(device); // descriptors/sampler must be idle before teardown
        for (VkDescriptorSet s : sets) {
            if (s) {
                ImGui_ImplVulkan_RemoveTexture(s);
            }
        }
        if (sampler) {
            vkDestroySampler(device, sampler, nullptr);
        }
    }
    VkScenePanel(const VkScenePanel&)            = delete;
    VkScenePanel& operator=(const VkScenePanel&) = delete;

    // ImGui texture id for `slot`, (re)registering the descriptor if the view changed.
    // Returns a null id if the view isn't available.
    ImTextureID texture(uint32_t slot, VkImageView view) {
        if (view == VK_NULL_HANDLE || slot >= sets.size()) {
            return ImTextureID{};
        }
        if (regView[slot] != view) {
            if (sets[slot]) {
                ImGui_ImplVulkan_RemoveTexture(sets[slot]);
            }
            sets[slot]    = ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_GENERAL);
            regView[slot] = view;
        }
        return reinterpret_cast<ImTextureID>(sets[slot]);
    }
};
#endif

Application::Application() = default;

Application::~Application() {
    shutdown(); // backstop; normally already done at the end of run()
    layout_.saveNow(); // flush the layout while the ImGui context is still alive
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    // Scene-panel descriptors/sampler first — they need the ImGui-Vulkan backend
    // (vkUi_) and the device (renderer_) still alive.
    vkScenePanel_.reset();
    // Tear down the Vulkan ImGui context BEFORE the renderer: its destructor does
    // vkDeviceWaitIdle + ImGui_ImplVulkan_Shutdown using the renderer's device (and
    // also ImGui::DestroyContext). renderer_ is still alive here — it's a member,
    // destroyed after this body runs.
    vkUi_.reset();
#endif
    if (imguiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
}

void Application::run(bool useVulkan) {
    using namespace threepp;
    useVulkan_ = useVulkan;

    canvas_ = std::make_unique<Canvas>(
            Canvas::Parameters()
                    .title("utsyn")
                    .size(1280, 800)
                    .vsync(true)
                    // We drive quitting through the UI, not the Escape key.
                    .exitOnKeyEscape(false));

    // DPI scale from the primary monitor — both backends derive UI sizes from it.
    dpiScale_ = monitor::contentScale().first;
    if (dpiScale_ <= 0.0f) {
        dpiScale_ = 1.0f;
    }

#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    if (useVulkan_) {
        // Constructing the VulkanRenderer switches the canvas to Vulkan and brings
        // up the instance/device/swapchain. threepp's ImguiContext wires ImGui to
        // the renderer's native handles + a per-frame overlay callback that draws
        // the panels inside the present pass. The scene renders fullscreen — there
        // is no offscreen render target (setRenderTarget is a stub), so the docked
        // viewport panel is GL-only.
        auto vk     = std::make_unique<VulkanRenderer>(*canvas_);
        vkRenderer_ = vk.get();
        renderer_   = std::move(vk);
        vkUi_       = std::make_unique<ImguiFunctionalContext>(
                *canvas_, *vkRenderer_, [this] { renderUi(); });
        // threepp's ImguiContext creates the ImGui context but does NOT enable
        // docking (utsyn's GL initImGui() does). Without this, DockSpaceOverViewport
        // is a no-op: panels can't dock and the central node has no rect (which the
        // camera gate needs). The GL path is unaffected.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        layout_.applyImGuiIniPath(); // stable ini path (before the first frame loads it)
        loadFonts(); // JetBrains Mono into the Vulkan ImGui atlas (ImguiContext sets FontScaleDpi)
        // Scene-color -> ImGui::Image descriptor cache (uses the renderer's native
        // device + the ImGui-Vulkan backend that vkUi_ just initialised).
        vkScenePanel_ = std::make_unique<VkScenePanel>(
                static_cast<VkDevice>(vkRenderer_->nativeDevice()),
                vkRenderer_->framesInFlight());
        Logger::instance().info("Application: Vulkan backend (deferred-hybrid, fullscreen overlay)");
    }
#else
    if (useVulkan_) {
        Logger::instance().error(
                "Application: --vulkan requested but built without UTSYN_WITH_VULKAN; using OpenGL");
        useVulkan_ = false;
    }
#endif

    if (!useVulkan_) {
        // OpenGL (default). Constructing the GL renderer initialises the window's
        // OpenGL context, which ImGui's backend needs to exist first.
        auto gl     = std::make_unique<GLRenderer>(*canvas_);
        glRenderer_ = gl.get();
        renderer_   = std::move(gl);
        initImGui();
        applyTerminalStyle();
    }

    // ROS / plugin backbone. RosCore comes up first (its node backs every
    // subscription); without ROS2 it is inert. The registry + broker provide the
    // SubscriptionBroker plugins see; SceneManager is still a stub but the
    // PluginContext requires it.
    rosCore_   = std::make_unique<RosCore>();
    registry_  = std::make_unique<SubscriptionRegistry>(*rosCore_);
    broker_    = std::make_unique<SubscriptionBroker>(*registry_);
    scene_     = std::make_unique<SceneManager>();

    // One 3D viewport, hosted in a dockable panel. Bind its scene into the
    // SceneManager so plugins can add objects to it (ctx.scene).
    viewports_ = std::make_unique<ViewportManager>();
    Viewport& vp = viewports_->create();
    scene_->bindScene(kPrimaryViewport, vp.scene());
    viewportPanel_ = std::make_unique<ViewportPanel>("3D Viewport", vp);

#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    if (useVulkan_) {
        // Drive camera zoom off threepp's mouse-wheel events (ImGui's io.MouseWheel
        // doesn't receive the scroll under the Vulkan canvas). Only zoom when the
        // cursor is over the 3D Viewport panel (set in renderUi).
        auto wheel = std::make_unique<WheelListener>();
        wheel->cb  = [this](float dy) {
            if (vkPanelHovered_) {
                viewports_->at(0).dolly(dy);
            }
        };
        canvas_->addMouseListener(*wheel);
        vkWheel_ = std::move(wheel);
    }
#endif

    // Register the core panels first so they head the View menu; plugins append theirs
    // during initialize() (below).
    statusPanel_   = panels_.add("utsyn", "Core");
    viewportEntry_ = panels_.add("3D Viewport", "Core");

    // One PluginContext shared by all plugins. Its references must outlive every
    // plugin, so it is built after the collaborators and torn down before them.
    ctx_ = std::make_unique<PluginContext>(
            PluginContext{*broker_, *scene_, *viewports_, Logger::instance(), panels_});

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

    layout_.applyImGuiIniPath(); // stable ini path (before the first frame loads it)

    loadFonts(); // JetBrains Mono — the terminal aesthetic
    // Crisp DPI scaling via ImGui's dynamic-font rasterization (re-rasterizes the font
    // at the scaled size), not FontGlobalScale (which stretches the bitmap and blurs).
    // The Vulkan path's ImguiContext sets this same field itself.
    ImGui::GetStyle().FontScaleDpi = dpiScale_;

    ImGui_ImplGlfw_InitForOpenGL(
            static_cast<GLFWwindow*>(canvas_->windowPtr()), true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    imguiInitialized_ = true;
}

void Application::loadFonts() {
#ifdef UTSYN_ASSET_DIR
    ImGuiIO&    io   = ImGui::GetIO();
    const char* path = UTSYN_ASSET_DIR "/fonts/JetBrainsMono-Regular.ttf";
    // Base 16px; ImGui's FontScaleDpi re-rasterizes crisply per DPI. Called from both
    // initImGui (GL) and run() (Vulkan) after the ImGui context exists but before the
    // first frame builds the atlas.
    ImFont* font = io.Fonts->AddFontFromFileTTF(path, 16.0f);
    if (font != nullptr) {
        io.FontDefault = font;
        Logger::instance().info("Application: loaded JetBrains Mono");
    } else {
        Logger::instance().warn(std::string("Application: failed to load font: ") + path);
    }
#else
    Logger::instance().warn("Application: UTSYN_ASSET_DIR undefined; using default ImGui font");
#endif
}

void Application::applyTerminalStyle() {
    // Terminal/ASCII aesthetic — applied once at startup, never per-widget.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding       = 0.0f;
    style.GrabRounding      = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ChildRounding     = 0.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f; // boxy frames around inputs/buttons (terminal)
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

    // Boxy green frames + terminal row-hover (Selectable/header rows use these).
    colors[ImGuiCol_Border]         = ImVec4(0.15f, 0.40f, 0.15f, 0.55f);
    colors[ImGuiCol_Separator]      = ImVec4(0.15f, 0.40f, 0.15f, 0.70f);
    colors[ImGuiCol_Header]         = ImVec4(0.12f, 0.40f, 0.12f, 1.0f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.18f, 0.55f, 0.18f, 1.0f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.10f, 0.30f, 0.10f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.28f, 0.16f, 1.0f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.18f, 0.36f, 0.18f, 1.0f);

    // Scale all metrics for the current DPI (sizes above are at 1x).
    style.ScaleAllSizes(dpiScale_);
}

void Application::frame() {
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    if (useVulkan_) {
        frameVulkan();
        return;
    }
#endif

    // ---- OpenGL path (unchanged) ----
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

#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
void Application::frameVulkan() {
    Viewport& vp = viewports_->at(0);

    // dt is the previous frame's value (ImGui updates it during ImguiContext::
    // render()'s NewFrame, which runs below) — fine for plugin scene updates.
    const float dt = ImGui::GetIO().DeltaTime;

    broker_->pump();
    plugins_->onSceneUpdate(dt);
    for (std::size_t i = 0; i < viewports_->count(); ++i) {
        viewports_->at(i).update(dt);
    }

    // Camera navigation is driven by the "3D Viewport" panel (renderUi, below) via an
    // InvisibleButton over the scene image — the same gestures as the GL ViewportPanel.

    // Match the camera aspect to that panel so the scene fills it without distortion.
    // The panel size is measured in renderUi() (which runs below, inside vkUi_->
    // render()), so this uses last frame's value — one frame of lag. Fall back to the
    // window aspect until the panel has been drawn once.
    float aspect = 0.0f;
    if (vkPanelW_ > 0.0f && vkPanelH_ > 0.0f) {
        aspect = vkPanelW_ / vkPanelH_;
    } else if (const auto sz = vkRenderer_->size(); sz.height() > 0) {
        aspect = static_cast<float>(sz.width()) / static_cast<float>(sz.height());
    }
    if (aspect > 0.0f) {
        vp.camera().aspect = aspect;
        vp.camera().updateProjectionMatrix();
    }

    // Render the primary viewport's scene fullscreen (deferred-hybrid).
    // vkUi_->render() builds the panels (renderUi) and the renderer's overlay
    // callback draws them on top, inside the present pass.
    vkRenderer_->render(vp.scene(), vp.camera());
    vkUi_->render();

    // ImguiContext::render() resets ImGuiStyle on its first call (DPI setup), which
    // wipes the terminal palette — re-apply it once after that first frame.
    if (!vkStyleApplied_) {
        applyTerminalStyle();
        vkStyleApplied_ = true;
    }
}
#endif

void Application::renderUi() {
    // Full-window dockspace so panels dock against the edges. The dockspace is opaque:
    // under Vulkan it hides the fullscreen scene present, which is shown instead in the
    // dockable "3D Viewport" panel (below) via the scene-color accessor; under GL the
    // docked viewport renders directly.
    ImGui::DockSpaceOverViewport();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Quit")) {
                canvas_->close();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            // Every registered panel (core + plugin-contributed), grouped, each with a
            // checkable toggle bound to its open state.
            std::string group;
            for (const auto& p : panels_.panels()) {
                if (p->group != group) {
                    group = p->group;
                    ImGui::SeparatorText(group.empty() ? "Panels" : group.c_str());
                }
                ImGui::MenuItem(p->name.c_str(), nullptr, &p->open);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (statusPanel_->open) {
        if (ImGui::Begin("utsyn", &statusPanel_->open, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextUnformatted("utsyn - ROS2 visualizer / HMI");
            ImGui::Separator();
            ImGui::Text("%.1f FPS (%.2f ms/frame)",
                        static_cast<double>(ImGui::GetIO().Framerate),
                        1000.0 / static_cast<double>(ImGui::GetIO().Framerate));
            ImGui::Text("DPI scale: %.2f", static_cast<double>(dpiScale_));
            ImGui::Text("Renderer: %s", useVulkan_ ? "Vulkan (deferred-hybrid)" : "OpenGL");
            ImGui::Text("ROS2: %s",
                        rosCore_->rosEnabled() ? "enabled" : "disabled (UTSYN_ROS2 off)");
            ImGui::TextDisabled("Plugins loaded: %llu",
                                static_cast<unsigned long long>(plugins_->count()));
        }
        ImGui::End();
    }

    // Docked offscreen viewport — GL only (under Vulkan the scene renders fullscreen).
    if (!useVulkan_ && viewportEntry_->open) {
        viewportPanel_->onImGui(*glRenderer_, &viewportEntry_->open);
    }
#if defined(UTSYN_WITH_VULKAN) && UTSYN_WITH_VULKAN
    // Under Vulkan the renderer can't render to an offscreen target, but it exposes
    // the per-frame scene-color image — draw it as a dockable "3D Viewport" panel via
    // the threepp nativeSceneColorView() accessor.
    if (useVulkan_ && vkScenePanel_ && vkRenderer_ && viewportEntry_->open) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool open = ImGui::Begin("3D Viewport", &viewportEntry_->open);
        ImGui::PopStyleVar();
        bool hovered = false;
        if (open) {
            const uint32_t    slot  = vkRenderer_->currentSceneColorSlot();
            const auto        view  = static_cast<VkImageView>(vkRenderer_->nativeSceneColorView(slot));
            const ImTextureID tex   = vkScenePanel_->texture(slot, view);
            const ImVec2      avail = ImGui::GetContentRegionAvail();
            vkPanelW_ = avail.x; // drives the camera aspect in frameVulkan (next frame)
            vkPanelH_ = avail.y;
            if (tex && avail.x > 1.0f && avail.y > 1.0f) {
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::Image(tex, avail);
                // Camera input: an InvisibleButton over the image (mirrors the GL
                // ViewportPanel). orbit/pan run here in renderUi (one frame before the
                // next render); zoom is the threepp wheel listener gated on hover.
                ImGui::SetCursorScreenPos(origin);
                ImGui::InvisibleButton("##vk_viewport_nav", avail,
                                       ImGuiButtonFlags_MouseButtonLeft |
                                               ImGuiButtonFlags_MouseButtonRight |
                                               ImGuiButtonFlags_MouseButtonMiddle);
                hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemActive()) {
                    Viewport&    vp = viewports_->at(0);
                    const ImVec2 d  = ImGui::GetIO().MouseDelta;
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        vp.orbit(-d.x * 0.01f, -d.y * 0.01f); // left drag: orbit
                    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                               ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                        vp.pan(d.x, d.y); // right/middle drag: pan
                    }
                }
            } else if (!tex) {
                ImGui::TextDisabled("scene-color image not available");
            }
        }
        vkPanelHovered_ = hovered;
        ImGui::End();
    }
#endif

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
    if (scene_) {
        scene_->clear(); // detach plugin objects while the viewport scene is alive
    }
    if (plugins_) {
        plugins_->unloadAll();
    }
}

} // namespace utsyn
