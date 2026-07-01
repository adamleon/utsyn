# CLAUDE.md — Utsyn

A ROS2 visualizer and HMI tool. rviz2 alternative built on threepp + ImGui.
Terminal aesthetic, plugin-based, multi-viewport.

---

## Maintaining Documentation

ARCHITECTURE.md is the living record of what the system currently is.
CLAUDE.md is the rules and patterns document.

**At the end of any session that adds, removes, or changes:**
- A module or file
- An interface or contract (especially IPlugin or PluginContext)
- A design decision
- The data flow between components
- Dependencies

Update ARCHITECTURE.md to reflect current state before closing.
If unsure whether a change warrants an update — update it anyway.

**Do not invent architecture that isn't in ARCHITECTURE.md.**
If you need something that isn't defined, stop and flag it rather than improvising.

ARCHITECTURE.md describes what exists now, not what is planned.
Use TODO comments in code for planned work, not the architecture doc.

---

## Build, Run & Test

- CMake 3.25+
- C++20 required — no C++17 fallbacks
- Primary compiler: MSVC (latest)
- Secondary compiler: clang-cl
- MinGW is explicitly rejected — do not add MinGW workarounds
- Windows primary target; Linux secondary
- Both Debug and Release use contract checking (see below)

The canonical Windows build uses the **Visual Studio multi-config generator**.
CMake locates `cl.exe` via vswhere, so the Strawberry MinGW/Ninja that may be on
PATH is ignored. The configure/build commands are also encoded in
`.vscode/tasks.json`.

```bash
# Configure (first run downloads all deps via FetchContent — slow)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build — config is chosen HERE, not at configure time. This is a multi-config
# generator, so -DCMAKE_BUILD_TYPE has no effect; use --config instead.
cmake --build build --config Debug --parallel                      # everything (incl. plugin_example)
cmake --build build --config Release --target utsyn --parallel     # app + utsyn_core only
```

Run the app: `build/Debug/utsyn.exe` (or `build/Release/utsyn.exe`).

**Tests:** there is no test suite yet — threepp's own tests are disabled
(`THREEPP_BUILD_TESTS=OFF`) and utsyn defines no test target. Do not assume a
`ctest`/`gtest` setup exists; if you add tests, wire the target and document it
here and in ARCHITECTURE.md.

**ROS2 is optional at configure time.** CMake does `find_package(ament_cmake
QUIET)`: if a ROS2 environment is sourced, it builds with ROS2 support and
defines `UTSYN_ROS2=1`; otherwise it warns and builds a ROS2-less binary. Gate
all rclcpp/tf2 code behind `#if UTSYN_ROS2`. To build with ROS2, source the
Jazzy setup script before `cmake -B build`.

**Lint:** clang-tidy is off by default — enable with
`-DENABLE_CLANG_TIDY=ON` (applies to `utsyn` and `utsyn_core`).
`compile_commands.json` is exported for editor/tidy use.

---

## Dependencies

All pulled via CMake FetchContent, except ROS2 (system `find_package`/ament).
There is no vcpkg in this project. See `CMakeLists.txt` for exact GIT_TAGs.

| Library | Version (tag) | Source | Purpose |
|---|---|---|---|
| threepp | master | FetchContent | 3D rendering, scene graph, URDF/glTF/STL/FBX/SVG loading (examples/tests OFF) |
| imgui | docking | FetchContent | UI layer, panels, windows, widgets (built as `imgui_lib`, no upstream CMake) |
| implot | master | FetchContent | Real-time plots, graphs, topic monitors (built as `implot_lib`) |
| nanosvg | master | FetchContent | SVG rasterization → ImGui textures |
| nlohmann/json | v3.11.3 | FetchContent | Layout persistence (JSON) |
| ROS2 | Jazzy | find_package (optional) | Topic subscriptions, tf2, message types |

Use threepp's bundled math types — **glm is not a dependency** (not fetched, not
linked). Do not introduce new dependencies without updating this table.

---

## Coordinate System

threepp uses a **right-handed coordinate system** (matches ROS2/URDF convention):
- X forward
- Y left  
- Z up

Never flip to left-handed. All pose/transform code must respect this.

---

## Project Structure

**ARCHITECTURE.md holds the authoritative module map and BUILT/STUB status** —
consult it rather than trusting this tree, which only sketches the layout. The
binaries are: `utsyn.exe` (app) → `utsyn_core.dll` (SHARED core, also linked by
plugins) → `plugin_example.dll` (reference plugin).

```
utsyn/
├── CMakeLists.txt
├── ARCHITECTURE.md                     # living record of current state
├── src/
│   ├── main.cpp                        # constructs Application, calls run()
│   ├── app/                            # Application, LayoutManager, Logger (singleton)
│   ├── rendering/                      # Viewport, ViewportManager, SceneManager
│   ├── ros/                            # SubscriptionBroker, TfListener
│   ├── plugins/                        # IPlugin (contract), PluginLoader
│   └── widgets/                        # TopicPlot, TfTree, ViewportPanel
├── plugins/example_plugin/             # ExamplePlugin.cpp — reference plugin
└── assets/fonts/                       # JetBrains Mono (.ttf not yet added — build warns & skips)
```

Most non-app modules are currently STUBs (header + .cpp compile, methods are
TODO). BUILT so far: Application, Viewport, ViewportManager, ViewportPanel,
IPlugin, ExamplePlugin. Check ARCHITECTURE.md before assuming a module works.

---

## Plugin Interface

Every plugin must implement `IPlugin`. This is the contract — do not change it without versioning.

```cpp
// src/plugins/IPlugin.hpp
#pragma once
#include <functional>
#include <string>

namespace utsyn {

class SceneManager;
class SubscriptionBroker;
class ViewportManager;
class Logger;
class PanelRegistry;

struct PluginContext {
    SubscriptionBroker& broker;    // Request topic subscriptions through this — never use rclcpp directly
    SceneManager&       scene;     // Add/remove 3D scene objects
    ViewportManager&    viewports; // Create or reference viewports
    Logger&             logger;    // Log messages
    PanelRegistry&      panels;    // Register UI panels so the View menu can list/toggle them (ABI v2)
    // Config store for plugin-specific persistent settings — TBD
};

class IPlugin {
public:
    virtual ~IPlugin() = default;

    // Called once after loading
    virtual void initialize(PluginContext& ctx) = 0;

    // Called every frame — render ImGui panels here
    // Must be fast and non-blocking
    virtual void onImGui() = 0;

    // Called every frame — update threepp scene objects here
    virtual void onSceneUpdate(float deltaTime) = 0;

    // Called before unloading
    virtual void shutdown() = 0;

    // Human-readable name shown in UI
    virtual std::string name() const = 0;

    // Semantic version string e.g. "1.0.0"
    virtual std::string version() const = 0;
};

} // namespace utsyn

// Each plugin .so/.dll must export:
// extern "C" utsyn::IPlugin* createPlugin();
// extern "C" void destroyPlugin(utsyn::IPlugin*);
```

Plugins subscribe to topics via `SubscriptionBroker`, not rclcpp directly:
```cpp
// In plugin initialize():
ctx.broker.subscribe<sensor_msgs::msg::JointState>(
    "/joint_states",
    [this](const auto& msg) { onJointState(msg); }
);
```
Core handles deduplication — 10 plugins subscribing to `/joint_states` = 1 ROS2 subscription.

---

## AnimatedValue Pattern (NOT YET IMPLEMENTED)

When animation is needed, it will live here as a spring-based `AnimatedValue<T>`.
Do not implement animations inline in widget code.
Do not implement this class until explicitly instructed.
When you encounter a place that needs animation: stub a TODO comment and move on.

Intended pattern when implemented:
- Spring integrator (not lerp) for natural bounce/overshoot
- Use for: button scale on click, slider position, panel open/close, value fade-in
- One `AnimatedValue` instance per animated quantity, updated once per frame

---

## UI Style

Terminal/ASCII aesthetic. Apply at startup, do not override per-widget.

```cpp
// Apply in Application::initialize()
ImGuiStyle& style = ImGui::GetStyle();
style.WindowRounding    = 0.0f;
style.FrameRounding     = 0.0f;
style.ScrollbarRounding = 0.0f;
style.TabRounding       = 0.0f;
style.FramePadding      = ImVec2(6, 3);
style.ItemSpacing       = ImVec2(8, 4);

// Dark terminal palette
ImVec4* colors = style.Colors;
colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
colors[ImGuiCol_TitleBg]        = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
colors[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.35f, 0.10f, 1.0f); // green accent
colors[ImGuiCol_CheckMark]      = ImVec4(0.20f, 0.85f, 0.20f, 1.0f);
colors[ImGuiCol_SliderGrab]     = ImVec4(0.20f, 0.75f, 0.20f, 1.0f);
colors[ImGuiCol_Button]         = ImVec4(0.12f, 0.40f, 0.12f, 1.0f);
colors[ImGuiCol_ButtonHovered]  = ImVec4(0.18f, 0.60f, 0.18f, 1.0f);
colors[ImGuiCol_ButtonActive]   = ImVec4(0.10f, 0.30f, 0.10f, 1.0f);
// Accent color: terminal green #33CC33 — use consistently
```

Font: JetBrains Mono, loaded at 16px (1x) and 32px (2x for HiDPI).
Select size at init based on DPI scale factor.

---

## Layout Persistence

Panel layouts save/load as JSON to `~/.config/utsyn/layout.json`.
Format: array of panel descriptors with plugin name, dock position, size, open state.
Use nlohmann/json or a minimal hand-rolled serializer — do not use XML.

---

## Contract Checking

Use assertions in both Debug and Release:
```cpp
#include <cassert>
// For invariants that must never be violated:
assert(plugin != nullptr);

// For recoverable errors (bad input, missing topic):
// Log and return, do not assert
```

Do not use exceptions for control flow. Log errors via a simple Logger singleton.

---

## Naming Conventions

- Types: `PascalCase`
- Functions/methods: `camelCase`
- Member variables: `camelCase_` (trailing underscore)
- Constants: `UPPER_SNAKE_CASE`
- Files: `PascalCase.hpp` / `PascalCase.cpp`
- Plugin entry points: `extern "C"` snake_case (`createPlugin`, `destroyPlugin`)

---

## What NOT To Do

- No MinGW
- No C++17 — use C++20 features freely (concepts, ranges, std::format)
- No `std::shared_ptr` for hot-path scene objects — prefer handles/indices
- No global mutable state outside of the Logger singleton and ImGui context
- Do not call ROS2 APIs from the render thread — use a queue
- Do not block the render loop — all ROS2 callbacks run on a separate thread
- Do not hardcode pixel sizes — derive from DPI scale factor
- Do not add OpenBridge-specific code yet — that comes later

---

## ROS2 Threading Model

ROS2 callbacks run on a dedicated thread. Data passes to the render thread via lock-free queues or double-buffered structs. Never call `rclcpp` from the render loop.

```
ROS2 thread → queue → Render thread → ImGui / threepp
```

---

## Open Decisions (do not resolve without discussion)

- Vulkan vs OpenGL backend default (threepp supports both — start with OpenGL, Vulkan path tracer opt-in)
- Whether ImGui docking layout state and utsyn layout.json should be unified or separate
- Plugin config store design (how plugins persist their own settings)
- ViewportManager API (who can create viewports — app only, or plugins too?)
