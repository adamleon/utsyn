# MILESTONE / RESUME — utsyn

Working handoff so a fresh session can pick up. Read this + ARCHITECTURE.md first.
Last updated: **GUI FEATURE SWEEP on top of the Vulkan backend** (branch
`feat/vulkan-backend`, pushed). On top of the opt-in Vulkan deferred-hybrid backend
(`--vulkan`; dockable 3D viewport via the threepp `nativeSceneColorView` accessor), five
GUI features, all verified:
- **JetBrains Mono** font — DPI-crisp via `FontScaleDpi`, both backends (`assets/fonts/`,
  baked `UTSYN_ASSET_DIR`).
- **Layout persistence** — `LayoutManager` points ImGui's ini at `%APPDATA%\utsyn\imgui.ini`.
- **View menu + `PanelRegistry`** — lists/toggles every panel incl. plugin ones; added a
  `PluginContext` field → **plugin ABI bumped to 2** (all plugins rebuilt).
- **TF Tree** — `TfListener` + `TfTree`: live `/tf` + `/tf_static`, terminal ASCII style
  ([+]/[-], green=dynamic/grey=static), x/y/z + roll/pitch/yaw, relative/absolute (world)
  toggle. Needs `tf2_msgs` (added to CMake).
- **Topic Plot** — `TopicPlot` (ImPlot): real-time `/joint_states` positions live, synthetic
  signal offline. ImPlot needs its own `CreateContext`/`DestroyContext` (added).

Validated against a live UR5e over DDS (RoboStack view demo). GL path stays the default.
threepp fork `feat/vulkan-rendertarget` (the accessor) is a pending PR. Prior milestone
holds: live UR5e → robot rendered + articulated (GL), `PackageResolver` resolving meshes.

## NEW since last session (uncommitted)
- **`src/ros/PackageResolver.{hpp,cpp}`** (utsyn_core) — rewrites `package://pkg/...` mesh
  URIs → absolute paths via search roots from `UTSYN_PACKAGE_PATH` / `AMENT_PREFIX_PATH` /
  `ROS_PACKAGE_PATH`. `RobotActor::rebuild()` runs the URDF through it before parsing.
  Without it, wire-delivered URDFs lose all meshes (no baseDir/package.xml to walk).
- **RoboStack workspace at `D:\development\robostack\`** — a pixi workspace (`pixi.toml`
  the user provided) with `ros-jazzy-desktop` + `ur-description` + `ur-moveit-config` +
  `moveit` from the `robostack-jazzy` channel. Built via `pixi install -e jazzy`. The
  `install/setup.bat` is a no-op stub (ROS env comes from conda activate.d; the colcon
  `build` task is for future source pkgs). `check.bat` verifies the env.
  - UR demo (publishes /robot_description latched + /joint_states + /tf):
    `pixi run --manifest-path D:/development/robostack/pixi.toml -e jazzy ros2 launch ur_description view_ur.launch.py ur_type:=ur5e`
  - UR meshes live at `D:\development\robostack\.pixi\envs\jazzy\Library\share\ur_description\meshes`
    (visual `.dae`, collision `.stl`). `build/ros-run.bat` now sets
    `UTSYN_PACKAGE_PATH` to that `...\share` dir so PackageResolver finds them.

## Gotchas learned wiring utsyn to live UR
- **Run helper batches THROUGH the pixi env**: `pixi run --manifest-path C:\pixi_ws\pixi.toml
  cmd /c <bat>`. Running `ros-run.bat` bare → `0xC0000135` (DLL-not-found: needs python +
  conda libs from the pixi env). Use the **PowerShell tool**, not Bash — Git Bash mangles
  `cmd /c` into `cmd C:/`.
- **One `/utsyn` node at a time.** Two utsyn instances (same node name) on one domain wedge
  DDS discovery (neither shows on the graph, no data). Kill ALL utsyn before relaunch.
- **`open_application "Utsyn"` LAUNCHES a new build/Debug utsyn** (no-ROS sample arm) —
  don't use it to focus the window; it spawns strays.
- Cross-build DDS interop works: C:\pixi_ws FastDDS ↔ robostack FastDDS, domain 0.
- threepp's URDFLoader renders **collision** geom as white wireframe ALWAYS, overlaid on
  visual — so the UR looks wireframe-y. A visual/collision toggle is the next polish.

---

## TL;DR state

- **utsyn** = ROS2 visualizer / rviz2 alternative on threepp (3D) + ImGui. Windows-
  primary, MSVC, C++20, `/W4 /WX`.
- **`main` branch** has PR1 (ROS/plugin plumbing) + PR2 (MessageMonitor + robot_monitor),
  committed (commit `3c75828`).
- **`feat/robot-description`** (renamed from `spike/threepp-shared`) — the live-URDF-robot
  work: shared threepp + SceneManager + Actor + robot_description + PackageResolver +
  ROS-enable + look-good pass. 8 commits on `3c75828`. **NOT pushed.**
- **`feat/vulkan-backend`** — on top of `feat/robot-description`, **PUSHED**. The opt-in
  Vulkan backend (build-enable + VMA / dual-backend render / panel docking / dockable 3D
  viewport via the scene-color accessor) **plus five GUI features** (JetBrains Mono font /
  layout persistence / View menu + PanelRegistry [plugin ABI v2] / TF Tree with live /tf +
  rel/abs toggle / real-time Topic Plot). ~13 commits on `feat/robot-description`. The
  branch is large — worth splitting when you PR it.
- **`D:\development\threepp`** — a standalone threepp clone, branch
  `feat/vulkan-rendertarget` (commit `5e1b4414`): the ~33-line `nativeSceneColorView`
  accessor. This is the pending **threepp PR**. `gh` is not installed → fork/push/PR is the
  user's call.
- Build trees:
  - **`build/`** — Debug, **no-ROS** (UTSYN_ROS2 off), GL. Day-to-day. Run:
    `build\Debug\utsyn.exe`.
  - **`build-ros/`** — Release, **ROS2 ON**, GL. Built against pixi ROS2 Jazzy. Run via the
    ROS env (below).
  - **`build-vk/`** — Debug, **Vulkan ON** (`UTSYN_WITH_VULKAN=ON`),
    `FETCHCONTENT_SOURCE_DIR_THREEPP` → the `D:\development\threepp` clone. Needs
    `VULKAN_SDK` set in the build env. Run: `build-vk\Debug\utsyn.exe --vulkan`.

---

## What's built & working (uncommitted on spike/threepp-shared)

1. **Aesthetics pass** — `src/widgets/TerminalUi.hpp` (ui::dashRule etc.), MessageMonitor
   uses ASCII `[+]/[-]` collapse markers + dash-rule separators. (User: "good enough.")
2. **Shared threepp** — threepp built as `threepp.dll` (one instance shared by core +
   plugins; REQUIRED because threepp's renderer uses RTTI, so a duplicate static copy
   breaks `dynamic_cast` across the DLL). `utsyn_core.dll` shrank (threepp externalized).
   A `POST_BUILD` step copies `$<TARGET_RUNTIME_DLLS:utsyn>` (threepp.dll/glfw3.dll) next
   to the exe. **Output stays `build/<cfg>/` — do NOT move to build/bin (breaks
   .vscode/launch.json; caused a 0xC0000135 once).**
3. **Scene injection** — `SceneManager` (was STUB) is the handle-based API
   (`SceneHandle` = id+generation): owns plugin-added `threepp::Object3D`s, forwards
   add/remove to the Viewport scene. `Viewport::scene()` accessor added; placeholder demo
   meshes removed (grid+axes remain).
4. **Actor framework** — `src/rendering/Actor.hpp` (header-only base): the rviz-"Display"
   concept — lifecycle `onAttach/onUpdate/onDetach/onInspector`, owns its scene objects.
   "Actor" is the agreed name (not Display/SceneObject).
5. **robot_description plugin** — `plugins/robot_description/` = `RobotActor` (loads URDF
   via threepp `URDFLoader::parse(string)->Robot`, articulates with `setJointValue`,
   name→index from `getArticulatedJointInfo()`) + `RobotDescriptionPlugin` (owns the actor
   + a MessageMonitor; ROS-guarded `/robot_description` + `/joint_states` feeds). Built-in
   primitive **sample arm** (2 revolute + 1 prismatic) + manual joint sliders for offline
   use; "Load broken URDF" negative test.

Verified (no-ROS, build/Debug): builds clean `/WX`, runs, sample URDF parses to **3 DOF**,
robot injected + articulated each frame, exit 0. **Visual appearance NOT yet eyeball-
confirmed** (machine was locked during the autonomous session) — that's a top TODO.

---

## ROS2 (pixi ROS2 Jazzy) — validated, this is the new part

ROS2 Jazzy prebuilt Windows binary lives at **`C:\pixi_ws\ros2-windows`**; its conda deps
are the parent pixi env at **`C:\pixi_ws\.pixi`** (pixi.toml = "Dependencies to build ROS 2
on Windows", conda-forge). pixi 0.59 on PATH.

**Activation pattern** (need BOTH the pixi env AND ros2 sourced):
`pixi run --manifest-path C:\pixi_ws\pixi.toml cmd /c <a .bat that first does
`call C:\pixi_ws\ros2-windows\local_setup.bat`>`. There are helper batches in `build/`
(gitignored): `ros-configure.bat`, `ros-build.bat`, `ros-run.bat`, `ros-query.bat`. Each
is just `call local_setup.bat` + the cmake/run command. Recreate if build/ is wiped.

**Configure** (Release, separate `build-ros` tree, reuses build/ dep sources to skip
re-download):
```
pixi run --manifest-path C:\pixi_ws\pixi.toml cmd /c D:\development\utsyn\build\ros-configure.bat
```
`ros-configure.bat`:
```
call C:\pixi_ws\ros2-windows\local_setup.bat
cmake -S D:\development\utsyn -B D:\development\utsyn\build-ros -G "Visual Studio 17 2022" -A x64 ^
  -DFETCHCONTENT_SOURCE_DIR_THREEPP=D:/development/utsyn/build/_deps/threepp-src ^
  -DFETCHCONTENT_SOURCE_DIR_IMGUI=D:/development/utsyn/build/_deps/imgui-src ^
  -DFETCHCONTENT_SOURCE_DIR_IMPLOT=D:/development/utsyn/build/_deps/implot-src ^
  -DFETCHCONTENT_SOURCE_DIR_NANOSVG=D:/development/utsyn/build/_deps/nanosvg-src ^
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=D:/development/utsyn/build/_deps/nlohmann_json-src
```
**Build (must be Release** — prebuilt ROS2 is Release; Debug would CRT-mismatch):
```
pixi run --manifest-path C:\pixi_ws\pixi.toml cmd /c D:\development\utsyn\build\ros-build.bat
```
(`ros-build.bat` = `call local_setup.bat` + `cmake --build D:\development\utsyn\build-ros --config Release --parallel`)

**Run:** `ros-run.bat` = `call local_setup.bat` + `build-ros\Release\utsyn.exe`.

**CMake changes that made ROS work** (already applied in CMakeLists.txt):
- Removed `ament_package()` (utsyn consumes ROS2, isn't a colcon package → no package.xml).
- Replaced all `ament_target_dependencies(...)` with direct modern linking on utsyn_core:
  `target_link_libraries(utsyn_core PUBLIC rclcpp::rclcpp tf2::tf2 tf2_ros::tf2_ros
  ${std_msgs_TARGETS} ${sensor_msgs_TARGETS} ${geometry_msgs_TARGETS})`. (ament's macro
  uses a PLAIN target_link_libraries → collides with our keyword style.) Plugins inherit
  ROS transitively from utsyn_core PUBLIC — no per-plugin ROS linkage.

**ROS validation done:** find_package resolves; full `#if UTSYN_ROS2` code compiles
(first time ever), Release, clean; at runtime `RosCore: node 'utsyn' up, executor
spinning`, real subscriptions created, no crash; `/utsyn` appears in `ros2 node list`.
**PENDING:** live data → robot. Test: run utsyn (ROS env) + in another sourced shell
`ros2 topic pub -r 50 /joint_states sensor_msgs/msg/JointState '{name: [joint1,joint2,joint3], position: [0.3,0.5,0.1]}'`
and a latched `ros2 topic pub -r 1 --qos-durability transient_local /robot_description std_msgs/msg/String '{data: "<the URDF>"}'`
→ the arm should move + MessageMonitor rows go `[LIVE]`/`[ L ]`.

---

## Vulkan backend — DONE (opt-in), this session

threepp's **Vulkan deferred-hybrid** renderer is wired as an opt-in backend on the RTX
3090 (LunarG SDK 1.4.350.0). Build: `UTSYN_WITH_VULKAN=ON` (FetchContent-pulls VMA v3.1.0,
`find_package(Vulkan)`, adds `imgui_impl_vulkan`, defines `THREEPP_WITH_VULKAN`). Run:
`utsyn.exe --vulkan`.

- **Build/run** (Debug, the `build-vk` tree, pointed at the threepp clone):
  ```
  cmake -S D:\development\utsyn -B D:\development\utsyn\build-vk -G "Visual Studio 17 2022" -A x64 ^
    -DUTSYN_WITH_VULKAN=ON -DFETCHCONTENT_SOURCE_DIR_THREEPP=D:/development/threepp
  $env:VULKAN_SDK='C:\VulkanSDK\1.4.350.0'   # must be set in the build env
  cmake --build D:\development\utsyn\build-vk --config Debug --target utsyn --parallel
  build-vk\Debug\utsyn.exe --vulkan
  ```
- **How it works:** the VulkanRenderer renders fullscreen (`setRenderTarget` is a stub).
  utsyn exposes the renderer's per-frame scene-color image via a new threepp accessor
  (`nativeSceneColorView` / `framesInFlight` / `currentSceneColorSlot`) and draws it in a
  dockable "3D Viewport" panel (`VkScenePanel`: one `ImGui_ImplVulkan_AddTexture` descriptor
  per in-flight slot, re-registered on resize). Camera aspect tracks the panel; orbit/pan
  via an InvisibleButton over the image, zoom via threepp's wheel listener gated on hover.
  See ARCHITECTURE.md "Vulkan Backend".
- **Gotchas:** ImGui's Vulkan backend needs threepp's `ImguiContext` (not direct init) +
  an explicit `ImGuiConfigFlags_DockingEnable` — `ImguiContext` doesn't set it, and that one
  flag was why docking + central-area camera input were dead. Descriptor-pool validation
  warnings (SAMPLER/SAMPLED_IMAGE not in threepp's COMBINED pool) are benign on NVIDIA. After
  editing the threepp clone, MSBuild may think threepp is up-to-date — **touch** the changed
  file to force the recompile. Kill any running `utsyn` before a build (it locks the DLLs).
- **Remaining:** true per-panel-size offscreen + real multi-viewport need a working
  `VulkanRenderer::setRenderTarget` (threepp Option B, weeks); the accessor (Option A) is the
  shipped path. Push the threepp PR (`feat/vulkan-rendertarget`).

---

## NEXT STEPS (priority order)

1–4. ~~Robot visual verify + live ROS data + commit + visual/collision look-good pass~~ —
   all DONE (see git log on `feat/robot-description`; UR5e renders + articulates from live
   data, studio lighting, monitor token fix).
5. ~~Vulkan swap + GUI feature sweep~~ — DONE this session on `feat/vulkan-backend`
   (**pushed**): Vulkan backend + font / layout persistence / View menu+PanelRegistry /
   TF Tree (live /tf) / Topic Plot. See the header.
6. ~~Push feat/robot-description + feat/vulkan-backend~~ — DONE (both on `origin`).
   **Still the user's call:** the **threepp PR** from `D:\development\threepp`
   `feat/vulkan-rendertarget` (`5e1b4414`) — `gh` not installed, so fork/push/open by hand.
   And consider splitting the large `feat/vulkan-backend` before opening its PR.
7. **Interaction layer** (selection / transform gizmos / picking) — separate layer on top
   of Actors, deferred until after the robot is solid.
8. **Vulkan multi-viewport / per-panel offscreen** — needs a real threepp
   `VulkanRenderer::setRenderTarget` (Option B). The current accessor path mirrors a single
   fullscreen render.
9. **Generic topic-field plotting** — the Topic Plot is currently hard-wired to
   `/joint_states` positions; a topic/field picker (message introspection) would generalize it.

## Gotchas / decisions to remember
- Shared threepp + RTTI (above). ImGui is also single-instance in utsyn_core via
  `IMGUI_API=__declspec(dllexport)` (WIN32-guarded); plugins must NOT link their own copy.
- Permissions: `.claude/settings.local.json` (gitignored) allows git/cmake; use plain
  `git <sub>` and `cmake …` (not `git -C`) so the rules match.
- Updating threepp = bump FetchContent tag + rebuild; shared adds no version lock.
- TfListener still STUB → single robot at world origin for now.
