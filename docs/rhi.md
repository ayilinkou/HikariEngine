# HikariEngine — The RHI

**Permanent document.** It replaces `rhi_extraction_plan.md` and `backend_readiness_plan.md`,
which drove Stages 5, 7.5, 7.6 and 7.7 and were deleted when the D3D12 backend landed on
15 September 2026. Their step lists are gone; their **decisions are here**, because the code
cites them by number — `D26` appears in ten source comments, `D24` in nine, `D9` in eight — and
a citation that leads nowhere is worse than no citation.

**Read it before touching anything under `engine/rhi/include/`.** It governs what the RHI's
public seam is allowed to say. Where it and `architecture_plan.md`'s Part IV disagree about the
seam, this document wins: Part IV was written when there was one backend and still spells some
interfaces in raw Vulkan.

Numbering is historical and has gaps in meaning, not in sequence: D0–D13 come from Stage 5,
D14–D46 from Stages 7.5–7.7, and **D14 supersedes D7** while **D15 supersedes D8's first half**.
Both are kept as redirects because the code still cites them.

---

## Contents

1. [What the seam is for](#1-what-the-seam-is-for)
2. [The rules that shape it](#2-the-rules-that-shape-it)
3. [Vocabulary](#3-vocabulary)
4. [Resources and lifetime](#4-resources-and-lifetime)
5. [Recording and submitting a frame](#5-recording-and-submitting-a-frame)
6. [Binding and pipelines](#6-binding-and-pipelines)
7. [Shaders and the shader build](#7-shaders-and-the-shader-build)
8. [Choosing and describing a device](#8-choosing-and-describing-a-device)
9. [D3D12's own decisions](#9-d3d12s-own-decisions)
10. [Evidence](#10-evidence)
11. [How the boundary is enforced](#11-how-the-boundary-is-enforced)
12. [The concept map](#12-the-concept-map)
13. [Deferred, and what would reopen it](#13-deferred-and-what-would-reopen-it)

---

## 1. What the seam is for

`Engine::RHI` is the only module that may name a graphics API. Everything above it — the
renderer, the asset layer, the editor, the apps — describes what it wants in neutral terms and
the backend decides how to say it. Two backends exist: Vulkan everywhere, D3D12 on Windows.

The point is not portability for its own sake. It is that **a second implementation is the only
honest test of an abstraction.** Every place the seam was vague, D3D12 found it — combined image
samplers, cull mode as command-list state, vertex semantics, present semaphores — and each of
those is a decision below rather than a `#ifdef` somewhere.

The governing instinct, which recurs in D17, D22, D41 and D42: **where one API cannot express
something, the seam stops claiming it.** It does not invent a lowest common denominator, and it
does not let one backend fake a concept the other has natively.

---

## 2. The rules that shape it

**D0 — Namespace `Rhi`, directory `rhi/`, included as `<rhi/IDevice.h>`.** PascalCase namespace
to match `Core`, `Platform`; lowercase directory to match `engine_module`'s convention. A
namespace is mandatory rather than stylistic: `Rhi::Texture` and `Rhi::Format` would otherwise
collide with the engine's own types and with anything D3D12's headers drag in.

**D1 — The public API is neutral, and the backends are invisible.** Nothing under
`engine/rhi/include/rhi/` may name a Vulkan, VMA, D3D12 or D3D12MA type. The backends live in
`src/vulkan/` and `src/d3d12/`, which nothing outside the module can reach. The cost is a
neutral counterpart and a conversion table for every enum at the boundary; §11 is what keeps it
true rather than aspirational.

**D2 — Resources are 32-bit handles, not RAII objects.** `Handle<Tag>` packs `index:24 |
generation:8`, `kInvalid = 0xFFFFFFFF`; the device owns the storage in a `HandlePool` and
`Destroy` bumps the slot's generation. Once D1 confines the backend types, a public RAII object
cannot hold a `VkBuffer`, and every alternative — `unique_ptr<IBuffer>`, pImpl, an opaque
fixed-size blob — pays a real cost to hide what a handle hides for free. The second reason is
as valuable: use-after-free becomes a detected generation mismatch that can be logged rather
than undefined behaviour. Eight generation bits wrap after 256 reuses of one slot, and the free
list is FIFO, so an aliasing collision needs 256 full cycles of the pool.

`Rhi::UniqueHandle<H>` is the RAII sugar for scope-local resources. Handles as the ABI with RAII
on top works; the reverse does not. RAII is not banished *inside* a backend — D2 is about what
crosses the seam.

**D3 — Virtual interfaces at object granularity.** `IDevice`, `ICommandList`,
`ICommandAllocator`, `IPresentTarget`, `IUploadContext`, `IPipelineCache` are abstract, and
`Rhi::CreateDevice(const DeviceDesc&)` returns a `unique_ptr<IDevice>`. A compile-time
`using Device = VulkanDevice` removes the vtable and makes it impossible for two backends to
coexist in one test binary — which is exactly what the cross-backend evidence in D26 needs — and
would have made D25's runtime `--backend` impossible without another rewrite. **This is the
decision most worth revisiting if profiling disagrees**; it is contained to the boundary and
touches neither D1 nor D2.

**D9 — One documented native escape hatch, for ImGui.** ImGui's backends take raw handles:
Vulkan's wants instance, physical device, device, queue and queue family; DX12's wants a device,
a queue, a command list and a descriptor heap. Wrapping ImGui is not the RHI's job, so
`rhi/vulkan/VulkanNative.h` and `rhi/d3d12/D3D12Native.h` exist and are **listed, not
available**: §11's boundary check names every file allowed to include them. D9 predicted "a
D3D12 build gets a sibling file, not an edit", and that is literally what happened.

**D13 — Where the two APIs disagree on a name, use D3D12's.** `Copy` not transfer,
`CommandList` not command buffer, `PixelStage` not fragment, `UnorderedAccess` not storage,
`ICommandAllocator` not command pool. The reason is asymmetry of harm: Vulkan is the backend
that already exists, so a Vulkan name in neutral code reads as natural and survives by accident
until a second backend makes it wrong. A D3D12 name is mildly unfamiliar, and the unfamiliarity
is doing work — it signals that the surrounding code is meant to be neutral. This applies to
names, not semantics, and where only one API has the concept at all its term stands. Utility
headers under `rhi/vulkan/` take a uniform `Util` suffix.

> One place the rule did not reach: `ICommandList::PushConstants` keeps Vulkan's word where
> D3D12 says "root constants". Consistent across the codebase, never argued for.

**D18 — The seam lands before the abstractions built on top of it.** Cited from
`architecture_plan.md`'s Stage 8, and the reason its steps 50–54 follow the seam rather than
precede it: converting the recorders into `Pass` classes first would mean writing a `Pass` whose
`Execute` — and, for `CloudSystem`, whose *constructor signature* — names Vulkan, then rewriting
it. Each recorder is touched twice either way, but for two clearly separated reasons: first onto
the neutral API in place, then into a class, a structural move with no API change. **Designing an
abstraction against one backend is what the layering exists to prevent**, and it applies to
whatever comes next as much as it applied to passes.

---

## 3. Vocabulary

**D4 — Barriers are the neutral `(PipelineStage, AccessFlags, TextureLayout)` triple.**
Vulkan's `VK_KHR_synchronization2` and D3D12's Enhanced Barriers split a barrier the same three
ways, so the seam does too, with named presets in `rhi/BarrierPresets.h`. Two honest caveats:
the DirectX specification claims no Vulkan parity and the enumerators are not interchangeable,
so the two conversion tables are hand-written and each checked against its own specification —
the neutral enum is a *superset shape*, not a proof of equivalence. And `D3D12_BARRIER_LAYOUT`
has queue-type-specific variants that may only be used on a compatible queue, with copy-queue
resources required to be in `COMMON`; the neutral `Common` layout exists to express that.

**D11 — Formats are a curated neutral enum, not a mirror of `VkFormat`.** `Rhi::Format` contains
only formats with both a `VkFormat` and a `DXGI_FORMAT`. Adding one means adding it to the enum
*and* to both conversion tables in the same commit, which §11's `default:`-free switches make a
build error rather than a runtime surprise.

**D6 — Queues are `QueueType { Graphics, Compute, Copy }`**, matching D3D12's DIRECT / COMPUTE /
COPY, which have no notion of a family index. Vulkan's family indices stay inside
`VulkanDevice`. Queue-family ownership transfer is a Vulkan-only mechanism, made conditional by
`VK_KHR_maintenance9`; D3D12 has no equivalent and requires `COMMON` instead. The seam expresses
the *intent* — this was written on the copy queue and will next be read on the graphics queue —
and each backend does what its API requires, which is why `UploadContext` returns an explicit
acquire record rather than the caller issuing release/acquire barriers.

**D10 — Clip-space handedness gets exactly one site.** Vulkan NDC is Y-down, D3D12 Y-up; both
use depth 0..1 and `GLM_FORCE_DEPTH_ZERO_TO_ONE` is set, so only Y is in question. It is one
conditional negation of the projection's Y scale, driven by `DeviceCaps::bFlipClipSpaceY`.
Anything that recomputes a projection reads the flag; anything that maps rows to clip space by
hand asks `ClipSpaceYPointsDown()`, which reads the sign of that scale rather than carrying a
flag of its own. **Stage 7.7 found two passes that had not**: the cloud pass reconstructed each
pixel's ray mirrored about the horizon while reading depth unmirrored, and the composite quad
sampled the cloud image upside down — 247,886 differing pixels, 12% of the frame, and the whole
of the first cross-backend diff.

---

## 4. Resources and lifetime

Buffers, textures, texture views, samplers, fences and bind groups are handles (D2), created
from a `*Desc` and destroyed by the device. Two rules about the descriptions:

**Debug names are a `Desc` field, not a `SetDebugName(handle, name)` call.** The name is set at
creation, so there is no second call to forget, and a setter would have had to work on handles
whose backing object does not exist yet on D3D12 — a view *is* a descriptor there.

**`IDevice::Destroy(handle)` is immediate.** There is no retirement queue keyed on a fence
value, deliberately (D20). The engine's universal answer to "the GPU might still be using it" is
to wait — resize does, `GrowInstanceBuffers` does. **The trigger to build deferred destruction is
not "something changed", it is "a stall we can no longer afford"**, visible in `frameMs`.

**D43 — One persistent descriptor heap per kind on D3D12, sized once.** See §9.

---

## 5. Recording and submitting a frame

**D16 — Submission and command-list allocation live behind `IDevice`.** Waits and signals are
`FenceHandle` + `uint64_t` (D5). This was the seam's weakest row for two stages: `FenceHandle`
existed as a type that no interface took, while the upload context waited privately on a
`VkFence` and the frame loop's fences and pools were raw Vulkan in the engine.

**D5 — CPU/GPU synchronization is a fence and a value.** D3D12 has exactly one primitive, an
`ID3D12Fence` and a monotonically increasing value; Vulkan's equivalent is a timeline semaphore,
core since 1.2. Modelling the intersection means **a wait can name a point in the past and return
immediately**, which is what makes "wait for the frame that used this slot" expressible without
resetting anything. Binary semaphores cannot be eliminated on Vulkan — `VUID-vkAcquireNextImageKHR-semaphore-03265`
and `VUID-vkQueuePresentKHR-pWaitSemaphores-03267` both require `VK_SEMAPHORE_TYPE_BINARY` — but
they are an implementation detail of the present path and never appear in a neutral header.

**D42 — A submit names the image it writes, and semaphores left the seam.** `SubmitDesc` carries
an optional `PresentTargetImage` — the target and the index its `Acquire` handed out — and
`SemaphoreHandle` is gone. The engine used to copy two semaphores out of the target into the
submit without deciding anything about them; what it actually knows is *which image this submit
writes*, and that is enough for the Vulkan backend to find both privately. D3D12 needs neither:
presents occur on the queue given at swapchain creation, so a present is already ordered behind
the rendering on it. *Rejected: semaphores kept but empty on D3D12*, a Vulkan-only concept left
in the seam with a validity rule at every call site; *rejected: emulating them on D3D12*,
synchronisation objects that synchronise nothing. **The cost:** `Submit` is coupled to
`IPresentTarget`, and a backend must recognise its own targets and refuse another device's.

**D19 — Command allocators are caller-owned, one per frame per recorder.** The frame already
records on several threads at once — the opaque and transparent recorders run on job threads
while the main thread records clouds, composite and ImGui — and `vkResetCommandPool`,
`vkAllocateCommandBuffers` and `ID3D12CommandAllocator` all require external synchronization on
the allocator. Queue affinity sits on the allocator, because both APIs put it there.

*Rejected: device-managed thread-local pools.* The call site would be smaller, and the pool count
becomes workers × frames rather than recorders × frames, because the job system does not pin a
recorder to a thread. Worse, resetting frame N's pools means touching every worker's pool from a
thread that does not own it — which resolves into either a lock or a job-system dependency inside
the RHI. **The deciding argument is that both APIs make the same external-synchronization promise
about the same object, so the neutral layer can state it honestly rather than approximate it.**
Hiding a thread-affinity rule behind a convenient call site produces intermittent corruption on
someone else's driver. **Reset is the dangerous operation** — it invalidates every list the
allocator produced — and caller ownership is what makes that moment visible.

**D17 — Dynamic rendering is the neutral rendering-scope model.** An attachment description —
view handle, load and store op, clear value — plus `BeginRendering`/`EndRendering`. **Render pass
objects are not reintroduced, here or later, and neither is a subpass concept**: D3D12 has no
equivalent, and adding one would be inventing a lowest common denominator neither API wants. The
renderer already used `vk::RenderingInfo`, which is much closer to `OMSetRenderTargets`, and
`vk::PipelineRenderingCreateInfo`'s colour formats correspond to a PSO's `RTVFormats`.

**D41 — Cull mode is a pipeline property, not command-list state.** D3D12 bakes it into the PSO
and no version of `ID3D12GraphicsCommandList` sets it, so `SetCullMode` and `bDynamicCull` left
the seam. The engine creates one opaque pipeline per cull mode — two, sharing one layout — and
the recorder picks per batch. This also deletes a rule the engine had already tripped on: a
Vulkan command buffer starts with *no* dynamic cull mode at all
(`VUID-vkCmdDrawIndexed-None-07840`). **The cost:** Vulkan gives up dynamic state it has, and each
future per-draw rasterizer variation multiplies pipelines at the call site — a problem handed,
visibly, to the material system.

---

## 6. Binding and pipelines

**D7 — superseded by D14.** It deferred the binding model on the grounds that bindless would
make the question moot. The half that held — Stage 5 left the model isolated rather than
abstracted — is history. **The cost it named is real, and D14 accepts it.**

**D8 — first half superseded by D15.** Pipelines stayed Vulkan-side only because the binding
model was not neutral. **The rest of D8 stands**: the pipeline *cache* is a neutral opaque blob
and `IPipelineCache` does not change shape — create it, hand it to pipeline creation, save it —
and its note that dynamic rendering is the portable choice is reaffirmed as D17.

**D14 — bindless is deferred until after D3D12; the binding model is narrow and neutral.** Four
reasons D7 did not survive. The architecture plan already required a non-bindless fallback, so
bindless was never a way to avoid designing the conventional model — only a second path on top
of it. The groundwork was not in place: only `descriptorBindingPartiallyBound` is enabled, and
bindless additionally needs `runtimeDescriptorArray` and
`shaderSampledImageArrayNonUniformIndexing` at minimum. Bindless does not remove the binding
model even where it applies — D3D12 sampler heaps are separate and cap at 2048, so samplers stay
conventional, and per-frame constants do too. And the convergence is version-gated on SM6.6.

So the model is scoped to the layouts that exist: **six of them** — global, composite, depth,
material, cloud dispatch, cloud noise bake — plus four push-constant ranges spanning two shader
stages. **What it costs:** `TextureBinding::COUNT` stays 3, so no emissive or occlusion maps
until the cap is raised deliberately. That turned out to cost less than assumed — neither is
parsed by the loader, present in `MaterialData`, or referenced by any shader, so raising the cap
would be *adding a feature*. **What it buys:** bindless lands later behind a stable seam and can
be verified on both backends rather than guessed at on one.

**D20 — Bind groups are immutable.** Created from a complete description; changing what one
points at means creating another. Every user has a natural creation point: a material set is
built when the material loads, the global set is one per frame in flight and only its buffer's
*contents* change, and the composite and depth sets are rebuilt on resize because their targets
are. **The argument is that the in-flight hazard stops being a rule and becomes inexpressible** —
writing a descriptor the GPU is still reading is left entirely to the caller in the mutable form,
unmentioned by any signature, and reproduces on someone else's driver rather than yours. The
immutable form has no call that can do it. It also maps directly onto a D3D12 descriptor table,
which is a baked range in a shader-visible heap.

What would force per-frame recreation is not in the roadmap: a frame graph forces it only if the
physical resource behind a logical one changes between frames, and ping-pong effects are served
by two alternating groups. An editor displaying arbitrary textures, or texture streaming, would
— and bindless removes that second pressure. The answer then is a **transient** variant from an
arena reset wholesale at frame boundaries, which needs no retirement queue.

**D21 — The vocabulary is narrow, and the inventory is pinned by a test.** Two mechanisms,
because one does not cover both risks. A curated `BindingType` with `default:`-free switches in
each backend is the *completeness* half: whatever the vocabulary becomes, both backends implement
all of it or the build fails. But that says nothing about the model *growing into* a
general-purpose descriptor abstraction, which is the risk D7 actually named — so
`BindGroupLayoutInventoryTests` asserts the exact set of layouts and their shapes. A fifth
layout, or a fourth binding on the material set, fails it, and `CLAUDE.md` forbids changing an
existing expectation without asking. **That converts "the model grew" from something noticed in
review, or not, into something that cannot land without a conversation.** The caveat, recorded so
it is not rediscovered as a complaint: if the inventory starts moving every other stage, that is
the signal the test has outlived its purpose, not that the rule needs relaxing.

**D22 — Samplers are separate from textures; combined image samplers are not in the vocabulary.**
**D3D12 cannot express one.** Samplers live in their own heap type, separate from CBV/SRV/UAV and
capped at 2048 shader-visible entries; a single descriptor holding both does not exist. A neutral
`CombinedTextureSampler` would therefore be a concept one backend has to *decompose* rather than
map. It is also the better model on its own terms: a small palette bound once — linear-wrap,
point-clamp, aniso — lets a shader choose filtering by naming one, so changing how a normal map
is filtered becomes a shader edit rather than a descriptor rewrite in C++.

**D23 — Pipeline layouts are explicit.** A `PipelineLayoutHandle` is an ordered list of bind
group layouts plus push-constant ranges, 1:1 with `VkPipelineLayout` and `ID3D12RootSignature`;
a range carries its shader stage. *Rejected: an implicit layout* derived from the pipeline
description. It does not remove the object — both APIs have a real one — it moves it where the
caller cannot see it and adds a hash to find it again, which means either a layout per pipeline
(wasteful on D3D12, where a root signature is heavyweight and meant to be shared) or a cache
whose key must be exactly right in the layer whose whole job is to not be subtly wrong on one
backend. **The deciding argument is that root-signature identity determines whether bound
descriptor tables survive a pipeline change** — a performance property worth reasoning about,
and exactly what the recorders lean on when they bind the global set once and the material set
per batch.

*Rejected, and worth recording because it looks like an obvious inclusion: dynamic offsets.*
D3D12 has no dynamic offset for a table entry; its analogue is a root CBV, a different kind of
root parameter. So the neutral concept is not "an offset on the bind call" but a distinction in
the layout between a binding in a table and one inline in the root signature — a generalisation
nothing needs yet.

**D15 — Pipelines are neutral.** `GraphicsPipelineDesc`, `ComputePipelineDesc` and
`PipelineLayoutDesc` describe both backends. **Amended (13 September 2026): the D3D12 backend
builds no pipeline cache.** `Save()` writes nothing; the promise that a later run is faster is
kept on real hardware by the driver — the RX 580 reports every `D3D12_SHADER_CACHE_SUPPORT` flag
including the OS-managed automatic disk cache. `ID3D12PipelineLibrary` was weighed and declined:
six pipelines, a driver that already caches them, CI runners starting with an empty disk, and a
library needing a cross-process hash, a lock (its reference page says loading one pipeline from
several threads "should synchronize themselves", while the seam promises concurrent creation) and
stale-file handling. It is a `backlog.md` row with a measured trigger. **The cost, accepted:**
WARP has no automatic cache, so every WARP run recompiles every pipeline. The cache file's name
carries the backend, or two backends on one machine overwrite each other's blob.

---

## 7. Shaders and the shader build

**D12 — Shaders are already portable; keep them that way.** Slang emits SPIR-V and DXIL as
first-class targets, so the shader *language* was never the problem — the shader *build* was. Do
not add SPIR-V-specific workarounds without an `#ifdef` on the target profile.

**D24 — Shader bytes come from the caller; the packaging difference is absorbed by the build.**
`CreateShaderModule` takes bytes, `DeviceCaps` reports the extension the device eats, and `Paths`
does the resolving — **the RHI never touches a file.** And the build emits **one blob per stage
for both targets**, so the runtime mapping is uniform: name, stage, extension. Without that, the
packaging difference (one SPIR-V module holding both entry points, versus a PSO taking separate
bytecode per stage) lands in the engine as a per-backend branch — precisely the `#ifdef` the seam
exists to prevent. *Rejected: the RHI resolving shader names itself*, which gives the layer whose
job is to talk to a GPU a filesystem responsibility — a widening you do not get back.

**D33 — One entry point per blob, named `main`.** After D24's split a module holds exactly one
entry point, so `ShaderStageDesc::EntryPoint` could only restate what the blob contains. It is a
state space with one valid point: Vulkan requires `pName` to match an `OpEntryPoint` of the right
execution model (`VUID-VkPipelineShaderStageCreateInfo-pName-00707`), so every other value fails
at pipeline creation, and `D3D12_SHADER_BYTECODE` is a pointer and a length that cannot read a
name at all. **A neutral description that one backend can only fail on and the other ignores
should not ask the question.** `ShaderModuleDesc::DebugName` carries the file name, so the stage
is still named in a debugger.

**D27 — DXIL is emitted on every platform, not only on Windows.** vcpkg's `shader-slang` carries
no DXC; the toolchain comes from `directx-dxc`, which installs `libdxcompiler.so` **and
`libdxil.so`** on Linux, so emission, signing and validation are all reachable where development
happens. The argument is the feedback loop rather than the artifacts: a Slang construct that
SPIR-V accepts and DXIL rejects fails in the edit that caused it. **A check that cannot run where
the work happens has the same shape as a check that always passes.**

**D29 — Shader bindings are pinned in the source, in D3D12 spelling.** With only
`[[vk::binding]]`, Slang assigns HLSL registers implicitly: everything lands in `space0` and
numbering follows declaration order, so inserting one resource renumbers every resource after it
— on D3D12 a wrong-resource read rather than an error. **Every resource carries an explicit
register, and bind group index N is register space N**, with four shift flags on the SPIR-V
compile (`-fvk-b-shift 0 all`, and `t`, `s`, `u`) making Slang derive the Vulkan binding from the
register. Two constraints: the index must be unique across classes within a space — `s3` beside
`t0`–`t2` — because Vulkan has one binding namespace per set where HLSL has four, and **a
collision is silent**, compiling clean under `-warnings-as-errors all` while aliasing the sampler
onto a texture's binding.

`[[vk::push_constant]]` stays, on the declarations that have it: Vulkan push constants are a
storage class rather than a descriptor, and without the attribute the declaration becomes an
ordinary uniform buffer. Push constants live in **space 7**, defined once in `Common.h` so C++
and Slang name the same constant. The number is a portability floor, not taste: Vulkan's Required
Limits give `maxBoundDescriptorSets` a minimum of 7 for a 1.4 implementation, so 0–6 are the most
any conformant device must expose, and 7 is the first number no portable bind group can occupy.

**D30 — Layout agreement is a unit test over reflection, not a `static_assert`.** A
`static_assert` is C++ asserting things about C++; it cannot see what either shader target thinks
the layout is. `slangc -reflection-json` reports every field's offset and size per target, and a
test compares them against `offsetof`, failing with the field name, both offsets and which target
disagreed. Measured when built: across 66 fields in `opaque.slang` the HLSL and SPIR-V
reflections agree everywhere. **So the divergence is latent rather than present — the test is
insurance against the next edit**, and without it the first struct to break the coincidence
breaks it silently on one backend.

**D31 — `ShaderTypes.h` speaks HLSL, and covers the constant blocks only.** It declares
`float4`, `float4x4` and `int`, with C++ aliasing glm into those names inside `#ifdef
__cplusplus`, which Slang's preprocessor skips. The language with the tighter constraints sets
the vocabulary, and the aliases are transparent — a `float4` *is* a `glm::vec4`. **A shared block
spells a boolean `bool32`, never `bool`**: a C++ `bool` is one byte and a shader's is four, so a
block carrying one disagrees about every offset after it. *Rejected: `#define bool int32_t`* —
`bool` is a keyword, so defining it is ill-formed, and Clang rejects it by default with
`-Wkeyword-macro` while GCC accepts it silently, which is the worse half of the result. Matrices
need a comment rather than a decision: both are 64 bytes, and what keeps them interchangeable is
the transposition convention.

**D32 — Vertex input is checked, not shared.** `VS_In` carries semantics C++ cannot express, so
the structs cannot become one struct; what is hand-mirrored is the attribute table's locations
and formats, and a reflection test asserts them — insert a field and every location after it
shifts while the C++ table keeps the old numbers. **Amended (13 September 2026): `VertexAttribute`
carries `SemanticName` and `SemanticIndex`.** `D3D12_INPUT_ELEMENT_DESC` matches on semantic name
and index, which the neutral description could not express, so an input layout could not be
filled in from what the seam said — the same class of gap as D22's combined image samplers, found
the same way. Slang's DXIL reflection lists each input's semantic, index and location together, so
the test compares semantics exactly as it compares locations. Vulkan ignores the fields.
*Rejected: reading the DXIL signature at pipeline creation*, a hand-written container parser or a
runtime `dxcompiler.dll`, with a mismatch found only when a pipeline fails.

---

## 8. Choosing and describing a device

**D25 — The backend is selected at run time, and Vulkan is always the default.** `--backend
Vulkan|D3D12`; a value the build does not contain is a **hard error** naming what was asked for
and listing what is available, because a run that quietly measured something else is worse than a
run that refused. **What run-time selection buys is the evidence model**: one build, one scene,
two runs, diff — where compile-time selection makes a cross-backend comparison an orchestration
problem spanning two builds and two CI jobs. An assertion that needs two builds stitched together
gets written once and then skipped; one that is a second `ctest` case runs on every push.

**Vulkan is the default on every platform, permanently** — not "until D3D12 reaches parity". A
bug report, a baseline capture and a run report then mean the same thing whoever produced them.
**The consequence has to be designed for rather than hoped away: if D3D12 is never a default, CI
is the only thing that will ever run it routinely.**

**D34 — Backend selection is a seam, and the RHI spells it.** `rhi/Backend.h` holds the enum, the
availability query and both string conversions — not `RhiTypes.h`, whose invariant is that every
enum in it has a `default:`-free conversion table, which `Backend` can never have; and not
`IDevice.h`, which would drag the whole API surface into the option parser.
**`AvailableBackends()` answers the *build* question, not the machine's**, returning a span over a
`constexpr` array decided by what CMake linked rather than by `_WIN32`. *Rejected: probing* —
building an instance per backend at startup turns a fact into a measurement and a pure query into
one with driver-loading side effects, and makes one word cover two unrelated failures.

The backend is a `DeviceDesc` field defaulting to `Backend::Vulkan`, so D25's permanent default
becomes a struct default, the hardest kind to get wrong. **An unavailable backend is refused
twice, deliberately**: at parse time with the usage block, and as a `CreateDevice` precondition,
because that is a public entry point the test fixture reaches without the parser.

**The RHI owns the spelling of enums that cross the process boundary** — those appearing as
command-line input or run-report output, and nothing else. One table behind `ToString`/
`FromString`, so `--backend D3D12` cannot drift from `"backend": "D3D12"`; the input half folds
case because it is typed by hand. This matters more here than elsewhere because the comparison
tool matches the report's `backend` as text to pick its tolerance. `Format` and `QueueType` never
cross the boundary and get nothing.

**D35 — Device identity is separate from device capability.** **Caps are branched on; info is
reported and never branched on** — the rule is in both comments so it survives. Putting a GPU name
into `DeviceCaps` would hand every caller the string needed to write exactly the driver branch
caps exist to prevent. `DeviceInfo` carries the device's half only — `Backend`, `Gpu`, `Driver`,
`ApiVersion` — while OS and architecture are properties of the process. **`ApiVersion` is one
opaque string that nothing parses, recording what the device supports rather than what the run
requested**: Vulkan writes `"1.4.354"`, D3D12 writes `"feature level 12_2"`, and the value rather
than the field name carries the disambiguation. *Rejected: two precise nullable fields*, which
makes the report's shape backend-dependent — and absence already means something specific to the
comparison tool.

**Amended (13 September 2026): `DeviceInfo` also carries the adapter's PCI vendor and device
IDs.** `VkPhysicalDeviceProperties::vendorID`/`deviceID` and `DXGI_ADAPTER_DESC1::VendorId`/
`DeviceId` are the same PCI identifiers by definition — the one piece of identity the two APIs
spell alike — so they let a comparison decide whether two reports from *different backends*
describe the same adapter. When `system.backend` differs, `apiVersion`, `driver` and `gpu` stop
gating pixels while the two IDs, `os` and `arch` must match. Software rasterizers carry their own
(WARP `0x1414`, lavapipe `0x10005`), so a pair of software runs is refused by identity rather than
by policy. *Rejected: a `--cross-backend` flag*, which takes comparability away from the reports;
*matching on names*, which nothing guarantees agree; *a LUID*, unique only until restart and so
never committable. **What PCI IDs do not prove:** they name the chip, not the card.

**D46 — Adapters are selected by name.** `--gpu <name-substring>`, a neutral `DeviceDesc` field
matched against Vulkan's `deviceName` and DXGI's `Description`; no match refuses and lists what
was found. A name, because enumeration order is not a stable identifier. *Rejected: an environment
variable read by the D3D12 backend*, a hidden global input — `VK_DRIVER_FILES` is read by the
Vulkan *loader*, this would be read by our own RHI. **The cost:** a substring means something only
within one backend, and a name cannot tell in-box WARP from NuGet WARP — deployment decides.

**D44 — Testing levers: disabling extensions is Vulkan's, single-queue is neutral.**
`--vk-disable-extension` is refused on a D3D12 run at parse time; D3D12 has capability bits rather
than extensions, and each branch worth forcing gets a dedicated lever as D38 gave enhanced
barriers. *Rejected: D3D12 reading the list as capability names*, where the list's rule that
unknown names are reported and ignored would let a typo run the path nobody asked for.
`--force-single-queue` and `run.forceSingleQueue` carry no prefix: `DeviceDesc` and `RunSpec`
already named it neutrally, and on D3D12 the lever moves uploads from the copy queue to the direct
one. D3D12 has no ownership transfer, so the GPU fixture's two ownership-transfer arrangements are
Vulkan's alone.

---

## 9. D3D12's own decisions

**D36 — The D3D12 backend carries the Agility SDK, for the debug layer.** **What decides it is
the debug layer, not enhanced barriers.** On Windows 10, `D3D12GetDebugInterface` fails with
`DXGI_ERROR_SDK_COMPONENT_MISSING` unless the SDK's `d3d12SDKLayers.dll` sits beside
`D3D12Core.dll`; without it, `validationErrors` would come from a validator that never loaded.
The second reason is the pinned runtime — the same D3D12 runtime here and on the CI runner, the
counterpart of pinning lavapipe with `VK_DRIVER_FILES`. The `D3D12SDKVersion` and `D3D12SDKPath`
exports must be in **every executable that can create a device**, through an OBJECT library linked
directly, exactly like `SanitizerShims`: a static library member nothing references is never
extracted. The backend checks the version the *loaded* `D3D12Core.dll` exports rather than
trusting CMake and the port to agree. *Rejected: the OS runtime*, which makes the debug layer
depend on the Graphics Tools optional feature — an environment requirement no version can pin.

**D37 — D3D12 builds two barrier paths behind one seam.** `Barrier.h` is the shape of *enhanced*
barriers. D3D12 also has legacy `ResourceBarrier`, where each subresource is in one
`D3D12_RESOURCE_STATES` value; enhanced barriers are optional per driver and the runtime
translates in one direction only. **The RX 580 reports `EnhancedBarriersSupported` false**, so a
legacy path is mandatory, and an enhanced path is built beside it and chosen on the capability
bit. Both, because the enhanced path maps nearly one-to-one onto the seam and has three places to
run — NuGet WARP locally, the same in CI, and a modern GPU weekly — and because with it present
**`TextureLayout::Undefined` stays in the seam**: enhanced barriers honour it natively, so
resolving it is private to the legacy path, and the oldest of the three barrier models does not
get to dictate the neutral API.

What the legacy path gives up: **no sync scope**, so `PipelineStage` is discarded — safe, but
D3D12 will never catch a Vulkan synchronization bug — and **no subresource range**, so a partial
range becomes several barriers. Every barrier today covers a whole resource, which keeps
`counters.frame.barriers` one-to-one with Vulkan; the first partial range breaks that unless the
legacy path counts what the caller asked for rather than what it issued. **The enhanced path is
the stricter checker**: the debug layer holds each barrier's sync, access and layout to the
Enhanced Barriers rules, which no legacy transition carries, so a sequence Vulkan and legacy
accept can still fail there.

**D38 — `--d3d12-barriers legacy|enhanced|auto` chooses the path, and an impossible request is
refused.** **An override exists so the two paths can be compared on one adapter** — without it a
modern GPU only ever runs enhanced and the RX 580 only legacy, so every difference between them is
confounded with hardware. `enhanced` on an adapter without support fails `CreateDevice`; either
value on a Vulkan run is refused at parse time. A tri-state defaulting to `auto` can tell "asked
for" from "left alone", which a boolean cannot. The report field must **not** gate the counters,
since the two paths are required to match. *Rejected: falling back to legacy with a warning*,
which turns a legacy-against-enhanced comparison into legacy against legacy, passing at zero
tolerance.

**D39 — The D3D12 device's floor: feature level 12_0 and unrestricted copy pitch.** 12_0 is the
RX 580's highest level and guarantees resource binding tier 2 and shader model 6.0. Tier 2's one
rule this engine could break — no unpopulated CBV or UAV entries in a table — it does not.
*Rejected: 11_0*, whose tier 1 forbids unpopulated entries in every heap, so the material's
optional textures would need a null-descriptor path no adapter in the test matrix could exercise.
**`UnrestrictedBufferTextureCopyPitchSupported` is also required, and `BufferTextureCopyRegion`'s
tight packing stands.** Without it D3D12 requires 256-byte row pitch and 512-byte offsets, so a
tightly packed readback buffer is the wrong size at any width not a multiple of 64; with it, in
*VulkanOn12*'s words, "both offset and row-pitch MUST be aligned only to the whole unit size of
the texture's format". *Rejected: the seam carrying the alignment*, which would edit every
readback and add vocabulary Vulkan answers trivially. **The cost, accepted:** D3D12 hardware below
12_0, and drivers without the relaxation, are refused outright — the latter in unknown numbers.
**An open condition:** the second test machine, a Windows 11 PC with a modern GPU, has not been
probed. Run the throwaway programs at `C:\Dev\d3d12-probe` there before its first session relies
on this; if its driver lacks the relaxation, the seam carries the alignment after all.

**D40 — Validation on D3D12: required, polled at every call, GPU-based by default.** The debug
layer is a hard requirement wherever validation is on, exactly as Vulkan's layer is; a layer that
does not load fails `CreateDevice`, because a degradable one would let every D3D12 scene test pass
while checking nothing. **Messages are polled**: `ID3D12InfoQueue1`'s callback is unavailable on
the Windows 10 machine's adapters even with the SDK, so each backend method ends by comparing
`GetNumStoredMessages` against its last value and draining into `Diagnostics` if it grew — which
is what puts the engine's calling line on the stack under `failfast`. *Rejected: break and catch*,
process-wide exception handling installed by the RHI around an exception code documented nowhere
as the mechanism.

**GPU-based validation patches every shader to check, at the point of use, what the CPU cannot
see** — uninitialized or incompatible descriptors, references to deleted resources, heap overruns,
and accesses to resources in incompatible state. A D3D12 descriptor names no state, so the CPU
layer knows what is bound but not what a shader reads, where Vulkan checks the equivalent on the
CPU routinely (`VUID-vkCmdDraw-None-09600`). With it off, D3D12 checks strictly less, exactly
where a mistake in D37's private `Undefined` handling would show. **Amended by measurement:**
resource-state tracking, which only `full` adds, is nearly all of the cost — a debug frame of the
test scene on the RX 580 is 1.3 ms off, 3.8 ms at `descriptors`, 22.8 ms at `full` — so
`--d3d12-gpu-based-validation off|descriptors|full` **defaults to `descriptors`** while the GPU
fixture, the scene suite and `backend_compare` all ask for `full`.

**D43 — One persistent descriptor heap per kind, sized once.** One shader-visible CBV/SRV/UAV heap
and one sampler heap created with the device, at a capacity `DeviceDesc` carries — 65,536
resource descriptors by default and the guaranteed 2,048 samplers. Every command list binds the
same two heaps, ImGui's included; a bind group receives its range at creation, identical samplers
share a slot, and exhaustion refuses with a message naming the capacity and the field. The rules
this answers: at most one of each heap type can be bound at a time, switching can cost a GPU stall
on some hardware, and descriptors cannot be changed while a submitted command list might reference
them. Measured at about 60 bytes of video memory per descriptor — 3.9 MiB at the default, against
a scene using tens. *Rejected: a heap that grows*, which needs `CreateBindGroup` never to run
while a list records (two recorders run on job threads) and cannot move under ImGui, whose DX12
backend stores the raw GPU descriptor handle as its texture ID. **The cost:** asymmetry with
Vulkan, whose pools grow — a scene that loads there can be refused here until the capacity is
raised.

---

## 10. Evidence

**D26 — Cross-backend evidence: exact counters, tolerant pixels.** Every comparison in the project
was exact, which works with one backend on one rasterizer and cannot survive a second: different
rasterizers differ in filtering, in rounding, and in float contraction in their shader back ends.

**Counters must match exactly, across backends.** They are statements about what the renderer
*decided*, not about what the rasterizer produced, so two backends disagreeing about a draw call
count is a bug in one of them, always.

**Pixels are compared with two caps** — no pixel may differ by more than N per channel, and at
most M% of pixels may differ at all. Each catches what the other misses: the ceiling catches one
catastrophically wrong pixel, the fraction catches an image drifted slightly everywhere.
*Rejected: MSE or PSNR*, where a single blazing-wrong pixel vanishes into the mean — precisely the
bug being hunted. *Rejected: SSIM*, which gives one score that is hard to act on: "0.987 against a
threshold of 0.99" does not say where or what.

Two things keep the tolerance honest. The thresholds are **committed constants**, so changing one
is changing an expected test result and needs a conversation. And the comparison **always reports
the measured delta**, not just a verdict — a bare PASS hides the approach to the cliff, and the
day it fails nobody can tell whether it fell or walked there over six months.

**Four amendments from Stage 7.7:**

- **Validation counters are held at zero, not at equality.** Two validators check different things
  at different granularity, so equality between them means something only at zero — read
  literally, the original rule would pass `1 == 1` for two unrelated warnings. *Rejected: warnings
  skipped across backends*, where a D3D12-only warning present from the first run would be agreed
  with by every same-backend comparison and asserted by nothing.
- **Each backend's own validation sub-mode must be on for counters to be compared across
  backends** — `vkSyncValidation` read from the Vulkan report, `d3d12GpuBasedValidation` from the
  D3D12 one. A sub-mode left off is mistakes the counters cannot report. *Rejected: neither gating
  across backends*, which would pass a D3D12 run with GPU-based validation off against Vulkan on
  zero warnings while checking less.
- **Upload batches are compared; upload submissions are measured.** The first D3D12 scene matched
  Vulkan's every counter but `uploadSubmissions`: 4 against 8, for the same four batches. How many
  submissions a batch costs is the backend's and the driver's — a Vulkan copy queue without
  `VK_KHR_maintenance9` hands each batch back in a second submission, D3D12 never does — so it was
  never a statement about what the engine decided.
- **Pixel constants are measured on one machine from two fresh runs, per build type, with no
  headroom.** One GPU, one driver, so the difference isolates the backend and nothing else.
  **Every differing region is explained before the constants are committed**, and anything
  unexplained is a bug fixed before re-measuring. *Rejected:* a multiple, meaningless on a large
  fraction; a fixed margin, a guess; **a limit derived from legitimate variation across presets,
  scenes or drivers — the one with an argument behind it, and the one to reach for if this is
  revisited.**

What the measurement found, and why it is worth keeping: 11,434 of 2,073,600 pixels differ in a
debug build, 62% of them by 1 and 93% by 8 or less, worst 120; release adds four sky pixels. Two
mechanisms account for all of it, each shown by a control run that removed it — **clip-space Y
reaching the framebuffer through the two APIs' opposite viewport mappings**, whose float rounding
lands some vertices on neighbouring subpixels, and **anisotropic filtering**, implementation-defined
in both specifications. The explanations live beside the constants in `ImageCompare.h`.

**The diff image scales to the worst delta on a logarithmic curve**, so the worst pixel is full
white and no differing pixel is dimmer than 32 — a straight line would draw a delta of 1 at
brightness 2, which is most of what moved.

**D28 — Windows CI asks for D3D12 explicitly.** WARP is a D3D12 adapter, so there is nothing for a
Windows GPU job to run until the backend exists, and the runner has no Vulkan ICD at all —
supplying one means pinning a third-party Mesa build, a supply-chain surface `vcpkg.json` has
otherwise kept clear. D25 is unchanged: Vulkan is still the default everywhere, and **asking for
D3D12 by name in CI is what makes CI the backend's routine exercise.**

---

## 11. How the boundary is enforced

Discipline does not hold a boundary. Four mechanisms do.

**A neutral-header check target.** `HeaderSelfContainment_RHI_Neutral` compiles every
`include/rhi/*.h` while linking only `Engine::Core` and `Engine::Platform`, so a neutral header
that includes a backend header fails to compile. A strong net, not a proof: a dependency also on
the default system include path is found regardless of what a target links.

**`tests/scripts/rhi_boundary_check.sh`.** Cheap, exact, immune to include paths, and run by CI's
`static-checks` job and by `precommit.sh`. It checks on **names rather than includes**, because a
precompiled header once put the whole API in scope for a module with no include and no allowlist
entry to show for it. Five checks:

1. No neutral header names a Vulkan, VMA, D3D12 or D3D12MA type.
2. Nothing in `engine/` or `apps/` outside the module names either API — one file per backend is
   exempt and listed by name: that backend's ImGui glue, permanently (D9).
3. The transitional headers under `include/rhi/vulkan/` and `include/rhi/d3d12/` are **frozen**,
   reachable only from allowlisted sites. Adding either fails, and so does leaving an allowlist
   entry behind after its include goes.
4. Every engine header opens its module's namespace.
5. **D45 — the backends stay out of each other** inside `engine/rhi/`, which the other checks
   exempt: `src/vulkan/` names no D3D12, `src/d3d12/` names no Vulkan or VMA, and shared sources
   name neither. A D3D12 name in Vulkan code already breaks the Linux build; **the direction
   nothing caught is D3D12 code depending on Vulkan**, which compiles everywhere D3D12 exists
   because D25 keeps Vulkan in every build. *Rejected: leaving the module exempt*, so that a port
   written with the other backend open beside it could quietly borrow its internals. **The cost:**
   a genuinely shared helper is neutral or duplicated.

The bare word `D3D12` stays legal, since `Backend::D3D12` is neutral vocabulary.

**Exhaustive switches with no `default:` label** in each backend's conversion tables. With
warnings as errors, `-Wswitch` turns "added an enumerator, forgot the mapping" into a build
failure on every CI configuration — and `/w14062` buys the same on MSVC. **Do not add a `default:`
case to a conversion switch; throw after the switch instead.**

New allowlist entries are argued for in `cmake/RhiBoundaryCheck.cmake`, next to the reason each
existing one is still there. As of the D3D12 backend landing, the transitional area is **three
headers used from four sites**, all permanent holes or argued test conveniences.

---

## 12. The concept map

How each concept is spelled on each side, and what the seam calls it. Every row is neutral; there
is no *partial* or *deferred* left.

| Concept | Vulkan | D3D12 | The seam |
|---|---|---|---|
| Instance / adapter | `VkInstance` + `VkPhysicalDevice` | `IDXGIFactory` + `IDXGIAdapter` | Inside the backend; identity via `DeviceInfo` (D35) |
| Device | `VkDevice` | `ID3D12Device` | `IDevice` |
| Queues | family index + `VkQueue` | `ID3D12CommandQueue` | `QueueType` (D6) |
| Allocator | VMA | D3D12MA | Private to each backend |
| Buffer / texture | `VkBuffer` / `VkImage` | `ID3D12Resource` | `BufferHandle` / `TextureHandle` (D2) |
| Texture view | `VkImageView` object | a descriptor in a heap | `TextureViewHandle` — same handle, different backing |
| Sampler | `VkSampler` | sampler descriptor | `SamplerHandle` (D22) |
| Barriers | sync2 triple | Enhanced Barriers triple, or legacy states | Neutral triple + presets (D4, D37) |
| Command recording | `VkCommandBuffer` | `ID3D12GraphicsCommandList` | `ICommandList` (D16, D17, D41) |
| Command pool | `VkCommandPool` | `ID3D12CommandAllocator` | `ICommandAllocator`, caller-owned (D19) |
| CPU/GPU sync | timeline semaphore | `ID3D12Fence` + value | `FenceDesc`, `FenceOperation`, `WaitForFence` (D5) |
| Present sync | binary semaphores | DXGI + the queue's own order | `IPresentTarget` + `PresentTargetImage` (D42) |
| Descriptors | sets / layouts / pools | root signature + heaps | `BindGroupLayoutDesc` + `BindGroupDesc` (D14, D20, D43) |
| Per-draw constants | push constants | root constants | `ICommandList::PushConstants` (D23) |
| Pipelines | `VkPipeline` + dynamic rendering | PSO, no render pass objects | `GraphicsPipelineDesc` / `ComputePipelineDesc` (D15, D17) |
| Pipeline cache | `VkPipelineCache` | the driver's own disk cache | Neutral opaque blob (D8, D15 amended) |
| Shaders | SPIR-V via Slang | DXIL via Slang | Bytes from the caller (D12, D24, D33) |
| Clip space | Y-down | Y-up | `bFlipClipSpaceY`, one site (D10) |
| Formats | `VkFormat` | `DXGI_FORMAT` | Curated `Rhi::Format` + tables (D11) |
| Debug names | `VK_EXT_debug_utils` | `ID3D12Object::SetName` | A `DebugName` on every `*Desc` |
| Validation | layers + messenger | debug layer + `ID3D12InfoQueue` | `Rhi::Diagnostics` (D40) |

---

## 13. Deferred, and what would reopen it

| What | Why it waits | What reopens it |
|---|---|---|
| **Bindless** (D14) | The conventional model is needed regardless; the groundwork is not enabled; it does not remove samplers or per-frame constants; and the convergence needs SM6.6 | Scheduled work behind a stable seam, verifiable on both backends |
| **A fourth material texture** (D14) | Emissive and occlusion are parsed by nothing and referenced by no shader, so raising `TextureBinding::COUNT` adds a feature rather than unblocking one | A shader that actually wants one |
| **Deferred destruction** (D20) | Nothing needs it: every mutation point already stalls | **A stall visible in `frameMs`** — not "bind groups changed" |
| **Transient bind groups** (D20) | A stable frame graph does not need per-frame recreation | An editor displaying arbitrary textures, or texture streaming |
| **Dynamic offsets** (D23) | The neutral concept is a layout distinction, not a bind-time argument; nothing would save anything by it | A site that needs one; the cost of waiting is one enum value and one argument |
| **`ID3D12PipelineLibrary`** (D15) | Six pipelines, a driver that already caches them, and a library needing a cross-process hash, a lock and stale-file handling | D3D12 markedly slower than Vulkan warm, or WARP pipeline creation dominating |
| **A `ShaderLibrary` layer** (D24) | ~120–150 lines replacing eight three-line sequences: barely shorter, and only cleaner once it owns variants, permutations or hot reload | The second shader responsibility |
| **Vtable dispatch** (D3) | The calls already cross into a driver, and recording is moving toward fewer, larger calls | Profiling disagreeing — it is contained to the boundary |
| **A growable D3D12 descriptor heap** (D43) | Switching heaps can stall, cannot move under ImGui, and cannot run while two job threads record | Scenes outgrowing any sensible capacity before bindless arrives |
| **Cross-backend pixel checks in CI** (D26) | The constants are measured on one adapter that no runner has | A `backlog.md` row with three candidate designs |
