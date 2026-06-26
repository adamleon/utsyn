# MILESTONE / RESUME — utsyn

Working handoff so a fresh session can pick up. Read this + ARCHITECTURE.md first.
Last updated: **LIVE ROS DATA → ROBOT VALIDATED.** utsyn (build-ros) received a real
UR5e's `/robot_description` + `/joint_states` over DDS from a RoboStack Jazzy install,
parsed to 6 DOF, resolved all 14 meshes (new `PackageResolver`), and rendered/articulated
the arm. Robot eyeball-confirmed. Earlier: robot_description + Actor framework (GL); ROS2
build+runtime validated vs pixi ROS2 Jazzy; Vulkan blocked on SDK.

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
- **Everything since is UNCOMMITTED** on branch **`spike/threepp-shared`** (name is now a
  misnomer — it carries the adopted work, not a throwaway). NOT pushed. Needs committing
  in sensible chunks (rename the branch first).
- Two build trees:
  - **`build/`** — Debug, **no-ROS** (UTSYN_ROS2 off), GL renderer. Day-to-day. Run:
    `build\Debug\utsyn.exe`.
  - **`build-ros/`** — Release, **ROS2 ON**, GL renderer. Built against pixi ROS2 Jazzy.
    Run via the ROS env (below).

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

## Vulkan renderer track — BLOCKED on SDK

User wants the main renderer to be threepp's **Vulkan deferred-hybrid** (RenderMode::
RasterFirst). GPU is **RTX 3090** (ideal). Blocker: **Vulkan SDK not installed**
(VULKAN_SDK empty; threepp's THREEPP_WITH_VULKAN needs `find_package(Vulkan REQUIRED)`).
User to install the LunarG SDK (vulkan.lunarg.com). Then it's a real rewrite (own spike):
THREEPP_WITH_VULKAN=ON, Vulkan Canvas + VulkanRenderer, ImGui Vulkan backend
(imgui_impl_vulkan), offscreen-viewport→VkDescriptorSet. See memory `vulkan-renderer-plan`.
Renderer-agnostic Actors are unaffected; only Viewport/ViewportPanel/ImGui-init change.

---

## NEXT STEPS (priority order)

1. ~~Visually verify the robot~~ — DONE (UR5e renders + articulates from live data).
2. ~~Live ROS data test~~ — DONE (real UR5e over DDS, 6 DOF, 14/14 meshes resolved).
   NEW next item: **visual/collision display toggle** — threepp's loader draws collision
   meshes as white wireframe over the visual; add a per-Actor toggle (part of the deferred
   display/interaction layer) so the UR shows clean solid like rviz.
3. **Commit the uncommitted work** — rename `spike/threepp-shared` to something real,
   commit in chunks (aesthetics / shared-threepp+SceneManager / Actor+robot_description /
   ROS-enable CMake). Push is the user's call (kept prompting).
4. **Interaction layer** (selection / transform gizmos / picking) — agreed as a SEPARATE
   layer on top of Actors, deferred until after the robot is solid.
5. **Vulkan swap** once the SDK is installed.

## Gotchas / decisions to remember
- Shared threepp + RTTI (above). ImGui is also single-instance in utsyn_core via
  `IMGUI_API=__declspec(dllexport)` (WIN32-guarded); plugins must NOT link their own copy.
- Permissions: `.claude/settings.local.json` (gitignored) allows git/cmake; use plain
  `git <sub>` and `cmake …` (not `git -C`) so the rules match.
- Updating threepp = bump FetchContent tag + rebuild; shared adds no version lock.
- TfListener still STUB → single robot at world origin for now.
