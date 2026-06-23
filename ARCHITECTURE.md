# ARCHITECTURE.md — Utsyn

Current state of the system. Updated at the end of every session that changes structure, interfaces, or data flow.
This describes what exists, not what is planned.

Last updated: 3D viewport implemented — scene rendered offscreen and embedded in a dockable ImGui panel

---

## What This Is

A ROS2 visualizer and HMI tool. Direct alternative to rviz2.
Terminal aesthetic. Plugin-based. Multi-viewport. Built on threepp + ImGui.

---

## Current Implementation Status

The build is fully wired and compiles end to end on MSVC: `utsyn.exe`,
`utsyn_core.dll`, and `plugin_example.dll` all build from a clean configure.

`Application` is implemented: `main()` constructs it and calls `run()`, which
opens a threepp `Canvas` (OpenGL) + `GLRenderer`, initialises ImGui directly
(GLFW + OpenGL3 backends) with docking enabled and the terminal style/palette,
and runs the render loop via `Canvas::animate`. Each frame clears, builds an
ImGui frame (a full-viewport dockspace, a File>Quit menu, and a status window),
and renders. Nothing loads plugins yet and there is no 3D scene content.

A **3D viewport** is implemented. `Viewport` owns a threepp `Scene` +
`PerspectiveCamera` and renders into an offscreen `RenderTarget` (FBO).
`ViewportManager` owns the viewports; `ViewportPanel` is an ImGui window that
sizes the viewport to the panel's content region, renders it, and displays the
target's color texture via `ImGui::Image` (V-flipped, since GL textures are
bottom-up). `Application` creates one viewport + panel and advances animation
each frame with `ImGui::GetIO().DeltaTime`. The demo scene is a Z-up world
(grid on the XY plane, axes, a rotating green box, a red sphere, a blue
cylinder, ambient + directional light). Camera **navigation** is wired: the
viewport runs its own orbit camera (target + radius + azimuth + elevation), and
`ViewportPanel` feeds it ImGui mouse input — left-drag orbits, right/middle-drag
pans, wheel zooms — captured via an InvisibleButton over the image so a drag
keeps working even if the cursor leaves the panel. This sidesteps threepp's
`OrbitControls`, which binds to the whole Canvas and would conflict with docking.

The remaining modules exist as **stubs** — header + .cpp are present and compile
into `utsyn_core`, but their methods are TODO placeholders. **ExamplePlugin** is
a working reference (full `IPlugin` impl + ImGui panel) but is not yet loaded by
the app.

Status legend below: STUB = compiles, no functionality yet; BUILT = implemented.

---

## Module Map

| Module | Location | Status | Purpose |
|---|---|---|---|
| Application | src/app/Application | BUILT | Window, ImGui dockspace + terminal style, render loop |
| LayoutManager | src/app/LayoutManager | STUB | Panel layout persistence (JSON) |
| Viewport | src/rendering/Viewport | BUILT | threepp scene + camera, rendered offscreen to a RenderTarget |
| ViewportManager | src/rendering/ViewportManager | BUILT | Owns and manages all Viewport instances |
| SceneManager | src/rendering/SceneManager | STUB | Manages 3D scene objects added by plugins |
| SubscriptionBroker | src/ros/SubscriptionBroker | STUB | ROS2 subscription ownership and deduplication |
| TfListener | src/ros/TfListener | STUB | Consumes tf2, exposes transforms to SceneManager |
| IPlugin | src/plugins/IPlugin | BUILT | Plugin contract (interface only — fully defined) |
| PluginLoader | src/plugins/PluginLoader | STUB | Dynamic .so/.dll loading |
| TopicPlot | src/widgets/TopicPlot | STUB | ImPlot-based real-time topic monitor panel |
| TfTree | src/widgets/TfTree | STUB | TF frame tree panel |
| ViewportPanel | src/widgets/ViewportPanel | BUILT | ImGui panel embedding a Viewport's render texture |
| Logger | src/app/Logger | STUB | Singleton logger, thread-safe |
| ExamplePlugin | plugins/example_plugin | BUILT | Reference plugin — full IPlugin impl, ImGui panel |

Update this table when modules are created or their status changes.

---

## Data Flow

```
┌─────────────────────────────────────────────────────┐
│ ROS2 Thread                                          │
│   rclcpp::spin() on dedicated thread                │
│   All topic callbacks land here                     │
│                                                     │
│   SubscriptionBroker                                │
│     - owns all rclcpp subscriptions                 │
│     - plugins request topics through it             │
│     - deduplicates (N plugins, 1 subscription)      │
│     - pushes messages into per-topic queues         │
└──────────────────────┬──────────────────────────────┘
                       │ lock-free queues /
                       │ double-buffered structs
┌──────────────────────▼──────────────────────────────┐
│ Render Thread                                        │
│   Fixed render loop (vsync or uncapped)             │
│                                                     │
│   Application::tick()                               │
│     │                                               │
│     ├── drain queues → plugin callbacks             │
│     │     └── plugins update their local state      │
│     │                                               │
│     ├── SceneManager::update()                      │
│     │     └── TfListener → scene transforms         │
│     │                                               │
│     ├── threepp render (all Viewports)              │
│     │                                               │
│     └── ImGui render                               │
│           ├── plugin->onImGui() for each plugin     │
│           ├── built-in panels (TfTree, TopicPlot)  │
│           └── ViewportPanels (embed threepp views)  │
└─────────────────────────────────────────────────────┘
```

**Rule:** Never call rclcpp from the render thread. Never block the render loop.

---

## Plugin System

### Loading
Plugins are shared libraries (.so on Linux, .dll on Windows).
PluginLoader opens them with dlopen/LoadLibrary and resolves two symbols:
```cpp
extern "C" utsyn::IPlugin* createPlugin();
extern "C" void destroyPlugin(utsyn::IPlugin*);
```

### Lifecycle
```
load → createPlugin() → initialize(ctx) → [onImGui / onSceneUpdate per frame] → shutdown() → destroyPlugin()
```

### PluginContext fields
```cpp
struct PluginContext {
    SubscriptionBroker& broker;    // Topic subscriptions
    SceneManager&       scene;     // 3D scene objects
    ViewportManager&    viewports; // Viewport access
    Logger&             logger;    // Logging
};
```
Do not add fields to PluginContext without updating this document and versioning the interface.

### Panel registration
Plugins render their own ImGui panels inside `onImGui()`.
The docking layout is managed by ImGui docking — plugins use standard `ImGui::Begin()`/`End()`.
Panel names must be unique — plugins should prefix with their name: `"MyPlugin: Joint States"`.

---

## Viewport System

Multiple 3D viewports are supported (Unity-style).
ViewportManager owns all Viewport instances. The app currently creates one.
Each Viewport wraps a threepp scene and camera and renders into its own
offscreen `RenderTarget` (FBO). ViewportPanel is the ImGui widget that embeds a
Viewport as a render texture: it resizes the target to the panel's content
region, calls `Viewport::render` (which sets the render target, renders, and
restores the default framebuffer), and draws the color texture with
`ImGui::Image` — UVs V-flipped because GL textures are bottom-up.

The scene render happens during ImGui frame construction (between `NewFrame`
and `Render`); the FBO is fully drawn before the main ImGui pass samples it.

**Navigation:** each Viewport owns an orbit camera (target, radius, azimuth,
elevation). `ViewportPanel` overlays an InvisibleButton on the image and feeds
ImGui mouse input to `Viewport::orbit/pan/dolly` — left-drag orbits,
right/middle-drag pans, wheel zooms. Because the button captures the drag, it
keeps tracking even if the cursor leaves the panel. We deliberately do **not**
use threepp's `OrbitControls` (it binds to the whole Canvas and fights docking).

**Open decision:** whether plugins can create new viewports, or only the app
can. Do not implement plugin viewport creation until this is decided.

---

## Threading Model

| Thread | Responsibility |
|---|---|
| Render thread (main) | ImGui, threepp, plugin onImGui/onSceneUpdate, queue draining |
| ROS2 thread | rclcpp::spin(), all topic callbacks, SubscriptionBroker queues |

No other threads at this time.
Do not add threads without updating this document.

---

## Persistence

Layout saved to `~/.config/utsyn/layout.json` (Linux) / `%APPDATA%\utsyn\layout.json` (Windows).
Format: JSON via nlohmann/json.
Contains: panel list, dock positions, open state, viewport configurations.
ImGui docking `.ini` state may be stored separately or unified — open decision.

---

## Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Dependency management | FetchContent | Simpler than vcpkg for a new project |
| UI framework | ImGui (docking branch) | Best docking system, ImPlot integration, fits terminal aesthetic |
| Plotting | ImPlot | Real-time data, direct ImGui integration, no texture roundtrip |
| Serialization | nlohmann/json | Header-only, clean API, no XML |
| Animation | Not implemented yet | Will use spring-based AnimatedValue when needed |
| ROS2 build | Plain CMake, no colcon | Source ROS2 setup script before cmake configure |
| Coordinate system | Right-handed (threepp native) | Matches ROS2/URDF convention |
| Exception policy | No exceptions for control flow | Assert invariants, log and return for recoverable errors |
| threepp examples/tests | Disabled (`THREEPP_BUILD_EXAMPLES/TESTS=OFF`) | We consume only the library; avoids fetching the `threepp_data` asset repo and keeps offline builds working |
| Strict warnings (`/W4 /WX`) | Scoped to our targets via the `utsyn_warnings` INTERFACE lib | Directory-wide flags would force `/WX` onto FetchContent deps (e.g. threepp) and break them |
| ImGui GLFW backend | `imgui_lib` links threepp's vendored `glfw` target | Reuses one GLFW; supplies `GLFW/glfw3.h` to the backend |
| utsyn_core linkage | SHARED with `WINDOWS_EXPORT_ALL_SYMBOLS` | App + plugins share one core instance (one Logger singleton); auto-export generates the import lib so they can link on Windows |
| Dependency headers | Included as SYSTEM (threepp via FetchContent `SYSTEM`; imgui/implot/nanosvg via `target_include_directories SYSTEM`) | Demotes third-party header warnings so our `/WX` only fails on our own code |
| ImGui integration | Init directly (GLFW+OpenGL3 backends), not threepp's `ImguiContext` helper | The helper resets `ImGuiStyle` on first frame, which would wipe our terminal palette |

---

## What Does Not Exist Yet

- OpenBridge widget layer (deferred)
- Vulkan path tracer integration (deferred — OpenGL default first)
- Plugin config persistence
- Multi-robot tf namespacing
- Any actual plugins beyond the example reference (which only draws a panel; broker/scene/viewport wiring is TODO inside it, pending those APIs)
