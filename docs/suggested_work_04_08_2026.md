# VulkanApp — Codebase Review & Suggested Work

**Date:** 04/08/2026

**Scope:** Full review of `src/`, `src/shaders/`, `CMakeLists.txt`, `CMakePresets.json` and the build scripts.

**Status:** In progress, completed tasks have been removed.

---

## Table of Contents

1. [How to read this document](#1-how-to-read-this-document)
2. [What the project does well](#2-what-the-project-does-well)
3. [Part 1 — Correctness bugs](#part-1--correctness-bugs)
4. [Part 2 — Architecture & code structure](#part-2--architecture--code-structure)
5. [Part 3 — Rendering & performance](#part-3--rendering--performance)
6. [Part 4 — Shaders](#part-4--shaders)
7. [Part 5 — Build system & tooling](#part-5--build-system--tooling)
8. [Part 6 — Prioritised work order](#part-6--prioritised-work-order)
9. [Appendix A — Quick checklist](#appendix-a--quick-checklist)

---

## 1. How to read this document

Each item has:

- **Where** — file and line references (as of this review).
- **What** — the problem.
- **Why it matters** — the observable consequence.
- **Fix** — a concrete suggested change.

Severity tags:

| Tag | Meaning |
|---|---|
| **P0** | Actively wrong / undefined behaviour / crash risk. Fix first. |
| **P1** | Wrong output or a latent bug that will bite soon. |
| **P2** | Design/architecture debt. Fix before the codebase grows further. |
| **P3** | Performance or polish. |

---

## 2. What the project does well

Worth stating explicitly, because these are the parts you should *not* rewrite:

- **`vulkan-hpp` + RAII throughout.** Almost no manual `vkDestroy*`. Very few leak
  opportunities in the Vulkan layer itself.
- **Dynamic rendering + `synchronization2`** rather than `VkRenderPass`/`VkFramebuffer`
  boilerplate. This is the right modern choice and it keeps the pass code readable.
- **`SetVkDebugName` on essentially every object.** Debugging in RenderDoc/NSight will be
  dramatically easier than in most hobby renderers. The `[[maybe_unused]]` +
  `#ifdef DEBUG` pattern is clean.
- **Weighted-blended OIT** is implemented correctly against the Casual Effects reference,
  including the `isinf` guard and the revealage blend factors.
- **Per-pass command pools per frame-in-flight**, with parallel recording of the two
  expensive passes on the thread pool. This is a real design, not an accident.
- **Batching by mesh+material with instanced `drawIndexed`** and a persistently mapped
  instance buffer. Correct instinct.
- **Platform abstraction is honest.** The macOS/MoltenVK comments in `CMakeLists.txt`,
  `pch.h` and `CreateSurface()` explain *why*, not *what*. Keep writing comments like that.
- **Volumetric clouds with a GPU-baked Perlin–Worley 3D texture** at quarter resolution,
  depth-aware. Ambitious and structurally sound.

---

# Part 1 — Correctness bugs

## 1.6 — **P0** `ModelData::Init` will throw or crash on sparse mesh indices

**Where** `src/ModelData.cpp:11-36`

**What**

```cpp
for (const Mesh& mesh : m_Meshes)
{
    Mesh* pMesh = const_cast<Mesh*>(&mesh);
    uint32_t meshIndex = mesh.GetMeshIndex();
    for (const glm::mat4& transform : m_MeshLocalTransforms.at(meshIndex))
    {
        m_Drawables.push_back(
            Drawable{.pMesh = pMesh,
                     .pMat  = mesh.GetMaterial(),
                     .blendMode = mesh.GetMaterial()->GetBlendMode(),  // nullptr deref
                     .Transform = transform});
    }
}
```

Two problems:

1. `resize(meshIndex + 1)` creates *default-constructed* `Mesh` objects for any index gap.
   Those have `m_bIsValid == false`, `m_MeshIndex == 0` and `m_Material == nullptr`.
   The loop does not skip them. `mesh.GetMaterial()->GetBlendMode()` is a null dereference.
2. Every invalid mesh reports `GetMeshIndex() == 0`, so it also duplicates mesh 0's
   drawables. If mesh 0 was itself never registered, `m_MeshLocalTransforms.at(0)` throws
   `std::out_of_range`.

The `const_cast` is also a smell: iterate by non-const reference instead.

**Fix**

```cpp
for (Mesh& mesh : m_Meshes)
{
    if (!mesh.IsValid())
        continue;

    const auto it = m_MeshLocalTransforms.find(mesh.GetMeshIndex());
    if (it == m_MeshLocalTransforms.end())
        continue;

    for (const glm::mat4& transform : it->second)
    {
        m_Drawables.push_back(Drawable{.pMesh = &mesh,
                                       .pMat = mesh.GetMaterial(),
                                       .blendMode = mesh.GetMaterial()->GetBlendMode(),
                                       .Transform = transform});
    }
}
```

Fixing [1.5](#15--p0-modeldataregistermesh-hands-out-pointers-that-resize-invalidates)
with option 1 removes the "gap" case entirely, but keep the `IsValid()` guard as defence
in depth: an assimp scene can legitimately contain a mesh that no node references.

---

## 1.11 — **P1** The cloud compute shader's depth read is not synchronised

**Where**
- `src/Utility.h:280-294` (the `eDepthAttachmentOptimal → eDepthReadOnlyOptimal` branch)
- `src/main.cpp:1774-1777` (where that transition is recorded)
- `src/shaders/clouds.comp.slang:4,76` (`depthTexture.Load`)

**What**

The barrier that makes the depth buffer readable specifies:

```cpp
barrier.dstStageMask  = eEarlyFragmentTests | eLateFragmentTests | eFragmentShader;
barrier.dstAccessMask = eDepthStencilAttachmentRead | eShaderRead;
```

`eComputeShader` is missing from `dstStageMask`, but the very next command buffer
(`frameData.CloudCommandBuffer`) samples that image from a **compute** shader.

**Why it matters**

Depth writes from the opaque pass are not guaranteed visible to the cloud dispatch.
Clouds may be occluded against stale or partially-written depth — an intermittent,
resolution-dependent artefact.

**Fix**

```cpp
barrier.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                       vk::PipelineStageFlagBits2::eLateFragmentTests |
                       vk::PipelineStageFlagBits2::eFragmentShader |
                       vk::PipelineStageFlagBits2::eComputeShader;
```

`TransitionImageLayout`'s own comment (`src/Utility.h:233-234`) already says this
if/else-if chain "is starting to not really make sense anymore". Agreed — see
[2.3](#23--p2-replace-transitionimagelayout-with-an-explicit-barrier-struct).

---

## 1.14 — **P2** The skybox is loaded but never rendered

**Where** `src/main.cpp:256-268` (load), `364-367` (unload)

**What**

`m_pSkybox` is loaded from `textures/skybox/*.jpg`, stored, and unloaded at shutdown. It
is never bound to a descriptor, never sampled, and there is no skybox pipeline. The
background is `SKY_COLOR` as a clear colour instead (`main.cpp:1675-1676`).

**Why it matters**

~6 JPEGs of VRAM and load time for nothing. It also means bug
[1.1](#11--p0-cubemaps-only-ever-upload-and-transition-face-0) is silent.

**Fix**

Either delete the load, or finish the feature. Finishing it is cheap and high-impact:

1. Add the cubemap to the global/composite descriptor set.
2. Draw it in the opaque pass with a full-screen triangle, reconstructing the view ray
   from `InvViewProj` (`clouds.comp.slang:63-69` already does exactly this — reuse the
   code), depth test `eLessOrEqual`, depth write off, drawn last.
3. Use the same cubemap for the ambient/reflection terms currently stubbed as
   `globalBuffer.SkyColor` in `opaque.slang:162` and `weightedBlendedOIT.slang:173`
   (which is already marked `// TODO: replace with environment map`).

---

## 1.15 — **P2** Sync objects are not recreated when the swapchain image count changes

**Where** `src/main.cpp:2006-2035` (`CreateSyncObjects`), `2037-2081`
(`RecreateSwapchainAndRenderImages`)

**What**

`m_RenderCompleteSemaphores` is sized by `m_SwapImages.size()` and indexed by
`imageIndex` in `DrawFrame` (`main.cpp:545`, `550`).
`RecreateSwapchainAndRenderImages` recreates the swapchain — which can return a
different image count — but never calls `CreateSyncObjects()`.

Note `CreateSyncObjects` uses `emplace_back` without clearing, so simply calling it again
would *append*. It needs a `clear()` first.

**Why it matters**

If the new swapchain has more images, `m_RenderCompleteSemaphores[imageIndex]` is an
out-of-bounds `std::vector` access. Drivers do change image count across resize (notably
when entering/leaving fullscreen or when `minImageCount` clamps against
`maxImageCount` — see `ChooseSwapMinImageCount`, `src/Utility.h:97-107`).

**Fix**

```cpp
void CreateSyncObjects()
{
    m_RenderCompleteSemaphores.clear();
    for (size_t i = 0; i < m_SwapImages.size(); i++) { ... }
    // per-frame objects only need creating once; guard or leave in place
}
```

and call it from `RecreateSwapchainAndRenderImages()` after
`CreateSwapchainImageViews()`. Also update `m_MinImageCount` /
`ImGui_ImplVulkan_SetMinImageCount` if the count changed.

# Part 2 — Architecture & code structure

## 2.1 — **P2** `main.cpp` is 2,765 lines and is the whole engine

`main.cpp` currently contains: SDL init, instance/device/swapchain creation, three
pipeline builders, descriptor layout/pool/set management, all seven command-buffer
recorders, render-target management, the global uniform buffer layout, the ImGui editor
UI, the frame loop, input handling, and `main()`.

This is the single biggest brake on the project's velocity. Everything else in this
document is easier after this split.

**Suggested target layout** (incremental — do it one box at a time, keep it compiling):

```
src/
  Core/        Log, Timer, ThreadPool, MyMacros, SwapbackArray, Common
  RHI/         VulkanContext (instance/device/queues/debug messenger)
               Swapchain     (swapchain + views + recreate + format choice)
               Image, Buffer (wrap the free functions in Utility.h)
               PipelineBuilder
               DescriptorAllocator
  Renderer/    Renderer      (frame loop, submit, present)
               OpaquePass, TransparentPass, CompositePass, CloudPass, ImGuiPass
               FrameData, GlobalBuffer
  Scene/       Entity, SceneComponent, SceneGraph, Transform, Node, Camera, Lights
  Assets/      ResourceManager, *Loader, Model, ModelData, Mesh, Material, Texture, Cubemap
  Editor/      EditorUI (everything currently in DrawImGuiFrame)
  main.cpp     ~40 lines: init SDL, construct App, Run, catch
```

**Suggested order of extraction**, cheapest and safest first:

1. `DrawImGuiFrame` → `Editor/EditorUI.cpp`. Pure UI, almost no coupling. Immediately
   removes ~120 lines and gives you somewhere to put the debug controls you will want for
   the cloud parameters and tonemapping.
2. `GlobalBuffer` / `CameraData` / `LightData` structs → `Renderer/GlobalBuffer.h`. These
   must stay byte-compatible with `src/shaders/common.slangh` — see
   [4.1](#41--p2-share-one-source-of-truth-for-gpu-struct-layouts).
3. Pipeline creation → `RHI/PipelineBuilder`. See
   [2.2](#22--p2-the-three-pipeline-builders-are-90-duplicated).
4. Instance/device/surface/debug-messenger → `RHI/VulkanContext`.
5. Swapchain + views + `RecreateSwapchainAndRenderImages` → `RHI/Swapchain`.
6. The five `Record*CommandBuffer` functions → one class per pass, behind a common
   `IRenderPass { void Record(FrameContext&); }` interface. This is what makes adding
   shadow maps or a depth prepass a bounded change instead of another 200 lines in
   `main.cpp`.

## 2.5 — **P2** Headers are not self-contained

Multiple headers compile only because `pch.h` is force-included into every TU by
`target_precompile_headers(VulkanApp PRIVATE src/pch.h)`.

Examples:

| Header | Uses | Missing include |
|---|---|---|
| `SwapbackArray.h` | `std::ranges::find` | `<algorithm>` / `<ranges>` |
| `Utility.h` | `std::ranges::find_if`, `std::clamp`, `SDL_Window`, `std::format` | `<algorithm>`, `SDL3/SDL.h`, `<format>` |
| `Mesh.h` | `std::vector<Vertex>` in a signature | `<vector>` |
| `Utility.h` | `vk::raii::*` | `vulkan/vulkan_raii.hpp` (only `vulkan.hpp` is included) |
| `Entity.h` | `std::unique_ptr`, `std::vector`, `std::string` | `<memory>`, `<vector>`, `<string>` |

**Why it matters**

- The headers cannot be reused in a different target (a unit-test target, a tools
  executable) without dragging in the whole PCH.
- If someone reorders or trims `pch.h`, unrelated files break with confusing errors.
- clangd/IntelliSense can report false errors depending on whether it picks up the
  force-include.

**Fix**

Make every header compile standalone. A cheap way to enforce it going forward:

```cmake
# Optional check target: compiles every header on its own.
file(GLOB_RECURSE all_headers ${CMAKE_CURRENT_LIST_DIR}/src/*.h)
add_library(HeaderSelfContainmentCheck OBJECT EXCLUDE_FROM_ALL)
target_sources(HeaderSelfContainmentCheck PRIVATE ${all_headers})
set_source_files_properties(${all_headers} PROPERTIES LANGUAGE CXX)
target_link_libraries(HeaderSelfContainmentCheck PRIVATE
    SDL3::SDL3 Vulkan::Vulkan glm::glm-header-only assimp::assimp imgui::imgui)
target_include_directories(HeaderSelfContainmentCheck PRIVATE src ${Stb_INCLUDE_DIR})
# deliberately NO target_precompile_headers here
```

Keep the PCH for build speed on the real target — it is doing useful work there
(`vulkan.hpp` alone is enormous). Just don't let it hide missing includes.

## 2.6 — **P2** Fixed limits that are too low, and fail loudly rather than gracefully

| Limit | Where | Value | Problem |
|---|---|---|---|
| `MAX_INSTANCE_COUNT` | `main.cpp:27` | 1024 | `UpdateInstanceBuffer` **throws** on overflow (line 2472-2473), killing the app |
| `s_MAX_MATERIAL_SET_COUNT` | `MaterialFactory.cpp:10` | 100 | Sponza alone has ~25; two or three models and descriptor-set allocation fails |
| `s_MAX_TEXTURE_COUNT_PER_MAT` | `MaterialFactory.cpp:9` | 3 | Hard blocker for emissive / occlusion / clearcoat maps |
| `MAX_POINT_LIGHTS` / `MAX_DIR_LIGHTS` | `src/Common.h` | — | Silently clamped in `UpdateGlobalBuffer` (lines 2200-2220), no warning |

**Fix**

- Grow the instance buffer instead of throwing: on overflow, `waitIdle`, reallocate to
  `max(needed, capacity * 2)`, remap, log once. Or move to a `eStorageBuffer` +
  `shaderDrawParameters` (already enabled at `main.cpp:938`) and index by
  `SV_InstanceID`, which removes the vertex-input plumbing for 8 attributes entirely.
- Make the material descriptor pool growable: keep a `std::vector<vk::raii::DescriptorPool>`
  and allocate a new pool when the current one returns
  `eErrorOutOfPoolMemory`. This is a ~30-line `DescriptorAllocator` and is the standard
  solution.
- Log a warning (once) when lights are clamped, rather than silently dropping them.

## 2.7 — **P3** Smaller code-quality items

- **`main.cpp:531` / `530`** — the `// TODO: even when ImGui is not showing, it's being
  submitted` comment is correct. `RecordImGui` always begins/ends a render pass and is
  always submitted. Either skip the command buffer in the submit array when
  `!m_bCursorVisible`, or fold ImGui into the composite pass.
- **`main.cpp:1553-1655` (`CreateCommandBuffers`)** — 100 lines of seven identical
  seven-line blocks. Loop over a small table of `{pool, &FrameData::member, name}`.
- **`main.cpp:2479-2551` (`CreateRenderTargets`)** — three identical 22-line blocks. A
  `CreateRenderTarget(format, name)` helper returning a `Texture` collapses it to 6 lines.
- **`CubemapLoader.cpp:51-90`** — the 6-case `switch` mapping index → path is more code
  than the data. Give `CubemapCreateInfo` a `std::array<std::string, 6> FacePaths` (or a
  `GetFace(size_t)` accessor) and loop.
- **`Camera.h`** — `m_MoveSpeed` and `m_LookSens` have no in-class initialisers (only
  `Camera()` sets them). Add defaults so a future constructor can't forget.
- **`Entity.h:76`, `Model.h:19`** — `static constexpr Transform GetDefaultTransform()`
  is duplicated and just returns `Transform{}`. Delete both; `Transform{}` is clearer.
- **`Entity.h:22-71`** — the four `GetComponents`/`GetFirstComponent` overloads use
  `dynamic_cast` in a loop. Fine at current scale; if the scene grows, a type-id keyed
  map is the usual next step.
- **`ResourceManager.cpp:36`, `MaterialFactory.cpp:23`, `CloudSystem.cpp:20,47,289`,
  `PBRMaterial.cpp:37-40`, `Camera.h:32-33`, `SceneGraph.h:8-10`, `Drawable.h:13`** —
  tab/space mixing that clang-format would fix. Add a `.clang-format` and a
  `format` target so this stops recurring:
  ```cmake
  find_program(CLANG_FORMAT_EXE clang-format)
  if(CLANG_FORMAT_EXE)
    add_custom_target(format COMMAND ${CLANG_FORMAT_EXE} -i ${SOURCES}
                      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
  endif()
  ```
- **`XmlParser.cpp:281-282, 291-292`** — `append_attribute(...) = Vec3ToString(...)`
  passes a `std::string` where the sibling `WriteTransform` (lines 257-261) passes
  `.c_str()`. Make them consistent.
- **`XmlParser`: round-tripping transforms.** `SaveScene` writes a transform on both the
  `<entity>` and each `<model>` (lines 310, 268), and `LoadScene` applies both
  (lines 192, 84). Since `Model::GetDrawables()` multiplies by
  `GetAccumulatedTransform()`, confirm that save→load is idempotent and doesn't
  double-apply the entity transform.

---

# Part 3 — Rendering & performance

## 3.1 — **P1** No mipmaps anywhere

**Where** `src/Utility.h:216` (`.mipLevels = 1`), `src/Utility.h:198`
(`.levelCount = 1u`), `src/main.cpp:2372` (`.maxLod = 0.f`)

Every texture is created with a single mip level and the sampler is clamped to LOD 0,
despite `mipmapMode = eLinear` and full anisotropy being enabled.

**Why it matters**

- Severe aliasing and shimmering on any surface viewed at an angle or at distance —
  Sponza's floor and curtains will crawl badly as the camera moves.
- Anisotropic filtering does almost nothing without a mip chain.
- Every sample is a full-resolution texture fetch, so texture cache hit rates are poor.
  This is often a large fraction of fragment cost in a scene like Sponza.

**Fix**

In `TextureLoader::Load`:

1. `mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1`.
2. Add `vk::ImageUsageFlagBits::eTransferSrc` to the image usage.
3. After the buffer→image copy, generate the chain with `cmd.blitImage` in a loop,
   transitioning mip *i* to `eTransferSrcOptimal` and mip *i+1* to `eTransferDstOptimal`.
   Check `eSampledImageFilterLinear` in `getFormatProperties(format).optimalTilingFeatures`
   first.
4. `CreateImageView` needs `levelCount = mipLevels` (add a parameter).
5. Sampler `maxLod = VK_LOD_CLAMP_NONE`.

Do the same for cubemap faces once [1.1](#11--p0-cubemaps-only-ever-upload-and-transition-face-0)
is fixed.

Alternative worth considering: a compute-shader downsample pass, which avoids the
`blitImage` format-support caveat and is faster for large atlases. The `blit` route is
simpler and correct — start there.

## 3.2 — **P1** Every texture upload does a full `queue.waitIdle()`

**Where** `src/Utility.h:164-172`

```cpp
inline void EndSingleTimeCommand(vk::CommandBuffer commandBuffer, vk::Queue queue)
{
    commandBuffer.end();
    vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();      // full pipeline drain, per resource
}
```

Called once per texture, once per cubemap, twice per model (vertex + index buffer), and
once for the noise bake. Sponza has ~25 materials × up to 3 textures ≈ 70 full GPU
drains during load, and the same on every scene switch.

**Fix**

Batch uploads behind a small `UploadContext`:

```cpp
class UploadContext
{
public:
    vk::raii::CommandBuffer& Begin();            // reuse one cmd buffer
    void Flush();                                // submit once, wait on one fence
    void KeepAlive(vk::raii::Buffer, vk::raii::DeviceMemory);  // staging lifetime
};
```

Record all transitions and copies for a whole model (or whole scene load) into one command
buffer, submit once, wait on one fence, then release the staging buffers. Expect load
times to drop by an order of magnitude.

While in here: use a **dedicated transfer queue** if the device exposes one.
`CreateLogicalDevice` (`main.cpp:899-975`) currently requests a single queue and
`ResourceManager::Init` is handed `m_GraphicsQueue`
(`main.cpp:332-333`). `CloudSystemCreateInfo` even has a
`// TODO: find and store a dedicated compute queue` (line 350-351). Enumerating and
storing graphics/compute/transfer families is a contained change with a good payoff.

## 3.3 — **P3** Asset loading blocks the frame loop

**Where** `src/main.cpp:623-649` (the `LoadSceneDlg` handler)

```cpp
m_Device.waitIdle();
std::unique_ptr<SceneGraph> tempSceneGraph = XmlParser::LoadScene(path);
```

Assimp import, image decode, staging, upload and ~70 `waitIdle`s all happen inside the
ImGui frame. The window is frozen (and marked "not responding" by Windows) for the
duration.

The `ThreadPool` already exists and is currently used only for command-buffer recording.

**Fix**

1. Dispatch `XmlParser::LoadScene`'s CPU work (assimp parse, `stbi_load`) to the thread
   pool, returning a `std::future<std::unique_ptr<SceneGraph>>`.
2. Poll the future in the frame loop; keep rendering the old scene meanwhile, with a
   progress indicator.
3. Perform GPU uploads on the main thread from a queue of completed CPU work (or on a
   dedicated transfer queue with its own command pool — Vulkan command pools are not
   thread-safe, so one pool per thread).

The existing comment at `main.cpp:634-639` about loading-before-unloading is good
thinking and stays valid.

## 3.4 — **P3** Batches, instance data and the global buffer are rebuilt from scratch every frame

**Where** `src/main.cpp:232` (`ModelManager::Get()->GenerateBatches()`),
`src/ModelManager.cpp:21-80`, `src/Model.cpp:19-29`

Every single frame, unconditionally:

```cpp
m_Drawables.clear();  m_InstanceDatas.clear();
m_OpaqueBatches.clear();  m_TransparentBatches.clear();

for (Model* pModel : m_Models)
{
    const std::vector<Drawable> drawables = pModel->GetDrawables();  // allocates a vector
    m_Drawables.insert(...);                // copies it
}
std::sort(m_Drawables.begin(), m_Drawables.end());                // full re-sort
// then a full pass computing glm::transpose(glm::inverse(transform)) per instance
```

Costs: one heap allocation per model per frame, a full copy of every drawable, a full
`std::sort` of every drawable, and a 4×4 matrix inverse per instance per frame — all for a
scene that is usually static.

**Fix**

1. **Dirty flag.** `ModelManager` regenerates only when a `Model` is added, removed, or
   its transform changes. `SceneComponent` transforms already go through
   `GetTransform()`, so a `MarkDirty()` there is straightforward.
2. **`Model::GetDrawables()` should not allocate.** Change it to
   `void AppendDrawables(std::vector<Drawable>& out) const` and `reserve` once in
   `GenerateBatches`.
3. **Cache the normal matrix.** `glm::transpose(glm::inverse(m))`
   (`ModelManager.cpp:63-64`) is expensive and only changes when the transform does.
   Compute it in `ModelData`/`Model` when the transform is set. Better: upload a `3x3`
   (padded) instead of a full `4x4` — the shader already casts to `float3x3`
   (`opaque.slang:70`) and there is a `// TODO: only upload 3x3 matrix` at
   `opaque.slang:48`. That halves the per-instance vertex-attribute bandwidth and frees
   4 of the 8 instance attribute slots.
4. **Frustum culling.** There is none. Add AABBs per mesh (assimp gives you
   `aiMesh::mAABB` with `aiProcess_GenBoundingBoxes`), transform to world, and test
   against the six frustum planes extracted from `viewProj` before emitting a drawable.
   With Sponza this alone is usually a large win when the camera is inside the atrium.

## 3.5 — **P3** No pipeline cache

**Where** `main.cpp:1190`, `1348`, `1471`, `CloudSystem.cpp:192`, `215` — all pass
`nullptr` for the `vk::PipelineCache`.

Five pipelines are compiled from scratch on every launch. Also
`initInfo.PipelineCache = VK_NULL_HANDLE` in `InitImGui` (`main.cpp:305`).

**Fix**

Create one `vk::raii::PipelineCache` at startup, seeded from a file
(e.g. `%LOCALAPPDATA%/VulkanApp/pipeline_cache.bin`), pass it to every
`vk::raii::Pipeline` constructor, and `getData()` → write to disk at shutdown. Roughly 30
lines for a noticeable startup improvement, and it scales as you add pipelines.

## 3.6 — **P3** Missing rendering features, in rough order of visual impact

These are features, not bugs — listed so the priorities are explicit.

| Feature | Why | Rough size |
|---|---|---|
| **Shadow maps** | Biggest single visual upgrade. Directional light → cascaded shadow maps; the OIT and opaque passes both just need a shadow lookup. | Large |
| **IBL / environment lighting** | `ambient = 0.1f * SkyColor * albedo * ao` (`opaque.slang:162`) is a flat hack. A prefiltered cubemap + BRDF LUT makes PBR materials actually read as metal/dielectric. The skybox cubemap is already loaded ([1.14](#114--p2-the-skybox-is-loaded-but-never-rendered)). | Medium-large |
| **Reverse-Z depth** | `NEAR_PLANE = 0.1f`, `FAR_PLANE = 10000.f` (`main.cpp:29-30`) is a 100,000:1 ratio — severe z-fighting at distance. Flip to `eGreater` + clear to 0 + swap near/far in `glm::perspective`. `GLM_FORCE_DEPTH_ZERO_TO_ONE` is already set, so this is a small, contained change. | Small |
| **Depth prepass** | Sponza has heavy overdraw and the PBR fragment shader is expensive. A depth-only prepass with `eEqual` in the main pass is a straightforward win. Also gives the cloud pass complete depth earlier. | Small-medium |
| **AO (SSAO/GTAO)** | `pcMatData.AO` is a per-material constant; there is no screen-space AO. | Medium |
| **Bloom** | You already render to `eR16G16B16A16Sfloat` and tonemap. Bloom is cheap to add and makes HDR lighting read correctly. | Small-medium |
| **Anti-aliasing** | `rasterizationSamples = e1` everywhere and no post-process AA. FXAA in the composite pass is ~40 lines; TAA needs motion vectors. | Small (FXAA) |
| **Transparent sorting hint** | Weighted-blended OIT is order-independent, which is the point — but it is an approximation. If specific objects need correctness, a per-object flag routing them to a sorted back-to-front pass is a useful escape hatch. | Medium |

## 3.7 — **P3** Cloud shader cost

**Where** `src/shaders/clouds.comp.slang:114-150`

Two issues in the march loop:

```cpp
for (uint i = 0u; i < pc.viewStepCount; i++)
{
    float3 samplePos = rayOrigin + rayDir * t;
    float density = SampleDensity(samplePos);        // 2 texture fetches

    float sunTNear, sunTFar;
    IntersectHeightSlab(samplePos, sunDir, ...);      // computed unconditionally
    sunTNear = max(sunTNear, 0.f);
    float sunStepSize = (sunTFar - sunTNear) / float(pc.sunStepCount);
    float sunT = sunTNear;

    if (density > 0.f) { /* sun march */ }
    ...
}
```

1. **The sun-slab setup runs even when `density == 0`.** Move
   `IntersectHeightSlab` and the `sunStepSize` computation inside the
   `if (density > 0.f)` block. Free win.
2. **`SampleDensity` costs two 3D texture fetches** — one for the density, one for the
   boundary perturbation (lines 35and 39). The boundary noise only depends on `pos.xz`,
   so it is constant along a vertical ray and could be hoisted out of the march for
   near-vertical rays, or baked into a separate 2D texture.

Additional notes:

- **`DirLights[0]` is read unconditionally** (lines 106-108) without checking
  `globalBuffer.Lights.DirLightCount > 0`. With no directional light in the scene,
  `sunDir = normalize(-float3(0))` is a NaN, which propagates into `phase` and then into
  the output image. Guard it, or early-out the whole dispatch.
- **`sunTFar` can be `-inf`/`+inf`** when `sunDir.y` is near zero
  (`IntersectHeightSlab` divides by `dir.y`, lines 24-25). The viewray has an
  `if (... || rayDir.y == 0.f)` guard (line 96) but the sun ray has none, and `== 0.f` is
  too strict anyway — use an epsilon on `abs(dir.y)` for both.
- **The quarter-res output is upsampled with a plain bilinear `Sample`**
  (`composite.slang:35`) with no depth-aware filtering, so clouds bleed across geometry
  silhouettes. A bilateral/nearest-depth upsample fixes the halos.
- **Temporal reprojection** with a 4- or 16-frame Bayer offset would let you cut
  `viewStepCount` substantially. This is how production volumetric clouds are affordable.
- The push-constant parameters (`windVelocity`, `minHeight`, `maxHeight`, `coverage`,
  `anisotropy`, `boundaryDisplacement`, `viewStepCount`, `sunStepCount`) are never
  exposed in the ImGui panel. `m_CloudData` is pushed from
  `CloudSystem::RecordDispatch` (line 353-354) but nothing writes it. Add sliders — you
  cannot tune clouds without them.

## 3.8 — **P3** Document the matrix convention (it is currently correct but inconsistent)

**Where** `src/main.cpp:2180`, `2195`, `2197-2198`; `src/shaders/opaque.slang:1-3, 66-67`;
`src/shaders/clouds.comp.slang:63-64`

Two different conventions are in use:

```cpp
// main.cpp — View and Proj are transposed on upload
m_GlobalBuffer.CamData.Proj = glm::transpose(colMajProj);
m_GlobalBuffer.CamData.View = glm::transpose(view);

// ...but InvViewProj is not
m_GlobalBuffer.CamData.InvViewProj =
    glm::inverse(glm::transpose(m_GlobalBuffer.CamData.Proj) * view);
```

and correspondingly in the shaders:

```hlsl
// opaque.slang — vector-first
o.Pos = mul(mul(worldPos, globalBuffer.Camera.View), globalBuffer.Camera.Proj);

// clouds.comp.slang — matrix-first
float4 nearPoint = mul(globalBuffer.Camera.InvViewProj, float4(ndc, 0.f, 1.f));
```

The two combinations (transpose-on-upload + `mul(v, M)`) and (no-transpose +
`mul(M, v)`) are mathematically equivalent, so **this is not a bug** — but it means a
reader has to re-derive the convention for every matrix, and the `Node.cpp:26`
`// TODO: this is already transposed somehow` comment suggests it has already cost you
time.

**Fix**

Pick one convention, apply it to all matrices, and state it once. The file header comment
in `opaque.slang:1-3` is the right place; extend it and reference it from
`common.slangh`. Then either transpose `InvViewProj` too and use `mul(v, M)` in
`clouds.comp.slang`, or stop transposing `View`/`Proj` and use `mul(M, v)` everywhere.
Consider passing `-matrix-layout-row-major` (or column-major) to `slangc` explicitly in
`CMakeLists.txt` so the layout does not depend on the compiler default.

---

# Part 4 — Shaders

## 4.1 — **P2** Share one source of truth for GPU struct layouts

`src/shaders/common.slangh:3-45` declares `PointLight`, `DirLight`, `LightData`,
`CameraData` and `GlobalBuffer`. `src/main.cpp:49-78` declares the same five structs
again in C++. They are kept in sync by hand.

`common.slangh` already does `#include "../Common.h"` for `MAX_POINT_LIGHTS` /
`MAX_DIR_LIGHTS`, so the mechanism exists.

**Why it matters**

The comment at `main.cpp:69-71` explains std140/std430 alignment rules, and there is a
runtime guard (`main.cpp:2144-2146`) that `sizeof(GlobalBuffer) % 16 == 0`. But a
mismatch in *field order* or a differently-sized padding member passes that check and
produces garbage lighting that looks like a shader bug.

**Fix**

Move the shared structs into a header included by both, using a small compatibility
shim:

```c
// src/shaders/SharedTypes.h— included from C++ and from Slang
#ifdef __cplusplus
    #include "glm/glm.hpp"
    using float2 = glm::vec2;
    using float3 = glm::vec3;
    using float4 = glm::vec4;
    using float4x4 = glm::mat4;
    using uint = uint32_t;
#endif

struct PointLightData { float3 Color; float Intensity; float3 Pos; float Padding; };
// ... etc
```

Then add `static_assert(sizeof(GlobalBuffer) == <expected>)` and `offsetof` assertions on
the C++ side so a divergence is a compile error.

## 4.2 — **P2** `opaque.slang` and `weightedBlendedOIT.slang` duplicate ~130 lines

The two files are byte-identical from line 7 (`struct MaterialPushConstant`) through
line ~165: the same push-constant struct, the same `VS_In`/`VS_Out`, the same `vertMain`,
the same three texture bindings, the same albedo/metallic/roughness sampling, the same
TBN normal mapping, the same two-sided normal flip, and the same two light loops.

They diverge only in the fragment output: `opaque` returns `float4(color, albedo.a)`,
`weightedBlendedOIT` computes the weight and writes `Accum`/`Revealage`.

**Why it matters**

Every material or lighting change has to be made twice, correctly. This is exactly the
kind of duplication that produces "transparency looks different from opaque" bugs later.

**Fix**

Extract `src/shaders/surface.slangh`:

```hlsl
// surface.slangh
struct MaterialPushConstant { /* ... */ };
[[vk::push_constant]] MaterialPushConstant pcMatData;

[[vk::binding(0, 1)]] Sampler2D albedoTex;
[[vk::binding(1, 1)]] Sampler2D normalTex;
[[vk::binding(2, 1)]] Sampler2D metallicRoughnessTex;

struct VS_In  { /* ... */ };
struct VS_Out { /* ... */ };

VS_Out TransformVertex(VS_In v);

struct SurfaceSample
{
    float4 Albedo;
    float3 N;
    float3 V;
    float  Metallic;
    float  Roughness;
    float  AO;
};
SurfaceSample SampleSurface(VS_Out f);
float3 ShadeSurface(SurfaceSample s);   // both light loops + ambient
```

`opaque.slang` becomes ~20 lines and `weightedBlendedOIT.slang` ~35.

Also worth cleaning while you are there:

- `VS_Out::Color : TEXCOORD1` (`opaque.slang:56`, `weightedBlendedOIT.slang:55`) is
  declared and interpolated in both shaders but never written or read. Delete it — it is
  wasted interpolator bandwidth in the hottest shader in the frame.
- `weightedBlendedOIT.slang:178-181`: `transmit` is a hardcoded `float3(0,0,0)`, so
  `premultipliedReflect.a *= 1.f - clamp(0, 0, 1)` is a no-op multiply by 1. Either wire
  up per-material transmission or delete the dead arithmetic and the `transmit`
  declaration.
- `weightedBlendedOIT.slang:188`: the commented-out `b /= sqrt(1e4 * abs(csZ));` refers to
  `csZ`, which does not exist in this shader (the reference implementation's camera-space
  Z). If you ever enable it you will need to pass view-space depth through `VS_Out`. Note
  that in the comment so future-you doesn't chase it.
- `clouds.comp.slang` includes `pbr.slangh` (line 2) but only uses
  `HenyeyGreenstein`. `composite.slang` includes it (line 2) for `HillACES` only. Consider
  splitting `pbr.slangh` into `brdf.slangh` (Fresnel/GGX/Smith) and
  `tonemap.slangh` + `phase.slangh`, so a change to the BRDF doesn't force a recompile of
  the clouds and composite shaders.

## 4.3 — **P3** Shader compilation ergonomics

- Slang diagnostics are not surfaced usefully because compilation happens as a
  `POST_BUILD` step of a dummy target — see
  [5.4](#54--p1-shader-compilation-is-not-part-of-the-dependency-graph).
- There is no `-warnings-as-errors` (or equivalent) on the `slangc` command line, so
  shader warnings scroll past.
- Consider adding `-fvk-invert-y` or documenting why the manual `colMajProj[1][1] *= -1`
  approach was chosen instead; right now the reason lives in three duplicated comments in
  `main.cpp` about `frontFace`.

---

# Part 5 — Build system & tooling

Some of the items from the earlier `compile_commands.json` discussion are already
applied — `CMakeLists.txt:12-22` now guards `file(CREATE_LINK)` on
`CMAKE_GENERATOR MATCHES "Ninja|Makefiles"` with a `RESULT` variable, and lines 125-134
now select `/MP` for the Visual Studio generator vs
`MSVC_DEBUG_INFORMATION_FORMAT "Embedded"` (`/Z7`) otherwise. Good. The following are
still outstanding.

## 5.2 — **P1** `ENABLE_SANITIZERS` passes GCC/Clang flags to MSVC

**Where** `CMakeLists.txt:24-30`

```cmake
option(ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)

if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=undefined -g)
  add_link_options(-fsanitize=address,undefined)
endif()
```

Two problems:

1. This block sits **before** `project()` (line 32), so no compiler has been detected yet
   and `MSVC` is not defined. Guarding on it here is impossible.
2. `cl.exe` does not accept any of these flags. MSVC's ASan is `/fsanitize=address` and
   it has no UBSan.

**Fix**

Move the block after `project()` and branch:

```cmake
option(ENABLE_SANITIZERS "Enable ASan (and UBSan where supported)" OFF)

if(ENABLE_SANITIZERS)
  if(MSVC)
    add_compile_options(/fsanitize=address /Zi)
    # MSVC ASan requires the dynamic CRT and is incompatible with /RTC and /INCREMENTAL
    add_link_options(/INCREMENTAL:NO)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                        -fno-sanitize-recover=undefined -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()
```

Given bugs [1.2](#12--p0-staging-buffers-request-the-wrong-memory-property-flags),
[1.3](#13--p0-materialdetectblendmode-reads-an-uninitialised-float) and
[1.5](#15--p0-modeldataregistermesh-hands-out-pointers-that-resize-invalidates), getting
a sanitizer running is high value. If MSVC ASan proves awkward, a
`ninja-asan-linux`/`ninja-asan-macos` preset used occasionally is enough — these bugs are
platform-independent.

## 5.3 — **P2** `project(... LANGUAGES CXX)` — good; a few related notes

`CMakeLists.txt:32-35` already restricts to `CXX`, which skips C compiler detection.
Remaining items in the same area:

- **`CMakePresets.json`**: the `msvc` preset still uses the `Visual Studio 17 2022`
  generator, which cannot emit `compile_commands.json`. Add the Ninja presets discussed
  previously (`windows-ninja-base` → `ninja-debug-windows` / `ninja-release-windows`,
  generator `Ninja`, `CMAKE_CXX_COMPILER: cl`) so the LSP has a compilation database on
  Windows. Keep the Visual Studio preset for `.sln` generation via `GENERATE_SLN.bat`.
- **Shared `VCPKG_INSTALLED_DIR`**: not required, but pointing multiple build trees at one
  `vcpkg_installed` saves ~1 GB per tree. Only one configure may hold the lock at a time.
- **`target_link_options` typo**: `CMakeLists.txt:145` has
  `$<$<CONFIG:Release>:/DEBUG >` — note the trailing space inside the genex, which
  produces the argument `"/DEBUG "`. Harmless with `link.exe` but remove it.
- **`VS_DEBUGGER_WORKING_DIRECTORY`** (`CMakeLists.txt:207-208`) only affects the Visual
  Studio debugger. Under Ninja you must launch from the source directory, because asset
  paths are CWD-relative (`"shaders/opaque.spv"`, `"models/sponza/Sponza.gltf"`,
  `"textures/skybox/right.jpg"`, `"scenes/"`). Two options:
  - Add a `launch.json` / debugger working directory for your editor, **and**
  - Better: resolve assets against an explicit content root instead of the CWD. A
    `Paths::Content()` helper that prefers an env var, then the executable's directory,
    then the source dir, removes a whole class of "works in VS, crashes from the
    terminal" reports.

## 5.5 — **P2** `.clangd`, `.clang-format`, `.editorconfig`

- **`.clangd`** — with `cl.exe`-generated `compile_commands.json`, clangd chokes on
  MSVC-specific PCH flags. Add:
  ```yaml
  CompileFlags:
    Remove: [/Yu*, /Fp*, /Yc*, /FI*]
  ```
  If the `compile_commands.json` symlink at the repo root can't be created (Windows
  requires Developer Mode or admin for symlinks), point clangd at the build directory
  instead with `--compile-commands-dir=build/ninja-debug`.
- **`.clang-format`** — the codebase is already consistently 4-space, Allman braces,
  80-column, `m_`/`s_`/`g_`/`b`-prefixed names. Codify it so the tab/space drift noted in
  [2.7](#27--p3-smaller-code-quality-items) stops.
- **`.editorconfig`** — trailing-whitespace and final-newline rules catch the rest.

## 5.6 — **P3** No tests, no CI

There is currently no way to know that a refactor of `ResourceManager` or `ModelManager`
broke something except by running the app and looking at it.

The pure-logic pieces are testable with no GPU at all:

- `SwapbackArray` — `RemoveAt`, `Erase`, iterator invalidation
- `ThreadPool` — submit/await, shutdown with pending jobs, the `hardware_concurrency() <= 1`
  case from [1.8](#18--p1-threadpoolinit-underflows-when-hardware_concurrency-returns-0)
- `Transform` / `Node::ToMat4` — matrix composition and the transpose convention
- `XmlParser` — save→load round-trip on a synthetic `SceneGraph`
- `ModelManager::GenerateBatches` — batch boundaries and instance ordering, given fake
  `Drawable`s
- `Material::DetectBlendMode` — the fix from
  [1.3](#13--p0-materialdetectblendmode-reads-an-uninitialised-float)

vcpkg already manages your dependencies, so adding `catch2` or `gtest` to `vcpkg.json`
plus a `VulkanAppTests` target is a small step. A GitHub Actions matrix
(windows-latest + ubuntu-latest, configure + build + test) would then catch the
platform-specific breakage that currently only shows up when you switch machines.

---

# Part 6 — Prioritised work order

Ordered so that each slice is independently shippable and low-risk, and so that the
debugging tools land before the hard bugs.

### Priority 1 — Turn on the tools (½ day)

Do this first. It changes how expensive everything after it is.

2. Fix the `ENABLE_SANITIZERS` MSVC branch ([5.2](#52--p1-enable_sanitizers-passes-gccclang-flags-to-msvc)); get one ASan-capable preset working somewhere.
3. Add the Ninja/MSVC presets + `.clangd` so the LSP works ([5.3](#53--p2-project-languages-cxx--good-a-few-related-notes), [5.5](#55--p2-clangd-clang-format-editorconfig)).

### Priority 3 — Synchronisation and resize correctness (1 day)

15. `eComputeShader` in the depth barrier ([1.11](#111--p1-the-cloud-compute-shaders-depth-read-is-not-synchronised)).
17. Recreate sync objects on image-count change ([1.15](#115--p2-sync-objects-are-not-recreated-when-the-swapchain-image-count-changes)).

### Priority 4 — Model loading correctness (1–2 days)

19. Guard `ModelData::Init` against invalid meshes ([1.6](#16--p0-modeldatainit-will-throw-or-crash-on-sparse-mesh-indices)).

### Priority 5 — Mipmaps and upload batching (2–3 days)

21. `UploadContext` to batch transfers and kill the per-resource `waitIdle` ([3.2](#32--p1-every-texture-upload-does-a-full-queuewaitidle)).
22. Mip generation + `maxLod` + `levelCount` ([3.1](#31--p1-no-mipmaps-anywhere)).

Big, immediately visible quality and load-time improvement.

### Priority 6 — First architecture slice (3–5 days)

26. Extract `Editor/EditorUI` out of `main.cpp` ([2.1](#21--p2-maincpp-is-2765-lines-and-is-the-whole-engine), step 1).
27. `surface.slangh` to de-duplicate the two surface shaders ([4.2](#42--p2-opaqueslang-and-weightedblendedoitslang-duplicate-130-lines)).
28. `SharedTypes.h` + `static_assert`s for GPU struct layouts ([4.1](#41--p2-share-one-source-of-truth-for-gpu-struct-layouts)).

This is the point at which the codebase stops fighting you.

### Priority 7 — Descriptor and resource management (3–5 days)

29. Growable `DescriptorAllocator`, remove the 100-material-set ceiling ([2.6](#26--p2-fixed-limits-that-are-too-low-and-fail-loudly-rather-than-gracefully)).
31. Growable instance buffer, or move to an SSBO + `SV_InstanceID`.

### Priority 8 — Frame-time work (3–5 days)

32. Dirty-flag batch regeneration, cached normal matrices, non-allocating `AppendDrawables` ([3.4](#34--p3-batches-instance-data-and-the-global-buffer-are-rebuilt-from-scratch-every-frame)).
33. Frustum culling with per-mesh AABBs.
34. Pipeline cache ([3.5](#35--p3-no-pipeline-cache)).
35. Reverse-Z ([3.6](#36--p3-missing-rendering-features-in-rough-order-of-visual-impact)) — small change, large quality win at `FAR_PLANE = 10000`.
36. Cloud shader: hoist the sun-slab setup, guard `DirLights[0]`, epsilon on `dir.y`, expose the parameters in ImGui ([3.7](#37--p3-cloud-shader-cost)).

### Priority 9 — Async loading (3–5 days)

37. Move scene loading onto the thread pool with a progress UI ([3.3](#33--p3-asset-loading-blocks-the-frame-loop)).
38. Enumerate and use a dedicated transfer queue (and a dedicated compute queue for the clouds).

### Priority 10 — Features (open-ended)

39. Finish the skybox and use it for IBL ([1.14](#114--p2-the-skybox-is-loaded-but-never-rendered), [3.6](#36--p3-missing-rendering-features-in-rough-order-of-visual-impact)).
40. Shadow maps (cascaded, for the directional light).
41. Depth prepass, SSAO, bloom, FXAA.
42. Tests + CI ([5.6](#56--p3-no-tests-no-ci)).
