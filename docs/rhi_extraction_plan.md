# Stage 5 — RHI Extraction: Implementation Plan

> **Temporary document.** It exists to carry Stage 5 (steps 24–34 of the architecture plan)
> and is scheduled for deletion when the stage completes. See [§10 Retirement](#10-retirement)
> for what to keep and what to throw away.

**Created:** 16 August 2026 · **Supersedes:** `architecture_plan.md` Part IV,
steps 24–34 · **Status:** in progress — see the [progress table](#progress)

---

## Table of contents

1. [Purpose and authority](#1-purpose-and-authority)
2. [Design decisions](#2-design-decisions)
3. [Module layout](#3-module-layout)
4. [How the boundary is enforced](#4-how-the-boundary-is-enforced)
5. [The step sequence](#5-the-step-sequence) — [progress table](#progress)
6. [Mapping back to Part IV](#6-mapping-back-to-part-iv)
7. [Out of scope](#7-out-of-scope)
8. [D3D12 readiness checklist](#8-d3d12-readiness-checklist)
9. [Risks](#9-risks)
10. [Retirement](#10-retirement)

---

## 1. Purpose and authority

Stage 5 turns the Vulkan code currently spread across `src/main.cpp`, `src/Utility.h` and the
resource classes into `Engine::RHI`, a library the rest of the engine talks to without
including a Vulkan header.

Part IV's steps 24–34 describe that extraction for a Vulkan-only engine: `IPresentTarget`
returns `vk::Image`, `PipelineBuilder` takes `vk::Format`, and resources stay RAII objects
holding Vulkan handles. Since a D3D12 backend is now an explicit goal, this document
re-plans the stage so that the public API is backend-neutral **from the start**, rather than
being written twice.

**For the duration of Stage 5 this document is the authority.** Where it disagrees with
Part IV steps 24–34, this document wins. Everything outside steps 24–34 — the target module
graph (§8), the directory layout (§9), the headless seams (§10), the test strategy
(Part III) — is unchanged and still governed by the architecture plan.

**Cost of the change of course:** Part IV budgeted ~2 weeks for Stage 5's 11 steps. This plan
has 17 steps and is closer to **~3 weeks**. The extra week buys a public API that Stages 6–10
can be written against once instead of twice.

---

## 2. Design decisions

Each decision records what was chosen, why, and what it costs. `D` numbers are referenced by
the steps in §5.

### D0 — Namespace `Rhi`, directory `rhi/`, include as `<rhi/Device.h>`

The architecture plan writes `rhi::Device`. This codebase spells namespaces `Log`,
`JobSystemDetail` — PascalCase — so the C++ namespace is `Rhi` while the include directory
stays lowercase to match `engine_module`'s convention (`<core/Timer.h>`, `<platform/Paths.h>`).

A namespace is mandatory here rather than optional: `Rhi::Texture`, `Rhi::Format` and
`Rhi::Device` are names that would otherwise collide with `src/Texture.h` and with anything
D3D12's headers drag in later.

### D1 — Neutral public API; Vulkan confined to the backend

`engine/rhi/include/rhi/*.h` contains no Vulkan type, no VMA type and no Vulkan header
include. Vulkan lives in:

- `engine/rhi/src/vulkan/` — the implementation, invisible outside the module.
- `engine/rhi/include/rhi/vulkan/` — a **transitional, explicitly-scoped** area of headers
  that do expose Vulkan. During Stage 5 this is where moved code lands before it is
  converted; at the end of the stage it retains only the deliberate escape hatch (D9).

That gives a mechanically checkable end state: `src/` may include `rhi/vulkan/...` only from
the ImGui backend glue. See §4.

**Cost:** every enum used at an API boundary needs a neutral counterpart and a conversion
table. That's ~1 day of typing plus the tests that keep the tables honest.

### D2 — Resources are 32-bit handles, not RAII objects

`Device::CreateBuffer` returns a `Rhi::BufferHandle` — an index + generation packed into a
`uint32_t`, following the `Handle<Tag>` template already specified in architecture plan
§11.1. The device owns the backing storage in a `HandlePool`; `Destroy(handle)` bumps the
slot's generation.

Why, in order of weight:

1. Once Vulkan types are confined to the backend (D1), a public RAII object cannot hold a
   `VkBuffer` member. The remaining options — `unique_ptr<IBuffer>` (vtable + heap allocation
   per resource), pImpl (allocation + indirection), or an opaque fixed-size blob (per-backend
   size constant) — all pay a real cost to hide the backend. A handle hides it for free.
2. Use-after-free of a GPU resource becomes a detected generation mismatch that can be
   logged, instead of undefined behaviour. That matters right now: `ResourceCache` holds
   `weak_ptr`s and `PBRMaterial` holds `shared_ptr<Texture>`, so texture lifetime is already
   refcounted, and `ResourceManager` / `ModelManager` / `MaterialFactory` are still singletons
   with unspecified destruction order relative to the device until Stage 7.
3. `core/Handle.h` and `core/HandlePool.h` are already in the target layout for `Core`
   (architecture plan §9), and Stages 9–10 assume 32-bit identities for `FrameSnapshot`, sort
   keys and bindless indices. Building them now is work pulled forward, not work added — and
   it is pure CPU code, so it lands in the `unit` tier.

**Handle layout** follows §11.1 exactly — `index:24 | generation:8`, `kInvalid = 0xFFFFFFFF`.
Eight generation bits wrap after 256 reuses of one slot; the free list is FIFO, so a slot is
not reused until every other free slot has been, which makes an aliasing collision require
256 full cycles of the pool. Acceptable, and worth a comment in `HandlePool.h` saying so.

**Cost, and the mitigation:** handles are worse for scope-local resources (staging buffers,
readback buffers) because every exit path must call `Destroy`. `Rhi::UniqueHandle<H>` — a
~30-line move-only wrapper holding `IDevice*` + handle — covers those cases. Handles as the
ABI with RAII sugar on top works; the reverse does not.

Note that RAII is *not* banished: `VulkanBuffer` inside the backend can hold `vk::raii`
members and be destroyed by its pool. D2 is about what crosses the seam.

### D3 — Virtual interfaces at object granularity, not compile-time backend selection

`Rhi::IDevice`, `Rhi::ICommandList` and (Stage 6) `Rhi::IPresentTarget` are abstract; a free
function `Rhi::CreateDevice(const DeviceDesc&)` returns `std::unique_ptr<IDevice>`.

Alternatives considered: a compile-time `using Device = VulkanDevice;` typedef removes the
vtable but makes it impossible for a null/recording backend to coexist with Vulkan in one
test binary, which the contract test tier (architecture plan §15.2) needs; and it leaves a
future runtime `--rhi d3d12` flag impossible without another rewrite.

The overhead is a vtable dispatch on calls that are already crossing into a driver. Resource
creation happens hundreds of times per run, not per frame. `ICommandList` calls are the only
hot ones, and Stage 8's frame graph moves recording toward per-batch and eventually indirect
draws, which collapses the call count regardless.

**This is the decision most worth revisiting if profiling later disagrees** — it is contained
to the RHI boundary and does not affect D1 or D2.

### D4 — Barriers are the neutral (Stage, Access, Layout) triple

Vulkan `VK_KHR_synchronization2` splits a barrier into `VkPipelineStageFlags2`,
`VkAccessFlags2` and `VkImageLayout`. D3D12 Enhanced Barriers splits it into
`D3D12_BARRIER_SYNC`, `D3D12_BARRIER_ACCESS` and `D3D12_BARRIER_LAYOUT` — "three enums
operating independently, replacing the monolithic `D3D12_RESOURCE_STATE`"
([DirectX-Specs, Enhanced Barriers](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)).

So the RHI exposes the same three-way split:

```cpp
// rhi/Barrier.h
namespace Rhi
{
enum class PipelineStage : uint32_t   // → VkPipelineStageFlags2 / D3D12_BARRIER_SYNC
{
    None = 0, Draw = 1 << 0, VertexStage = 1 << 1, PixelStage = 1 << 2,
    ComputeStage = 1 << 3, DepthStencil = 1 << 4, RenderTarget = 1 << 5,
    Copy = 1 << 6, Resolve = 1 << 7, AllGraphics = 1 << 8, All = 1 << 9,
};

enum class AccessFlags : uint32_t     // → VkAccessFlags2 / D3D12_BARRIER_ACCESS
{
    None = 0, VertexBufferRead = 1 << 0, IndexBufferRead = 1 << 1,
    ConstantBufferRead = 1 << 2, ShaderRead = 1 << 3, UnorderedAccess = 1 << 4,
    RenderTargetWrite = 1 << 5, DepthStencilRead = 1 << 6, DepthStencilWrite = 1 << 7,
    CopySrc = 1 << 8, CopyDst = 1 << 9,
};

enum class TextureLayout : uint32_t   // → VkImageLayout / D3D12_BARRIER_LAYOUT
{
    Undefined, Common, RenderTarget, ShaderResource, UnorderedAccess,
    DepthStencilWrite, DepthStencilRead, CopySrc, CopyDst, Present,
};
}
```

Today's `src/Barrier.h` already has the right *shape* — named preset functions such as
`UndefinedToTransferDst()` and `TransferDstToShaderRead()` returning an `ImageBarrierDesc`.
Those presets survive verbatim, re-expressed in neutral terms in `rhi/BarrierPresets.h`; only
the field types change. This is the single cheapest portability win in the stage, and it is
cheap precisely because the existing code already used sync2 rather than legacy barriers.

Two caveats to record honestly:

- The DirectX spec does **not** claim Vulkan parity, and the enumerators are not
  interchangeable. The two conversion tables are hand-written, per-backend, and each one has
  to be checked against its own specification. The neutral enum above is a *superset shape*,
  not a proof of equivalence.
- `D3D12_BARRIER_LAYOUT` has queue-type-specific variants (`DIRECT_QUEUE_COMMON`,
  `COMPUTE_QUEUE_COMMON`, …) that "may only be used within a compatible command queue", and
  copy-queue resources must be in `COMMON`. That interacts with D6; the neutral `Common`
  layout exists specifically to express it.

### D5 — CPU/GPU synchronization is fence + value; present sync stays behind the seam

D3D12 has exactly one synchronization primitive: `ID3D12Fence` with a monotonically
increasing value. Vulkan's equivalent is a timeline semaphore (core since 1.2). The RHI
therefore models queue and CPU waits as `FenceHandle` + `uint64_t Value`, not as binary
semaphores.

Binary semaphores cannot be eliminated, though — the swapchain requires them:

- `VUID-vkAcquireNextImageKHR-semaphore-03265`: the semaphore "**must** have a
  `VkSemaphoreType` of `VK_SEMAPHORE_TYPE_BINARY`".
- `VUID-vkQueuePresentKHR-pWaitSemaphores-03267`: all wait semaphores "**must** be created
  with a `VkSemaphoreType` of `VK_SEMAPHORE_TYPE_BINARY`".

(Both verbatim from `$VULKAN_SDK/share/vulkan/registry/validusage.json`, SDK 1.4.341.1.)

So binary semaphores are an implementation detail of the present path and never appear in a
neutral header. `IPresentTarget` (Stage 6) owns them. Stage 5 leaves today's per-frame
`PresentCompleteSemaphore` / `RenderCompleteSemaphores` in `App` untouched.

### D6 — Queues are `QueueType`, and ownership transfer is a backend concern

Neutral: `enum class QueueType : uint8_t { Graphics, Compute, Copy }` — chosen to match
D3D12's `DIRECT` / `COMPUTE` / `COPY` command list types, which have no notion of a queue
family index. Vulkan's family indices stay inside `VulkanDevice`.

The queue-family ownership transfer that step R12 adds is a Vulkan-only mechanism. D3D12
has no equivalent; it requires copy-queue resources to be in the `COMMON` layout instead.
The RHI expresses the intent — "this resource was written on the copy queue and will next be
read on the graphics queue" — and each backend does whatever its API requires. Concretely
that means `UploadContext` returns an explicit "acquire" record rather than the caller
issuing raw release/acquire barriers.

### D7 — The descriptor/binding model is *not* abstracted in Stage 5

Vulkan descriptor sets/layouts/pools and D3D12 root signatures + descriptor heaps have no
cheap common denominator; every portable RHI that tries pays for it in complexity. It is also
the part of the design that a later step makes largely moot: bindless (step 69) converges
Vulkan descriptor indexing — which this app **already enables**
(`VK_EXT_descriptor_indexing`, `main.cpp:1143`) — with D3D12 SM6.6 `ResourceDescriptorHeap`.

So `DescriptorAllocator` (R13) is written as a Vulkan-side component under `rhi/vulkan/`,
used by `MaterialFactory`, and is deliberately *not* given a neutral interface. The
requirement on Stage 5 is only that it stays isolated, so replacing it later is a contained
change.

Related: prefer push constants for per-draw data. They map 1:1 onto D3D12 root constants,
and the code already uses them for material data (`main.cpp:1680`).

### D8 — Pipelines stay Vulkan-side; the *cache* is a neutral opaque blob

`PipelineBuilder` and `ComputePipelineBuilder` keep taking `vk::Format` and friends under
`rhi/vulkan/` for the whole of Stage 5. Neutralizing pipeline creation means neutralizing
the binding model (D7), so it waits.

`PipelineCache` (R15) is neutral, because it can be: create at startup, seed from a file,
hand to pipeline creation, serialize on shutdown. D3D12's equivalent is a cached PSO blob or
`ID3D12PipelineLibrary`; both fit "opaque bytes on disk that may be rejected as stale".

One favourable accident worth recording: the renderer uses **dynamic rendering**
(`vk::RenderingInfo`, `main.cpp:1641`) rather than `VkRenderPass`/`VkFramebuffer` objects.
That is much closer to D3D12's `OMSetRenderTargets` model, and
`vk::PipelineRenderingCreateInfo`'s colour formats correspond to a PSO's `RTVFormats`.
Do not reintroduce render pass objects.

### D9 — One documented native escape hatch, for ImGui

ImGui's Vulkan backend needs raw `VkInstance`, `VkPhysicalDevice`, `VkDevice`, queue family
index, `VkQueue`, `VkDescriptorPool` and the swapchain format. Pretending otherwise would
mean wrapping ImGui, which is not Stage 5's job.

```cpp
// rhi/vulkan/VulkanNative.h — the ONLY sanctioned leak. Editor/ImGui glue only.
namespace Rhi::Vulkan
{
struct NativeDevice
{
    VkInstance       Instance;
    VkPhysicalDevice PhysicalDevice;
    VkDevice         Device;
    VkQueue          GraphicsQueue;
    uint32_t         GraphicsQueueFamily;
};

NativeDevice GetNative(IDevice& device);
VkImage      GetNativeImage(IDevice& device, TextureHandle handle);
VkImageView  GetNativeView(IDevice& device, TextureViewHandle handle);
VkBuffer     GetNativeBuffer(IDevice& device, BufferHandle handle);
}
```

The rule is that the escape hatch is *listed*, not *available*: R17 checks that no file in
`src/` includes `rhi/vulkan/` except the ImGui glue. When the editor is extracted in Stage 7,
this header goes with it.

### D10 — Clip-space handedness gets exactly one site

Vulkan NDC is Y-down; D3D12 NDC is Y-up. Both use depth 0..1, and
`GLM_FORCE_DEPTH_ZERO_TO_ONE` is already set, so the depth half is done. The Y half is
currently one line — `proj[1][1] *= -1.f` at `main.cpp:2076`.

Keep it one line, and make it conditional on a capability rather than on the build:
`DeviceCaps::bFlipClipSpaceY`. Anything that recomputes a projection matrix elsewhere later
must read that flag rather than repeating the constant.

### D11 — Formats: a curated neutral enum, not a mirror of `VkFormat`

`Rhi::Format` contains only formats that have both a `VkFormat` and a `DXGI_FORMAT`
equivalent. Everything the renderer uses today qualifies: `R8Unorm`, `RGBA8Unorm`,
`RGBA8Srgb`, `BGRA8Unorm`, `RGBA16Float`, `D32Float`, `D24UnormS8Uint`.

Adding a format means adding it to the enum *and* the conversion table in the same commit;
§4 explains the switch-without-`default` trick that makes forgetting a compile error.

### D12 — Shaders are already portable; keep them that way

`slangc` lists `dxil` and `hlsl` as first-class targets alongside `spirv`
(`$VULKAN_SDK/share/doc/slang/command-line-slangc-reference.md:1157`), so the shader
language is not a portability problem — the shader *build* is. Stage 5 changes nothing here,
but two rules start applying now:

- Do not add SPIR-V-specific workarounds to `.slang` sources without an `#ifdef` on the
  target profile.
- Binding annotations should stay expressible for both targets; avoid hand-assigned
  `[[vk::binding]]` where Slang's automatic layout would do.

The `add_slang_shader_target` function in the root `CMakeLists.txt` will need a target
parameter when D3D12 lands. Not now.

---

## 3. Module layout

```
engine/rhi/
├── CMakeLists.txt
├── include/rhi/                    # NEUTRAL. No Vulkan, no VMA, no vk:: — enforced (§4)
│   ├── RhiTypes.h                  # Format, QueueType, MemoryAccess, Extent2D/3D, SampleCount
│   ├── Handles.h                   # BufferHandle, TextureHandle, TextureViewHandle,
│   │                               #   SamplerHandle, FenceHandle  (Handle<Tag> from Core)
│   ├── UniqueHandle.h              # RAII sugar over a handle + IDevice*
│   ├── BufferDesc.h  TextureDesc.h  SamplerDesc.h
│   ├── Barrier.h                   # PipelineStage / AccessFlags / TextureLayout  (D4)
│   ├── BarrierPresets.h            # today's Barriers:: presets, neutral
│   ├── IDevice.h                   # creation, destruction, mapping, caps
│   ├── DeviceDesc.h                # DeviceRequirements, DeviceCaps
│   ├── ICommandList.h              # barriers + copies in Stage 5; draws in Stage 8
│   ├── UploadContext.h             # batched staging, one fence
│   ├── PipelineCache.h             # opaque blob on disk
│   └── Diagnostics.h               # validation policy + counters
│
├── include/rhi/vulkan/             # TRANSITIONAL + the sanctioned escape hatch (D9)
│   ├── VulkanNative.h              # survives Stage 5 — ImGui/editor only
│   ├── PipelineBuilder.h           # stays Vulkan-shaped through Stage 5 (D8)
│   ├── ComputePipelineBuilder.h
│   └── DescriptorAllocator.h       # deliberately Vulkan-only (D7)
│
└── src/vulkan/
    ├── VulkanDevice.{h,cpp}        # instance, messenger, physical, logical, queues, VMA
    ├── VulkanCommandList.{h,cpp}
    ├── VulkanConversions.{h,cpp}   # every ToVk()/FromVk() table lives here and nowhere else
    ├── VulkanBuffer.h  VulkanTexture.h     # pool payloads; may use vk::raii freely
    ├── VulkanUploadContext.cpp
    ├── VulkanPipelineCache.cpp
    ├── VulkanDiagnostics.cpp
    ├── VMAImpl.cpp                 # moved from src/
    └── PipelineBuilder.cpp  ComputePipelineBuilder.cpp  DescriptorAllocator.cpp
```

`engine/core` gains two headers this stage: `core/Handle.h` and `core/HandlePool.h`
(architecture plan §9 already lists both).

CMake wiring:

```cmake
# engine/rhi/CMakeLists.txt
engine_module(RHI
  SOURCES src/vulkan/VulkanDevice.cpp ...          # explicit list, no globbing
  LINK_LIBRARIES Engine::Core Engine::Platform Vulkan::Vulkan
                 GPUOpen::VulkanMemoryAllocator)
```

Root `CMakeLists.txt`: `add_subdirectory(engine/rhi)` after `engine/platform`, then
`Engine::RHI` added to `VulkanApp`'s `target_link_libraries` **and** to the
`engine_header_self_containment(App ... LINK_LIBRARIES ...)` list — a header under `src/`
that includes `<rhi/...>` fails the header check otherwise.

---

## 4. How the boundary is enforced

Discipline does not hold a boundary for three weeks. Three mechanisms do:

**1. A neutral-header check target.** `engine_module` already creates
`HeaderSelfContainment_RHI`, which links the module itself and therefore *can* see Vulkan. A
second, stricter target compiles only `include/rhi/*.h` (excluding `include/rhi/vulkan/`)
while linking **only `Engine::Core` and `Engine::Platform`**:

```cmake
# engine/rhi/CMakeLists.txt, after engine_module(RHI ...)
file(GLOB neutral_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/*.h)
engine_header_self_containment(RHI_Neutral
  HEADERS ${neutral_headers}
  LINK_LIBRARIES Engine::Core Engine::Platform
  INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include)
```

A neutral header that includes `vulkan/vulkan.hpp` then fails to compile. `HeaderSelfContainment.cmake`
already documents the caveat that applies here: a dependency also present on the default
system include path is found regardless of what a target links, and on a typical Arch box
that can cover Vulkan. So this is a strong net, not a proof — hence:

**2. A grep gate in `scripts/precommit.sh`.** Cheap, exact, and immune to include paths:

```bash
# scripts/rhi_boundary_check.sh
! grep -rn --include='*.h' -E 'vulkan|vk_mem_alloc|\bvk::|\bVk[A-Z]' \
    engine/rhi/include/rhi --exclude-dir=vulkan
```

Plus, from R17 onward, the `src/` side of the same rule (only ImGui glue may include
`rhi/vulkan/`).

**3. Exhaustive switches with no `default:` label** in `VulkanConversions.cpp`. With
`-Wall -Wextra` and `CMAKE_COMPILE_WARNING_AS_ERROR ON`, `-Wswitch` turns "added an
enumerator, forgot the mapping" into a build failure on all nine CI configs. Do not add a
`default:` case to a conversion switch; throw after the switch instead.

**Per-step verification, every step, no exceptions:**

```bash
scripts/precommit.sh                  # configure + build + header check + tests + format
tests/scripts/baseline_test.sh        # writes tests/screenshots/ + tests/reports/
```

then diff the report against the one in `tests/baseline/` (referred to by directory rather
than filename, since re-capturing a baseline changes its timestamped name).
`validationErrors` must be 0 and `drawCalls` / `batches` / `instances` must be identical
unless the step's **Verify** line says otherwise. "It still builds" is not evidence.

---

## 5. The step sequence

Sizes use Part IV's scale: XS < 1h · S 1–3h · M ½–1 day · L 2–4 days.
Every step ends with a compiling, running application.

### Progress

**This table is the authority on what is done.** Update it in the same commit as the step.
`CLAUDE.md`'s stage table is coarse (Stage 5 as a whole) and deliberately does not repeat
per-step status, so there is only one place to change.

Where the as-built result differs from the step's **Do** text — because implementing it
revealed something the plan got wrong — the step carries an **As built** note. Those notes
are the ones later steps need to read.

| Step | Status |
|---|---|
| R1 — `core/Handle.h` + `core/HandlePool.h` | ✅ done |
| R2 — `engine/rhi` skeleton and the neutral vocabulary | ✅ done |
| R3 — Move the RHI leaf types | ✅ done |
| R4 — Dissolve `Utility.h` | not started |
| R5 — Extract `Rhi::Device` | not started |
| R6 — Enumerate all queue families | not started |
| R7 — `Rhi::Diagnostics` | not started |
| R8 — `Rhi::ICommandList` and the neutral barrier API | not started |
| R9 — Buffers become handles | not started |
| R10 — Textures, views and samplers become handles | not started |
| R11 — `UploadContext` — batch transfers | not started |
| R12 — Use the dedicated transfer queue | not started |
| R13 — Growable `DescriptorAllocator` | not started |
| R14 — Growable instance buffer | not started |
| R15 — `PipelineCache` | not started |
| R16 — First GPU tests | not started |
| R17 — Seal the boundary and update the docs | not started |

### R1 — `core/Handle.h` + `core/HandlePool.h`

- **Do:** Add `Handle<Tag>` exactly as specified in architecture plan §11.1
  (`index:24 | generation:8`, `kInvalid`, `Index()`, `Generation()`, `IsValid()`,
  `operator<=>`). Add `HandlePool<T, Tag>`: dense storage, FIFO free list, generation bump on
  release, `Get()` returning `nullptr` on a stale handle, `Size()`/`Capacity()`. Add
  `tests/unit/core/HandleTests.cpp` to the `core_tests` target: create/get/destroy, stale
  handle rejected, generation wrap behaviour, reuse ordering, capacity growth.
- **Why now:** D2. It is CPU-only, so it is testable before any GPU code depends on it.
- **Verify:** `ctest -L unit` passes with the new tests. Application untouched, so the
  headless report must be byte-identical to the baseline.
- **As built:** `Handle` gained `FromIndexAndGeneration()` so `HandlePool` never open-codes
  the bit layout, and `kMaxIndex = kIndexMask - 1` so that no valid handle can collide with
  `kInvalid` (index `0xFFFFFF` at generation `0xFF` *is* `kInvalid`). §11.1's `MeshHandle` /
  `MaterialHandle` / `TextureHandle` / `EntityHandle` aliases were **not** added: a global
  `TextureHandle` in `Core` is the collision D0 warns about, and R2/R9/R10 declare those
  under `Rhi::` instead.
- **Size:** S · **Needs:** —

### R2 — `engine/rhi` skeleton and the neutral vocabulary

- **Do:** Create the module with `engine_module(RHI ...)`, `add_subdirectory`, and both
  header checks from §4 including `rhi_boundary_check.sh` wired into `scripts/precommit.sh`.
  Add `RhiTypes.h`, `Handles.h`, `Barrier.h`, `BufferDesc.h`, `TextureDesc.h`,
  `SamplerDesc.h`, and `src/vulkan/VulkanConversions.{h,cpp}` with `ToVk`/`FromVk` for every
  enumerator. Add `tests/unit/rhi/ConversionTests.cpp` in a new `rhi_tests` target
  (CPU-only — no device is created) asserting round-trips.
- **Note:** nothing uses any of this yet. That is intentional: the vocabulary is in place
  before the first type moves, so moved code can be converted in the same step it lands.
- **Verify:** `HeaderSelfContainment` and `rhi_boundary_check` pass. `ctest -L unit` passes.
  Headless report identical to baseline.
- **As built:** four departures, each with a consequence for a later step:
  - **`FromVk` exists only where the mapping is one-to-one.** `PipelineStage`, `AccessFlags`,
    `TextureLayout` and the usage/aspect flag enums are `ToVk`-only, because one neutral value
    can expand to several Vulkan values (`DepthStencil` is both fragment-test stages) or two
    neutral values can share one (`Common` and `UnorderedAccess` are both `eGeneral`). Where
    `FromVk` does exist it is *derived* from `ToVk` by search, not hand-written twice.
  - **`Format` omits `D16UnormS8Uint`.** D11 requires a DXGI equivalent and there is none —
    `dxgiformat.h` offers stencil only with 24-bit unorm or 32-bit float depth. It is
    currently the last candidate in `FindDepthFormat` (`main.cpp:2278`), so **R10 must drop it
    from that list or accept a promotion to `D24UnormS8Uint`.** Vertex-attribute formats are
    also absent, since vertex input stays Vulkan-side until Stage 8 (D8).
  - **`QueueType` is tested with a predicate, not a bit mapping.** `FamilySupports(flags, role)`
    replaces what would have been `ToVk(QueueType)`. The spec lets a graphics or compute family
    omit `VK_QUEUE_TRANSFER_BIT` while still being able to copy, so `Copy` is satisfied by any
    of `eTransfer`/`eGraphics`/`eCompute` — an "any of" test, which a caller handed a raw mask
    would likely write as "all of". There is no reverse mapping: a universal family serves all
    three roles, so "which role is this family" has no single answer.
  - **The boundary check is `cmake/RhiBoundaryCheck.cmake`** with thin `.sh`/`.bat` wrappers
    in `tests/scripts/`, not a shell grep in `scripts/`. It strips comments before matching,
    because §4's literal pattern would reject this document's own specimen comments
    (`// → VkPipelineStageFlags2`). It bans a *dependency*, not a mention.

  Also: §4's claim that the exhaustive-switch mechanism "fails the build on all nine CI
  configurations" was **false as written** — MSVC's C4062 is a level-4 warning that `/W3`
  leaves off, so the RHI target now sets `/w14062` explicitly. Verified by adding an unmapped
  enumerator and watching MSVC fail.
- **Size:** M · **Needs:** R1

### R3 — Move the RHI leaf types

- **Do:** Move `AllocatedBuffer`, `AllocatedImage`, `VulkanAllocator`, `VMAImpl.cpp`,
  `Barrier.h`, `Texture`, `Cubemap`, `PipelineBuilder`, `ComputePipelineBuilder` from `src/`
  into `engine/rhi`, initially under `include/rhi/vulkan/` + `src/vulkan/`. Update includes
  in `src/`, the `SOURCES` list in the root `CMakeLists.txt`, and the module's `SOURCES`.
  **No logic changes.**
- **Note:** `Texture` and `Cubemap` are conceptually Assets-layer types (a cache key plus a
  GPU image). Stage 7 moves them; leaving them in RHI for now matches Part IV step 24 and
  avoids pre-empting that decision.
- **Verify:** Headless report and screenshot identical to baseline. `src/` no longer contains
  those files.
- **As built:** the move exposed one ordering problem and one silent-breakage hazard.
  - **`SetVkDebugName` had to come along, out of R4.** Both pipeline builders call it, and it
    lived in `src/Utility.h` — so moving them into the module would have left module code
    depending on a header in `src/`, which is the dependency direction the whole stage exists
    to prevent. R4's first bullet already specifies `rhi/vulkan/DebugNames.h` for exactly this
    function, so that one piece was pulled forward verbatim; `src/Utility.h` now includes it
    and R4 has correspondingly less to do. Nothing else in the moved set needed anything from
    `Utility.h`.
  - **The RHI target now defines `DEBUG` PUBLIC in Debug configs.** `SetVkDebugName` is a
    template whose body is `#ifdef DEBUG`, so it is instantiated per calling translation unit.
    Before this step every caller was in `VulkanApp`, which defines `DEBUG` itself, so they all
    agreed. With callers now on both sides of the boundary, a module that did not define it
    would instantiate an empty body while the application instantiated a real one — an ODR
    violation whose only symptom is debug names going missing from some objects and not others,
    invisible to the baseline report. Verified present on the module's compile flags.
  - **There are now two files called `Barrier.h`**: `rhi/Barrier.h` (neutral, R2) and
    `rhi/vulkan/Barrier.h` (the moved Vulkan presets). Nothing includes both and no type names
    overlap, so confusing them is a compile error rather than a silent substitution. R8 deletes
    the latter. The moved file carries a comment saying so.
  - **`TextureBinding` (`Albedo`/`Normal`/`MetallicRoughness`) rode along inside `Texture.h`**
    and is a material concept, not an RHI one. Left in place because R3 moves files verbatim
    and Stage 7 relocates `Texture` wholesale anyway — but it should not acquire new users
    while it sits here.
- **Size:** M · **Needs:** R2 · **Was:** step 24

### R4 — Dissolve `Utility.h`

- **Do:** Split its remaining lines by concern:
  `rhi/vulkan/BufferUtil.h` (`CreateBuffer`, `CopyBuffer`, `CreateStagedBuffer`),
  `rhi/vulkan/ImageUtil.h` (`CreateImage`, `CreateImageView`, `CopyBufferToImage`,
  `CreateRenderTexture`), `rhi/vulkan/BarrierUtil.h` (`RecordImageBarrier`),
  `rhi/vulkan/SwapchainSupport.h` (`ChooseSwapchainFormat`, `ChoosePresentMode`,
  `ChooseSwapchainExtent`, `ChooseSwapMinImageCount`), `rhi/vulkan/CommandBufferUtil.h`
  (`BeginSingleTimeCommand`, `EndSingleTimeCommand`).
  Delete `FindMemoryType` (dead since VMA, and its own comment says so).
- **Already done:** `rhi/vulkan/DebugNames.h` (`SetVkDebugName`) was pulled forward into R3,
  which could not move the pipeline builders without it. `Utility.h` includes it today.
- **Correction to Part IV step 25:** `EnsureParentDirectoryExists` and `EnsureExtension` are
  filesystem helpers with nothing to do with rendering. They go to
  `engine/platform/include/platform/FileSystem.h`, not to `rhi/`.
- **Verify:** Headless report identical. `src/Utility.h` no longer exists.
- **Size:** M · **Needs:** R3 · **Was:** step 25

### R5 — Extract `Rhi::Device`

- **Do:** Move `CreateInstance`, `SetupDebugMessenger`, `IsPhysicalDeviceSuitable`,
  `PickPhysicalDevice`, `CreateLogicalDevice` and the VMA allocator out of `App` into
  `VulkanDevice`, behind the neutral `Rhi::IDevice` (D3). Add `DeviceDesc` /
  `DeviceRequirements` with the present vs non-present split already separated
  (architecture plan §10.3) even though both are still required — Stage 6 flips the flag.
  Add `DeviceCaps` including `bFlipClipSpaceY` (D10). Add `rhi/vulkan/VulkanNative.h` (D9)
  and route ImGui init through it. Surface creation stays in `App`; the surface is passed to
  `CreateDevice` as an opaque `uint64_t`/`void*`.
- **Verify:** Compare the startup log line-for-line against a saved baseline: same physical
  device, same queue index, same swapchain image count, same validation output. Headless
  report identical.
- **Size:** L · **Needs:** R4 · **Was:** step 26

### R6 — Enumerate all queue families

- **Do:** In `VulkanDevice`, find graphics+present, dedicated compute and dedicated transfer
  families; log all of them; expose them as `QueueType` (D6). **Keep using the graphics queue
  everywhere** — this step discovers and reports only.
- **Use** `Rhi::Vulkan::FamilySupports(familyFlags, role)` to test a family rather than
  comparing `queueFlags` yourself. R2 found that the spec lets a graphics or compute family
  omit `VK_QUEUE_TRANSFER_BIT` while still being able to copy, so a plain
  `flags & eTransfer` test would reject a capable family on any driver that takes that option;
  `FamilySupports` encapsulates the "any of these capabilities" rule so the call site cannot
  get it wrong. "Dedicated transfer" is the narrower, separate test — supports `Copy` but not
  `Graphics` — and is R12's concern, not this step's.
- **Verify:** Log lists the families the GPU exposes. Headless report identical. Resolves the
  information half of the dedicated-compute-queue `TODO` at `main.cpp:576` (Part IV cites
  `main.cpp:400` and `main.cpp:2173` for this; both line numbers are stale, and only the
  compute one still exists as a comment).
- **Size:** S · **Needs:** R5 · **Was:** step 27

### R7 — `Rhi::Diagnostics`

- **Do:** Promote step 6's global counters into a `Diagnostics` object owned by the device,
  with `ValidationPolicy { Ignore, Count, FailFast }` and message capture. Keep the interface
  neutral — D3D12's debug layer + `ID3D12InfoQueue` fits the same shape — and keep
  `g_ValidationErrorCount` working for the run report until Stage 7 moves it.
- **Verify:** `--strict-validation` still exits non-zero on an injected error. `FailFast`
  aborts at the first error with the message printed. Headless report identical.
- **Size:** S · **Needs:** R5 · **Was:** step 28

### R8 — `Rhi::ICommandList` and the neutral barrier API

- **Do:** Add `ICommandList` with, for now, only what Stage 5 needs: `Barrier(...)`,
  `CopyBuffer`, `CopyBufferToTexture`, `CopyTextureToBuffer`, plus begin/end. Implement
  `VulkanCommandList` over `vk::CommandBuffer`. Convert `src/Barrier.h`'s preset functions to
  neutral `BarrierPresets.h` and route every existing `RecordImageBarrier` call through
  `ICommandList::Barrier`. Batch multiple image barriers into one `pipelineBarrier2` call,
  which resolves the `TODO` at the top of the old `RecordImageBarrier`.
- **Boundary:** draw/bind/viewport recording stays on raw `vk::CommandBuffer` in `App` until
  Stage 8. Moving it now would drag the pipeline and descriptor model along (D7, D8).
- **Verify:** Headless report identical, **validation errors 0 with synchronization
  validation enabled** (`validate_sync` is already on at `main.cpp:1076`). Barrier count per
  frame logged and sane.
- **Size:** M · **Needs:** R5

### R9 — Buffers become handles

- **Do:** `BufferHandle` + `BufferDesc` + `IDevice::CreateBuffer/Destroy/Map/Unmap`, backed
  by a `HandlePool<VulkanBuffer>`. Add `UniqueHandle`. Convert every `AllocatedBuffer` user:
  quad buffers, instance buffers, global buffers, staging buffers, the screenshot staging
  buffer, and `ModelData`'s vertex/index buffers. `AllocatedBuffer` becomes the pool payload
  `VulkanBuffer` and stops being visible outside the module.
- **Verify:** Headless report identical. Live-buffer count logged at shutdown is 0 — the
  first thing the handle model buys.
- **Size:** L · **Needs:** R8

### R10 — Textures, views and samplers become handles

- **Do:** `TextureHandle`, `TextureViewHandle`, `SamplerHandle` + descs + pools. Convert
  `Texture`, `Cubemap`, the render targets, the depth resources and the texture sampler.
  `src/Texture.h` and `src/Cubemap.h` become thin asset-side wrappers holding a handle plus
  their name/path/create-info, so `ResourceCache` and `MaterialFactory` keep working
  unchanged until Stage 7.
- **Decide first:** `FindDepthFormat` (`main.cpp:2278`) currently ends its candidate list with
  `eD16UnormS8Uint`, which `Rhi::Format` deliberately does not carry — there is no DXGI
  equivalent (R2's as-built note). Either drop that candidate or map it to
  `D24UnormS8Uint`. Doing neither means the conversion throws on whatever hardware falls
  through to it.
- **Verify:** Headless report identical, screenshot identical. Live-texture count 0 at
  shutdown.
- **Size:** L · **Needs:** R9

### R11 — `UploadContext` — batch transfers

- **Do:** Replace the per-resource `EndSingleTimeCommand` → `queue.waitIdle()` with a context
  that records many copies into one command buffer, submits once, waits on one fence, then
  releases staging buffers. Route `TextureLoader`, `CubemapLoader`, `ModelLoader` and
  `CreateQuadBuffers` through it. Interface is neutral and handle-based.
- **Verify:** **Time a Sponza load before and after** with `core/Timer.h`. Sponza performs
  ~70 full GPU drains today; expect a large reduction. Headless report identical.
- **Size:** L · **Needs:** R6, R10 · **Was:** step 29

### R12 — Use the dedicated transfer queue

- **Do:** Point `UploadContext` at the transfer family when one exists, with its own command
  pool, plus queue-family ownership release/acquire before first graphics use — expressed as
  an acquire record returned by `UploadContext` rather than raw barriers at the call site
  (D6).
- **Read before writing:** the Vulkan spec's synchronization chapter on queue-family
  ownership transfer. A release in the source family and an acquire in the destination family
  must both be issued, with identical subresource ranges, and the acquire must be ordered
  after the release by a semaphore. This is the single easiest thing in the stage to get
  plausibly-but-wrongly right.
- **Verify:** Headless report identical, **zero validation errors** with synchronization
  validation on. Load time improves further on discrete GPUs.
- **Size:** M · **Needs:** R11 · **Was:** step 30

### R13 — Growable `DescriptorAllocator`

- **Do:** `std::vector<vk::raii::DescriptorPool>`; on `eErrorOutOfPoolMemory`, allocate
  another pool at ~1.5× and retry. Use it in `MaterialFactory`, deleting
  `s_MAX_MATERIAL_SET_COUNT` (`MaterialFactory.cpp:10`). Vulkan-side by design (D7).
- **Verify:** **Temporarily set the initial pool size to 4**, load Sponza (~25 materials),
  confirm it loads with pool growth logged, then restore a sensible size. Headless report
  identical.
- **Size:** M · **Needs:** R5 · **Was:** step 31

### R14 — Growable instance buffer

- **Do:** Replace the `throw` in `UpdateInstanceBuffer` (`main.cpp:2318`) with: wait idle,
  reallocate to `max(needed, capacity * 2)`, remap, log once. `MAX_INSTANCE_COUNT` becomes an
  initial capacity, not a ceiling.
- **Verify:** Author `content/scenes/stress.map` with > 1024 instances and confirm it renders
  instead of throwing. Existing scenes' reports identical.
- **Size:** S · **Needs:** R9 · **Was:** step 32

### R15 — `PipelineCache`

- **Do:** One cache object created at startup, seeded from `<user data dir>/pipeline_cache.bin`
  via `platform/Paths.h`, passed to all five pipeline creations (all currently pass `nullptr`)
  and to `ImGui_ImplVulkan_InitInfo::PipelineCache`. Write on shutdown. Neutral interface,
  opaque blob (D8).
- **Verify:** Log pipeline-creation time; second launch measurably faster. Delete the file
  and confirm it regenerates. **Corrupt the file and confirm it is rejected gracefully**
  rather than crashing.
- **Size:** M · **Needs:** R5 · **Was:** step 33

### R16 — First GPU tests

- **Do:** Add `tests/support/RhiTestFixture.h` (one device per binary, SKIP if no ICD) and
  `ValidationGuard.h`. Tests: device creation reports the required features; buffer upload →
  readback round-trips byte-exactly; image upload → readback matches; **all six cubemap faces
  differ as expected**. Requires a `LABEL` parameter on `engine_test` in `cmake/Testing.cmake`,
  which currently hardcodes `LABELS "unit"`. Label these `gpu` and keep them out of
  `run_unit_tests.sh`.
- **Expect a failure:** the cubemap test should fail first time — that is companion-doc bug
  1.1, `CopyBufferToImage` and the layout transition hardcoding `layerCount = 1` (visible in
  the old `Utility.h:245`). Fix it here; the test locks it down permanently.
- **Verify:** `ctest -L gpu` passes locally, and skips with a clear message when no ICD is
  present. `ctest -L unit` unaffected.
- **Size:** L · **Needs:** R11 · **Was:** step 34

### R17 — Seal the boundary and update the docs

- **Do:** Move anything left in `include/rhi/vulkan/` that is not `VulkanNative.h`,
  `PipelineBuilder.h`, `ComputePipelineBuilder.h` or `DescriptorAllocator.h` down into
  `src/vulkan/`. Extend `scripts/rhi_boundary_check.sh` with the `src/` rule (only the ImGui
  glue may include `rhi/vulkan/`). Walk the checklist in §8 and record the answer for each
  row. Update `CLAUDE.md`: stage table to ✅, repository layout to list `engine/rhi/`, and
  remove the Stage 5 pointer to this document.
- **Verify:** `scripts/precommit.sh` green; boundary check green; headless report identical
  to baseline with `validationErrors: 0`.
- **Size:** S · **Needs:** R16

---

## 6. Mapping back to Part IV

| This plan | Part IV | Note |
|---|---|---|
| R1 | — | Pulled forward from §11.1 / §9 (`Core` handles) |
| R2 | — | New: neutral vocabulary + conversion tables + boundary enforcement |
| R3 | 24 | Unchanged |
| R4 | 25 | Two filesystem helpers redirected to `Platform`, not `RHI` |
| R5 | 26 | Plus `IDevice`, `DeviceCaps`, the native escape hatch |
| R6 | 27 | Plus neutral `QueueType` |
| R7 | 28 | Unchanged |
| R8 | — | New: `ICommandList` + neutral barriers (also lands the barrier-batching TODO) |
| R9 | — | New: buffers become handles |
| R10 | — | New: textures/views/samplers become handles |
| R11 | 29 | Now handle-based |
| R12 | 30 | Ownership transfer expressed as intent, not raw barriers |
| R13 | 31 | Explicitly stays Vulkan-only (D7) |
| R14 | 32 | Unchanged |
| R15 | 33 | Neutral interface over an opaque blob |
| R16 | 34 | Plus a `LABEL` parameter for `engine_test` |
| R17 | — | New: boundary audit + doc updates |

---

## 7. Out of scope

Explicitly **not** in Stage 5, to keep the boundary of this plan sharp:

- `IPresentTarget` / `SwapchainTarget` / `OffscreenTarget` — Stage 6 (steps 35–40). The
  swapchain stays in `App`.
- A second RHI implementation. Decided: the null/recording backend waits for Stage 6's
  `OffscreenTarget`. Until then D1 is held by §4's checks, not by a second compiler target.
- Draw-call recording through `ICommandList`, render passes, the frame graph — Stage 8.
- Any neutral descriptor/binding abstraction — deferred to bindless, step 69 (D7).
- Neutral pipeline creation — Stage 8, once the binding model exists (D8).
- Removing `ResourceManager` / `ModelManager` / `MaterialFactory` singletons — Stage 7.
- Moving `Texture`/`Cubemap` to an `Assets` module — Stage 7.
- Shader build changes for a DXIL target (D12).

---

## 8. D3D12 readiness checklist

The audit R17 runs. For each row: is the concept neutral, isolated, or knowingly deferred?

| Concept | Vulkan today | D3D12 equivalent | Stage 5 outcome |
|---|---|---|---|
| Instance / adapter | `VkInstance` + `VkPhysicalDevice` | `IDXGIFactory` + `IDXGIAdapter` | Inside `VulkanDevice` |
| Device | `VkDevice` | `ID3D12Device` | `Rhi::IDevice`, neutral |
| Queues | family index + `VkQueue` | `ID3D12CommandQueue` (DIRECT/COMPUTE/COPY) | `QueueType`, neutral (D6) |
| Allocator | VMA | D3D12MA (same author, similar API) | Inside device |
| Buffer | `VkBuffer` + `VmaAllocation` | `ID3D12Resource` | `BufferHandle` (D2) |
| Texture | `VkImage` | `ID3D12Resource` | `TextureHandle` (D2) |
| Texture view | `VkImageView` object | descriptor in a heap | `TextureViewHandle` — same handle, different backing |
| Sampler | `VkSampler` object | sampler descriptor / static sampler | `SamplerHandle` |
| Barriers | sync2 triple | Enhanced Barriers triple | Neutral triple + presets (D4) |
| Command recording | `VkCommandBuffer` | `ID3D12GraphicsCommandList` | `ICommandList`, partial — copies/barriers only |
| Command pool | `VkCommandPool` | `ID3D12CommandAllocator` | Inside backend |
| CPU/GPU sync | timeline semaphore | `ID3D12Fence` + value | `FenceHandle` + value (D5) |
| Present sync | **binary** semaphores (VUIDs in D5) | DXGI + fence | Behind `IPresentTarget`, Stage 6 |
| Descriptors | sets / layouts / pools | root signature + heaps | **Deferred** (D7) — isolated, not abstracted |
| Per-draw constants | push constants | root constants | Already 1:1 |
| Pipelines | `VkPipeline` + dynamic rendering | PSO, no render pass objects | Vulkan-side (D8); dynamic rendering is the portable choice |
| Pipeline cache | `VkPipelineCache` | cached PSO blob / `ID3D12PipelineLibrary` | Neutral opaque blob (D8) |
| Shaders | SPIR-V via Slang | DXIL via Slang | Already portable (D12) |
| Clip space | Y-down | Y-up | One site, `DeviceCaps::bFlipClipSpaceY` (D10) |
| Formats | `VkFormat` | `DXGI_FORMAT` | Curated `Rhi::Format` + tables (D11) |
| Debug names | `VK_EXT_debug_utils` | `ID3D12Object::SetName` | `SetDebugName(handle, name)` |
| Validation | layers + messenger | debug layer + `ID3D12InfoQueue` | `Rhi::Diagnostics` (D7 step, neutral) |

---

## 9. Risks

- **R9 and R10 are the dangerous steps.** They touch every resource creation and use site in
  the codebase. They are deliberately split (buffers, then textures) so a baseline comparison
  runs between them. Do not merge them.
- **R12, queue-family ownership transfer**, is the classic "compiles, renders correctly on
  one driver, fails intermittently on another" change. Read the spec's synchronization
  chapter, keep synchronization validation on, and treat a clean run as necessary but not
  sufficient.
- **The escape hatch (D9) will try to grow.** Every new `GetNative*` function is a hole in
  the abstraction. Adding one should require a line in this document saying why.
- **The neutral-header check has a known blind spot** (system include paths — see the comment
  block in `cmake/HeaderSelfContainment.cmake`). That is why the grep gate exists too. If
  both are ever bypassed, the boundary is unenforced and this plan quietly stops working.
- **Handle generation is 8 bits.** FIFO slot reuse makes aliasing require 256 full pool
  cycles; if a pool is ever used for high-churn per-frame resources, revisit the split rather
  than assuming it holds.
- **The estimate.** ~3 weeks assumes the baseline comparison stays trustworthy. If a step
  produces an unexplained report diff, stop and find it — a silent behaviour change carried
  forward is worth more than the remaining schedule.

---

## 10. Retirement

When R17 is done and `CLAUDE.md`'s stage table reads Stage 5 ✅:

**Promote before deleting.** The step list is disposable; the design decisions are not. Move
into `docs/architecture_plan.md` (or a small permanent `docs/rhi.md`):

- §2 D1–D12 — the rationale future work has to respect.
- §4 — how the boundary is enforced, since the checks stay in the build.
- §8 — the D3D12 readiness checklist, which becomes the starting backlog for the backend.

**Then delete:** this file, and the Stage 5 pointer in `CLAUDE.md`'s working rules and
roadmap table.
