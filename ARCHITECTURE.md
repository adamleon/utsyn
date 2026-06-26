# ARCHITECTURE.md — Utsyn

Current state of the system. Updated at the end of every session that changes structure, interfaces, or data flow.
This describes what exists, not what is planned.

Last updated: **live ROS2 data → robot is now validated end to end.** utsyn (ROS2 ON,
GL renderer) received a real UR5e's latched `/robot_description` over DDS from a separate
RoboStack Jazzy install, reparsed to 6 DOF, and articulated from live `/joint_states`.
A new `PackageResolver` rewrites `package://` mesh URIs to absolute on-disk paths so the
URDF's meshes load even when they live in a different ROS install than utsyn runs in.
Scene injection (SceneManager) + Actor framework + robot_description plugin are built on a
SHARED threepp. Most of this is uncommitted on branch `spike/threepp-shared`. See
MILESTONE.md for the resume guide (pixi+ROS build commands, git state, next steps).

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

The **ROS/plugin backbone is now wired** (the "plumbing-first" PR). `Application`
constructs `RosCore`, a `SubscriptionRegistry` + `SubscriptionBroker`, a
`SceneManager`, and a `PluginLoader`; builds one `PluginContext`; loads every
`plugin_*.dll` next to the executable; and drives the plugin lifecycle. Each frame
it runs `broker.pump()` → `onSceneUpdate(dt)` → scene/threepp render →
`onImGui()`, and on exit performs an ordered `shutdown()` (stop ROS → plugin
`shutdown()` → clear broker → unload DLLs). **ExamplePlugin is now loaded** and
logs through the real `Logger`. Verified end to end both with **ROS2 OFF** (plugins
load, render, exit 0) and, in a separate Release `build-ros` tree, with **ROS2 ON**
(pixi ROS2 Jazzy): `RosCore` inits rclcpp, node 'utsyn' spins, subscriptions go live
on the graph. **The live receive path is now validated**: utsyn received a real UR5e's
latched `/robot_description` over DDS from a separate RoboStack Jazzy install (cross-build
FastDDS interop, domain 0) and articulated the arm from live `/joint_states`.

Because ROS2 is optional and was absent at build time, all rclcpp-touching code is
compiled out behind `#if defined(UTSYN_ROS2) && UTSYN_ROS2`; `RosCore::rosEnabled()`
is false and topic subscriptions are inert (they report `RosDisabled`). The broker
stats/queue machinery exists and compiles, but the live receive path only runs when
ROS2 is present.

The **MessageMonitor widget + a robot_monitor demo plugin (PR2) are now built and
verified.** `MessageMonitor` renders one collapsible block per topic — the
always-visible header carries an ASCII status token + topic + rate + age,
dual-encoded by token and `theme` color; a quiet latched topic reads healthy, not
stale. A plugin creates rows untyped via `addRow()` (so they render even without
ROS2) and attaches a real subscription with the header-only `bind<Msg>()`. The
`RobotMonitorPlugin` watches `/joint_states` and the latched `/robot_description`
and is the seed of the eventual robot_description plugin. Verified by running with
ROS2 OFF: both plugins load, the monitor panel renders two `[-OFF-]` rows with the
expanded TYPE/STATE/RATE/COUNT/LAST/SIZE grid and an editable topic field, and the
app exits cleanly. The live (ROS2-on) feedback path is still unvalidated.

**3D scene injection + the first robot (in progress, GL renderer).** threepp is now
built as a **shared library** (`threepp.dll`) so the ONE threepp instance is shared
by `utsyn_core` and every plugin — required because threepp's renderer uses RTTI,
so a plugin must create threepp objects with the same instance the renderer uses.
`SceneManager` is now the handle-based scene-injection API (`SceneHandle` = id +
generation): it owns plugin-added `threepp::Object3D`s and forwards add/remove to a
`Viewport`'s scene (exposed via a new `Viewport::scene()` accessor; the placeholder
demo meshes were removed, leaving grid + axes). `Actor` (header-only base in
`src/rendering`) is utsyn's rviz-"Display" equivalent — a plugin-contributed 3D
visualization with an onAttach/onUpdate/onDetach/onInspector lifecycle that owns
its scene objects. `RobotActor` (in the robot_description plugin) loads a URDF
string with threepp's `URDFLoader::parse()` → `Robot` and articulates it via
`setJointValue` (name→index from `getArticulatedJointInfo()`); `RobotDescriptionPlugin`
drives it, with a built-in primitive sample arm + manual joint sliders for offline
use and ROS-guarded `/robot_description` + `/joint_states` feeds. Before parsing, the
actor runs the URDF through a `PackageResolver` that rewrites `package://<pkg>/...` mesh
URIs to absolute on-disk paths (threepp's loader otherwise can't resolve `package://`
without a baseDir/package.xml to walk — and a wire-delivered URDF has neither). Verified
both with ROS2 OFF (sample arm, 3 DOF, no meshes) and ROS2 ON against a live UR5e (6 DOF,
mesh URIs resolved via `UTSYN_PACKAGE_PATH`). Collision geometry is hidden by default
(`Robot::showColliders(false)`, rviz-style) with an inspector toggle; the inspector joint
sliders mirror the robot's live `getJointValue()` so they track incoming `/joint_states`
(dragging one re-takes manual control). The interaction layer (selection / transform
gizmos / picking) is deliberately deferred to a later layer.

The still-stubbed modules (`LayoutManager`, `TfListener`, `TopicPlot`, `TfTree`)
have header + .cpp that compile but whose methods are TODO.

Status legend below: STUB = compiles, no functionality yet; BUILT = implemented.

---

## Module Map

| Module | Location | Status | Purpose |
|---|---|---|---|
| Application | src/app/Application | BUILT | Window, ImGui dockspace + terminal style, render loop; wires the ROS/plugin backbone, runs plugin lifecycle, ordered shutdown |
| LayoutManager | src/app/LayoutManager | STUB | Panel layout persistence (JSON) |
| Logger | src/app/Logger | BUILT | Thread-safe singleton logger (info/warn/error + ring buffer, mirrors to stderr) |
| Viewport | src/rendering/Viewport | BUILT | threepp scene + camera, rendered offscreen to a RenderTarget; exposes scene() for SceneManager (grid + axes only — demo meshes removed) |
| ViewportManager | src/rendering/ViewportManager | BUILT | Owns and manages all Viewport instances |
| SceneManager | src/rendering/SceneManager | BUILT | Handle-based scene-injection API (SceneHandle = id+generation); owns plugin-added threepp Object3Ds, forwards add/remove to a Viewport's scene; render-thread-only |
| Actor | src/rendering/Actor | BUILT | Header-only base for a plugin 3D visualization (rviz "Display"): onAttach/onUpdate/onDetach/onInspector lifecycle, owns its scene objects |
| RosCore | src/ros/RosCore | BUILT | ROS2 lifecycle: rclcpp init, "utsyn" node, SingleThreadedExecutor + spin thread. Inert without ROS2 |
| TopicStats / StatsCell | src/ros/TopicStats | BUILT | Per-topic stats POD + lock-free published-index double-buffer (ROS→render). rclcpp-free |
| SpscRing | src/ros/SpscRing | BUILT | Single-producer/single-consumer payload queue (ROS→render). rclcpp-free |
| ISubscriptionRegistry | src/ros/ISubscriptionRegistry | BUILT | Non-template, ABI-stable core contract for subscriptions. rclcpp-free |
| SubscriptionBroker | src/ros/SubscriptionBroker | BUILT | Header-only typed facade (subscribe<Msg>/monitor<Msg>) over the registry. This is what PluginContext exposes |
| SubscriptionRegistry | src/ros/SubscriptionRegistry | BUILT | Concrete registry: dedup + refcount + per-topic StatsCell + drainers + pump()/clear(). rclcpp only in the .cpp |
| TfListener | src/ros/TfListener | STUB | Consumes tf2, exposes transforms to SceneManager |
| PackageResolver | src/ros/PackageResolver | BUILT | Resolves `package://pkg/rel` URIs to absolute paths via search roots (UTSYN_PACKAGE_PATH / AMENT_PREFIX_PATH / ROS_PACKAGE_PATH); rewrites a URDF's mesh URIs pre-parse. rclcpp-free |
| IPlugin | src/plugins/IPlugin | BUILT | Plugin contract (interface) + UTSYN_PLUGIN_ABI_VERSION sentinel |
| PluginLoader | src/plugins/PluginLoader | BUILT | Loads plugin_*.dll/.so, resolves entry points, ABI-checks, drives lifecycle, ordered unload |
| TopicPlot | src/widgets/TopicPlot | STUB | ImPlot-based real-time topic monitor panel |
| TfTree | src/widgets/TfTree | STUB | TF frame tree panel |
| ViewportPanel | src/widgets/ViewportPanel | BUILT | ImGui panel embedding a Viewport's render texture |
| MessageMonitor | src/widgets/MessageMonitor | BUILT | Reusable per-topic streaming-feedback widget plugins embed (one collapsible block per topic; addRow + bind<Msg>) |
| Theme | src/app/Theme | BUILT | Named status colors (StatusLive/Stale/Error/Idle) for the terminal palette |
| ExamplePlugin | plugins/example_plugin | BUILT | Reference plugin — full IPlugin impl + ImGui panel; loaded by the app, logs via Logger |
| RobotMonitorPlugin | plugins/robot_monitor | BUILT | Demo: monitors /joint_states + /robot_description via MessageMonitor |
| RobotDescriptionPlugin | plugins/robot_description | BUILT | Loads URDF (RobotActor) + articulates from /joint_states; inspector with sample arm + manual joint sliders. GL renderer; visual not yet eyeball-verified |

Update this table when modules are created or their status changes.

---

## Data Flow

```
┌─────────────────────────────────────────────────────┐
│ ROS2 Thread  (RosCore: SingleThreadedExecutor.spin) │
│   All topic callbacks land here                     │
│                                                     │
│   SubscriptionRegistry (behind SubscriptionBroker)  │
│     - owns all rclcpp subscriptions                 │
│     - plugins request topics through the broker     │
│     - deduplicates (N plugins, 1 subscription)      │
│     - per message: update stats, push payload       │
└──────────────────────┬──────────────────────────────┘
        payload: SpscRing │ stats: StatsCell
        (per consumer)    │ (published-index double-buffer)
┌──────────────────────▼──────────────────────────────┐
│ Render Thread                                        │
│   Render loop (vsync), Application::frame()         │
│     │                                               │
│     ├── broker.pump()                               │
│     │     └── drain payload queues → plugin cbs     │
│     │                                               │
│     ├── plugins.onSceneUpdate(dt)                   │
│     │     └── (later) SceneManager / TfListener     │
│     │                                               │
│     ├── Viewport.update(dt) + threepp render        │
│     │                                               │
│     └── renderUi() / ImGui                          │
│           ├── status window + ViewportPanel         │
│           └── plugins.onImGui()  (plugin panels;    │
│               read TopicStats via the broker)       │
└─────────────────────────────────────────────────────┘
```

**Rule:** Never call rclcpp from the render thread. Never block the render loop.
Subscription map mutation (acquire/release/pump/clear) is render-thread-only
(asserted); the ROS thread only writes a live topic's StatsCell/SpscRing.

---

## Plugin System

### Loading
Plugins are shared libraries (.so on Linux, .dll on Windows). `PluginLoader`
scans the executable's directory for `plugin_*.{dll,so}` and, for each, opens it
with LoadLibrary/dlopen and resolves three symbols:
```cpp
extern "C" utsyn::IPlugin* createPlugin();
extern "C" void           destroyPlugin(utsyn::IPlugin*);
extern "C" int            utsynPluginAbiVersion();   // must == UTSYN_PLUGIN_ABI_VERSION
```
A missing symbol or an ABI-version mismatch is a recoverable error: the loader
logs and skips the plugin (it does not assert).

### Lifecycle
```
load → ABI check → createPlugin() → initialize(ctx)
     → [pump → onSceneUpdate → onImGui per frame]
     → shutdown() → destroyPlugin() → unload
```
Teardown order is load-bearing and performed by `Application::shutdown()`: stop
the ROS thread → plugin `shutdown()` → `registry.clear()` (drops the
subscription/drainer closures, which capture plugin-defined message types) →
unload the DLLs. Clearing the broker before unload is what keeps a closure from
being destroyed after its DLL is gone.

### ImGui across the DLL boundary
ImGui keeps global state (`GImGui`) per copy of its code. ImGui/ImPlot are linked
**only** into `utsyn_core.dll` (as the single instance) and their API is exported
(`IMGUI_API=__declspec(dllexport)`); plugins do **not** link their own copy — they
get the headers from `utsyn_core`'s include dirs and resolve the symbols from
`utsyn_core`. A plugin that statically linked a second ImGui copy would have a NULL
`GImGui` and crash on its first `ImGui::Begin()`.

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
| Render thread (main) | ImGui, threepp, plugin onImGui/onSceneUpdate, broker.pump() (queue draining), registry mutation |
| ROS2 thread | RosCore's SingleThreadedExecutor.spin(): all topic callbacks; per-topic stat update + payload push |

The ROS2 thread is `RosCore`'s spin `std::thread`. Without ROS2 (`UTSYN_ROS2`
off) it is never started and `rosEnabled()` is false. The single-threaded
executor is load-bearing: it makes every `StatsCell` a single-writer, which is
what the lock-free published-index double-buffer relies on. The `Logger` mutex is
the only lock shared by both threads. No other threads at this time.
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
| Subscription broker | Two layers: header-only typed `SubscriptionBroker` facade (`subscribe<Msg>`) + non-template ABI-stable `ISubscriptionRegistry` impl in core | The `Msg` template instantiates in the *plugin* TU, so no message-type template ever crosses the DLL boundary; core exports only non-template symbols |
| ROS→render hand-off | Per-topic `StatsCell` (published-index double-buffer) for stats + `SpscRing` for payloads; drained on the render thread in `broker.pump()` | Lock-free, single-writer (ROS thread) / single-reader (render thread); honors "never block the render loop, never rclcpp on it" |
| Single ImGui instance | ImGui/ImPlot linked PRIVATE into `utsyn_core` and exported via `IMGUI_API=__declspec(dllexport)`; plugins resolve from core, never link their own copy | A second ImGui copy has its own NULL `GImGui` → crash on first `ImGui::Begin()` in a plugin |
| Plugin ABI gate | `UTSYN_PLUGIN_ABI_VERSION` + exported `utsynPluginAbiVersion()`; loader rejects mismatches | Detects stale plugins. Does NOT cover CRT/build-config mismatch — plugins must share utsyn_core's toolchain+config |
| Registry mutation thread | acquire/release/attachDrainer/pump/clear are render-thread-only (asserted); slot reuse via a free-list + handle generation | Keeps plain-int refcounts race-free; generation invalidates stale handles to reused slots |
| URDF mesh resolution | `PackageResolver` rewrites `package://` → absolute paths (string transform) before threepp parses the URDF; search roots from `UTSYN_PACKAGE_PATH` / `AMENT_PREFIX_PATH` / `ROS_PACKAGE_PATH` | threepp's loader only resolves `package://` by walking up to a `package.xml` from a baseDir; a URDF arriving over the wire has neither, so meshes drop silently. Env-driven roots decouple "where the robot's packages live on disk" from "which ROS env utsyn runs in" |

---

## What Does Not Exist Yet

- **A validated live ROS2 receive path** — the project has only been built/run
  with ROS2 OFF. The rclcpp receive/stat/teardown code compiles but is unexercised;
  the MessageMonitor's live/stale/latched/rate feedback has only been seen in the
  ROS-disabled (`[-OFF-]`) state.
- **JetBrains Mono font** — the default ImGui font lacks non-ASCII glyphs (an
  em-dash renders as a box), so UI strings stay ASCII until the font is loaded.
- Graph-poll detection of advertised/QoS-incompatible/type-mismatch states (the
  `StreamState` enum has the cases; only `Live`/`NotAdvertised`/`RosDisabled` are
  currently produced). Serialized-size sampling for `lastMsgBytes`.
- Safe runtime `release()` while the ROS executor is spinning (review item M1):
  today `release()`/`clear()` assume the ROS thread is stopped. Needs an
  executor-synchronized teardown before a plugin retargets a topic at runtime.
- Monitor-only generic (type-string) subscriptions for a future topic-browser.
- robot_description plugin (URDF load + joint-state animation) — the end goal.
- OpenBridge widget layer; Vulkan path tracer; plugin config persistence;
  multi-robot tf namespacing — all deferred.
