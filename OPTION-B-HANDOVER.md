# HANDOVER — Option B: Vulkan offscreen render targets → true multi-viewport

Self-contained brief so a fresh session can start cold. Read this + `MILESTONE.md` +
`ARCHITECTURE.md` ("Vulkan Backend" section) first.

---

## 0. Goal in one paragraph

The Vulkan backend currently renders **one** fullscreen scene (one camera) to the swapchain
and mirrors that single image into a docked "3D Viewport" panel (that's **Option A**, shipped).
**Option B** = implement `VulkanRenderer::setRenderTarget()` in threepp so the renderer can draw
a scene/camera to an **offscreen image** (a `RenderTarget`) instead of the swapchain — which is
what utsyn needs for **N independent Vulkan viewports** (different cameras, GL-parity), and for
per-panel-resolution rendering. Today `setRenderTarget` is an empty stub.

**Definition of done (minimum):** utsyn can show two docked Vulkan panels, each rendering a
*different camera* of the scene, live. (Stretch: each at its own panel resolution.)

---

## 1. Where things stand (Option A, shipped)

- **threepp clone:** `D:\development\threepp`, branch `feat/vulkan-rendertarget`, commit
  `5e1b4414`. Adds a **33-line accessor** to `VulkanRenderer`: `nativeSceneColorView(slot)` /
  `framesInFlight()` / `currentSceneColorSlot()` — exposes a *view* of the renderer's existing
  per-frame scene-color image so utsyn can sample the fullscreen render. **This is the pending
  threepp PR.** `setRenderTarget`/`getRenderTarget` are still stubs
  (`src/threepp/renderers/VulkanRenderer.cpp:15442-15443`).
- **utsyn:** branch `feat/vulkan-backend` (pushed). `Application.cpp`'s `frameVulkan()` renders
  one fullscreen scene; a `VkScenePanel` struct caches `ImGui_ImplVulkan_AddTexture` descriptors
  per in-flight slot and draws `nativeSceneColorView()` as `ImGui::Image` in the "3D Viewport"
  panel. Single viewport only; camera aspect tracks the panel (one-frame lag).

Option B is listed as a next-step in `MILESTONE.md` ("Vulkan multi-viewport / per-panel offscreen").

---

## 2. Build / run / test loop (do this first to get oriented)

The Vulkan build tree is `build-vk`, pointed at the threepp clone via
`FETCHCONTENT_SOURCE_DIR_THREEPP`:

```
cmake -S D:\development\utsyn -B D:\development\utsyn\build-vk -G "Visual Studio 17 2022" -A x64 ^
  -DUTSYN_WITH_VULKAN=ON -DFETCHCONTENT_SOURCE_DIR_THREEPP=D:/development/threepp
$env:VULKAN_SDK='C:\VulkanSDK\1.4.350.0'                       # must be set in the build env
cmake --build D:\development\utsyn\build-vk --config Debug --target utsyn --parallel
D:\development\utsyn\build-vk\Debug\utsyn.exe --vulkan
```

**Gotchas (learned the hard way):**
- Use the **PowerShell tool**, not Bash, for these (Git Bash mangles paths).
- **Kill any running `utsyn` before building** — a running instance locks `utsyn_core.dll` /
  `threepp.dll` → `LNK1168`.
- After editing a file **in the threepp clone**, MSBuild often thinks threepp is up-to-date and
  skips it — **`touch` the changed file** (PowerShell: set `(Get-Item path).LastWriteTime =
  Get-Date`) to force the recompile.
- Descriptor-pool validation warnings (SAMPLER / SAMPLED_IMAGE not in threepp's COMBINED pool)
  are **benign on NVIDIA** (RTX 3090 here).
- For a live robot to test against, see `MILESTONE.md` (RoboStack UR5e view demo over DDS).

---

## 3. threepp side — the contract, the template, the scope

### 3a. The contract to implement (`include/threepp/renderers/Renderer.hpp:103-106`)
```cpp
virtual RenderTarget* getRenderTarget() = 0;
virtual void setRenderTarget(RenderTarget* renderTarget, int activeCubeFace = 0,
                             int activeMipmapLevel = 0) = 0;
```
`setRenderTarget(nullptr)` = back to the default target (swapchain). For 2D viewports the
cubeFace/mip args are unused.

### 3b. `RenderTarget` (`include/threepp/renderers/RenderTarget.hpp:17-78`)
A `RenderTarget` owns a color `std::shared_ptr<Texture> texture` (the render output), a `width`/
`height`, optional `depthBuffer`/`depthTexture`, and `viewport`/`scissor`. Constructed via
`RenderTarget::create(w, h, Options{})`. **This is the object the Vulkan backend must associate
with offscreen VkImages.** (utsyn already uses this for the GL path — see §4.)

### 3c. THE TEMPLATE — `WgpuRenderer` already does offscreen (study this first)
WebGPU is the modern backend and its offscreen implementation is the closest analog to what
Vulkan needs. Read these:
- `WgpuRenderer::setRenderTarget` (`src/threepp/renderers/WgpuRenderer.cpp:4095`): **lazy** — just
  stores `currentRenderTarget_`; GPU resources are created on the first `render()` after.
- `WgpuRenderTargets` (`src/threepp/renderers/WgpuRenderTargets.{hpp,cpp}`): an **`RTEntry`
  cache keyed by RenderTarget UUID** — each entry holds the offscreen color image/view/sampler +
  depth (+ MSAA variants + a resolve texture for user-sampled depth). `getOrCreate(rt, sampleCount)`
  allocates/caches; recreates when size changes.
- `render()` branches on `currentRenderTarget_ == nullptr ? surface : rt` to pick the attachment
  (`WgpuRenderer.cpp:2386` onward).
- `nativeRenderTargetTexture()` (`:4142`) exposes the offscreen color image (for ImGui). Also
  note `renderTargetFlipY()` — WebGPU is top-left origin (Vulkan is too; GL is bottom-left, hence
  utsyn's GL path V-flips and the Vulkan path does not).

**Plan for Vulkan:** mirror this — a `VulkanRenderTargets` cache of `RTEntry { VkImage color,
VkImageView colorView, VkSampler, VkImage depth, VkImageView depthView, ... }` keyed by RT UUID;
`setRenderTarget` stores the pointer (lazy); `render()` picks swapchain-vs-offscreen; expose a
`nativeRenderTargetImageView()` for utsyn's `ImGui_ImplVulkan_AddTexture`.

### 3d. THE HARD PART — VulkanRenderer's swapchain coupling (scope is HIGH)
`src/threepp/renderers/VulkanRenderer.cpp` is ~16,000 lines. It's a deferred-hybrid path tracer
(raster G-buffer → ray query / ReSTIR DI+GI → SVGF denoise → **TAA upsample** → bloom → present).
Crucially it uses a **dual-extent architecture**:
- **render extent** (`renderExtent()`, `:1651-1659`) = `swapchainExtent × renderScale_`. The PT
  accumulators, G-buffer, reservoirs, denoiser, bloom, and the **scene-color** are all sized to
  this. Allocated in `createAccumImage()` (`:8540`), `createRasterGbufImages()` (`:8881`), the
  `Denoiser`/`TaaResolve`/`BloomPass` helpers.
- **swapchain extent** = the final target. **TAA upsamples render-extent → swapchain-extent**
  (`reallocateRenderExtentResources()`, `:12909`; TAA `createImages(inExt, outExt)` at `:12932`).
  One G-buffer image (`unjitDepth`, `:8956-8964`) is deliberately swapchain-sized.

There are **~23-25 render-extent-sized images**, each with **`imageCount_ × kFramesInFlight`
(=2) descriptor sets** baked to those exact views, plus framebuffers created at render extent
(`:8968`). `render()` is at `:15348` → `renderFrame()` `:15277`; swapchain acquire at `:14960`;
present (`vkQueuePresentKHR`) in `endFrame()` `:15206`. The resize hub that re-sizes everything is
`reallocateRenderExtentResources()` (`:12909`) + `recreateSwapchainAndDescriptors()` (`:12959`).

**Honest scope:** this is a real, multi-day threepp effort, **not a stub fill** (ignore the
explore agent's optimistic "1-2 hours" line — its own inventory of ~23-25 sized resources +
descriptor rewiring is the credible signal). The pipeline assumes exactly two extents and one
present target.

### 3e. Two implementation paths (pick per how far you need to go)
1. **B-lite — "redirect the final image" (recommended first milestone).** Leave the whole pipeline
   at its normal (swapchain) render extent. Make `setRenderTarget(rt)` cause the **final
   composited scene-color** (the image the accessor already exposes / what would be presented) to
   be **copied/blit into the RT's offscreen VkImage** (a `vkCmdBlitImage` to the RT's color, which
   can even scale to the RT size) **instead of / in addition to** presenting to the swapchain.
   - Pros: avoids re-sizing the 23-25 resources + descriptor rewrite. Gives **true multi-camera**
     (render scene A with camera A → RT A; render scene/camera B → RT B) at manageable cost.
   - The crux to solve: **rendering N times per frame without N swapchain presents.** `render()`
     currently acquires + presents a swapchain image each call. For offscreen renders you must
     **skip the acquire/present** and end on the offscreen copy (only the *last*, or a dedicated
     one, touches the swapchain — or present a compositor pass). This is the main design problem
     of B-lite and where to spend the spike.
   - Resolution caveat: content is rendered at swapchain-ish extent then blit-scaled into each
     panel (not natively per-panel-crisp), but it's correct and multi-camera.
2. **B-full — "parameterize the extent" (cleaner, larger).** Turn `renderExtent()` into a
   selectable *target extent* and give each RenderTarget its own resource set (images + descriptor
   sets + framebuffers) at its size; `render()` selects the active set. This is the ~1500-1700 LOC,
   deeply-baked refactor (touches `:1651`, `:8540`, `:8881`, `:10583`, `:12909-12956`, all the
   descriptor writes). Gives native per-panel resolution + N viewports properly.

**Recommendation:** spike **B-lite** to prove end-to-end multi-camera (one offscreen RT shown in a
panel, then two cameras), and only escalate to B-full if per-panel resolution / perf demands it.

---

## 4. utsyn side — integration points (from the consumer-side exploration)

### 4a. How the GL path already does multi-target (the parity target)
- `Viewport` (`src/rendering/Viewport.{hpp,cpp}`) owns a `Scene` + `PerspectiveCamera` + a
  `threepp::RenderTarget` (`renderTarget_`). `Viewport::resize(w,h)` **recreates** the target +
  sets camera aspect (`Viewport.cpp:68`); `Viewport::render(GLRenderer&)` does
  `setRenderTarget(rt) → render(scene,cam) → setRenderTarget(nullptr)` (`:157`);
  `Viewport::textureId()` returns the GL texture id (`:166`).
- `ViewportPanel::onImGui(GLRenderer&, bool* open)` (`src/widgets/ViewportPanel.cpp:14`) sizes the
  target to the panel (HiDPI-aware), renders, and `ImGui::Image`s it (V-flipped for GL).
- `ViewportManager` (`src/rendering/ViewportManager.cpp`) owns the viewports (`create()`, `at()`,
  `count()`); the app creates **one** today (`Application.cpp:~220`).
- `SceneManager` binds ONE scene (`kPrimaryViewport`) today; `bindScene(ViewportId, Scene&)` and
  `ViewportId` already exist as seams for multi-viewport.

### 4b. What the Vulkan path does now
- `Application::frameVulkan()` (`src/app/Application.cpp:~418`): one `vkRenderer_->render(vp.scene(),
  vp.camera())`, then `vkUi_->render()`.
- `VkScenePanel` (struct in `Application.cpp:~64`): sampler + `VkDescriptorSet` per in-flight slot,
  re-registered when the `VkImageView` changes; drawn in the single "3D Viewport" panel in
  `renderUi()`.

### 4c. What Option B needs in utsyn (once threepp offers offscreen targets)
- **`Viewport`**: hold a Vulkan render target (or route `render()` per-backend); expose the
  offscreen `VkImageView` per frame slot.
- **`ViewportManager`**: allow N viewports (already supports it structurally).
- **`SceneManager`**: bind a scene per `ViewportId` (map instead of single `primary_`), OR share one
  scene across viewports with per-viewport cameras (simpler; probably start here — same scene, N
  cameras).
- **`frameVulkan()`**: loop viewports, render each to its target.
- **`renderUi()` / `VkScenePanel`**: one panel + one descriptor cache **per viewport** (extend the
  cache to be keyed by viewport as well as slot); per-viewport panel size (`vkPanelW_/H_` → arrays)
  and input routing (each panel's InvisibleButton drives its own viewport's camera — currently
  hardcoded to `viewports_->at(0)`).

Simplest first utsyn milestone: **same scene, two cameras, two panels** — avoids the SceneManager
multi-scene work.

---

## 5. Recommended phasing (spike → feature)

1. **Spike (threepp):** implement `setRenderTarget`/`getRenderTarget` as **B-lite** for a SINGLE
   offscreen RT — render the scene normally but end by blitting the final scene-color into the RT's
   VkImage instead of presenting; expose `nativeRenderTargetImageView()`. Prove it by having utsyn
   show that RT in a panel (replacing the accessor path for one panel). **Solve the
   no-present-on-offscreen problem here.**
2. **Two cameras (utsyn + threepp):** render the same scene twice (camera A, camera B) into two
   RTs in one frame; show both in two docked panels with independent orbit. This is the
   "definition of done."
3. **Polish:** per-viewport panel resolution (blit-scale or escalate toward B-full), input routing,
   `ViewportManager`/`SceneManager` generalization, plugin-created viewports (still an open design
   decision — see CLAUDE.md "Open Decisions").
4. **Upstream:** fold `setRenderTarget` into the threepp PR (or a second PR) alongside the accessor.

---

## 6. Key file:line index (verified this session)

**threepp (`D:\development\threepp`):**
- `include/threepp/renderers/Renderer.hpp:103-106` — the virtual contract.
- `include/threepp/renderers/RenderTarget.hpp:17-78` — RenderTarget class + Options.
- `src/threepp/renderers/WgpuRenderer.cpp:4095,4142` + `WgpuRenderTargets.{hpp,cpp}` — **the template.**
- `src/threepp/renderers/GLRenderer.cpp:1206-1275` (+ `GLTextures.cpp:562-658`) — the GL FBO analog.
- `src/threepp/renderers/VulkanRenderer.cpp`: `renderExtent()` `:1651`; `createAccumImage()` `:8540`;
  `createRasterGbufImages()` `:8881`; `ensureHybridResources()` `:10583`;
  `reallocateRenderExtentResources()` `:12909`; `recreateSwapchainAndDescriptors()` `:12959`;
  `render()` `:15348`; `renderFrame()` `:15277`; acquire `:14960`; `endFrame()`/present `:15206`;
  **`setRenderTarget` stub `:15443`**; the Option-A accessor impls (search `nativeSceneColorView`).

**utsyn (`D:\development\utsyn`):**
- `src/rendering/Viewport.{hpp,cpp}` — `render`/`resize`/`textureId`/`scene`/`camera`.
- `src/widgets/ViewportPanel.cpp` — GL panel (offscreen + ImGui::Image, V-flip).
- `src/rendering/ViewportManager.{hpp,cpp}` — `create`/`at`/`count`.
- `src/rendering/SceneManager.hpp` — `ViewportId`, `bindScene`, `kPrimaryViewport`.
- `src/app/Application.cpp` — `frameVulkan()`, `VkScenePanel` struct, the Vulkan branch of
  `renderUi()`; `src/app/Application.hpp` — the `vk*` members.

---

## 7. Open questions to settle early

- **Do we actually need per-panel resolution, or is N-cameras-at-swapchain-res enough?** If the
  latter, B-lite is dramatically less work. Decide before touching the pipeline.
- **The no-present-on-offscreen-render problem** (how to render N times/frame with ≤1 swapchain
  present) is the true crux — spike it before estimating anything.
- **Shared scene + N cameras vs scene-per-viewport** — start shared (simpler).
- **Plugin-created viewports** — still an open design decision (CLAUDE.md). Keep app-only for now.
