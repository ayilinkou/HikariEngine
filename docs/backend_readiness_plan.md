# Stage 7.5 — Backend readiness: making a second backend possible

> **Retained document.** Unlike Stage 7's own plan, which was deleted when that stage ended,
> this one outlives its stage.
> Its decisions govern how the RHI's public seam spells recording, binding, pipelines and
> submission, and a D3D12 backend — plus everything written against the seam afterwards — has
> to respect them. See [§11 Retention](#11-retention).

**Created:** 5 September 2026 · **Rewritten:** 6 September 2026, after the `/grill-me`
interview §0 demanded · **Supersedes:** `rhi_extraction_plan.md` D7 and D8;
`architecture_plan.md` Part IV steps 48–56 in part, and §20's bindless row ·
**Status:** Stages 7.5 and 7.6 complete. 7.6's interview ran on 6, 11 and 12 September 2026 and
finished — D27–D35 and §4.1–§4.4 are its output. **Stage 7.7 is planned and not started**: its
interview ran on 13 September 2026, on Linux and then on this project's Windows install, and
finished — D36–D46, amendments to D15, D26, D32 and D35, and §5 are its output.

---

## Table of contents

0. [What the grill changed](#0-what-the-grill-changed)
1. [Purpose and authority](#1-purpose-and-authority)
2. [Design decisions](#2-design-decisions)
3. [The step sequence](#3-the-step-sequence)
4. [Stage 7.6 — backend prerequisites](#4-stage-76--backend-prerequisites)
5. [Stage 7.7 — the D3D12 backend](#5-stage-77--the-d3d12-backend)
6. [What this stage needs from other stages](#6-what-this-stage-needs-from-other-stages)
7. [Out of scope](#7-out-of-scope)
8. [Definition of done](#8-definition-of-done)
9. [Open investigations](#9-open-investigations)
10. [Risks](#10-risks)
11. [Retention](#11-retention)

---

## 0. What the grill changed

The first draft of this document was written in one sitting, from reading the code and the
existing plans, and it committed the project to a shape for four seams that a second backend
then has to live inside. It said so, and refused to let step B1 start until it had been
pressure-tested. That interview happened on 6 September 2026 and this document is its output.

Two things are worth recording about it, because they are the argument for doing it again.

**The plan was one day old and four of its premises had already moved.** Stage 7 landed
between the writing and the grilling: `src/` ceased to exist and the renderer became
`engine/engine/src/Engine.cpp`, so every `src/main.cpp:NNNN` reference was stale. Two
prerequisites the plan listed as pending — steps 46 and 47 — had shipped. The transitional
allowlist had grown from 17 sites to 18, because the split gave the UI backend an entry of its
own. And the plan's claim that synchronization validation was off was simply wrong:
`validate_sync` is hardcoded `VK_TRUE` in `VulkanDevice::CreateInstance`, so every Debug run
and every GPU test already has it. That last one had a step built on top of it, which the
grill deleted.

**The interview found design gaps, not just stale facts.** The frame records command buffers
on several threads at once, which B1 had not accounted for. `TextureBinding::COUNT` turned out
to cap a feature that does not exist rather than one being held back. And the combined image
samplers the material set uses cannot be expressed on D3D12 at all — the single most
consequential thing the grill turned up, and one nothing in the original document was looking
for.

The rule this produced now lives in `CLAUDE.md`: **grill before every stage**, and re-grill an
already-grilled plan against four mechanical checks before starting it.

---

## 1. Purpose and authority

Stage 5 made the RHI's **resource** API backend-neutral: devices, queues, buffers, textures,
views, samplers, barriers, formats, the pipeline cache blob. That is why
`rhi_extraction_plan.md` §8's checklist has so many rows marked *Neutral*.

It did not make the **frame** neutral. `ICommandList` is `Begin`/`End`, `Barrier` and three
copy entry points, and nothing else. Every draw the engine issues is raw Vulkan recorded into
a `vk::CommandBuffer` the application owns, against pipelines built by a Vulkan-side builder,
bound through descriptor sets the material layer writes by hand, submitted on a queue the RHI
does not hand out.

**This stage closes that gap and nothing else.** When it ends, a D3D12 backend is a matter of
implementing interfaces rather than of designing them.

It is Stage **7.5** rather than a renumbering because a whole stage inserted at 8 would cascade
through every cross-reference in three documents. The `.5` costs nothing; the renumber would
cost a day of chasing references and would leave stale ones behind. Stages 7.6 and 7.7 follow
the same precedent, which is why the roadmap now reads 7, 7.5, 7.6, 7.7, 8. That looks faintly
ridiculous and is still cheaper than the alternative.

### Authority

**For the duration of this stage, this document is the authority on the RHI's public seam.**
Where it disagrees with `rhi_extraction_plan.md`'s D0–D13, this wins — it was written later,
with the seam built and the gaps visible. Where it disagrees with Part IV, this wins for the
same reason the RHI plan did.

Two specific reversals, both argued in §2: **D14 supersedes D7** (binding) and **D15
supersedes D8** (pipelines). Everything else in D0–D13 stands unchanged, D4's barrier triple,
D10's single clip-space site, D11's curated formats and D13's D3D12-first naming especially.

### The inclusion test

One question decides what belongs here: **does a second backend need this to exist?**

Not "is this good work", not "is this nearby", not "are we already touching the file". The
test keeps the frame graph, the data-oriented rewrite and bindless out on principle rather than
by argument, and it is also the test that says when the stage is finished. Work that fails it
and is worth doing goes to `backlog.md` or stays in the stage that already owns it.

The test earned its keep during the grill: it is what sent the emissive texture map to the
backlog, and what kept step 58 in Stage 9.

---

## 2. Design decisions

`D` numbers continue the series `rhi_extraction_plan.md` §2 started, rather than restarting at
D1. Both documents govern the same seam, and two live decisions numbered D7 in different files
would be a trap for exactly the reader who most needs to find one.

D14–D18 come from the original draft. D19–D26 come from the 6 September grill of Stage 7.5.
D27–D33 come from Stage 7.6's own interview on 11 September, and D34–D35 from its conclusion on
12 September — the two of that session's decisions that govern the seam rather than the stage.
D36–D46 come from Stage 7.7's interview on 13 September 2026, which also amended D15, D26, D32 and
D35 in place; its decisions about the stage rather than the seam are in §5.

### D14 — Bindless is deferred until after the D3D12 backend; the binding model is narrow and neutral

**Supersedes D7**, which deferred the binding model on the grounds that bindless (step 70)
would make the question "largely moot". That does not hold, for four reasons.

**The architecture plan already requires the conventional path.** §20's table, row 5, mitigates
bindless portability with "keep a non-bindless fallback path behind a device-capability flag;
the `gpu` test suite runs both". Honour that and the conventional binding model gets built
regardless — so bindless is not a way to avoid designing it, it is a second path layered on
top of it. D7 and that row have pointed in different directions since both were written.

**The groundwork is not in place.** §20's row and the plan's §5 S2-3 both say descriptor
indexing is "already enabled". What `VulkanDevice.cpp` enables is one bit,
`descriptorBindingPartiallyBound`, and it is used for the partially-bound material set that
lets an untextured material render. Bindless additionally needs `runtimeDescriptorArray` and
`shaderSampledImageArrayNonUniformIndexing` at minimum, and realistically
`descriptorBindingVariableDescriptorCount` and the update-after-bind bits. None are enabled.
Step 70 is rated XL for good reason.

**Bindless does not remove the binding model even where it applies.** D3D12 sampler heaps are
separate from the CBV/SRV/UAV heap and cap at 2048 entries, so samplers stay conventional in
practice. Per-frame constants stay conventional too — a root CBV or dynamic UBO beats indexing
camera and light data through a heap. Bindless removes the *material* set; the global set, the
sampler path and the pipeline layout all survive it.

**The convergence is version-gated.** D3D12's `ResourceDescriptorHeap` needs SM6.6, the
Agility SDK and a recent driver. Below that the shape is descriptor tables with volatile
ranges, which is not the same design. D7's "bindless converges the two APIs" holds at the top
of the stack and weakens underneath it.

**So:** build a neutral binding model scoped to the layouts that exist today, plus the
push-constant ranges those layouts carry. That maps 1:1 onto a Vulkan descriptor set and a
D3D12 descriptor table plus root constants.

**Correction from the grill.** The first draft said "four layouts and one range". There are
**four ranges, not one**, and they span two shader stages: fragment `MaterialData` on the
opaque and transparent layouts, compute `CloudPushConstants` on the cloud dispatch, and compute
`BakeConstants` on the noise bake. The composite layout has none. The neutral model must
therefore express a range's stage, not merely its size. See D23.

**Second correction, found while building step 4: there are six layouts, not four.** The
original count listed the global, material, composite and depth sets and stopped there.
`CloudSystem` owns two more of its own, and neither appeared in any step's scope:

| Layout | Bindings | Where |
|---|---|---|
| Global | uniform buffer | done, step 4 |
| Composite | 4 textures + 1 sampler | done, step 4 |
| Depth | 1 texture, pixel *and* compute | done, step 4 |
| Material | 3 textures, partially bound | step 5 |
| **Cloud dispatch** | **storage image + combined image sampler** | **step 11** |
| **Cloud noise bake** | **storage image** | **step 11** |

Three things follow, and the first is the one that matters.

**`UnorderedAccessTexture` is not optional.** Both cloud layouts bind storage images —
`RWTexture2D` and `RWTexture3D` in the shaders — and `BindingType` has no value for them. It
cannot be added when convenient; it is a prerequisite of those sets becoming neutral, which is
step 11. When it lands the pair reads the way `TextureLayout`'s `ShaderResource` and
`UnorderedAccess` already do.

**The cloud dispatch set holds a combined image sampler**, which D22 forbids outright. So step
11 carries a second sampler split, in `clouds.comp.slang`, exactly as step 4 carried the
composite one.

**The narrowness argument is unaffected, but its arithmetic was wrong.** Six layouts across a
whole renderer is still narrow, and the ratchet still holds — but a count used as evidence for
narrowness has to be the real count. The pinned inventory ends at six, not four, and
`BindGroupLayoutInventoryTests` grows to match at steps 5 and 11 rather than only at step 5.

**What it costs.** `TextureBinding::COUNT` stays 3, so no emissive, occlusion or clearcoat maps
until either the cap is raised deliberately or step 70 lands. The grill checked what that
actually costs and the answer is less than `suggested_work.md` §2.6 assumed: emissive and
occlusion are not parsed by the loader, are absent from `MaterialData`, and are referenced by
no shader. Nothing is being dropped on the floor, so raising the cap is *adding a feature*
rather than unblocking one — which fails the inclusion test outright. It is now its own backlog
row, not a rider on step 70.

**What it buys.** Step 70 stops being on the critical path to a second backend, and lands
afterwards as a change behind a stable seam that can be verified on *both* backends instead of
guessed at on one. §20's row 5 also stops being live for the duration: with no bindless path
there is no fallback to maintain, one fewer capability flag and one fewer axis in the GPU
suite.

**The risk D7 was right about** is that a narrow model metastasises into the general one as
passes multiply. D21 is the mitigation, and it is a stronger one than the first draft promised.

### D15 — Pipelines become neutral in this stage

**Supersedes D8's first half.** D8 kept `PipelineBuilder` Vulkan-side because "neutralizing
pipeline creation means neutralizing the binding model (D7), so it waits". D14 neutralises the
binding model, so the reason expires and pipelines follow in the same stage.

D8's second half stands and is reaffirmed by D17: the pipeline *cache* is already a neutral
opaque blob, and `IPipelineCache` does not change shape here. The grill confirmed this against
the header — `PipelineCacheDesc{Path, DebugName}` and `Save()`, whose own documentation says
the whole of what a caller does is create it, hand it to pipeline creation and save it. Only
what it is handed to changes.

One consequence does follow, from D25: a single machine can now run both backends, so the
cache's default path must be backend-distinguished or the two will overwrite each other's blob
on every run.

**Amended by Stage 7.7 (13 September 2026): the D3D12 backend builds no pipeline cache, and
`IPipelineCache` does not move.** On D3D12, `Save()` writes nothing; the promise that a later run
is faster is kept on real hardware by the driver, which on the RX 580 reports every
`D3D12_SHADER_CACHE_SUPPORT` flag including the OS-managed automatic disk cache, documented as
storing "compiled shaders on disk to accelerate future runs of the application". `PipelineCache.h`'s
comment says so. The cache file's name gains the backend, as above. `ID3D12PipelineLibrary` would
store pipelines under names, and a library keyed by a stable hash of each description was weighed:
six pipelines, a driver that already caches them, CI runners that start with an empty disk, and a
library that needs a cross-process hash, a lock — its reference page says loading one pipeline from
several threads "should synchronize themselves" — and stale-file handling. It went to `backlog.md`,
with its trigger: warm `startupMs` and `firstFrame` on both backends and on NuGet WARP, read from the
first D3D12 scene reports. *Rejected: a caller-chosen cache name on the pipeline description*, which
would break the promise D8 and D17 made. **The cost, accepted:** in-box and NuGet WARP have no
automatic cache, so every WARP run recompiles every pipeline.

### D16 — Submission and command-list allocation move behind `IDevice`

`rhi_extraction_plan.md` §8 calls CPU/GPU sync "the weakest row": `FenceHandle` exists as a
type in `Handles.h` and **no interface takes one**. `IUploadContext::Flush` waits on a
`VkFence` it owns privately — not even the timeline semaphore D5 settled on — and the frame
loop's fences, binary semaphores and command pools are raw Vulkan in the engine. §8 expected
Stage 6 to build this because Stage 6 was where a caller would first wait on something the RHI
owned. Stage 6 shipped without it, so the shape is decided and unbuilt, and no step in Part IV
owns it.

It lands here, first. The RHI allocates and recycles command lists per queue and takes them
back at submit, with waits and signals expressed as `FenceHandle` + `uint64_t` (D5) plus the
present target's `SemaphoreHandle`s (D5's binary-semaphore carve-out for the swapchain is
unaffected).

This is deliberately first, and it is the one seam that does not depend on the other three: a
command list the RHI hands out can be recorded through the native escape hatch while the
recording API is still being built. The grill verified the mechanism rather than assuming it —
`WrapCommandList(IDevice&, vk::CommandBuffer)` and `GetNative(ICommandList&)` already exist and
already convert in both directions, so the intermediate state is one the codebase can express
today.

D19 settles the part this decision left open: *what* the RHI hands out.

### D17 — Dynamic rendering is the neutral rendering-scope model

The renderer already uses `vk::RenderingInfo` rather than `VkRenderPass`/`VkFramebuffer`
objects, which D8 recorded as a favourable accident: it is much closer to D3D12's
`OMSetRenderTargets`, and `vk::PipelineRenderingCreateInfo`'s colour formats correspond to a
PSO's `RTVFormats`.

The neutral form is an attachment description — view handle, load and store op, clear value —
and `BeginRendering`/`EndRendering` on `ICommandList`. **Render pass objects are not
reintroduced**, here or later, and neither is a subpass concept: D3D12 has no equivalent and
adding one would be inventing a lowest common denominator that neither API wants.

That last clause is the general principle behind D22 as well.

### D18 — The seam lands before the pass conversions, not during them

Part IV's steps 50–54 convert each recorder into a `Pass` class. Those conversions and this
stage touch the same recorders, and the order matters.

**This stage first.** Steps 8–11 move the recorders onto the neutral API in place, and Stage
8's 50–54 then move already-neutral code into `Pass` classes — a structural move with no API
change. Each recorder is touched twice, but for two clearly separated reasons, which is the
plan's own stated philosophy about not superimposing two large refactors.

The alternative — convert to `Pass` classes first — means `Pass::Execute` takes a
`vk::CommandBuffer` and every pass is then edited again when it stops doing so. That is the
same two touches with the second one spread across a class hierarchy instead of concentrated
in one step, and it means designing the `Pass` abstraction against Vulkan, which is exactly
what §7 refuses to do for the frame graph.

The grill sharpened the evidence for this. `CloudSystem`'s create info takes
`vk::raii::DescriptorSetLayout&`, `CommandPool&` and `Queue&` by reference, and `RecordDispatch`
takes a `vk::raii::CommandBuffer&` and two `vk::raii::DescriptorSet&`. Converting it to a
`CloudPass` first would mean writing a `Pass` whose *constructor signature* names Vulkan, and
rewriting that signature within the same stage.

The cost, accepted: the recorder work lands inside a 2,476-line `Engine.cpp`, which stays large
until Stage 8 dismantles it into a shape the frame graph decides. That is the right thing to
accept — dismantling it earlier means guessing at that shape. It is also why steps 8–11 are one
recorder each rather than one step (D-series aside, see §3).

### D19 — Command allocators are caller-owned, one per frame per recorder

D16 says command lists move behind `IDevice`. It does not say what a caller gets, and the
answer matters more than it looks, because **the frame records on several threads at once**:
`RecordOpaqueCommandBuffer` and `RecordTransparentCommandBuffer` are submitted to the job
system while the main thread records clouds, composite and ImGui.

`vkResetCommandPool` and `vkAllocateCommandBuffers` both require external synchronization on
the pool. `ID3D12CommandAllocator` carries the identical rule. Two threads recording
concurrently is therefore not an optimisation to be added later; it is what the frame already
does, and any neutral shape has to survive it.

**The RHI hands out a caller-owned allocator.** `ICommandAllocator` — D3D12's term, per D13 —
created per frame per recorder, reset as a unit, handing out `ICommandList`s. Queue affinity
sits on the allocator rather than the list, because both APIs put it there: a `VkCommandPool`
carries a queue family index and an `ID3D12CommandAllocator` carries a
`D3D12_COMMAND_LIST_TYPE`.

This mirrors what the engine already has. `CreateCommandPools` builds seven pools per frame —
draw-layout, opaque, cloud, transparent, composite, ImGui, final-layout — plus one generic, and
they are keyed by *recorder*, which is what makes the parallel recording safe: two threads
never touch one pool. So step 1 is a move, not a redesign.

**Rejected: device-managed thread-local pools.** The call site would be smaller
(`AcquireCommandList(QueueType)` and nothing else), and the costs are real. Pool count becomes
workers × frames rather than recorders × frames, because `SharedQueueJobSystem` does not pin a
recorder to a thread. Worse, resetting frame N's pools then means touching every worker's pool
from a thread that does not own it, under a rule that says do not — which resolves either into
a lock or into per-thread resets scheduled onto each worker, putting a job-system dependency
inside the RHI.

The deciding argument is that both APIs make the same external-synchronization promise about
the same object, so the neutral layer can express it *honestly* rather than approximate it.
Hiding a thread-affinity rule behind a convenient call site produces intermittent corruption on
someone else's driver, which is the category `CLAUDE.md` already refuses to gamble on.

**Reset is the dangerous operation**, and caller ownership is what makes it visible: reset
invalidates every list the allocator has produced, and it is the one place a use-after-reset
bug can live. A caller that names the object can see the moment.

### D20 — Bind groups are immutable

A bind group is created from a complete description and cannot be written into afterwards.
Changing what it points at means creating a new one.

Every current user already has a natural creation point. A material set is built once when the
material loads. The global set is one per frame in flight and never changes identity
afterwards — only the buffer's *contents* change, which immutability does not touch. The
composite and depth sets are rebuilt on resize, because their targets are.

**The argument is that the in-flight hazard stops being a rule and becomes inexpressible.**
Writing a descriptor the GPU is still reading is a hazard the mutable form leaves entirely to
the caller, unmentioned by any signature, caught by the validation layers only under the right
settings, and reproducing on someone else's driver rather than yours. The immutable form does
not have a call that can do it.

It also maps more directly onto D3D12, where a descriptor table is a baked range in a
shader-visible heap.

**This settles what the first draft left as an open question**: the composite and depth sets
*are* bind groups. The worry was that Stage 8's frame graph may want to own them as transient
resources and that modelling them now builds something step 56 replaces. It does not. A frame
graph owns when views are created and how their memory aliases; something still has to bind
them, and binding is what a bind group is. Step 56 changes who calls `CreateBindGroup`, not
whether the concept was worth having. Leaving them out, meanwhile, would strand the composite
recorder at step 8 and keep `GetImageView` alive in `VulkanNative.h`, so step 12 could not seal
anything.

**No deferred destruction is built here, deliberately.** Immutability plus per-frame recreation
would require a retirement queue keyed on the fence value that says a frame's work is done, and
the RHI has none: `IDevice::Destroy(handle)` is immediate. Nothing needs one yet. All four sets
are stable, and the only mutation point — resize — already stalls, so nothing is in flight when
they are replaced.

The grill checked what would force per-frame recreation and the honest answer is: nothing in
the roadmap. A frame graph forces it only if the physical resource behind a logical one changes
between frames, and in a stable graph it does not. Ping-pong effects are served by two
alternating immutable groups, exactly like frames in flight. What *would* force it is an editor
displaying arbitrary textures — an asset browser, a material inspector, a render-target debug
view — or texture streaming, where residency changes under a material. Note that bindless
removes that second pressure rather than adding to it.

**The trigger to watch for is not "bind groups changed", it is "a stall we can no longer
afford".** Deferred destruction is the mechanism that replaces stalls, and this engine's
universal answer to "the GPU might still be using it" is currently to wait — resize does it,
`GrowInstanceBuffers` does it. When a stall becomes visible in `frameMs`, build the retirement
queue; bind groups will be one more thing that uses it.

When step 56 does want per-frame bind groups, the answer is a **transient** variant allocated
from an arena reset wholesale at frame boundaries — no individual destroy, so no retirement
queue and no heap fragmentation. Immutability does not preclude it. That is a written
prerequisite of step 56 rather than a surprise found halfway through it.

### D21 — The binding vocabulary is narrow, and the inventory is pinned by a test

D14 promised "the `Rhi::Format` ratchet" as the mechanism for keeping the binding model narrow.
The grill established that the promise does not transfer, and this decision replaces it.

The `Format` ratchet works because a format is a **leaf value**: a curated enum plus a
`default:`-free switch in the backend means adding one without mapping it fails the build. That
is a *completeness* mechanism. It does not stop the enum growing; it stops it growing unmapped.
A binding model is a structure, not a leaf, so the same trick catches "you added a binding type
and did not map it on D3D12" and says nothing whatever about "the model grew into a
general-purpose descriptor abstraction" — which is the risk D7 actually named.

**Two mechanisms, therefore.**

**A curated `BindingType` with `default:`-free switches in each backend.** This is the
completeness half, and it is the `Format` ratchet applied where it genuinely fits. Whatever the
vocabulary becomes, both backends implement all of it or the build fails.

**A pinned layout inventory, as a unit test.** The test asserts that the engine creates exactly
four bind group layouts, with exactly these shapes. A fifth layout, or a fourth binding on the
material set, fails it — and `CLAUDE.md` already forbids changing an existing test's
expectation without asking first. That is what makes it work: it converts "the model grew" from
something noticed in review, or not, into something that **cannot land without a
conversation**. It is the same governance the transitional allowlist uses, expressed as a test
because layouts are built at runtime and a static check would have to parse C++ to see them.

**The caveat, recorded so it is not rediscovered as a complaint.** A test that pins an inventory
gets edited whenever the inventory legitimately changes, and if that happens often it becomes
noise people edit reflexively. Four layouts across a whole renderer, with step 70 deferred,
should not move often. If the count starts moving every other stage, that is the signal the
test has outlived its purpose — not that the rule needs relaxing.

### D22 — Samplers are separate from textures; combined image samplers are not in the vocabulary

**D3D12 cannot express a combined image sampler.** Samplers live in their own descriptor heap
type, `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER`, separate from the CBV/SRV/UAV heap and capped at
2048 shader-visible entries. A single descriptor holding both a texture and a sampler is not a
naming difference; it does not exist.

So a neutral `CombinedTextureSampler` binding type would be a concept one backend has to
**decompose** rather than map — the D3D12 backend splitting one neutral binding into two root
signature entries across two heaps, with the layout description needing interpretation instead
of translation. That is the same trap D17 refuses when it rules out reintroducing render pass
objects.

The engine is already half-way there, which makes this smaller than it sounds: the composite
set is three `eSampledImage` plus one `eCombinedImageSampler`, the depth set is a plain sampled
image, and only the material set is fully combined. Step 5 finishes a split that is already
under way.

**It is also the better model on its own terms.** With separate samplers, a small palette bound
once — linear-wrap, point-clamp, aniso — lets a shader choose its filtering by naming one, so
changing how a material's normal map is filtered is a shader edit rather than a descriptor
rewrite in C++. That is not available at all with combined, where the sampler is baked into the
descriptor write. It survives bindless too: D14 already notes samplers stay conventional under
D3D12's heap limits, so a sampler palette is where this ends up regardless.

This is scoped into step 5 rather than deferred, because doing it later means editing the
pinned inventory of D21 — which requires a conversation — and touching the same four shaders,
`MaterialFactory` and `PBRMaterial` a second time. Same sampler state, same results, so the
baseline is expected pixel-identical, which gives the change a real check.

### D23 — Pipeline layouts are explicit

A `PipelineLayoutHandle` is created from an ordered list of bind group layouts plus
push-constant ranges. Pipeline descriptions reference one; `SetBindGroup` and `PushConstants`
take one. It is 1:1 with `VkPipelineLayout` and `ID3D12RootSignature`.

**Rejected: an implicit layout** derived from the pipeline description, with no public handle.
That is fewer public concepts, which is what D14's "narrow" asks for, and it does not actually
remove the object — both APIs have a real one — it moves it somewhere the caller cannot see and
adds a hash to find it again. The backend would then either create a layout per pipeline, which
is wasteful on D3D12 where a root signature is heavyweight and meant to be shared across many
PSOs, or deduplicate by hashing the description, which introduces a cache whose key must be
exactly right in the layer whose entire job is to not be subtly wrong on one backend.

The deciding argument is that root-signature identity is what determines whether bound
descriptor tables survive a pipeline change. That is a performance property worth being able to
reason about on both backends, and it is exactly what the opaque and transparent recorders lean
on today when they bind the global set once per recorder and the material set per batch.

It costs less against D14's narrowness goal than it appears: one new handle type and four
objects created at startup, in exchange for a pipeline description that shrinks to a layout
reference plus shader modules plus state.

**A range carries its stage.** See D14's correction: there are four ranges across two shader
stages, not one fragment range.

**Rejected, and worth recording because it looks like an obvious inclusion: dynamic offsets.**
Vulkan's dynamic uniform buffer is a descriptor *type* declared in the layout, with an offset
supplied at bind time. D3D12 has no dynamic offset for a descriptor table entry; its analogue
is a root CBV, which takes a GPU virtual address at bind time but is a different kind of root
parameter from a table. So the neutral concept is not "an offset on the bind call" — it is a
distinction in the layout between a binding that lives in a table and one that is inline in the
root signature, and only once that exists does a bind-time offset mean anything.

That is a generalisation, and D21's ratchet exists to keep generalisations out until something
needs them. Nothing does: the global set is a plain `eUniformBuffer` with one set per frame in
flight, and there is no site where a dynamic offset would save anything. If a case appears, the
cost of having waited is one enum value in the layout description plus an argument on the bind
call, added at a point where its purpose is known.

### D24 — Shader bytes come from the caller; the packaging difference is absorbed by the build

Today every caller passes a **file path** — `.Shaders(m_Paths.Shader("opaque.spv").string())` —
and the builder reads it. That is Vulkan-shaped twice over. The extension obviously is. Less
obviously, so is the packaging: `opaque.spv` is one module holding both `vertMain` and
`fragMain`, because `-fvk-use-entrypoint-name` lets Vulkan select an entry point out of a
module, whereas a D3D12 PSO takes separate bytecode per stage.

**The engine loads the bytes; the device reports what format it eats; the RHI never touches a
file.** `CreateShaderModule` takes bytes, `DeviceCaps` reports the format or extension, and
`Paths` — which the engine already owns — does the resolving.

**And the build emits one blob per stage for both targets**, so the runtime mapping is uniform:
name, stage, extension. `opaque.vert.spv` and `opaque.frag.spv` alongside `opaque.vert.dxil`
and `opaque.frag.dxil`. Without this, the packaging difference lands in the engine as a
per-backend branch — one file on Vulkan, two on D3D12 — which is precisely the `#ifdef` the
seam exists to prevent.

The cost is honest and worth stating: the Vulkan side of the shader build changes before D3D12
exists, splitting `opaque.spv` into two blobs and re-running the baseline to prove nothing
moved, for the benefit of a backend not yet written. It is worth it, because the alternative is
discovering the packaging mismatch while writing a PSO, and because it makes the second target
a copy of the first rather than a special case.

**Rejected: the RHI resolves shader names itself.** The engine would hold zero backend
knowledge, which is attractive, and the price is that the layer whose job is to talk to a GPU
acquires filesystem responsibilities and needs a shader root handed to it. That is a widening
you do not get back.

**Rejected for now: a `ShaderLibrary` layer** owning the name-to-bytes mapping, caching modules
and owning their lifetime. Roughly 120–150 lines, replacing about eight three-line sequences —
so barely shorter, and only meaningfully cleaner once it also owns variants, permutations or
hot reload, none of which exist. It fails the inclusion test. It is a backlog row whose trigger
is the second shader responsibility, and the engine-side helper that emerges from D24 is about
sixty lines short of being it, so promoting it later is a rename and a move.

### D25 — The backend is selected at run time, and Vulkan is always the default

The project is cross-platform, and a Linux build cannot contain a D3D12 backend at all. So
Windows links both backends and Linux links Vulkan only.

**Selection is at run time, not compile time.** `--backend Vulkan|D3D12` feeds `RunSpec`. A
value the build does not contain is a **hard error** naming what was asked for and listing what
is available — the same policy `backlog.md` argues for `--present-mode`, for the same reason: a
run that quietly measured something else is worse than a run that refused.

The flag is `--backend` rather than `--rhi` because "backend" is the word this project already
uses everywhere for exactly this concept — "backend-neutral", "the backend lives in
`engine/rhi/src/vulkan/`", and a document titled *backend readiness*. It is also more accurate:
there is one RHI, and what is being selected is which implementation it uses. The word is
mildly overloaded against `IUiBackend`, which does not bite, because the UI backend is not
user-selectable.

**What run-time selection buys is the evidence model.** Under compile-time selection,
comparing two backends means two builds, two CI jobs and a comparison spanning build artefacts
— an orchestration problem before it is a rendering one. Under run-time selection it is one
build, one scene, two runs, diff, and a scene test can assert cross-backend counter equality in
a single CI job. An assertion that requires two builds stitched together gets written once and
then skipped; one that is a second `ctest` case runs on every push.

**The cost, accepted:** a Windows build carries both VMA and D3D12MA and ships both `.spv` and
`.dxil` beside the executable, and the RHI's creation seam gains a neutral `Backend` enum plus
an availability query — a small, permanent widening of the public API.

**Vulkan is the default on every platform, permanently.** Not "until D3D12 reaches parity" —
always. D3D12 is reached only by asking for it. The benefit is that a bug report, a baseline
capture and a run report mean the same thing whoever produced them and wherever.

**The consequence has to be designed for rather than hoped away:** if D3D12 is never a default,
**CI is the only thing that will ever run it routinely.** That promotes Stage 7.6's Windows GPU
job from useful coverage to the backend's sole regular exercise, and it is the reason that job
is a prerequisite of the backend rather than a follow-up to it.

### D26 — Cross-backend evidence: exact counters, tolerant pixels

Every comparison in the project today is exact — counters must match the committed baseline
exactly, and the pixel diff must produce an empty bounding box. That works because there is one
backend on one rasterizer, and it cannot survive a second.

lavapipe and WARP are different rasterizers with different filtering, different rounding in the
raster and blend paths, and different float contraction in their shader back ends. Two *real*
GPUs on the same API disagree in the low bits. So the instrument the first draft called "the
highest-value prerequisite in the roadmap" did not, as built, answer the question it was said
to answer.

**The two signals are split by what each can honestly promise.**

**Counters must match exactly, across backends.** `drawCalls`, `batches`, `instances`,
`barriers`, `barrierCalls`, `validationErrors`, `uploadSubmissions` are statements about what
the renderer *decided*, not about what the rasterizer produced. Two backends disagreeing about
a draw call count is a bug in one of them, always.

**Pixels are compared with tolerance**: a per-channel delta with two caps — no pixel may differ
by more than N per channel, and at most M% of pixels may differ at all. Two numbers, each
catching what the other misses. The ceiling catches one catastrophically wrong pixel; the
fraction catches an image that has drifted slightly everywhere. A failure can name the worst
pixel and its coordinates.

*Rejected: MSE or PSNR*, because averaging over a 1920×1080 frame lets a single blazing-wrong
pixel vanish into the mean, and that is precisely the backend bug being hunted. *Rejected:
SSIM or a perceptual metric*, which is more robust to differences nobody would see and gives a
single score that is hard to act on when it fails — "0.987 against a threshold of 0.99" does
not say where or what.

**Two things keep the tolerance honest.**

The thresholds are **committed constants**, so changing one is changing an expected test result
and is gated by the rule in `CLAUDE.md` that already requires asking first. That is what stops
a threshold becoming a knob nudged upward whenever CI goes red.

And the comparison **always reports the measured delta**, not just pass or fail. "Worst channel
delta 3, 0.4% of pixels differ" against limits of 8 and 2% shows drift while it is still
headroom. A bare PASS hides the approach to the cliff, and the day it fails nobody can tell
whether it fell or walked there over six months.

**One implementation, two settings.** Within a backend the tolerance is zero and the check stays
exactly as strict as it is today; across backends it is the configured limits. There is no
second tool to keep in step.

**Amended by Stage 7.7 (13 September 2026), in three parts.**

**Validation counters are held at zero, not at equality.** Two validators check different things at
different granularity — one mistake can be one D3D12 message and three Vulkan ones — so equality
between them means something only at zero; read literally, the rule above would pass `1 == 1` for two
unrelated warnings. **When `system.backend` differs, `validationErrors` and `validationWarnings` must
both be zero in both reports**, and `ReportCompare` gains that as a rule beside `Compared`, `Measured`
and `Condition`. Within a backend they stay exact. The escape hatch is a message-ID suppression argued
beside its entry, on either backend — Vulkan already has one, D3D12's counterpart is a deny list on
`ID3D12InfoQueue`. *Rejected: warnings skipped across backends*, where a D3D12-only warning present from
the first run would be agreed with by every same-backend comparison and asserted by nothing. *The cost:*
a D3D12 warning with no Vulkan counterpart is fixed or argued away before a cross-backend comparison
passes.

**Each backend's own validation sub-mode must be on for counters to be compared across backends**
(decided at step 7, 14 September 2026). `run.vkSyncValidation` and `run.d3d12GpuBasedValidation` gate
counters because a sub-mode left off is mistakes the counters cannot report, and within a backend a
difference in either still skips them. Across backends the two always differ, since each exists on one
backend only — so, read literally, every cross-backend comparison would skip its counters and step 7's
gate would pass having compared nothing. When `system.backend` differs, each is read from the report of
the backend it belongs to alone, and the counters are skipped, naming the field, unless it is on there: a
zero means most when each validator ran in full. A report records a sub-mode as off on the backend that
lacks it. *Rejected: neither gating across backends*, which would pass a D3D12 run with GPU-based
validation off against Vulkan on zero warnings while checking less. *The cost:* a deliberately weakened
run — a release run with `--vk-sync-validation off`, taken for its timings — cannot be compared with the
other backend on counters.

**Upload batches are compared; upload submissions are measured** (decided at step 7, 14 September 2026).
The first D3D12 scene run on the RX 580 matched Vulkan's every counter but `uploadSubmissions`: 4 against
8, for the same four batches. How many submissions a batch costs is the backend's and the driver's — the
Windows AMD Vulkan driver has a separate copy family without `VK_KHR_maintenance9`, so each batch is a
copy and an ownership-transfer submission, where D3D12 has no handover and RADV needs none — so the count
was never a statement about what the engine decided, and a Vulkan run on Windows would have moved against
the Linux baseline on it too. **The report gains `counters.run.uploadBatches`, compared exactly across
backends and drivers, and `uploadSubmissions` stays as a measurement**, reported and never compared, so a
report still shows what the handover costs. *Rejected: batches alone*, which hides the handover outside
the log; *rejected: submissions compared within a backend only*, which leaves the batching guard short of
spanning backends and still moving across Vulkan drivers. *The cost:* a field more, and a Compared field
reclassified. **The user approved the expectation changes that follow:** `ReportCompareTests` classifying
`uploadSubmissions` as measured, and the committed baseline gaining `uploadBatches` at its Linux refresh.

**The two pixel constants are measured on one machine, from two fresh runs.** The pair is this
project's RX 580 under Vulkan and under D3D12, taken back to back — one GPU, one driver, so the
difference isolates the backend and nothing else. Two alternatives crossed more than the backend: WARP
against a software Vulkan driver fetched onto a Windows runner, whose tolerance would absorb two
rasterizers and then excuse any D3D12 bug smaller than their gap; and Linux lavapipe against Windows
WARP, which crosses backend, rasterizer, OS and compiler at once. No per-backend reference image is
committed: during the stage D3D12's image converges on Vulkan's and a promotion on every step would
prove little. *The cost:* until the post-stage checks in `backlog.md` land, a D3D12 bug that renders the
same wrong image every run passes CI, and after parity a D3D12-only regression smaller than the tolerance
goes unnoticed.

**Every differing region is explained before the constants are committed, and they carry no
headroom.** Each area of the first full-parity diff is accounted for by a named mechanism — coverage at
geometry edges, float accumulation in the cloud raymarch, filtering — and anything unexplained is a bug,
fixed before re-measuring; the explanations are written beside the constants in `ImageCompare.h`. About
half of the test scene is sky from the cloud compute pass, so low-bit drift across the whole sky is
expected, the fraction cap will be set mostly by it, and the channel-delta cap does the work for
geometry. The constants are then **exactly the measured values**: the pair is deterministic, so the
difference moves only when something changes, and headroom would decide which changes pass unexplained
rather than absorb noise. That replaces the "headroom" framing above — the measured delta is still
always reported, and approaching the limit now means something changed. Measuring needs the diff image
scaled by the measured worst delta rather than by the tolerance, which is zero until then.
*Rejected:* a multiple, meaningless on a large fraction; a fixed margin, a guess; a limit derived from
legitimate variation across presets, scenes or drivers — the one with an argument behind it, and the one
to reach for if this is revisited. *The cost:* every driver update and every rendering change that moves
the gap needs a re-measure, a re-explanation and approval. Cross-backend identity is D35's, as amended.

### D27 — DXIL is emitted on every platform, not only on Windows

vcpkg's `shader-slang` carries no DXC, so `-target dxil` fails outright with it: `failed to load
downstream compiler 'dxc'`, `dxil library not found`, `failed to load dynamic library
'dxcompiler'`. The toolchain comes from vcpkg's `directx-dxc` port, which installs
`libdxcompiler.so` **and `libdxil.so`** on Linux and the DLL equivalents on Windows — so
emission, signing and validation are all reachable on the platform this project is developed on.

**It goes in `vcpkg.json` unconditionally, and every build emits both blob sets.** The argument
is the feedback loop rather than the artifacts: development happens on Linux,
`scripts/precommit.sh` is what this project's rules treat as proof a change is done, and a check
that cannot run where the work happens is the same shape as a check that always passes. A Slang
construct that SPIR-V accepts and DXIL rejects then fails in the edit that caused it, on the
machine that made it.

The cost is honest — a dependency and a second blob set on a platform that can never load them.
It is small: shader compilation is a fraction of a second per shader, so build time is not the
deciding factor in either direction.

**Rejected: Windows-only emission.** It leaves the DXIL half of the content pipeline exercised
in three of six CI jobs and in none of the local ones, which means every shader edit's
consequence for the target this stage exists to enable is discovered on a push at the earliest.

### D28 — The Windows GPU job belongs to Stage 7.7, and Windows CI asks for D3D12

The first draft put "Windows GPU coverage on WARP" in 7.6. **WARP is a D3D12 adapter**, reachable
only through the backend 7.7 builds, so in 7.6 the item could only have meant Vulkan on Windows —
and the runner has no Vulkan ICD at all. Supplying one means pinning a third-party Mesa build in
CI, which is a supply-chain surface `vcpkg.json` has otherwise kept clear.

**So the job moves to 7.7, and every Windows job there runs `--backend D3D12` explicitly.**
D25 is unchanged: Vulkan is still the default everywhere, and asking for D3D12 by name in CI is
what makes CI the backend's routine exercise. Vulkan on Windows keeps the coverage it has today,
which is compilation and the unit tests.

**The cost is accepted rather than argued away.** Stage 7.6 ends without the Windows job its own
definition of done originally named, and 7.7's first CI run is then also the backend's first CI
run — the confound that the 7.5/7.6 split exists to avoid. What replaces it is a **local Windows
install**, used to build and test D3D12 work as it is written, so the backend runs on real
hardware long before a job exists to run it.

### D29 — Shader bindings are pinned in the source, in D3D12 spelling

With only `[[vk::binding]]`, Slang assigns HLSL registers implicitly: everything lands in
`space0`, and the numbering follows declaration order. The descriptor-set structure the neutral
bind groups describe is silently lost, and inserting one resource renumbers every resource
declared after it — on D3D12 a wrong-resource read rather than an error, with nothing at build
time to say so. DXIL that compiles and validates in that state satisfies a gate while being
unusable, which is the failure shape this project has already been bitten by once.

**Every shader resource therefore carries an explicit register, and bind group index N is
register space N.** `[[vk::binding]]` goes away entirely, replaced by four flags on the SPIR-V
compile — `-fvk-b-shift 0 all`, and the same for `t`, `s` and `u` — which make Slang derive the
Vulkan binding from the register instead. Verified: the SPIR-V produced this way carries exactly
the sets and bindings the attributes produce today, so it is a spelling change with no Vulkan-side
effect. One annotation per declaration rather than two, in the vocabulary D13 already chose for
everything else, and the only one of the two that can express a space.

Two constraints come with it.

**The register index must be unique across classes within a space** — `s3` beside `t0`–`t2`
rather than `s0` — because Vulkan has one binding namespace per descriptor set where HLSL has
four. **A collision is silent.** Written deliberately, it compiled clean under
`-warnings-as-errors all` and aliased the sampler onto the texture's binding; Vulkan validation
would catch the resulting layout mismatch at pipeline creation, but the build says nothing.

**`[[vk::push_constant]]` stays**, on the four declarations that have it. It is not a duplicate
binding: Vulkan push constants are a storage class rather than a descriptor, and without the
attribute the declaration becomes an ordinary uniform buffer. The D3D12 side needs no equivalent,
because root constants *appear* to a shader as a constant buffer at a `b` register — the root
signature, not the declaration, is what decides that slot holds constants rather than a table.

**Push constants live in one fixed reserved space**, its number defined once in `Common.h` so
that C++ and Slang name the same constant rather than agreeing by coincidence. *Rejected: one
past the last bind group*, which moves the moment a layout gains a group — the renumbering hazard
this decision exists to remove, reintroduced somewhere smaller. *Rejected: inside the space of
the bind group it accompanies*, which reads naturally and makes a pipeline-layout property share
a namespace with a separately-managed object whose own `b0` would then collide.

**The number is 7, and the reason is a portability floor rather than taste.** Vulkan's Required
Limits table gives `maxBoundDescriptorSets` a minimum of 4 in core and **7 for a 1.4
implementation**, so sets 0–6 are the most any conformant 1.4 device is obliged to expose, and the
engine's four bind groups — global, depth, composite, material — fit inside even the core floor of
4. Space 7 is therefore the first number no portable bind group can occupy. It costs nothing on
either side: on Vulkan `[[vk::push_constant]]` is what the SPIR-V path reads, so the space never
becomes a descriptor set, and on D3D12 register spaces are an arbitrary `uint32` rather than a
budget, so reserving one caps nothing. That reasoning belongs in the comment beside the macro in
`Common.h`, not only here — it is what stops the next reader treating 7 as arbitrary and moving it.

### D30 — Layout agreement is a unit test over reflection, not a `static_assert`

Part IV's step 48 asserts `sizeof` and `offsetof` in C++, which is C++ asserting things about
C++. Sharing the declaration removes the transcription error; it does not touch the layout-rule
divergence, and a `static_assert` cannot see what either shader target thinks.

`slangc -reflection-json` reports every field's offset and size, per target. **A unit test reads
it and compares against `offsetof` computed in C++**, failing with the field name, the two
offsets, and which target disagreed. `cmake/Shaders.cmake` emits the JSON beside each blob.

Measured, and worth recording because it sets what the test is for: across the 66 fields that
carry an offset in `opaque.slang`, **the HLSL and SPIR-V reflections agree everywhere**. The
current structs are 16-byte-friendly with explicit padding and survive both rule sets by
construction, and `bool` lands at four bytes on both, matching the C++ `int`. So the divergence is
**latent rather than present**: the test is insurance against the next edit, not a fix, and
without it the first struct that breaks the coincidence breaks it silently on one backend.

**Rejected: generating the assertions from the reflection at build time.** It moves the failure
to compile time, which is better, and pays for it with a build-graph edge that does not exist
today — the engine's compilation would have to wait on the shader compilation, which currently
runs as an independent target. Every other check in this repository is a separate target or
script for the same reason.

### D31 — `ShaderTypes.h` speaks HLSL, and covers the constant blocks only

The shared header declares `float4`, `float4x4` and `int`, with C++ aliasing glm into those names
inside `#ifdef __cplusplus` — which Slang's preprocessor skips, so its half of the file is
invisible to the shader compiler. Verified on both compilers, field offsets matching.

**The language with the tighter constraints sets the vocabulary**, and the aliases are
transparent: the C++ side keeps every glm operation it has today, since a `float4` *is* a
`glm::vec4`. D13's D3D12-first naming points the same way.

**Scope is the constant and push-constant blocks**: `GlobalBuffer`, `CameraData`, `LightData`,
the two light `Data` structs, and the three push-constant blocks. Eight declarations collapse to
five, and the duplicate `MaterialPushConstant` that `opaque.slang` and `weightedBlendedOIT.slang`
each carry disappears as a side effect.

Two riders. **A shared block spells a boolean `bool32`, never `bool`** — a C++ `bool` is one byte
and a shader's is four, so a block carrying one disagrees about every offset after it. Built at
step 11 as an alias with a half in each language: `int32_t` under `__cplusplus`, and a `typedef bool
bool32` for Slang. That keeps the shader side reading as a boolean — `if (pc.bTwoSided)`, no `!= 0`
— while both sides lay it out the same, and it is honest in both directions, where a C++ type
*named* `bool` that occupies four bytes would not be.

*Rejected: `#define bool int32_t` around the declarations, `#undef` after.* It gives the shared
block a literal `bool`, and the undef does contain the leak — but `bool` is a keyword, so defining
it is ill-formed (`[macro.names]`), and that is not academic: **Clang rejects it by default** with
`-Wkeyword-macro`, which this repository would hit through clangd whatever the build did. GCC
accepts it silently, which is the worse half of the result.

The four-byte shader side is measured rather than assumed, and re-measured on every build: the
reflection reports these fields as `bool` of size 4, and the layout test compares that against
`sizeof` on both targets. Two further cases read the header as *text*, which is the only way to
catch what a compiler cannot object to — one refuses a plain `bool` anywhere but the alias itself,
naming the line; the other pins the struct inventory, since a struct missing from the layout test's
list would otherwise pass by never being looked at.

And matrices need a comment rather than a decision: `glm::mat4` and `float4x4` are both 64 bytes, so
what keeps them interchangeable is the transposition convention — the engine transposes on upload
and the shaders multiply row-vector first — which belongs next to the shared declaration rather than
in one shader's header comment.

### D32 — Vertex input is checked, not shared — and the seam cannot express it yet

`VS_In` carries semantics (`float3 Pos : POSITION0`) that C++ has no way to express, so the
vertex structs cannot become the same struct. What is hand-mirrored is
`GetAttributeDescriptions()`'s locations and formats, and the reflection reports both — an
`index` per input, plus a scalar type and element count that map onto `Rhi::Format`. **The test
asserts those**, which catches the live hazard: insert a field into `VS_In` and every location
after it shifts while the C++ table keeps the old numbers.

**Semantics are excluded**, because there is no C++ side to compare them against. An assertion on
`POSITION1` would agree only with a constant typed beside it.

**And that absence is a gap in the seam.** `VertexAttribute::Location` is Vulkan's spelling;
`D3D12_INPUT_ELEMENT_DESC` matches on semantic *name and index*, which the neutral description
does not carry, so an input layout cannot be filled in from what the RHI's public API says today.
Stage 7.5 neutralised the seam and this survived it — the same class of thing as D22's combined
image samplers, and found the same way. Three candidate answers, all of which want a backend in
front of them: a semantic field on `VertexAttribute`; a convention synthesising a name from the
location; or the backend reading semantics from reflection at pipeline creation. **Recorded here,
decided in 7.7.**

**Decided by Stage 7.7 (13 September 2026): `VertexAttribute` carries `SemanticName` and
`SemanticIndex`**, filled in by the C++ attribute tables and ignored by the Vulkan backend. The mapping
is already known at build time — Slang's DXIL-target reflection lists each vertex input's semantic
name, semantic index and location together (`TModelCol0` is `POSITION`/1 at location 4; an index of 0
is omitted) — so the test compares semantics exactly as it compares locations, and the exclusion above
ends because there is now a C++ side. D13's rule covers the spelling: where only one API has a concept,
its term stands. *Rejected: a semantic derived from the location*, which rewrites every `VS_In` to names
that mean nothing to a reader or to D3D12's tools; *rejected: reading the DXIL signature at pipeline
creation*, a hand-written container parser or a runtime `dxcompiler.dll`, with a mismatch found only
when a pipeline fails. **The cost, accepted:** a D3D12-only concept on the seam, and each attribute
declared twice, held together by the test — the arrangement already accepted for locations.

### D33 — One entry point per blob, named `main`, and the seam stops carrying entry names

D24 emits one blob per stage for both targets, and Stage 7.6 step 8 is where that lands. After it,
a module holds exactly one entry point, and `ShaderStageDesc`'s `EntryPoint` string can only
restate what the blob already contains.

**Every blob's entry point is named `main`, and `ShaderStageDesc` loses `EntryPoint`**, leaving the
module handle alone. `cmake/Shaders.cmake` drops `-fvk-use-entrypoint-name`, which is the flag that
makes Slang carry the source name into SPIR-V; without it the entry is named `main` — measured, not
assumed. Graphics then matches compute, which passes `main` today.

The field is not merely redundant: it is a state space with one valid point. Vulkan requires `pName`
to name an `OpEntryPoint` whose execution model matches the stage
(VUID-VkPipelineShaderStageCreateInfo-pName-00707), so every other value fails at pipeline creation;
D3D12's `D3D12_SHADER_BYTECODE` is a pointer and a length, so it cannot read a name at all. A neutral
description that one backend can only fail on and the other ignores should not ask the question.
`IPlatform.h`'s `WindowMode` rejects "windowed, but exclusive" for the same reason.

**What is lost, and what covers it.** A debugger or a validation message no longer says `vertMain`.
`ShaderModuleDesc::DebugName` already carries the file name — `opaque.vert.spv` after the split —
and `VulkanDevice::CreateShaderModule` attaches it to the module, so the stage is still named.

**Rejected: keeping `vertMain`/`fragMain`.** It changes no interface, and it keeps SPIR-V entry names
matching the source. It also leaves the neutral seam carrying a string that one backend requires and
the other ignores, spelled at every call site, which can only ever be right one way.

A module holding several entry points would need the field back. D24's per-stage packaging for both
targets means none is planned.

### D34 — Backend selection is a seam, and the RHI spells it

D25 decided *that* the backend is chosen at run time and that Vulkan is permanently the default.
It left the seam itself undescribed, and every part of that seam is a public-API widening.

**`rhi/Backend.h` holds the enum, the availability query and both string conversions.** Not
`RhiTypes.h`: that header states an invariant about itself — every enum in it is paired with a
conversion table in `src/vulkan/VulkanConversions.h`, and the `ToVk` switches carry no `default:`
label so the build breaks until the mapping exists. `Backend` can have no such mapping, because it
selects which backend runs rather than being vocabulary a backend translates. Not `IDevice.h`
either, which pulls fourteen headers and would drag the whole API surface into the option parser.

**`AvailableBackends()` answers the *build* question, not the machine's**, returning a
`std::span<const Backend>` over a `constexpr` array with no allocation and no side effects. Its
contents are decided by what CMake actually linked rather than by `_WIN32`, so a Windows build
configured without D3D12 reports the truth. Membership is derived from the list; there is no
second predicate function, because every caller wants either the list or membership in it, and
deriving a list from a predicate needs enum iteration while the reverse is one line.

*Rejected: probing.* Answering "can this backend create a device here?" means building an instance
and enumerating physical devices, per backend, at startup, before the window exists — turning a
fact into a measurement and a pure query into one with driver-loading side effects. It also makes
one word cover two unrelated failures, so "available: vulkan" would print on a machine where
Vulkan is installed and its loader is broken. Runtime failure stays `CreateDevice`'s to report,
with whatever detail the backend can give.

**The backend is a `DeviceDesc` field defaulting to `Backend::Vulkan`, not a `CreateDevice`
parameter.** The struct already carries fields only one backend can interpret —
`DisabledOptionalExtensions` and `bForceSingleQueue` — so one object describes the whole request
and no reader has to hold two arguments together to know what a desc means. D25's permanent
default becomes a struct default, which is the hardest kind to get wrong. *Rejected: a parameter*,
which reads more honestly about dispatch, and pays for it by moving the default away from every
other default and churning each call site.

**An unavailable backend is refused twice, deliberately, with different messages for different
audiences.** `ParseEngineOptions` refuses at parse time with `CommandLineError` — D25's message,
naming what was asked for and listing what this build has — before the platform, the job system or
the content root exist, and in the same shape as every other bad flag. `CreateDevice` refuses as a
precondition, tersely, because it is a public entry point that the GPU test fixture and future
callers reach without the parser, and dispatching on an enumerator with no implementation behind
it is not something to leave to chance. *Rejected: `CreateDevice` alone*, which surfaces after a
window is already on screen and skips the usage block.

**The RHI owns the spelling of enums that cross the process boundary** — those appearing as
command-line input or run-report output, and nothing else. `ToString` and `FromString` sit over
one table, so a new backend is named in exactly one place and `--backend D3D12` cannot drift from
`"backend": "D3D12"`. The spellings are the project's own proper nouns, which keeps a run report
internally consistent — its `os` is `"Linux"` and its `arch` is `"x86_64"`, because one is a name
and the other an identifier — and the input half folds case, since it is typed by hand while the
output half is written to a file. That matters more here than for any other enum, because the
comparison tool
matches the report's `backend` as text to pick its tolerance, and `--backend` has to accept the
word a report contains. `PresentModeJson` in `RunApp.cpp` is the engine-side precedent, and it
survives only because `--present-mode` does not exist yet; when `backlog.md`'s row for it lands,
`PresentMode` becomes the second member of this rule. `Format` and `QueueType` never cross the
boundary and get nothing.

### D35 — Device identity is separate from device capability

`DeviceCaps` carries an instruction about how it is meant to be used: read it rather than testing
the backend or the platform. Every field in it is something the renderer branches on. Device
identity — which GPU, which driver, which API level, which backend — is the opposite: nothing
branches on it, and it exists to be printed into a run report.

**They get separate accessors: `DeviceInfo` and `IDevice::GetInfo()`,** and the rule goes into
both comments so it survives — **caps are branched on; info is reported and never branched on.**
Putting a GPU name into `DeviceCaps` would hand every caller the string needed to write exactly
the driver branch that caps exist to prevent, three lines below a comment telling it not to.
`ShaderExtension` is the exception that proves the rule rather than the precedent that dissolves
it: it exists so that callers need *not* know the backend, which is the opposite of what a GPU
name gets used for.

**`DeviceInfo` carries the device's half only** — `Backend`, `Gpu`, `Driver`, `ApiVersion`. OS and
architecture are properties of the process, not the device, and arrive as compile definitions
beside `HIKARI_BUILD_CONFIG`; `IDevice` has no business reporting them.

**`ApiVersion` is one opaque string that nothing parses, and it records what the device supports
rather than what the run requested.** Vulkan writes `"1.4.321"` from
`VkPhysicalDeviceProperties::apiVersion`; D3D12 writes `"feature level 12_2"` from the
`MaxSupportedFeatureLevel` that `CheckFeatureSupport` reports for `D3D12_FEATURE_FEATURE_LEVELS`.
Both APIs carry a requested number too — our `VkApplicationInfo::apiVersion` and the minimum
feature level handed to `D3D12CreateDevice` — and both are source constants identical on every
machine, so they say nothing about the machine a report describes. The value rather than the field
name carries the disambiguation, so a feature level never masquerades as a version number.
`Driver` is opaque for the same reason.

*Rejected: two precise nullable fields*, `apiVersion` and `featureLevel`, each null on the other
backend. It never says anything untrue, and it makes the report's shape backend-dependent — which
matters because absence already means something specific to the comparison tool, where a missing
field triggers a provisional comparison. Making absence normal for two fields weakens that signal
for every other field.

**Amended by Stage 7.7 (13 September 2026): `DeviceInfo` also carries the adapter's PCI vendor and
device IDs.** `VkPhysicalDeviceProperties::vendorID`/`deviceID` and `DXGI_ADAPTER_DESC1::VendorId`/
`DeviceId` are the same PCI identifiers by definition — measured on the RX 580 through DXGI as
`0x1002`/`0x67DF`, matching the identity Windows lists for the device — so they are the one piece of
identity the two APIs spell alike. They let the comparison decide whether two reports from
*different backends* describe the same adapter. **When `system.backend` differs,
`system.apiVersion`, `system.driver` and `system.gpu` stop gating pixels** — the first differs by
construction, the other two are free text each API spells its own way — **while the two IDs,
`system.os` and `system.arch` must match**, and D26's cross-backend tolerance applies. Like every
`system.*` field, the IDs never gate counters. Software rasterizers carry vendor IDs of their own —
WARP `0x1414`, lavapipe `VK_VENDOR_ID_MESA` (`0x10005`) — so a pair of software runs is refused by
identity rather than by policy. *Rejected:* **a `--cross-backend` flag the caller passes**, which takes
comparability away from the reports, so two reports from two machines plus the flag would pass;
**matching on names**, which nothing guarantees agree across APIs; **a LUID**, which Vulkan reports
only where LUIDs exist and which is unique only until restart, so it could never appear in a committed
reference. *What PCI IDs do not prove, accepted:* they name the chip, not the card — identical cards
and board partners' variants share them, as does one card listed twice under two Vulkan drivers
(within a backend, `system.driver` still gates) — and the driver cannot be proven the same across
backends, since the two APIs encode driver versions differently; acceptable for two runs taken back to
back.

### D36 — The D3D12 backend carries the Agility SDK, for the debug layer

The D3D12 runtime normally comes with Windows. The Agility SDK lets an application carry its own —
`D3D12Core.dll` in a subdirectory, opted into by two symbols the executable exports — so the runtime
is the same on every machine the application runs on. The driver underneath is still the machine's.

**What decides it is the debug layer, not enhanced barriers.** On the Windows 10 machine this stage
must run on, `D3D12GetDebugInterface` fails with `DXGI_ERROR_SDK_COMPONENT_MISSING` (`0x887A002D`)
unless the SDK's `d3d12SDKLayers.dll` sits beside `D3D12Core.dll`; with it the layer loads and
`ID3D12Debug1` is available. D26 holds `validationErrors` equal across backends and the scene suite
asserts it zero, so without the SDK that number would come from a validator that never loaded. The
second reason is the pinned runtime itself: the same D3D12 runtime here and on the CI runner, the
counterpart of pinning lavapipe with `VK_DRIVER_FILES`. Enhanced barriers were the reason first
proposed, and they turned out not to be available on this machine's driver at all (D37).

**`directx-headers`, `directx12-agility` and `d3d12-memory-allocator` join `vcpkg.json` as
Windows-only dependencies**, the last because D25 already chose D3D12MA as VMA's counterpart. The
exports `D3D12SDKVersion` and `D3D12SDKPath` must be in each executable that can create a device — the
two apps, `scene_tests` and `rhi_gpu_tests` — and they are an OBJECT library linked directly, like
`SanitizerShims`, because a static library member that nothing references is never extracted. The
exported version is generated by CMake from the port's version, and the backend checks the version the
*loaded* `D3D12Core.dll` exports at startup rather than trusting the two to agree. A malformed
`D3D12SDKPath` fails the first D3D12 call with `D3D12_ERROR_INVALID_REDIST` rather than falling back
to the in-box runtime (measured).

*Rejected: the OS runtime*, which would make the debug layer depend on the Graphics Tools optional
feature on this machine and every runner — an environment requirement no version can pin — and let
the runtime differ between a Windows 10 desktop and a Windows Server runner, the kind of difference
D26's exact counters are least forgiving of. *Rejected: `ID3D12SDKConfiguration::SetSDKVersion`*, the
nearest thing to `VK_ADD_LAYER_PATH`, which the redistributable specification restricts to Developer
Mode.

**The cost, accepted:** a `D3D12\` folder beside each output directory — `build/<preset>/` and
`build/<preset>/tests/` — and, because the port ships `d3d12SDKLayers.dll` only in its debug tree, a
Release build copies it from there, which D40 makes mandatory rather than convenient.

### D37 — D3D12 builds two barrier paths behind one seam

`Barrier.h` is the shape of D3D12's *enhanced* barriers: sync, access and layout as independent
halves, the same shape as Vulkan's synchronization2. D3D12 also has the older model, legacy
`ResourceBarrier`, where each subresource is in one `D3D12_RESOURCE_STATES` value and a transition names
before and after. Enhanced barriers are optional per driver — "not currently a hardware or driver
requirement", in `D3D12_FEATURE_DATA_D3D12_OPTIONS12`'s words — and the runtime translates in one
direction only: every `ResourceBarrier` call becomes enhanced barriers at the driver interface, and
nothing runs enhanced barriers on a driver without them.

**The RX 580 reports `EnhancedBarriersSupported` false**, on AMD's Polaris and Vega branch at 25.8.1 and
again at 26.5.2, with the Agility SDK proven loaded. The stage must run on that machine, so **a legacy
path is mandatory**. **An enhanced path is built beside it** and chosen on the capability bit.

Both, because the enhanced path is the cheaper of the two — it maps nearly one-to-one onto a seam
designed on its shape — and it has three places to run: NuGet WARP, which reports enhanced barriers and
is deployed beside every executable, on this machine at every step; the same in CI; and a second
machine with a modern GPU, weekly. And because with an enhanced path present,
**`TextureLayout::Undefined` stays in the seam**: enhanced barriers honour it natively, so resolving it
is private to the legacy path rather than a seam change, and the oldest of the three barrier models does
not get to dictate the neutral API. Beyond that one value the legacy path needs no general state
tracker, since every `TextureBarrier` already carries its old layout, and the debug layer checks the
result: a wrong before-state is reported at `ExecuteCommandLists` (measured).

What the legacy path gives up, and must be written knowing: legacy barriers carry **no sync scope**, so
`PipelineStage` is discarded — safe, but D3D12 will never catch a Vulkan synchronization bug — and **no
subresource range**, so a partial-range barrier becomes several. Today every barrier covers a whole
resource and `ALL_SUBRESOURCES` keeps `counters.frame.barriers` one-to-one with Vulkan; the first
partial range breaks that unless the legacy path counts what the caller asked for rather than what it
issued.

*Rejected: legacy only, enhanced deferred* — recommended at the time on the premise that the enhanced
path would run nowhere anyone could see it, which the measurement of NuGet WARP beside the executable
disproved. *Rejected: enhanced only*, which refuses the stage's own machine.

**The cost, accepted:** two implementations that must produce identical counters and pixels, so D26's
exact counters span three configurations — Vulkan, D3D12-legacy, D3D12-enhanced — and every
barrier-touching step is verified twice on the D3D12 side; and NuGet WARP becomes a pinned dependency,
arriving through a repository overlay port because vcpkg has none.

### D38 — `--d3d12-barriers` chooses the path, and an impossible request is refused

`--d3d12-barriers legacy|enhanced|auto`, defaulting to `auto`, beside `--vk-sync-validation`, with a run
report field recording the path taken.

**An override exists so the two paths can be compared on one adapter.** Without it a modern GPU only
ever runs enhanced and the RX 580 only legacy, so every difference between them is confounded with
hardware; with it, one session on one GPU runs both at zero tolerance, which is the evidence that makes
owning two paths safe.

**`enhanced` on an adapter without support fails `CreateDevice`**, naming the adapter and the missing
capability. An explicit `legacy` or `enhanced` on a Vulkan run is refused at parse time with the other
contradictory options. Both follow the rule `RejectContradictoryOptions` already applies: an option
that reads stricter than it is gets refused, not quietly weakened. A tri-state defaulting to `auto` can
tell "asked for" from "left alone", which a boolean defaulting to on cannot — the only reason
`--vk-sync-validation` goes unchecked under D3D12. `legacy` is always accepted. The report field must
**not** gate the counters, since D37 requires them to match across the two paths.

*Rejected: falling back to legacy with a warning*, which turns a legacy-against-enhanced comparison into
legacy against legacy, passing at zero tolerance, caught only if someone reads the report field.
*Rejected: no override*, for the confound above.

### D39 — The D3D12 device's floor: feature level 12_0 and unrestricted copy pitch

**`CreateDevice` refuses a D3D12 adapter below feature level 12_0.** 12_0 is the RX 580's highest level,
and it guarantees resource binding tier 2 and shader model 6.0 (*Hardware Feature Levels*). Tier 2's
one rule this engine could break — no unpopulated CBV or UAV entries in a descriptor table — it does
not: every constant-buffer and UAV binding in `BindGroupLayouts.h` is mandatory. The shader-model floor
is 6_0, what the blobs are compiled to; nothing consumes a higher model until bindless, which D14
defers. *Rejected: 11_0*, whose tier 1 forbids unpopulated entries in every heap (*Hardware Tiers*), so
the material's optional textures would need a null-descriptor path no adapter in the test matrix could
exercise — the RX 580, both WARPs and the weekly machine are all tier 3, a hardware property no lower
feature-level request switches on. *Rejected: requiring tier 3*, which refuses tier-2 hardware over
restrictions the code does not hit.

**`CreateDevice` also refuses a D3D12 adapter without `UnrestrictedBufferTextureCopyPitchSupported`,
and `BufferTextureCopyRegion`'s tight packing stands.** Without the relaxation D3D12 requires 256-byte
row pitch and 512-byte offsets, so a tightly packed readback buffer is the wrong size at any width not a
multiple of 64. With it, in the words of Microsoft's *VulkanOn12* specification — where the relaxation
was introduced for exactly this contract — "both offset and row-pitch MUST be aligned only to the whole
unit size of the texture's format". The RX 580 and NuGet WARP support it; in-box WARP does not and is
not used. `BufferTextureCopyRegion`'s comment stops calling alignment "the backend's problem" and names
the requirement. Uploads were never affected: `UploadContext.h` keeps staging offsets from the caller.
*Rejected: the seam carrying the alignment*, which would edit every readback, add vocabulary Vulkan
answers trivially, and repack rows on the CPU wherever the width is unaligned.

**The cost, accepted:** D3D12 hardware below 12_0, and drivers without the relaxation, are refused
outright — the latter in unknown numbers. **A condition:** the second test machine, a Windows 11 PC
with a modern GPU, is probed before its first weekly session relies on this; if its driver lacks the
relaxation, the seam carries the alignment after all.

### D40 — Validation on D3D12: required, polled at every call, and GPU-based by default

**The debug layer is a hard requirement wherever validation is on** — by default in Debug, or through
`--validation on` — exactly as Vulkan's layer already is. A layer that does not load fails
`CreateDevice`; on the machine this stage runs on, the layer is absent unless deployed (D36), so a
degradable layer would let every D3D12 scene test pass while checking nothing.

**Messages are polled, and every backend method checks.** `ID3D12InfoQueue1`, the callback interface,
is unavailable on both adapters of the Windows 10 machine even with the SDK's layers (measured), so when
validation is on each D3D12 backend method ends by comparing `ID3D12InfoQueue::GetNumStoredMessages`
with its last value and drains into `Diagnostics` if it grew. `failfast` therefore aborts inside the RHI
method, with the engine's calling line on the stack — the frame `Diagnostics` promises. Counts are
current at every call boundary, so reading them needs no separate drain, except that a run's final drain
follows a wait for the GPU (below). Errors the layer only detects at `ExecuteCommandLists` abort at the
submit. *Rejected: break and catch* — `SetBreakOnSeverity` does raise inside the offending call with no
debugger attached, as exception `0x87A`, which a vectored handler can catch and read (measured), but that
is process-wide exception handling installed by the RHI, and `0x87A` is numerically `FACILITY_DXGI` and
documented nowhere as the mechanism. *Rejected: draining at submit*, whose dump lands at the submit
rather than the recorder. *The cost:* a check in each of roughly eighty backend virtuals, a forgotten one
pointing an abort at the wrong line, and an unmeasured per-call cost in validated runs.

**GPU-based validation is on wherever validation is on, behind `--d3d12-gpu-based-validation on|off`** —
the API's own term (`ID3D12Debug1::SetEnableGPUBasedValidation`). It is a mode of the debug layer, not a
second layer: it patches every shader to check, at the point of use, what the CPU cannot see —
"uninitialized or incompatible descriptors in a shader", descriptors referencing deleted resources, heap
overruns, and "shader accesses of resources in incompatible state". A D3D12 descriptor names no state,
so the CPU layer knows what is bound but not what a shader reads; Vulkan checks the equivalent on the
CPU routinely, because a Vulkan descriptor write names the layout (`VUID-vkCmdDraw-None-09600`). With it
off, D3D12 would check strictly less, exactly where a mistake in D37's private `Undefined` handling would
show. Microsoft's guidance fits the stage: enable it "with smaller data sets (for example, engine demos …
with fewer PSO's and resources) or during early application bring-up". Its output arrives after the GPU
executes, "asynchronous with other CPU-timeline validation", so under `failfast` it aborts at the next
checking call after that, and a run's final drain must follow a wait for the GPU. *Rejected: off by
default*, leaving the one check D3D12 needs to match Vulkan unrun. **A condition:** its cost is measured
at the first step that renders a scene under D3D12, and if it makes the WARP scene suite prohibitive the
default is revisited with the numbers.

The validation *counters* across backends are D26's, as amended.

**Amended at step 7 (14 September 2026): a default run validates descriptors, and tests validate in
full.** The condition above came due with the first D3D12 scene. On the RX 580, headless on the test scene
in debug, GPU-based validation took a frame from 1.3 ms to 22.8 ms and the first frame from 5 ms to 1.75 s;
Vulkan's layer with synchronization validation costs 1.5 ms. The cost is per frame, not per pixel — 19.3
ms at 640x360 — and almost all of it is resource-state tracking: with
`ID3D12Debug2::SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING)` the frame
is 3.8 ms and the first 0.37 s, while the patch mode that keeps the tracking and drops the checks is 19.2 ms.
Microsoft documents that flag as skipping resource-state validation, "which greatly reduces the performance
cost", with descriptors and descriptor heaps still validated. On NuGet WARP at 640x360 the three are 488,
333 and 77 ms. **`--d3d12-gpu-based-validation` becomes `off|descriptors|full`, defaulting to
`descriptors`**: four of the six checks the mode documents — uninitialized or incompatible descriptors and
samplers, descriptors referencing deleted resources, indexing past the heap — for about 2.5 ms, where the
other two, incompatible resource states and promotion and decay, cost about nineteen. **The GPU and scene
suites and `backend_compare` ask for `full` explicitly**, as `RunScene` already asks for validation, so the
state checks this decision was made for still run under every gate; and D26's rule that each backend's own
sub-mode be on for a cross-backend counter comparison means `full`. The other levers were weighed and
measured: guarded patching changes nothing, front-loading patched pipelines moves the first frame's cost into
startup without reducing it, and per-command-list patch modes cannot reach the tracking, which is
device-wide. *Rejected: off by default*, this decision's original rejection, since a default run would then
check nothing only the GPU side can see; *rejected: full by default*, a 22.8 ms debug frame in every
interactive run from step 9 on. *The cost:* a default run misses a resource-state mistake until a test
runs, and the report's field becomes a word. **The flag takes exactly those three words** — `on` is refused,
since it no longer says which level — and the user approved `ParseEngineOptionTests` changing to match.
**A D3D12 run whose frame times are watched for drift over time, as the Vulkan baseline's are, runs at
`descriptors`**, the user's call: close enough to Vulkan's validation cost to track, where `full` would drown
the frame in tracking. No such run exists yet — `backend_compare` runs `full` for its counters and its timings
are not read — so where it lives is decided with a D3D12 baseline.

### D41 — Cull mode is a pipeline property, not command-list state

**`ICommandList::SetCullMode` and `GraphicsPipelineDesc::bDynamicCull` leave the seam.** The engine
creates one opaque pipeline per cull mode it uses — two, sharing one layout — and the recorder picks per
batch with `SetPipeline`. D3D12 bakes cull mode into the pipeline state object, and no version of
`ID3D12GraphicsCommandList` sets it.

This is D22's move again: where one API cannot express something, the seam stops claiming it rather than
one backend faking it. It also deletes the rule the engine already tripped on — a Vulkan command buffer
starts with no dynamic cull mode at all (`VUID-vkCmdDrawIndexed-None-07840`) — instead of giving it a
D3D12 twin. The seam's existing promise that a bound group survives `SetPipeline` when the layout is the
same becomes load-bearing, on a path no test exercised: every scene today is all single-sided or all
two-sided, so a scene mixing both joins the tests.

*Rejected: variants hidden behind one handle*, a D3D12 pipeline secretly three PSOs with `SetCullMode`
quietly a PSO switch — and compiling a variant lazily was never possible, since the opaque pass records
on a job-system thread. *Rejected: the description declaring its modes*, which makes the cost visible
but keeps the hidden switch.

**The cost, accepted:** Vulkan gives up dynamic state it has, for two pipelines and a pipeline bind per
batch where materials alternate; and each future per-draw rasterizer variation multiplies pipelines at
the call site — a problem handed, visibly, to Stage 8's material system.

### D42 — A submit names the image it writes, and semaphores leave the seam

**`SubmitDesc` gains an optional present image — the target and the acquired index — and
`SemaphoreHandle`, both semaphore spans, `AcquiredImage::WaitSemaphores` and
`IPresentTarget::GetRenderCompleteSemaphore` are removed.**

A Vulkan frame's submit must wait on the semaphore the acquire signalled for the image it writes, and
signal the one its present — or, headless, the next acquire of that image — waits on. The engine used to
do that by copying both semaphores from the target into the submit without ever deciding anything about
them. What it actually knows is which image the submit writes, and that is enough for the Vulkan
backend to find both semaphores privately. D3D12 needs nothing: "Present operations occur on the 3D queue
provided at swapchain creation" (*Swap Chains*), so a present is ordered behind the rendering already on
that queue, and a D3D12 offscreen target's next write is queued behind its last. The back buffer must be
in `D3D12_RESOURCE_STATE_PRESENT` at present, which `GetRequiredFinalLayout` already answers; and the
same page's advice to "always use a frame-count fence to limit CPU frames in flight" is what the frame
loop already does.

*Rejected: semaphores kept, empty on D3D12* — a concept only Vulkan has, left in the neutral seam with a
validity rule at every call site. *Rejected: a target attaching its semaphores to the next graphics
submit after an acquire* — correctness by call order, since an upload or readback submitted in between
would consume the wait. *Rejected: emulating semaphores on D3D12*, synchronisation objects that
synchronise nothing.

**The cost, accepted:** `Submit` is coupled to `IPresentTarget`, and the Vulkan backend must recognise its
own targets and refuse another device's. The seam can no longer wait on a present semaphore for an
unrelated submit, which nothing did; fences cover every other ordering (D5).

### D43 — One persistent descriptor heap per kind, sized once

**The D3D12 backend creates one shader-visible CBV/SRV/UAV heap and one sampler heap with the device, at
a capacity `DeviceDesc` carries** — 65,536 resource descriptors by default, and the guaranteed 2,048
samplers. Every command list binds the same two heaps, ImGui's included. A bind group receives its range
at creation, identical samplers share a slot, `Destroy` recycles the range under the lifetime discipline
the engine already keeps for Vulkan, and exhaustion refuses with a message naming the capacity and the
field. The Vulkan backend ignores the capacity and keeps growing its pools.

The rules this answers, from *Descriptor Heaps Overview*: "At most one CBV/SRV/UAV combined heap and one
Sampler heap can be bound at any one time"; switching "within the same command list or in different
ones" is acceptable but can cost "a GPU stall" on some hardware; and "descriptors cannot be changed while
a command list submitted for execution might reference that location". The page's "pre-fill" strategy is
this design, and its "one huge array" is where bindless later takes the same heap. Measured on the RX
580, a shader-visible resource heap costs about 60 bytes of video memory per descriptor — 3.9 MiB at the
default — against a scene that uses tens.

*Rejected: a heap that grows*, which needs `CreateBindGroup` never to run while a list records — two
recorders run on job threads — risks a stall at the switch, and cannot move under ImGui, whose DX12
backend stores the raw GPU descriptor handle as its texture ID; ImGui would need a heap of its own, and
every frame would switch heaps between lists. *Rejected: staging heaps and a per-frame scratch heap* —
never exhausted by scene size, but a descriptor copy on every bind of every frame, and a design bindless
would replace.

**The cost, accepted:** asymmetry with Vulkan. A scene that loads there can be refused on D3D12 until the
capacity is raised — the kind of ceiling Vulkan's allocator was changed to remove. *What would reopen
it:* scenes outgrowing any sensible capacity before bindless arrives.

### D44 — Testing levers: disabling extensions is Vulkan's, and single-queue is neutral

**`DisabledOptionalExtensions` is Vulkan's, and refused on a D3D12 run.** `--vk-disable-extension` with
`--backend D3D12` is rejected at parse time. D3D12 has capability bits rather than extensions, and each
D3D12 branch worth forcing gets a dedicated lever when it arises, as D38 gave enhanced barriers; the only
other branch today is tearing, which only decides whether Immediate is offered. *Rejected: D3D12 reading
the list as capability names*, a second way to force legacy barriers beside D38, where the list's rule
that unknown names are "reported and ignored" would let a typo run the path nobody asked for.

**Single-queue is one neutral lever.** `--vk-force-single-queue` becomes `--force-single-queue` and the
report's `run.vkForceSingleQueue` becomes `run.forceSingleQueue`. `DeviceDesc` and `RunSpec` already
named it neutrally; only the flag and the key carried a prefix written when there was one backend. On
D3D12 the upload context uses a copy queue by default and the lever moves uploads to the direct queue —
the seam's `QueueType` already has `Copy`, so D3D12 honours copy-queue submissions regardless. D3D12 has
no ownership transfer (a resource reaches a copy queue by being in `COMMON`), so the GPU fixture's two
ownership-transfer arrangements are Vulkan's alone. **The user approved renaming the key in the committed
baseline**, value unchanged. *Rejected: one single-queue lever per backend*, two flags and two fields for
one concept, one always false.

### D45 — The boundary check covers both backends, and keeps them apart

`rhi_boundary_check`'s patterns were Vulkan's alone, so nothing stopped `ID3D12`, `DXGI_` or `D3D12_`
appearing in a neutral header or in engine code.

**Its first check gains D3D12's patterns** — `ID3D12…`, `IDXGI…`, `D3D12_…`, `DXGI_…`, `D3D12MA…`, and
includes of `d3d12`, `dxgi` and `directx/` — while the bare word `D3D12` stays legal, since
`Backend::D3D12` is neutral vocabulary. **D9 governs the D3D12 ImGui glue as it governs Vulkan's**: one
listed escape hatch, `include/rhi/d3d12/D3D12Native.h`, reachable only from allowlisted sites, because
ImGui's DX12 backend takes a raw device, queue and heap. **The naming check gains a D3D12 list with one
permanent entry**, the sibling `engine/editor/src/D3D12UiBackend.cpp` that D9 already promised; tests
stay exempt, as for Vulkan.

**And a fifth check keeps the backends out of each other inside `engine/rhi/`**, which the other four
exempt: `src/vulkan/` names no D3D12, `src/d3d12/` names no Vulkan or VMA, and the module's shared
sources name neither. A D3D12 name in Vulkan or shared code already breaks the Linux build; the direction
nothing caught is D3D12 code depending on Vulkan, which compiles everywhere D3D12 exists because D25 keeps
Vulkan in every build. *Rejected: leaving the module exempt*, so that a port written with the other
backend open beside it could quietly borrow its internals. **The cost, accepted:** a check that guards an
architectural rule rather than any build a supported configuration compiles; a genuinely shared helper is
neutral or duplicated.

### D46 — Adapters are selected by name

**`--gpu <name-substring>`, a neutral `DeviceDesc` field**, matched against each backend's adapter name —
Vulkan's `deviceName`, DXGI's `Description`. No match refuses and lists what was found. With nothing
given, each backend keeps its rule, first in enumeration order, which on the stage's machine picks the RX
580 under both.

It is the real case the standing decision to defer device selection was waiting for: D37 makes running
D3D12 on WARP, on a machine with a GPU, a per-step need, and `--gpu "Basic Render"` does it — measured
to load NuGet WARP when that copy is deployed beside the executable. The shape was already designed in
`backlog.md` — a name, because "enumeration order is not a stable identifier". *Rejected: a two-valued
`--adapter default|software`*, which a later `--gpu` would overlap; *rejected: an environment variable
read by the D3D12 backend*, a hidden global input — `VK_DRIVER_FILES` is read by the Vulkan loader, this
would be read by the engine's own RHI.

**The cost, accepted:** a substring means something only within one backend, since each API spells names
its own way; and a name cannot tell in-box WARP from NuGet WARP — deployment decides which runs.

---

## 3. The step sequence

**Twelve steps.** The first draft had six, four of them L. The grill split them for two
reasons: the work is being followed step by step deliberately, and a step that ends where a
baseline comparison means something is a step whose green result is unambiguous.

Steps are numbered flat — 1 to 12, no letters.

Each ends in a compiling, running application with the baseline unchanged, per Part IV's rule.
That rule is not relaxed here, and it matters more than usual because these steps touch every
draw site in the engine.

Every step's verification includes `scripts/precommit.sh` plus a baseline comparison
(`tests/scripts/baseline_test.sh`, counters and decoded pixels). Synchronization validation is
already on — `validate_sync` is hardcoded `VK_TRUE` whenever validation is enabled — so it
applies to every step here without anything being switched on first.

### 1 — Command allocators and command lists

- **Do:** `ICommandAllocator` per queue type, caller-owned, one per frame per recorder, reset
  as a unit and handing out `ICommandList`s (D19). The engine's seven-per-frame pools move
  behind the RHI as allocators. The engine still submits its own lists on its own queue,
  recording through the escape hatch.
- **Verify:** baseline unchanged, counters unchanged.
- **Size:** M
- **Done.** Amended while building: the **generic pool stays raw**, against this step's
  original wording. `CloudSystem`'s noise bake reaches it through `CommandListUtil`, which
  begins, submits and waits on a one-shot buffer — so converting it needs submission behind the
  RHI *and* dispatch recording, which is steps 2 and 11. Forcing it here would have meant
  handing a raw pool back out of an allocator, widening the escape hatch to narrow it later.
  One thing came free in the other direction: `CloudSystem::RecordDispatch` now takes an
  `ICommandList&` rather than a `vk::raii::CommandBuffer&`, since the caller owns the allocator
  and must begin and end the list anyway.

### 2 — Submission and fences

- **Do:** `IDevice` gains a submit entry point taking recorded lists plus waits and signals as
  `FenceHandle` + value (D5, D16). `FenceHandle` becomes a type an interface actually takes.
  The per-frame fences move behind the RHI; the present target's semaphores are passed as
  `SemaphoreHandle` and stay behind `IPresentTarget`.
- **Retires:** `tests/support/GpuReadback.h`'s `VulkanNative.h` entry and
  `tests/gpu/rhi/ValidationCoverageTests.cpp`'s — both were submission and fence waiting, and
  both are now fully neutral. 19 sites down to 17.
- **Not** `tests/gpu/rhi/PresentTargetTests.cpp`'s, against this step's first estimate: its
  remaining uses are a raw render pass for the clears and a `VkImageView` for the attachment,
  which are step 3's to remove rather than this step's.
- **Verify:** baseline unchanged; zero validation errors, and those errors now mean something
  across submissions — see §9. This is the step where a clean synchronization validation run is
  load-bearing rather than decorative, because it is the step that moves every submit in the
  engine.
- **Size:** L

### 3 — Rendering scope and dynamic state

- **Do:** a neutral attachment description (view handle, load/store op, clear value),
  `BeginRendering`/`EndRendering`, `SetViewport`, `SetScissor` on `ICommandList` (D17). Nothing
  here depends on the binding model, which is why it precedes the bind groups.
- **Retires:** `tests/gpu/rhi/PresentTargetTests.cpp`'s `VulkanNative.h` entry, deferred here
  from step 2. Its clears were a raw render pass and its attachment took a `VkImageView`; both
  are neutral now, and the file reaches only for `OffscreenTarget` through the module's own
  sources. 17 sites down to 16.
- **Verify:** baseline unchanged. The recorders still bind pipelines and draw through the escape
  hatch; only the scope and dynamic state have moved.
- **Size:** M
- **Done.** `LoadOp` and `StoreOp` are named for D3D12's beginning- and ending-access types
  under D13 — `Preserve`/`Clear`/`Discard` rather than Vulkan's load/store vocabulary.
  `StoreOp` has exactly two values, and that is a correction: it briefly had a third,
  `NoAccess`, for the transparent pass reading depth it never writes. That was wrong twice
  over. D3D12's `NO_ACCESS` means the resource is **neither read nor written** and must be
  paired with a `NO_ACCESS` beginning access, so it describes the one case a depth-reading pass
  is not; and the fact it was trying to state is already stated by
  `DepthStencilTarget::bReadOnly`, so keeping both would be two fields able to disagree. The
  backend derives Vulkan's `STORE_OP_NONE` and the read-only layout from `bReadOnly`, and
  rejects a read-only target that asks to discard. The ImGui recorder came out fully neutral as
  a side effect: everything raw it did was open and close a scope.

### 4 — Bind groups: global, depth and composite

- **Do:** a bind-group layout description, a bind-group description, a handle type, and
  `SetBindGroup` on `ICommandList` (D14, D20, D23). Immutable, created from a complete
  description. The curated `BindingType` with `default:`-free switches lands here, as does the
  pinned layout inventory test (D21) covering these three layouts.
- **Why these three first:** no partial binding, no material lifetime, and their contents change
  only at the resize point that already stalls.
- **Verify:** baseline unchanged.
- **Size:** L
- **Done.** Three things worth knowing.

  **D22's sampler split happened here, not at step 5.** The composite layout already held a
  combined image sampler at binding 3, and `BindingType` has no such value, so the split could
  not wait: binding 3 became a sampled texture and binding 4 a `Sampler`, and `composite.slang`
  changed with it. Only the cloud target is sampled — the other three are fetched by texel — so
  one sampler serves the layout. **Step 5 is correspondingly smaller**: the material set alone.

  **The binding model carries stage visibility per binding**, because the depth group is read by
  the cloud dispatch as well as by pixel shaders. A graphics-only assumption would not have
  survived the first layout it met.

  **Two transitional accessors joined `VulkanNative.h`**, both expiring at step 6:
  `GetDescriptorSet` and `GetDescriptorSetLayout`. Binding a group needs a pipeline layout and
  creating a pipeline layout needs the raw set layouts, and neither is neutral until D23 makes
  `PipelineLayoutHandle` real. So the renderer creates its groups through `IDevice` and still
  binds them itself. **`SetBindGroup` therefore moves to step 6**, against this step's original
  wording — it cannot exist before the thing it takes as an argument. No allowlist entry moved:
  every site involved already had one.

  Validation earned its keep twice. It caught the pipeline layouts still naming the deleted
  raii objects, and then caught a sampled *depth* view being described as
  `SHADER_READ_ONLY_OPTIMAL` when the barrier had left it in `DEPTH_READ_ONLY_OPTIMAL`. The
  second is why `VulkanTextureView` now records its aspect: which layout a sampled view wants is
  a Vulkan rule, not something the neutral description should be made to state.

### 5 — Bind groups: the material set

- **Do:** the material set moves behind the neutral API, and combined image samplers become
  separate texture and sampler bindings (D22) — four shaders (`opaque`, `weightedBlendedOIT`,
  `composite`, `clouds.comp`), `MaterialFactory` and `PBRMaterial`. Both stop writing
  descriptors directly. The pinned inventory grows to four layouts, on its way to six — see
  D14's second correction for the two `CloudSystem` owns.
- **The partially-bound behaviour must survive the move.** It is what lets an untextured
  material render, and losing it silently would change what the test cubes look like rather
  than failing a build.
- **Retires:** `DescriptorAllocator.h` and both its allowlist entries;
  `MaterialFactory.cpp`'s and `PBRMaterial.cpp`'s `VulkanNative.h` and `DebugNames.h` entries.
  Six of eighteen.
- **Verify:** baseline unchanged — in particular the untextured and transparent cube cases from
  step 47's matrix, which are the ones that exercise partial binding. Same sampler state means
  the capture should be pixel-identical, so any movement here is a real defect.
- **Size:** L
- **Done.** The neutral layout grew one field for this step: `BindGroupLayoutBinding::bOptional`,
  which is what lets a slot be left empty. All three material textures set it, the sampler does
  not, and the bind group description simply omits the maps a material lacks. Vulkan spells the
  permission `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT`; D3D12 reaches the same place from the
  other direction, since a descriptor a shader never accesses need not be valid. The flags
  structure is chained only when a layout actually asks for it.

  The material set carried a second sampler split, like the composite one: three combined image
  samplers became three textures plus one shared sampler, in `opaque.slang` and
  `weightedBlendedOIT.slang`.

  **`DescriptorAllocator.h` left the transitional area by moving rather than by deletion.** The
  RHI itself now allocates bind groups through it, so the header still exists — it just lives in
  `src/vulkan/` where nothing outside the module can reach it, which is the same outcome the
  ratchet was measuring. Six allowlist entries went with it: **7 headers from 16 sites down to
  6 from 10**.

  The pinned inventory is four of six layouts. `CloudSystem`'s two remain, and they need
  `UnorderedAccessTexture` before they can move — see D14's second correction.

### 6 — Graphics pipelines and pipeline layouts

- **Do:** `PipelineLayoutHandle` (D23), a neutral graphics pipeline description, and shader
  modules created from bytes the engine supplies (D24). `DeviceCaps` reports the shader format;
  `cmake/Shaders.cmake` starts emitting one blob per stage. Feeds the existing neutral
  `IPipelineCache` unchanged (D15). Formats come from `Rhi::Format`, so `GetNativeFormat`
  leaves the call sites that currently translate for the builders.
- **Retires:** `PipelineBuilder.h` and `Engine.cpp`'s entry for it.
- **Verify:** baseline unchanged. The pipeline cache still warms — `startupMs` on a second run
  should not regress, which is the only externally visible sign the cache is still working.
- **Size:** L
- **Done.** Four notes.

  **`SetPipeline`, `SetBindGroup` and `PushConstants` all landed here**, not at steps 8–11 as
  written. Each takes a pipeline layout, so none of them could exist before `PipelineLayoutHandle`
  did — and once it does, leaving those calls raw needs *more* escape-hatch accessors rather than
  fewer. Steps 8–11 keep vertex and index binding, draws and dispatches, which is a cleaner split
  by what is being recorded.

  **`Rhi::Format` grew three vertex-attribute formats** — `RG32Float`, `RGB32Float`,
  `RGBA32Float` — because vertex input needs them and both APIs carry vertex and texture formats
  in one enum. That expired a unit test's specimen: `ConversionTests` asserted that
  `eR32G32B32A32Sfloat` was outside the curated set, and its comment said the choice held only
  "until pipeline creation is neutralized". It now names `eR8G8B8Unorm` instead, and the
  mappings are spot-checked alongside the others.

  **`DeviceCaps::ShaderExtension`** is how the engine learns which blob to load. The build still
  emits one module per shader holding both entry points, so the pipeline description names the
  same module twice with different entry points — legal on Vulkan, not on D3D12. **Splitting the
  blobs per stage moved to Stage 7.6**, where DXIL emission restructures the shader build anyway;
  doing it twice would mean touching `cmake/Shaders.cmake` for the same reason in two stages.

  **`PipelineBuilder` is gone**, header and implementation: **5 transitional headers used from 9
  sites**. `ComputePipelineBuilder` stays until step 7, and the two descriptor accessors stay
  until steps 7 and 11, because `CloudSystem`'s own layouts are still raw.

### 7 — Compute pipelines

- **Do:** a neutral compute pipeline description, consuming the same layouts. `CloudSystem`'s
  dispatch pipeline and its noise-bake pipeline both move.
- **Retires:** `ComputePipelineBuilder.h` and `CloudSystem.cpp`'s entries for it and for
  `DebugNames.h`. **4 transitional headers used from 7 sites.**
- **Verify:** baseline unchanged.
- **Size:** M
- **Done.** Larger than M, because of a dependency the plan did not see.

  **`CloudSystem`'s two bind group layouts moved here, not at step 11.** A compute pipeline
  needs a pipeline layout, a pipeline layout is built from bind group layouts, and
  `CreatePipelineLayout` takes handles — so its layouts had to be neutral before its pipelines
  could be. That pulled in everything D14's second correction had assigned to step 11:
  `BindingType::UnorderedAccessTexture` for the `RWTexture2D`/`RWTexture3D` storage images, and
  a third combined-image-sampler split, in `clouds.comp.slang`. **Step 11 shrinks to the
  dispatch recording it names**, and the pinned inventory is complete at six layouts.

  `SetPipeline`, `SetComputeBindGroup` and `Dispatch` are separate compute entry points rather
  than shared ones, because both APIs keep the two apart — Vulkan by bind point, D3D12 by having
  `SetComputeRootSignature` and `SetGraphicsRootSignature` be different calls — so one call
  would have to guess which the caller meant.

  **A defect from step 4 surfaced here and is fixed.** The RHI's bind group descriptor pool was
  sized for uniform buffers, sampled images and samplers, and storage images were the first
  binding type it had never been asked for. It showed up as two validation *warnings* and a
  changed `validationWarnings` counter — not an error, because the specification lets an
  implementation fail to report the out-of-pool condition it should. The pool now carries a size
  for every `BindingType`.

### 8–11 — The recorders

Written as four steps, one per recorder, and built as four; committed as one, because after
steps 6 and 7 each had shrunk to binding geometry and issuing a draw.

- **Do:** `SetVertexBuffer`, `SetIndexBuffer`, `SetCullMode` and `DrawIndexed` on `ICommandList`,
  then move the composite, opaque, transparent and cloud recorders onto them. Composite first:
  one draw, one bind group, no per-batch loop, so it is the smallest proof the recording API
  works before it is applied to anything harder.
- **Retires:** `CommandListUtil.h`, `CloudSystem.cpp`'s two remaining entries, and
  `Engine.cpp`'s `VulkanNative.h`. **3 transitional headers used from 5 sites.**
- **Verify:** baseline unchanged, with an unchanged screenshot as the load-bearing evidence
  rather than a formality.
- **Size:** M across the four.
- **Done.** Four things worth recording.

  **The compiler found the finish line.** Once the draws were neutral, `-Wunused-but-set-variable`
  fired on the `cmd` variable in all three surface recorders at once — none of them had any
  remaining use for a raw command buffer. The same happened to `NativeSet`, `NativeSetLayout` and
  `NativeView`, the transitional accessors added at steps 4 and 6: each was left with only its
  own definition, so all three are gone along with the `GetDescriptorSet` and
  `GetDescriptorSetLayout` hatch functions they wrapped.

  **The noise bake now submits through the RHI**, which is what retired `CommandListUtil` — it
  begins, submits and waits on a one-shot buffer, so it needed both submission (step 2) and
  dispatch recording (step 7) before it could go. With it went the engine's last raw command
  pool: `CreateCommandPools` and `m_GenericCommandPool` are deleted outright.

  **The bake stays on the graphics queue, deliberately.** Converting it to `QueueType::Compute`
  looked obvious -- it is a dispatch, and the device has a dedicated compute family -- and would
  have been wrong: the noise volume is written there and sampled by the frame's dispatch, so it
  would cross queue families with nothing owning the transfer. The old code passed the graphics
  queue under the name `ComputeQueue` with a comment saying why; that reasoning now sits at the
  submit itself.

  **`CloudSystem` holds no Vulkan at all.** It kept a `vk::raii::Device&` since Stage 5 for
  building pipelines and descriptors; both are neutral now, so the reference, the include and the
  `using namespace` are gone.

### 12 — Seal the seam

- **Do:** delete `VulkanNative.h`'s RAII accessors, which exist only for code that builds Vulkan
  objects itself and by now has none. Shrink `rhi/vulkan/` to its permanent residue and update
  `cmake/RhiBoundaryCheck.cmake`'s two lists to match. Remove the remaining `DebugNames.h`
  entries as the objects they name finish moving behind the RHI.
- **Also decide, but do not assume:** this is the natural moment to retire
  `rhi_extraction_plan.md` by promoting its D0–D13, §4 and §8 into permanent homes. `CLAUDE.md`
  is explicit that retiring it is a deliberate decision rather than a roadmap step, so it is
  proposed here and taken then.
- **Verify:** `rhi_boundary_check` passes against the reduced lists; `precommit.sh` green.
- **Size:** M
- **Done. 2 transitional headers used from 4 sites**, against 7 from 18 when the stage began.
  §8 predicted 2 headers from 3 sites; it ended one over, and the extra site was closed
  immediately afterwards by the format-support query §8 now describes. **The final count is 2
  headers from 3 sites, exactly as predicted.**

  `VulkanNative.h` lost the RAII accessors, the buffer, view, sampler and semaphore resolvers,
  `WrapCommandList`, and the two descriptor accessors added at steps 4 and 6. What remains is
  permanent by design plus one exception: `GetPhysicalDevice`, kept for the depth-format query,
  because the neutral API has no way to ask whether a format is usable for a given purpose. That
  is the one thing the seal does not cover, and it is a missing query rather than a leak.

  `DebugNames.h` left the transitional area by moving into `src/vulkan/`, as `DescriptorAllocator.h`
  did at step 5: the RHI names its own objects with it, so the header lives on where nothing
  outside can reach it.

  **`Engine.cpp` went from 2,476 lines to 2,159** across the stage, without a single line moving
  to a new home — every one of those 317 lines is Vulkan the renderer no longer writes.

---

## 4. Stage 7.6 — backend prerequisites

Six things a second backend needs that Stage 7.5's frame API does not provide: the shader pipeline
it loads from, the instrument that will compare it against Vulkan, the switch that makes a release
run assert, the one struct-layout hazard a second target introduces, the flag that chooses a
backend at all, and the device identity that says whether two reports describe the same machine.

**Two of the six are seam work, which is a deliberate reversal.** The first draft called this stage
"the backend's non-seam prerequisites" and listed four items; backend selection (D25) and device
info both widen `IDevice`. They are here because D25 and D26 already fixed what each has to do, so
a second backend has nothing left to teach about their shape, and because 7.7 is a learning
exercise whose steps should be about D3D12 rather than about command-line plumbing.

They are a separate stage rather than extra steps of 7.5 for one reason: **verification
independence.** If they lived inside 7.5, that stage's own steps would be checked by machinery
being built in the same stage, and a bug in the new comparison tooling and a bug in step 10 would
look identical. Keeping them apart means every step above is verified by the harness that already
works, and 7.6 is verified against a codebase that is not moving.

There is a sequencing bonus: DXIL emission can be validated before a single line of D3D12
exists.

| # | What | Why it is here | Steps |
|---|---|---|---|
| 1 | **The comparison tool**, with D26's tolerance built in from the start | `backlog.md`'s P1 row — decode both captures, report the diff, diff the `counters`. Today `CLAUDE.md` walks a human through PIL by hand, and that recipe compared only the alpha channel until it was corrected. §4.1 | 1–4, 7 |
| 2 | **The shader build** — per-stage blobs, D29's registers, DXIL emission and the gate that proves validation ran | `cmake/Shaders.cmake` is SPIR-V only. The first draft put this in the backend stage; that is wrong, because it is build-system and content-pipeline work with its own failure modes, and doing it there means finding out whether Slang's DXIL path handles `pbr.slangh` halfway through writing a device. §4.2 | 8–10 |
| 3 | **Step 48 — `ShaderTypes.h` shared with Slang, extended with per-target layout assertions** | See below | 11 |
| 4 | **Runtime-selectable validation** | `backlog.md` P2. The engine gates validation on `NDEBUG`, so a release run reports zero validation errors trivially. Two backends mean two validation surfaces, and 7.7's Windows release job is worth having assert rather than silently pass — which it cannot do unless this lands first | 12 |
| 5 | **Backend selection** — `--backend`, the neutral `Backend` enum and the availability query | D25 decided all three and no step ever scheduled them. Doing it here keeps 7.7's steps about D3D12, and the only behaviour a Linux build will ever have — refusing `--backend D3D12` and listing what it does have — is testable now | 5 |
| 6 | **Device info in the run report** — GPU, driver, API version, OS, architecture, and which backend | `backlog.md` P2, which §6 places here. The comparison tool is its first consumer: without it, a run on another GPU differs from the committed baseline with nothing in either report to say why | 6 |

**Step 48 needs extending, and the reason is the only silent-corruption path a second backend
introduces.** Part IV's step 48 shares the declaration between C++ and Slang, which removes the
transcription error. It does not remove the layout-rule divergence: SPIR-V follows
`std140`/`std430`, HLSL constant buffers follow their own 16-byte packing rules, and the two do
not agree in every case. So a C++ struct that matches the SPIR-V layout can silently mismatch
the DXIL one and corrupt on exactly one backend. Sharing the declaration is necessary;
asserting offsets *per target* is what actually closes it.

**Definition of done for 7.6:** every shader compiles to DXIL and passes the gate that proves it was
validated, with its registers and spaces where D29 says they are; two captures that are not
bit-identical can be compared, and the result reported with its measured delta and a diff image; a
`--validation on` makes a release build assert rather than report zero trivially, and the Linux
release job runs the scene suite because of it; a struct whose C++ and shader layouts disagree fails
a test rather than corrupting on one backend; `--backend` chooses a backend at run time and refuses
one the build does not contain, naming what it does have; and a run report's `system` block names
the device, driver, API level and backend that produced it.

**The Windows GPU job is no longer here.** D28 moved it to 7.7, where WARP is reachable at all.
That is the one item the first draft's definition of done named which this stage does not
deliver.

### 4.1 The comparison tool

Decided on 11 September 2026. The tool comes **first**, before any shader work: it is the
instrument the rest of the stage is verified with, and building it while nothing else moves is §4's
own independence argument applied one level down. The alternative, DXIL first, was rejected once
§9's toolchain unknowns were measured during the interview rather than during the step — that
measurement was the only thing DXIL-first bought.

**One implementation in C++, three callers.** A `TestSupport` library under `tests/support/`, where
`architecture_plan.md` §16 already puts the harness and names an `ImageCompare`; a thin command-line
tool, `HikariCompare`, at `tests/tools/compare/main.cpp`; the scene tests, which drop their own `==`
for it; and 7.7's cross-backend comparison. D26's "one implementation, two settings" is the reason:
*Rejected: Python with Pillow*, which cannot reach the scene tests' in-memory captures, would leave
two implementations of the same comparison, and would be ported to C++ in 7.7 anyway. *Rejected: an
engine module*, which ships verification code in the engine libraries for reuse nobody has asked
for.

**Reading the report: nlohmann-json**, a test-only dependency as `catch2` already is. Its `at()`
throws and names the missing key; rapidjson's `operator[]` asserts through `RAPIDJSON_ASSERT`, which
is `assert` and therefore absent in release builds — the unsafe path is the default one, in exactly
the configuration nothing would catch it. *Rejected: a hand-written parser*, which is a JSON parser
to maintain for no gain.

**Reading the capture: `Asset::ReadPng`**, beside `WritePng`, with stb_image's implementation moving
out of `engine/engine/src/TextureLoader.cpp`. stb_image's functions are `extern` and may be
implemented in exactly one file; `scene_tests` links the engine, so a second copy in `TestSupport`
would collide. `ImageWriter.h` already claims the Asset module owns the image library, which this
makes true. Comparisons cover all four channels: captures are opaque, so alpha costs nothing and a
capture that stops being opaque is caught rather than hidden.

**Each signal is compared only when the conditions it depends on match**, and a skip names the field
that caused it. Refusing outright is not available: D26 requires counters compared *across*
backends. The table has three kinds of cell.

- **Shown dependency, so it gates.** A captured final frame reads one barrier higher
  (`Engine.cpp:541-544`); `--validation-policy ignore` never counts an error
  (`Diagnostics.cpp:16`); `NDEBUG` decides whether validation runs at all (`Engine.cpp:108`);
  `noUi`, the extent, the scene, the camera and the input script all change pixels.
- **Required independence, so it must never gate** — gating would excuse the defect the comparison
  exists to find. Backend and device never gate counters (D26: "a bug in one of them, always");
  `jobCount` never gates anything, since a difference there is a race
  (`SceneLaunchTests.cpp:167-169`); `headless` never gates pixels, verified pixel-identical at step
  46; `noUi` never gates counters, because the UI pass records either way.
- **Unknown, which gates.** Most of the table. The planned comparisons never reach an unknown cell —
  the baseline run, the scene tests and 7.7's cross-backend pair each differ only in fields that are
  already classified — so caution costs nothing where it counts, and misattributing a flag
  difference as a code regression is the expensive failure.

**Pixel tolerance follows from the backends named in the two reports**, so there is no flag to
nudge: the same backend means exact, and in 7.6 a cross-backend pair reads "not comparable", because
D26's limits cannot be chosen until 7.7 has two backends to measure between.

**A field a report lacks, or one the table does not classify, is a hard failure** — the second
enforced by a unit test asserting that every field `WriteRunReport` emits is classified, which makes
it fail in precommit rather than at comparison time. `WriteRunReport` becomes reachable from tests,
which the round-trip test needs too. Only the failures that prove the *baseline* is stale suggest
recapturing it; a signal that moved prints what moved and asks whether that was expected, because
nothing in the tool can know.

**A missing field still produces a provisional comparison**, treating the absent fields as matching,
labelled as provisional, still exit 3. Without it every field-adding step — 5, 6 and 7 — would
promote a new baseline with no evidence that nothing moved, at the exact moment it is touching the
code the baseline protects. It is also what lets steps 5 and 6 land their fields and be verified on
the way past, leaving step 7 to classify everything in one pass and recapture once.

**The report gains eleven `run` fields** in step 7, each because something in the code shows it
changes a signal: `scene`, `cameraPreset`, `inputScript` (`null` when none), `captureFrame` (`null`
when none), `validationEnabled`, `validationPolicy`, `vkSyncValidation`, `vkDisabledExtensions`,
`vkForceSingleQueue`, `framesInFlight` and `windowMode`. The two validation fields are recorded
*before* step 12 makes either selectable, and deliberately: both are true properties of every run
today — one derived from `NDEBUG`, the other hardcoded `VK_TRUE` — so recording them is honest
immediately, it makes the gating table classify what it actually depends on rather than inferring
it from `buildConfig`, and it leaves step 12 adding no report fields at all. Paths are recorded as given on the command line, and the gate compares the strings.
`windowMode` records the mode in effect at the end of the run, asked of the window system rather
than remembered from the request, since `SdlPlatform` falls back from exclusive to borderless
(`SdlPlatform.cpp:334-349`) and `SetWindowMode` is a request the window system may refuse; it is
`null` for headless runs, as `presentMode` is. `IPlatform` gains the getter that needs.
*Left out:* the content root, which differs per machine while the scene path already identifies the
scene; the editor's window *size*, already covered by the extent; `--strict-validation`, which only
changes the exit code.

**Exit codes**, strongest first: **3** no verdict (a field missing or unclassified, an unreadable
input, bad arguments), **1** a compared signal moved, **2** nothing moved but a signal was skipped,
**0** everything compared and matched. A difference in a signal that *was* compared is real whatever
happened to the others, which is why 1 outranks 2. 7.7 needs "not comparable" and "moved" told
apart, which a single failure code cannot do.

**Failures write images** — actual, expected and an amplified diff — from the library, so both
callers get them. Amplification scales each pixel's largest channel delta so that the active
setting's limit is full brightness; within a backend the limit is zero, so any difference is fully
bright. CI cannot retrieve them yet and that is a `backlog.md` row, not 7.6 work.

**The workflow becomes one command.** `baseline_test.sh` captures and then compares against
`tests/baseline/`, whose two files take fixed names, and exits with the tool's status.
`--update` recaptures, prints the comparison and replaces both committed files together; it refuses
when the run's conditions differ from the baseline's, apart from fields the baseline lacks, so it
cannot quietly move the baseline to another machine or configuration; it does nothing when nothing
moved, since PNG encoding is not reproducible and rewriting an identical image would still put a
binary diff into git. Precommit does not run any of it: it needs a display and the baseline's GPU.

### 4.2 The shader build

Decided on 11 September 2026, and measured rather than assumed — see §9's second entry.

**`sm_6_0`**, the lowest model every current shader compiles at. Raising it is one flag in
`cmake/Shaders.cmake`, and each raise lifts the minimum hardware the D3D12 backend will run on, so
it waits until a shader needs it.

**Entry points become `main` and the seam stops carrying them** — D33.

**D29's registers arrive through one macro.** Every resource declaration swaps `[[vk::binding(i, s)]]`
for `: register(<class><i>, space<s>)`, and the SPIR-V compile gains `-fvk-b-shift 0 all` and the
same for `t`, `s` and `u`, which is what makes Slang derive the Vulkan set and binding from the
register. The push constants keep `[[vk::push_constant]]` *and* need a register, because the two
APIs need different halves: Vulkan reads the attribute, D3D12 reads the register. Both live in one
place instead of at every declaration:

```slang
// engine/engine/src/shaders/registers.slangh
#define PUSH_CONSTANT(Type, name) [[vk::push_constant]] Type name : register(b0, space7)
```

**A shader header rather than `Common.h`**, which this section first said. `Common.h` holds the
values shared with C++ and is compiled as C++ too, and nothing in `engine/` outside the RHI module
may name Vulkan — `rhi_boundary_check` catches `vk::` there on sight, and it was right to. The new
header is what `bakePerlinWorley.comp.slang` includes, since it is the one shader that wants the
convention and not the global buffer.

so each shader writes `PUSH_CONSTANT(MaterialPushConstant, pcMatData);`. *Rejected: Slang's own
`[push_constant]` alone*, which compiles but leaves the D3D12 side at an implicit `b1` in space 0,
numbered by declaration order — the hazard D29 exists to remove. *Rejected: pasting the space from a
number in `Common.h`*, because Slang's preprocessor will not expand a function-like macro inside
another macro's body or as an argument to one; it fails at the `#define`. *Rejected: passing the
space from the build with `-D`*, which does work and makes the number exist once for both languages,
at the price of moving it out of the source and breaking any hand or editor invocation of `slangc`.
The number is therefore written twice — `7` for C++, `space7` in the macro — and step 48's reflection
test is what ties them, since reflection reports the space (`"space": 7`).

**The DXIL gate is a signature check, not a second validator.** DXC validates and signs every
compile, so the one silent failure is validation being switched off, which leaves the container hash
all zeros. `cmake/CheckDxilSignature.cmake` reads the container's magic and 16-byte hash after each
DXIL compile and fails the build on a wrong magic, an all-zero hash or the preview-bypass pattern —
the same placement `spirv-val` has, and for the same reason its comment gives: a check that can
silently disappear is worse than no check. *Rejected: trusting DXC's default*, which leaves the gate
as a downstream tool's default that nothing in this repository asserts. *Rejected: `dxv`*, which
vcpkg's port does not install and which passed the unsigned control anyway.

Built at step 10, with every constant taken from DirectXShaderCompiler's own
`include/dxc/DxilContainer/DxilContainer.h` rather than from memory: `DxilContainerHeader` is packed
to 1, so the four-character code `DXBC` is at offset 0 and the sixteen hash bytes at offset 4, and
`PreviewByPassHash` is sixteen bytes of `0x02`. All four branches were exercised against real
containers — a signed one passes, `-Xdxc -Vd` fails on the zeroed hash, a hand-patched bypass hash
fails, and a wrong magic fails. The `-Vd` control was also run through the build itself, which
stopped on the first shader.

### 4.3 The step sequence

Twelve steps, flat-numbered now that the order is settled — the same count Stage 7.5 took.

| Step | What | Verified by | Size |
|---|---|---|---|
| 1 | `Asset::ReadPng`; stb_image's implementation moves out of `TextureLoader.cpp` | a write-then-read unit test; scene tests; one last baseline check by the manual recipe | S |
| 2 | `TestSupport` and the image comparison — limits, worst pixel, fraction, bounding box, diff images. Scene tests stop using `==` | unit tests with positive controls (a one-pixel change fails and names its coordinates); the scene tests | M |
| 3 | Report reading, the gating table over today's fields, the four outcomes, missing and unclassified fields, the provisional comparison, the completeness and round-trip tests | unit tests | M |
| 4 | `HikariCompare`; `baseline_test.sh` and its `.bat` capture then compare; `--update` and its guard; the baseline renamed to fixed names; `CLAUDE.md`'s regression section rewritten | a run against the still-unchanged baseline exits 0; an edited PNG exits 1 with a diff image; `--update` refused on a release build | M |
| 5 | Backend selection (D34): `rhi/Backend.h`, `DeviceDesc::Backend`, parse-time refusal, `--backend` | `--backend D3D12` refused on Linux naming what the build has; `baseline_test.sh` exits 0, since this step adds no report field | M |
| 6 | Device info (D35): `DeviceInfo`, `GetInfo()`, the report's `system` block, `os` and `arch` as compile definitions | exit 3, provisional, nothing moved | M |
| 7 | The eleven new `run` fields, `IPlatform`'s window-mode getter, the input-script path, every classification; the baseline recaptured once through `--update` | a provisional "nothing moved", then `--update` | M |
| 8 | Per-stage blobs; entry points become `main`; `EntryPoint` leaves `ShaderStageDesc` (D33) | `baseline_test.sh` exits 0; gpu and scene tests | M |
| 9 | D29's registers through the `PUSH_CONSTANT` macro; the four `-fvk-*-shift` flags | `baseline_test.sh` exits 0 | S–M |
| 10 | `directx-dxc` as a host dependency; the DXIL target at `sm_6_0`; the signature check | all six CI configurations build, Windows included, which is what tests the library lookup; one deliberate `-Vd` compile fails the build, then is reverted | M |
| 11 | Step 48: `ShaderTypes.h` beside the shaders, and the reflection test over both targets' JSON | unit tests comparing the reflection against `offsetof` and the attribute table | M |
| 12 | Runtime validation: `--validation`, `--vk-sync-validation`, `RunScene` setting validation explicitly, the Linux release `ctest -L scene` | the release job runs the scene suite, whose `ValidationErrors == 0` checks can now fail | S–M |

Step 1 precedes 4, which is where `ReadPng` is first needed; 2 precedes 3 precedes 4. **5 precedes
6**, because `system.backend` is spelled in the enum `rhi/Backend.h` introduces. **5 and 6 sit
between 4 and 7** so that step 4 is verified against a baseline nothing has touched yet, and so
that every field the stage adds is classified in one pass and recaptured once — the recapture count
is not the reason, the single classification pass is. 10 follows 9, since DXIL without explicit
registers "satisfies a gate while being unusable" (D29), and follows 8 so the first DXIL blob is
already per stage. 11 follows 10, because its reflection rides on the compiles that ship. 12 is
free of all of it and sits last because it is the only step that changes CI.

### 4.4 Runtime validation, and what the second interview settled

The 11 September session stopped with six open decisions. They were taken on **12 September 2026**,
and the two that govern the seam left this section for the D-series: **D34** for backend selection
and **D35** for device identity. The reserved push-constant space became part of **D29** — it is 7,
and the reason is written there. What follows is the rest.

**Runtime validation is two flags, and neither decides what a message means.**

`--validation on|off` decides whether the backend's validation layer is loaded at all — it feeds
`DeviceDesc::bEnableValidation`, already a runtime field that the engine was deciding at compile
time. It is tri-state in `RunSpec` (`std::optional<bool>`): **absent keeps today's behaviour
exactly**, with `NDEBUG` deciding, so nothing changes for anyone who does not ask.

It is deliberately *not* folded into `--validation-policy`, which decides what happens to a message
once the layer is loaded. A level below `ignore` would put two different questions behind one word:
`ignore` loads the layer and discards messages late, which `Diagnostics.h` argues for precisely so
that the flag cannot change what the layer prints, and `ValidationPolicy` is an RHI enum owned by
`Diagnostics`, which has no business knowing whether a layer was loaded. *Rejected: redefining
`ignore` to mean off*, which contradicts reasoning already recorded in the header. The one
meaningless combination — validation off with `--validation-policy failfast` — is refused at parse
time, beside the existing `--strict-validation` check.

`--vk-sync-validation on|off`, defaulting to on, exposes the `validate_sync` layer setting that
`VulkanDevice::CreateInstance` hardcodes to `VK_TRUE`. **Prefixed rather than neutral**: D3D12 has
no synchronization validator, so a neutral flag would be one that exactly one backend can honour —
the shape of leak this stage exists to catch — and `--vk-disable-extension` and
`--vk-force-single-queue` already mean "Vulkan-only testing lever" here. The want is manufactured by
this stage: a release run that validates is the point of `--validation`, a release run is the only
one whose `timings` mean anything, and sync validation is the expensive sub-mode. The objection
that a switch is a way to silently weaken every run is answered by the tooling being built
alongside it — `vkSyncValidation` is a `run` field, so a run with it off is visibly not comparable
rather than quietly different.

**Best practices gets nothing, and that is a decision rather than an omission.** It is commented
out for a layer crash, and vcpkg still offers only 1.4.357.0 — the tag the upstream fix landed
after — checked again on 12 September 2026. A flag whose only useful position segfaults is worse
than no flag, and unlike sync validation this one would exist to turn *on* something that does not
work. `backlog.md`'s row now records what re-enabling actually involves: best practices emits
performance *warnings*, so `validationWarnings` moves off zero and the baseline has to be
recaptured deliberately, nothing here has ever run with it on so a triage pass is needed, and the
flag-versus-unconditional question is decided at that moment rather than now.

**The scene tests set validation explicitly in `RunScene`**, rather than inheriting it from the
build configuration. That makes the suite configuration-independent — the same eleven cases assert
the same thing in all six CI jobs, and the four `ValidationErrors == 0` checks stop being trivially
true in a release build — and it promotes the Linux release job by deleting one `if:` in `ci.yml`
rather than threading a flag through CTest. Stage 7.7's Windows release job inherits it for free.
*Rejected: an environment variable read by the fixture*, which gets release coverage at the price
of a second way of deciding the same thing and a test whose meaning depends on its environment.

**The report gains a fourth block, `system`.** `RunReport`'s three blocks exist because they are
read differently, and identity is read differently again: it is neither an expectation, nor a
measurement, nor a condition the run was asked for. So **`run` is what was asked for and `system`
is what answered** — `backend`, `gpu`, `driver`, `apiVersion`, `os`, `arch`. `backend` lives there
despite being chosen by a flag, following the principle §4.1 already set for `windowMode`: record
what was in effect, not what was requested. A run with no `--backend` still ran on something. It
also keeps D26's pairing intact, since "backend and device never gate counters" is one rule and
reads better as one block. The cost, accepted: answering "are these two reports comparable?" now
means reading two blocks, and a block-level never-gates classification is slightly weaker than
per-field ones at forcing thought when a field is added — for identity the rule is genuinely
uniform, which is what makes the trade worth it.

`os` and `arch` are compile definitions beside `HIKARI_BUILD_CONFIG`, not runtime queries. The OS
*version* would need an `IPlatform` getter with a real implementation per platform, and `gpu` and
`driver` already distinguish the machines it would distinguish — the driver string being the thing
that actually changes rendering behaviour. A `backlog.md` row covers it for whoever is first stuck
comparing two reports and unable to tell why they differ. The block is deliberately mixed in case:
`backend` is lowercase because `--backend` must accept exactly that text, `os` is capitalised as a
proper noun (`"Windows"`, `"Linux"`), `arch` stays lowercase as an identifier (`"x86_64"`).

**Step 48's reflection rides on the compiles that ship**, which is what places it after step 10.
`cmake/Shaders.cmake` adds `-reflection-json` to the invocations already there, so the JSON
describes exactly the blob produced, under exactly the flags it was produced with — step 9's four
`-fvk-*-shift` options included, which means the same artefact carries the real register and space
assignment. *Rejected: a `-target hlsl` pass of its own*, which would need no DXIL toolchain and
would free the step to land anywhere, and would reflect an artefact nobody runs: one flag added to
the real compile and not the reflection one produces a test that agrees with a parallel universe
while staying green. §4.2 states the principle already — a check that can silently disappear is
worse than no check.

**`ShaderTypes.h` lives beside the shaders**, at `engine/engine/src/shaders/ShaderTypes.h`. `slangc`
is invoked with no `-I` at all and resolves shader includes relative to the including file, so this
costs no build change; putting it beside the C++ would mean adding `engine/engine/src` to the
shader compiler's include path, opening the whole private source tree to `#include` from a shader
for no gain. The recursive `engine/engine/src/*.h` glob means it is header-checked and formatted
either way, so that is not a differentiator. C++ reaches it as `#include "shaders/ShaderTypes.h"`,
which says at the point of use that editing it has consequences in two languages.

**The test asserts only what C++ independently declares.** Every assertion compares two sources
written separately that must agree; none compares the reflection against a literal table typed into
the test. That gives field offsets, sizes and types against `offsetof`, and vertex input locations
and formats against `GetAttributeDescriptions` — and it excludes the entry point name, because
after D33 nothing in C++ names it, so asserting `main` would mean typing `main` into the test and
checking that the reflection agrees with the test. That belongs with the build flag it guards,
beside the DXIL signature gate.

**It stays narrow, and the register comparison is a backlog row.** Comparing every resource's
register, space and kind against the bind group layouts would catch D29's reordering hazard
directly, and it was considered and dropped for this stage. Most of that hazard is already caught:
a type mismatch or a missing binding fails pipeline creation under both backends' validation, and
two same-type resources swapped shows up immediately in the baseline pixel comparison. So it buys a
precise message rather than new coverage, at the price of hauling the layout arrays out of
`Engine.cpp` — the file the working rules say not to touch outside its scheduled step. It becomes
cheap once Stage 8 splits that file into passes that own their layouts, which is what the backlog
row is blocked on.

Three corrections this interview turned up in `architecture_plan.md` are **made**: §16.6 described
`ImageCompare` as a "perceptual diff" and risk row 7 said "perceptual tolerance", both superseded by
D26 and now stating its two limits; and the Stage 7.6 summary in its Part IV preamble listed
"Windows GPU coverage on WARP", which D28 moved to 7.7. §15.4's own "perceptual metric and a
tolerance" is left as it stands, because §15 already records that D26 supersedes it.

---

## 5. Stage 7.7 — the D3D12 backend

**Status: in progress — steps 1–6 done.** Grilled on 13 September 2026 — on Linux, then in two sittings on
this project's Windows install, where the measurements only that machine could take were made. The
decisions that govern the RHI's seam are **D36–D46** in §2, together with amendments to **D15, D26,
D32 and D35**. This section is the stage: what bounds it (§5.2), the facts it rests on (§5.3), the
decisions about the stage rather than the seam (§5.4), and the ten steps (§5.5). Before step 1, the
re-grill's four mechanical checks in `CLAUDE.md` apply as for any grilled plan.

### 5.1 What the interview changed

The interview started from a survey written the day Stage 7.6 closed. It found seven places where the
seam said something only Vulkan could honour — the kind of finding D22 was — and it found that several
of the survey's premises were wrong or missing.

**Premises that moved.**

- **The Linux translation layer was misnamed.** DXVK translates D3D8–11; the one that translates D3D12
  is vkd3d-proton. `backlog.md`'s row is corrected.
- **WARP is not only the copy Windows ships.** Microsoft publishes newer builds as the
  `Microsoft.Direct3D.WARP` NuGet package. In-box WARP on the Windows 10 machine offers shader model 6_2
  and no enhanced barriers; NuGet WARP 1.0.20 offers 6_9 and enhanced barriers — and it loads only from
  beside the executable, silently losing to `System32` when placed anywhere else.
- **The RX 580 has no enhanced barriers**, on AMD's legacy Polaris and Vega driver branch before and after
  an update, so the survey's question stopped being "is the Agility SDK in" and became "is a legacy
  barrier path built".
  The SDK then earned its place through something the survey did not mention: the debug layer, which does
  not load on that machine without it (D36).
- **Debug-layer messages cannot reach a callback there**, so validation on D3D12 is polled (D40).
- **The copy-pitch gap has a D3D12 answer made for it.** The unrestricted copy pitch relaxation, from
  Microsoft's *VulkanOn12* specification, makes the seam's tight packing legal (D39).
- **D3D12MA had already been chosen**, by D25, and needed no question.
- **ImGui's DX12 backend stores raw GPU descriptor handles as texture IDs**, which is what ruled out a heap
  that can move (D43).

**The seven gaps, and what answered each.**

| Gap the survey found | Answer |
|---|---|
| `SetCullMode` has no D3D12 equivalent | D41 — cull mode leaves the command list |
| A tightly packed readback buffer is the wrong size on D3D12 | D39 — the unrestricted copy pitch is required |
| Only one shader-visible heap of each kind can be bound | D43 — one persistent heap per kind, sized once |
| `SubmitDesc`'s semaphores are a Vulkan-only concept | D42 — the submit names the image it writes |
| `NativeWindowHandle` is an `SDL_Window*`, not a native handle | §5.4 — the D3D12 backend asks SDL for the HWND |
| The UI backend is chosen by name at three call sites | §5.4 — a factory in `engine/editor` |
| The boundary check has no D3D12 half; the GPU fixture no backend axis | D45, and §5.4's per-process registration |

Two more gaps were already booked for this stage and are answered too: vertex input semantics (D32,
amended) and D26's two constants (D26, amended).

### 5.2 Scope, and what binds it

**The requirement: the stage must work on this project's Windows install**, an AMD Radeon RX 580. It is
the machine the D3D12 work is built and judged on, so its adapter's answers in §5.3 are constraints, not
data points. A second Windows 11 PC with a modern GPU is available roughly weekly, as a real-hardware
check rather than a safety net.

**Done means full parity.** Both apps run under `--backend D3D12`, windowed and headless, with the UI
drawn; the GPU and scene suites pass locally and on WARP in CI; and D26's two constants are committed.
*Rejected: headless parity*, with the editor refusing D3D12 and windowed work carried to a later stage,
which would end the stage with nothing on screen and design the swapchain later, on its own — while §7
puts the frame graph after this stage precisely so it is written with two whole backends in front of it.
*Rejected: the device alone*, which contradicts D26's obligation to commit pixel tolerances from scene
runs. **The cost, accepted:** the longest stage so far. The mitigation is §5.4's ordering, which lands the
scene suite and CI mid-stage.

**Obligations earlier stages already placed on it.**

| Source | What it obliges |
|---|---|
| **D25** | Vulkan is the default on every platform, permanently. A Windows build links both backends; a Linux build refuses `--backend D3D12` at parse time, which it already does |
| **D26**, amended | Counters match exactly across backends, validation counters at zero; two pixel caps, measured and committed by this stage |
| **D28** | The Windows GPU job is this stage's, and every Windows CI job asks for D3D12 by name |
| **D15**, amended | The pipeline cache's path is backend-distinguished; D3D12 builds no cache |
| **D24, D33** | The engine loads shader bytes by `DeviceCaps::ShaderExtension`, one stage per blob, entry point `main`; the D3D12 side needs no new loading code |
| **D13** | Naming already follows D3D12 wherever the APIs disagree |
| **D10, D11, D12** | Clip-space handedness has one site, `Rhi::Format` is curated with `default:`-free switches, the Slang shaders are portable. Already done |

**The seam as surveyed, for scale.** On 12 September `engine/rhi/include/rhi/` was 22 headers, and a
D3D12 backend implements six interfaces — `IDevice` (42 virtuals), `ICommandAllocator` (2),
`ICommandList` (21), `IPresentTarget` (9), `IUploadContext` and `IPipelineCache`. The Vulkan backend was
6,385 lines across 37 files. The inventory: six bind group layouts, pinned by
`BindGroupLayoutInventoryTests`; three graphics pipelines, four after D41; two compute pipelines; seven
command allocators per frame in flight, two recorded on job-system threads; one submit per frame carrying
seven lists. D41 and D42 remove one virtual each from `ICommandList` and `IPresentTarget`. The root
signature budget is not a constraint: `D3D12_MAX_ROOT_COST` is 64 DWORDs and no layout comes close.

**Out of the stage**, each a `backlog.md` row: the post-stage checks — committed per-backend references,
D3D12 pixels in CI, revisiting D26's no-headroom rule, per-region caps as a candidate; an
`ID3D12PipelineLibrary` (D15, amended); on/off flags accepting `true`/`false` and `1`/`0`; and running the
D3D12 backend over vkd3d-proton on Linux.

### 5.3 The machine, and what was measured

**The machine.** AMD Radeon RX 580, Windows 10 Pro 19045, AMD Adrenalin 26.5.2 for Polaris and Vega
(`31.0.21925.1001`) since 13 September 2026. AMD's in-app update check does not offer that branch's
releases — it reported 25.8.1 as current while 26.5.2 existed — so a newer driver is found on AMD's
support page for the card, and the installed version is `RadeonSoftwareVersion` under
`HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0001`. D26's
constants are measured against a driver, so confirm it is current before step 10.

**Capabilities**, under Agility SDK 1.619.5 proven loaded:

| | RX 580 | WARP in-box (6.2.19041.5794) | WARP NuGet 1.0.20 |
|---|---|---|---|
| Feature level | **12_0** | 12_1 | 12_2 |
| Shader model | **6_7** | **6_2** | 6_9 |
| `EnhancedBarriersSupported` | **false** | **false** | true |
| `ResourceBindingTier` / `ResourceHeapTier` | 3 / 2 | 3 / 2 | 3 / 2 |
| `UnrestrictedBufferTextureCopyPitchSupported` | true | false | true |
| `InvertedViewportHeightFlipsYSupported` | true | false | true |
| `MaxViewDescriptorHeapSize` / `MaxSamplerDescriptorHeapSize` | 33,554,432 / 4,096 | 1,000,000 / 2,048 | 2,097,152 / 2,097,152 |
| `D3D12_SHADER_CACHE_SUPPORT` flags | `0x1F`, automatic disk cache included | `0x3` | `0x3` |
| PCI vendor / device | `0x1002` / `0x67DF` | `0x1414` / `0x008C` | `0x1414` / `0x008C` |

The RX 580's values were identical on Adrenalin 25.8.1 and 26.5.2.

**Validation, on both adapters.** Without the SDK's `d3d12SDKLayers.dll`, `D3D12GetDebugInterface` fails
with `DXGI_ERROR_SDK_COMPONENT_MISSING`. `ID3D12InfoQueue` is available, `ID3D12InfoQueue1` is not. A
zero-width texture and a wrong legacy before-state are both reported as errors, the latter at
`ExecuteCommandLists`; GPU-based validation adds a second, GPU-side report of the barrier error on the RX
580. `SetBreakOnSeverity` raises exception `0x87A` inside the offending call with no debugger attached.
DXGI's own debug layer is in-box (`System32\DXGIDebug.dll`), and DXGI reports tearing support.

**How it was measured.** Four throwaway programs live outside the repository at `C:\Dev\d3d12-probe`, with
a README: `d3d12probe` (capabilities and shader-cache support), `d3d12validation` (the debug layer and
deliberate errors), `d3d12break` (break-on-severity) and `d3d12heapcost` (descriptor heap memory). Its
`build.bat` pins the project's vcpkg baseline. Two traps it records. **Which runtime loaded has to be
proven** before a false is believed — `GetModuleHandle("D3D12Core.dll")`, its path, and the
`D3D12SDKVersion` that loaded core exports — because a tool running on the in-box runtime reports false
for everything new. And **this machine's `%VCPKG_ROOT%` is a classic checkout older than the project's
baseline**, so anything installed in classic mode resolves older ports; the first probe got Agility
1.619.4 that way, and everything was re-measured on the pinned 1.619.5.

**Facts filed for the steps that need them.**

- Descriptor sizes are queried per device, never assumed: the RX 580 reports CBV/SRV/UAV 32, sampler 16,
  RTV 32 and DSV 216 — and 152 for all four with the debug layer on.
- A shader-visible resource heap costs about 60 bytes of video memory per descriptor on the RX 580: 3.9
  MiB at D43's default.
- `EnumAdapters1` lists the RX 580 at 0 and WARP (the Microsoft Basic Render Driver) at 1, and ordinary
  enumeration loads NuGet WARP when it is deployed beside the executable. Both WARPs share PCI IDs.
- `InvertedViewportHeightFlipsYSupported` is optional, so D10's `bFlipClipSpaceY` stays the mechanism.
- `uploadSubmissions` is exact across backends (D26), so the D3D12 upload context must batch as
  Vulkan's does — one scene's textures in one load scope producing the same handful of submissions.
- Mixing legacy and enhanced barriers on one subresource is allowed, with rules — a non-simultaneous-access
  texture in `D3D12_RESOURCE_STATE_COMMON` before an enhanced barrier references it, in
  `D3D12_BARRIER_LAYOUT_COMMON` before a legacy one does (the Enhanced Barriers specification). D37 runs
  one path per process, so nothing mixes; it matters only if that changes.
- Slang finds vcpkg's DXC on Windows: CI run 34715242114 compiled all eight `.dxil` blobs on
  `ninja-debug-windows`, each passing `CheckDxilSignature`.

### 5.4 Decisions about the stage

Each gives the decision, what it rules out and why, and the cost accepted. The seam's decisions are §2's.

**Module layout and deployment.** D3D12 sources live in `engine/rhi/src/d3d12/`, appended to the RHI's
source list under `if(WIN32)` together with `HIKARI_RHI_D3D12`, as `Backend.cpp` says they land. The three
Windows presets carry both backends (D25). One CMake function per executable — `HikariEditor`,
`HikariHeadless`, `scene_tests`, `rhi_gpu_tests` — copies `D3D12Core.dll` and `d3d12SDKLayers.dll` into
`D3D12\` (the layers always from the port's debug tree, per D40) and `d3d10warp.dll` beside the
executable; vcpkg's automatic DLL copying cannot, since none of the three is an import. **NuGet WARP
arrives through a repository overlay port**, `directx-warp`, downloading the package by SHA-512 nearly line
for line as `directx12-agility`'s own portfile does, so every binary dependency comes through one channel
and CI's existing vcpkg cache. *Rejected: a CMake download at configure time*, a second channel outside
that cache; *rejected: committing the DLL*, a 15 MB binary per version under a licence describing it as
"for testing and development purposes". *The cost:* the repository's first overlay port.

**The present-mode mapping, a fact.** Vulkan defines Mailbox as waiting for the vertical blank,
"Tearing cannot be observed", with a single-entry queue whose new request "replaces the existing entry";
DXGI's flip model at sync interval 0 will "discard this frame if a newer frame is queued". So the D3D12
swapchain maps Mailbox to sync interval 0, Immediate to sync interval 0 with `ALLOW_TEARING`, and Fifo to
sync interval 1, and never offers FifoRelaxed, which DXGI has no counterpart for and the engine's
preference order never picks. Reports name the same mode for the same behaviour on both backends, which
`run.presentMode`, a comparison condition, needs.

**The UI backend is chosen by a factory.** `Editor::CreateUiBackend(Rhi::Backend)` is one switch returning
an owning pointer, shaped like `Rhi::CreateDevice`'s dispatcher down to its `#ifdef HIKARI_RHI_D3D12` case.
Both apps and `SceneLaunchTests.cpp` pass the parsed backend and include no concrete UI backend.
`UiBackendDesc` gains nothing: a D3D12 UI backend reaches the queue and D43's heap callbacks through
`D3D12Native.h`, and ImGui's platform half takes the SDL window whatever the API. `imgui` gains
`dx12-binding` as a Windows-only feature. The engine cannot choose, since `Editor` sits above `Engine`.
*Rejected: a switch at each call site*, the same choice written at four construction sites. *The cost:* a
new public function, and a `main.cpp` that no longer shows which class runs.

**The D3D12 backend asks SDL for the HWND** — `SDL_GetPointerProperty` on the window's properties with
`SDL_PROP_WINDOW_WIN32_HWND_POINTER` — just as the Vulkan backend asks SDL for its surface, and both its
window-system extensions and surface creation already come from SDL. The seam is unchanged;
`DeviceDesc.h`'s comment stops claiming the backends want "a native window pointer versus an HWND".
*Rejected: the platform answering real native handles, with SDL removed from the RHI*, which would mean
reimplementing Vulkan surface creation for Win32, X11 and Wayland, the latter two testable only on Linux;
*rejected: the platform answering only the HWND*, two kinds of handle for two backends. *The cost:* the RHI
stays coupled to SDL in both backends, and `NativeWindowHandle` keeps a name that overstates it.

**The GPU and scene suites choose their backend per process, and CMake registers each twice.** A test
environment variable tells the fixture's `MakeDesc`, `RunScene` and the launched `HikariHeadless` which
backend to use; `tests/CMakeLists.txt` registers `rhi_gpu_tests` and `scene_tests` once per backend the
build contains, with the variable and a label set on the registration, so `ctest` supplies the choice and
CI selects D3D12 by label. Backend-specific cases are tagged and skipped on the other backend. A variable
naming a backend the build lacks fails rather than skips. D3D12's fixture arrangements are `Default` and
`SingleQueue`, crossed with D38's barrier path. *Rejected: a backend axis inside one binary*, every
`RequireDevice` site edited to loop and the Windows job still needing a variable to say Vulkan is not
expected there. *The cost:* a test binary run by hand gives Vulkan unless the variable is set.

**Sequencing — six decisions.**

- **Interleaved.** Each seam change lands in the step whose D3D12 code first needs it. Seam first, as D18
  did, was judged the more correct order and set aside so that the backend is not kept waiting. *The
  cost:* steps carry a Vulkan-side seam change beside new D3D12 code, and a rendering change can land beside
  a D3D12 backend that cannot yet render the scene it would be checked against.
- **Gated within a step, one commit.** When a step carries a seam change, it is made first on Vulkan alone,
  and the D3D12 half begins only when §5.5's gate passes; the step ends as a single commit. That recovers
  seam-first's one-suspect property for Vulkan regressions without delaying the backend. *The cost:* the
  seam's shape is settled before the D3D12 code that might reshape it exists, and history cannot bisect
  between a step's halves.
- **Headless first.** Device, resources, pipelines, the offscreen target and the scene suite come before
  the swapchain and the editor, because the gate needs a D3D12 run to check against and the scene suite
  is headless. Headless still initialises the UI backend, so a minimal `D3D12UiBackend` arrives with the
  first scene. *Rejected: windowed first*, with D3D12 correctness judged by eye for most of the build-out.
  *The cost:* nothing in a window until past the middle, and the present path comes late.
- **Legacy first, enhanced in the very next step.** The RX 580 runs only legacy, so enhanced first would
  leave every early gate on a software rasterizer. Enhanced follows immediately because every barrier goes
  through one method, so the path is contained, and a second independent check on every barrier sequence
  is worth most under every step that follows. *Rejected: late*, divergences surfacing together at the
  end; *rejected: the backlog*, which would reopen D37. *The cost:* the harder mapping first, and every
  later gate longer by a WARP scene run.
- **Counters compared across backends from the first scene step.** Counters are counted by the engine —
  draws and batches by engine code, barriers from what `ICommandList::Barrier` returns for the engine's own
  calls — so they should equal Vulkan's as soon as a frame runs, while D3D12's pixels are still wrong.
  *Rejected: at parity only*, with every divergence surfacing together. *The cost:* two headless runs per
  gate.
- **CI runs D3D12 from the first step with D3D12 tests.** The runner is the second machine that proves the
  deployment — a Windows Server image with no GPU, no Graphics Tools and its own in-box D3D12 — so
  proving it there early keeps a deployment failure from being confused with a rendering one. *The cost:*
  CI debugging from the start over a slow loop, and longer Windows CI runs.

### 5.5 The step sequence

**Ten steps.** Each ends in a compiling, running application, per Part IV's rule, and each is one commit
(§5.4).

**The gate.** Every step's verification is `scripts/precommit` green with the GPU and scene suites
confirmed to have run rather than skipped. A step that changes the seam passes that on Vulkan before its
D3D12 half is written, and a seam change that could alter rendering also passes a before-and-after Vulkan
capture pair compared with `HikariCompare` at zero tolerance — on this machine, or against the committed
baseline on the Linux boot. The committed baseline is Linux's, and on this machine a run compared against
it checks nothing: the editor runs there in `immediate` present mode against the baseline's `mailbox`, and
`run.presentMode` gates counters as well as pixels (found at step 1). So on Windows the before-and-after
pair is taken on this machine — headlessly, since an offscreen target has no present mode — and
`--update` refuses another OS regardless.
From step 7 the gate also runs `HikariHeadless` on a scene under each backend on the RX 580 and compares
the reports without images; a moved counter fails, and exit 2 — pixels not compared — passes until step
10.

| Step | What | Seam | Verified by | Size |
|---|---|---|---|---|
| 1 | **Deployment and device.** `directx-headers`, `directx12-agility`, `d3d12-memory-allocator` and the `directx-warp` overlay port; `src/d3d12/`, the exports OBJECT library, per-executable deployment; the device — adapter order and `--gpu`, the 12_0 and copy-pitch refusals, the SDK version check, the debug layer as a hard requirement with GPU-based validation and polling; `DeviceInfo` with PCI IDs, filled on both backends | D35 am., D36, D39, D40, D46 | `HikariHeadless --backend D3D12` creates a device on the RX 580 and, with `--gpu "Basic Render"`, on NuGet WARP, its report's `system` block naming each; the loaded `D3D12Core.dll` proven to be the SDK's; the Vulkan report provisional, nothing moved | M |
| 2 | **Tests, registration, guards and CI.** Per-backend registration of both suites; `DeviceTests` under D3D12, and a D3D12 validation positive control — the wrong before-state; `rhi_boundary_check`'s D3D12 half and the isolation check; `--vk-disable-extension` refused on D3D12; the Windows CI job running the D3D12 GPU suite on WARP. The ASan preset's compatibility with the debug layer and WARP is found out here, and decided here if it fails | D44 (first half), D45 | `ctest -L gpu` under both registrations, on the RX 580 and WARP; the positive control failing when its deliberate error is removed; a planted violation tripping each boundary check, then reverted; Windows CI green | M |
| 3 | **Resources and uploads.** Buffers, textures, views and samplers through D3D12MA; the upload context on a copy queue; `--force-single-queue`, with the report key renamed | D44 (second half) | Vulkan gate for the rename; `UploadRoundTripTests` under D3D12 in both arrangements, on both adapters | M |
| 4 | **Command lists, submission and legacy barriers.** Allocators, lists, fences, rendering scope; legacy transitions with the private resolution of `TextureLayout::Undefined` | D37 (legacy) | The submission and barrier GPU tests under D3D12; the debug layer silent, and its before-state check live | L |
| 5 | **Bind groups and heaps.** The two persistent heaps and `DeviceDesc`'s capacity; root signatures from pipeline layouts; sampler deduplication | D43 | Bind group GPU tests under D3D12; `BindGroupLayoutInventoryTests` unchanged; exhaustion refused with the capacity named | M |
| 6 | **Pipelines.** Graphics and compute PSOs; cull mode leaves the command list, with a scene mixing single- and two-sided materials; vertex semantics and `ShaderLayoutTests` comparing them; the cache file named by backend, and no D3D12 cache | D15 am., D32 am., D41 | Vulkan gate, the mixed scene included, with a before-and-after capture pair; all four graphics and two compute pipelines created under D3D12, debug layer silent | L |
| 7 | **Offscreen target and first headless scene.** The submit names its image; the D3D12 offscreen target; a minimal `D3D12UiBackend` and the factory; the scene suite on the RX 580 and WARP; the counters-comparison script and the validation zero-on-both rule; CI's scene label. **Measured here:** GPU-based validation's cost, warm `startupMs` and `firstFrame` on both backends and WARP, and WARP's scene-run time | D26 am., D42 | Vulkan gate with a before-and-after capture pair; `ctest -L scene` under D3D12 on both adapters; the counters comparison exiting 2 with nothing moved; Windows CI green | L |
| 8 | **Enhanced barriers.** The enhanced path; `--d3d12-barriers`, its refusals, and its report field classified so it does not gate counters; registrations run enhanced on WARP | D37 (enhanced), D38 | Both suites on WARP under enhanced; legacy and enhanced reports' counters equal on WARP; `enhanced` refused on the RX 580 | S–M |
| 9 | **Swapchain and editor.** The DXGI flip-model swapchain target, the HWND from SDL, the present-mode mapping and tearing; the D3D12 UI backend completed; the editor under D3D12 | — | `HikariEditor --backend D3D12` windowed on the RX 580, the report naming its present mode; the input scripts' resize and capture cases under D3D12 | L |
| 10 | **Parity.** The diff image scaled by the measured worst delta; every differing region explained beside the constants; D26's two constants measured and committed with approval; the cross-backend pixel rule; the Linux baseline refreshed | D26 am., D35 am. | `HikariCompare` on a Vulkan and a D3D12 run of the test scene exiting 0 under the committed tolerance; the refreshed baseline exiting 0 on Linux | M |

**Amended at step 1: its gate is read from the log, not the report.** A run report is written only when a
run completes, and a step-1 device can create itself and nothing else, so `HikariHeadless --backend D3D12`
fails at its first missing call with no report. The device logs what the `system` block would hold — adapter,
PCI IDs, driver, feature level — and the path and version of the `D3D12Core.dll` it loaded, and that log is
the evidence. `DeviceInfo` under D3D12 is first asserted directly by the device tests at step 2 and first
written to a report at step 7. What step 1 also measured: the RX 580 at `0x1002`/`0x67DF`, driver
`31.0.21925.1001`, feature level 12_0; NuGet WARP at `0x1414`/`0x008C`, driver `1.0.20.0` — the package's
version, which is what proves the copy beside the executable loaded — feature level 12_2; in-box WARP, with the
NuGet copy moved aside, refused for its copy pitch; and `D3D12\` moved aside failing every D3D12 call with
`D3D12_ERROR_INVALID_REDIST`, which the refusal now names. A defect in `Engine.cpp` surfaced with it:
`Shutdown` dereferenced the pipeline cache and shut down the UI backend even when `Init` had thrown before
creating either, so any start that failed early crashed in teardown and lost its buffered log. The two steps
are guarded now.

**Amended at step 2: the scene suite's D3D12 registration moves to step 7, and D3D12's test spec grows.**
`engine_test` gained `BACKEND_SPECS`, one registration per backend with a Catch2 test spec and
`HIKARI_TEST_BACKEND`; labels are `gpu` for Vulkan and `gpu-d3d12`, so `ctest -L gpu` runs both. D3D12's spec
names what its backend implements rather than excluding what it does not — `[device]~[vulkan]~[queues]`
and `[d3d12]` at step 2 — so each later step widens it as its gate lists (`[upload]` and `[queues]` at step
3, and so on), and a neutral case never fails merely for arriving early. Registering the scene suite for
D3D12 now would add eleven cases that cannot pass before step 7, so it stays Vulkan-only until then, together
with `RunScene` and the launched `HikariHeadless` reading the variable. `HIKARI_TEST_GPU` names the adapter,
which is what runs the suite on WARP on the RX 580. D45's permanent D3D12 naming entry,
`D3D12UiBackend.cpp`, lands with that file, since an entry matching no file fails the check. **The ASan
preset is compatible**: the D3D12 cases pass under ASan with the debug layer on both adapters, so every
Windows CI job runs them.

**Amended at step 3: the upload context and its round-trip tests move to step 4.** Both need what step 4
builds — the D3D12 upload context records copies into a command list, submits them and signals a fence, and
`GpuReadback.h` reads results back through a command allocator, barriers, a fence and `Submit` — so step 3's
gate as written could not pass until step 4 existed. Step 3 is the resources alone: buffers and textures
through D3D12MA, views and samplers held as their descriptions until step 5's persistent heaps give them
descriptors, `IsFormatSupported`, and the single-queue rename. Its gate is the neutral `[resources]` cases —
creation, mapping, live counts, description, and a stale destroy reported — on Vulkan, the RX 580 and WARP
with the debug layer silent. **Step 4's gate gains `UploadRoundTripTests` under D3D12 in both arrangements and
`uploadSubmissions` batching as Vulkan's does.** A sampled depth texture is created typeless on D3D12, since a
fully typed depth resource admits no shader view of another format; the resource and view formats are
`D3D12Conversions.h`'s. `ValidateTextureDesc` moved from the Vulkan backend to a shared header, as D45 requires
of a helper both backends need.

**Amended at step 4: what the legacy path and the upload context rest on.** Legacy barriers follow D3D12's
implicit state rules (*Using Resource Barriers*): a texture in `COMMON` is promoted to a copy or shader-read state
on first use; a copy queue's resources, and read-only promotions, decay back to `COMMON` once executed; and a
barrier on a resource in `COMMON` may name `COMMON` or any promotable state as its before-state. So the upload
context copies on the copy queue with no barriers, leaving textures in `COMMON`, which a later transition from
`ShaderResource` may name; in the single-queue arrangement a write promotion does not decay, so there it
transitions explicitly, as Vulkan does. **`TextureLayout::Undefined` resolves to the state the texture's
submitted lists left it in** — tracked per texture and advanced at `Submit` in list order, not at recording,
because recorders on other threads may have recorded but not submitted — which is what the engine's per-frame
from-Undefined barriers on its render targets need; D37's "no general tracker" holds for every other layout,
which the barrier names. A barrier whose resolved states match records nothing, since D3D12 rejects it, but is
still counted. Each list has its own native allocator, because D3D12 lets only one list per allocator record at
a time and the seam does not. **The rendering scope** binds targets by CPU descriptor from two non-shader-visible
heaps, written on a view's first use; a clear over the whole target passes no rectangle, because a rectangle does
not count as initializing a target in memory D3D12MA did not zero (the debug layer's ID 1422, measured). **Two
debug-layer IDs are muted**, `CLEARRENDERTARGETVIEW_` and `CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE`: the seam
carries no optimized clear value, so every clear would warn and D3D12's warning count could never equal Vulkan's
zero. The neutral `RenderingScopeTests` clears a colour and a depth target and reads the colour back, under both
backends; with the upload round-trips it is step 4's gate, on the RX 580 and WARP.

**Amended at step 5: how bind groups sit in D43's heaps.** `DeviceDesc` gains `ResourceDescriptorCapacity` and
`SamplerDescriptorCapacity`, which the Vulkan backend ignores. A layout splits its bindings into a resource table
and a sampler table, since the two live in different heaps; a group takes a contiguous range of each — first fit
over a free list that merges neighbours — and groups whose samplers are identical share one sampler range,
reference-counted, which is the deduplication D43 names. Optional textures left empty get null descriptors, so
tables are fully populated whatever the binding tier; an unbound constant buffer or unordered-access texture is
refused, as tier 2 requires. A root signature holds one table per group per heap — register space N for group N,
a binding's slot as its register (D29) — and one root-constants parameter at `b0`, space 7, the space
`registers.slangh` reserves; it is serialized at version 1.0. Uniform buffers are rounded up to 256 bytes at
creation, since a constant buffer view's size must be a multiple of 256 and cannot run past its resource. No
bind group GPU tests existed, so step 5 adds them: a neutral `BindGroupTests` under both backends, and two
D3D12 cases — sixteen groups with one sampler fitting a heap of four sampler descriptors, and a full heap refusing
a group with `ResourceDescriptorCapacity` in the message and reusing a released range.

**Amended at step 6: what the pipelines rest on, and the test the gate names.** D41 landed as written: the opaque
pass creates a back-culling and a two-sided pipeline over one layout and binds whichever a batch needs, and the
transparent pipeline names `CullMode::None` rather than inheriting it. `scenes/mixed_sidedness.map`, one
single-sided and one two-sided cube, joins the scene suite at two draws, two batches and two instances with
validation clean, and the headless before-and-after Vulkan pairs of it and of the test scene compared identical at
zero tolerance. `VertexAttribute` gains `SemanticName` and `SemanticIndex` (D32 am.), which the D3D12 backend
requires and Vulkan ignores; the tables name what `VS_In` declares — the instance streams are `POSITION1`–`4` and
`NORMAL1`–`3` — and `ShaderLayoutTests` compares each against both reflections, the composite quad's included.
The cache file is `pipeline_cache_<backend>.bin`, and D3D12's cache saves nothing: the driver already caches
compiled shaders, and `ID3D12PipelineLibrary` needs a stable hash of every description, which six pipelines did
not justify. A D3D12 pipeline state object matches Vulkan's pipeline field for field — counter-clockwise front
faces, depth clipped rather than clamped, independent blend — with a colour blend factor on alpha mapped to its
alpha twin, since D3D12 forbids the colour form there, and every field a disabled feature ignores set to d3dx12's
default rather than zero. **No test created the engine's pipelines**, so step 6 adds one: the neutral
`EnginePipelineTests` builds all four graphics pipelines — the composite into both formats a present target may
have — and both compute pipelines from the compiled shaders, with the renderer's layout table, vertex tables and
constant blocks, and asserts no errors and no warnings. It passes on Vulkan, the RX 580 and WARP; with the clouds
pipeline's depth layout left out it fails on D3D12 with the debug layer's ID 882, "Root Signature doesn't match
Compute Shader".

**Amended at step 7: what the first scene rests on, what it found, and what it measured.** D42 landed as
written: `SubmitDesc::PresentImage` names the target and the index, `SemaphoreHandle` is private to the Vulkan
backend, and every target tracks each image from Acquire through its one submission to Present, refusing a
second submission or a Present before one — on both backends, so a caller correct on one is correct on the
other — while a submission naming another device's target is refused. The Vulkan before-and-after headless pairs
of the test scene and `mixed_sidedness.map` compared identical at zero tolerance; the readback helpers wait on
the frame's fence value instead of a pending signal. The D3D12 offscreen target has the Vulkan one's shape. Draw
and dispatch recording sets a root signature only when it changes, since a different one makes earlier bindings
stale and the same one keeps them; binds vertex buffers at the draw with the bound pipeline's strides; writes push
constants as whole 32-bit root constants; and binds the device's two heaps when a list begins. **The first scene
found one defect**: a view of `Format::Undefined` reached `CreateShaderResourceView` as `DXGI_FORMAT_UNKNOWN`,
which D3D12 refuses whenever a description is passed (the debug layer's ID 28, then device removal), so a view's
format is resolved to its texture's at creation, as Vulkan resolves it. **The UI backend** is ImGui's DX12 backend
behind `Editor::CreateUiBackend`, keyed on `HIKARI_EDITOR_D3D12`, with the concrete backends private to the Editor
module. `include/rhi/d3d12/D3D12Native.h` hands it the device, the direct queue and the resource heap, allocates
its texture descriptors one at a time out of D43's heap, and hands out a list's native object only after the list
forgets its bindings, since ImGui sets its own; ImGui's pipeline state is created at Init so a failure throws, and a
changed target format is refused until the swapchain exists. The boundary check freezes `rhi/d3d12/` beside
`rhi/vulkan/` — three headers from four sites — `engine_module` gained `WINDOWS_ONLY_HEADERS` so the header check
skips that directory where nothing can compile it, `DirectX-Headers` is PUBLIC as `Vulkan::Vulkan` is, and the
namespace check spells `d3d12` as `D3D12`. **The scene suite is registered per backend** (`scene-d3d12`), passing
the backend, the adapter and full GPU-based validation to the `HikariHeadless` it launches, and Windows CI runs it on
WARP in all three jobs. **The report** records each validation sub-mode as off on the backend without it, gains
`uploadBatches` and D40's word, and `ReportCompare` gains D26's two cross-backend rules. **The gate:** precommit
green with 275 unit, 59 GPU and 24 scene cases, devices required; the D3D12 scene suite passing on the RX 580 and
on WARP; `backend_compare` exiting 2 with nothing moved and every counter compared; and ImGui drawing under D3D12
with no validation message. **Measured** — headless, test scene, 1920x1080, the warm second of two runs. Debug
with validation: Vulkan on the RX 580 starts in 2.59 s, first frame 4.8 ms, 1.5 ms a frame; D3D12 on the RX 580
at `full` 2.83 s, 1.75 s, 22.8 ms, and with GPU-based validation off 2.51 s, 5.3 ms, 1.3 ms; WARP at `full` 4.24 s,
4.5 s, 4.0 s, and off 3.43 s, 12 ms, 0.60 s. Release without validation: Vulkan on the RX 580 about 0.80 s with a
1.6–2.4 ms first frame, D3D12 about 0.76 s and 3.1–3.5 ms, WARP 1.64 s and 4.6–5.0 ms. The D3D12 scene suite takes
77 s on the RX 580 and on WARP alike, against 7.4 s for Vulkan's: startup and `full`'s first-frame shader patching
dominate it, not frames. **The triggers, read with the user:** GPU-based validation's cost reopened D40's default,
now `descriptors` (D40, amended); the pipeline library's does not fire, since D3D12 starts as fast as Vulkan warm
and WARP's setup does not dominate a local step; and WARP's scene-run time does not reopen running enhanced barriers
under every gate. CI's WARP step times are read when the step's run completes. **Left for step 10:** the D3D12
capture draws the cloud layer over the car where Vulkan's car occludes it, which looks like the cloud pass
reconstructing depth at a vertically mirrored coordinate — D10's clip-space flip — and is a parity question.

**Amended at step 8: how the enhanced path maps the seam, and what it found.** D37 and D38 landed as written:
`DeviceDesc::BarrierPath` and `--d3d12-barriers legacy|enhanced|auto`, auto taking enhanced barriers where
`D3D12_FEATURE_DATA_D3D12_OPTIONS12::EnhancedBarriersSupported` says so, `enhanced` refused at device creation
elsewhere naming the adapter and the capability, and a named path refused on Vulkan at parse time. The path taken
is `DeviceInfo::BarrierPath` and the report's `run.d3d12Barriers` — `null` on Vulkan — classified to gate
nothing, since the two paths must agree on counters and pixels alike. Each `TextureBarrier` is one
`D3D12_TEXTURE_BARRIER` with its halves and range as given, recorded through `ID3D12GraphicsCommandList7`. The
mapping follows the Enhanced Barriers specification's tables, with three points decided by them: no access is
`NO_ACCESS`, never `COMMON`, which as a before-access means every write; `AllGraphics` is `SYNC_DRAW`, the scope
that supersedes every graphics stage; and **`DepthStencilRead` is `DIRECT_QUEUE_GENERIC_READ`**, because the
depth buffer after the opaque pass is both a read-only attachment and sampled by the cloud pass, and
`DEPTH_STENCIL_READ` admits no shader read — the queue-specific layout is the only one the specification lists
as admitting both, and every list that records a barrier is a direct list. **Enhanced barriers neither promote
nor decay**, so the upload context latches the textures a copy queue filled from `COMMON` to shader resource
in a submission of its own on the direct queue, ordered after the copy's fence: a layout-only barrier with no
sync on either side, the shape the specification gives for exactly that. That makes a flush two submissions,
which `uploadSubmissions` — measured since step 7 — shows and `uploadBatches` does not. **The enhanced path's
first run found a mistake in test support** that Vulkan and the legacy path had both let through:
`ReadRenderedTexture`'s barrier named a render target's write as its source while naming the shader-resource
layout the image was in, an access that layout does not allow, and the list failed to close with
`E_INVALIDARG`. Its source scope is empty now, as `ReadTextureLayers`' already was, since the write it follows is
in a submission the fence wait orders it after. D3D12's fixture arrangements are `Default` and `SingleQueue`
crossed with the barrier path — the added `LegacyBarriers` and `SingleQueueLegacyBarriers` run the legacy path
on an adapter that has enhanced barriers — and two D3D12 cases join: the path a request gets, and an enhanced
positive control, a barrier through the RHI from a layout the texture is not in, which the debug layer reports.
**The gate:** both suites on NuGet WARP under enhanced barriers, 32 GPU cases and 12 scene cases; the test scene
on WARP under legacy and enhanced, compared with `HikariCompare`, matching in every counter and identical in
every pixel at zero tolerance; and `enhanced` refused on the RX 580.

**Why this order.** Step 1 needs no seam change, so the deployment — the part most likely to differ between
machines — is proven before anything is built on it, and step 2 then proves it on the CI runner. Steps 3–6
build what a frame needs in dependency order: resources before command lists that use them, command lists
before the bind groups and pipelines they bind. Step 7 is the first point at which the engine runs a whole
frame under D3D12, so it carries the first scene, the first counters comparison and the measurements with
triggers. Step 8 follows at once (§5.4). Step 9 is the window, isolated after everything headless is known
good. Step 10 is last because D26's constants cannot be measured before parity.

**Conditions and triggers, and when each comes due.**

- **Step 2:** whether the ASan preset tolerates the D3D12 debug layer and WARP; if not, a decision then.
- **Step 7:** GPU-based validation's cost, which reopens D40's default if it makes the WARP scene suite
  prohibitive; warm startup and first-frame times, the pipeline library's trigger (D15, amended); and
  WARP's scene-run time, which reopens running enhanced under every gate if it is too slow.
- **Before the Windows 11 machine's first weekly session:** run `C:\Dev\d3d12-probe` there. If it reports
  no unrestricted copy pitch, D39's requirement is reopened.
- **After steps 1, 3 and 8**, which change the report, and no later than step 10: refresh the committed
  baseline on the Linux boot, carrying `DeviceInfo`'s PCI IDs, the renamed `run.forceSingleQueue` key —
  whose rename the user approved — and the barrier-path field. The Windows gate does not depend on it.
- **Before step 10:** confirm the RX 580's driver is current (§5.3).
- **When step 1 lands:** delete `backlog.md`'s P3 `--gpu` row, as the backlog's own rule requires.

---

## 6. What this stage needs from other stages

| What | Where | Status |
|---|---|---|
| **Step 47** — headless scene tests in CI | Stage 7 | **Done.** The instrument every step's verification leans on. D26 is what adapts it to two backends |
| **Step 46** — `IUiBackend` + `VulkanUiBackend` | Stage 7 | **Done.** D9's ImGui escape hatch is now a leaf file that a D3D12 build replaces with a sibling, rather than a hole in the renderer |
| **Step 48** — `ShaderTypes.h` shared with Slang | Stage 8 | Pulled into Stage 7.6, extended — see §4 |
| **Steps 50–54** — recorders become `Pass` classes | Stage 8 | Follow this stage, not precede it (D18) |
| **Step 58** — `Mesh*`/`Material*` become handles | Stage 9 | **Stays in Stage 9.** See below |
| **Step 70** — bindless | Stage 10 | Explicitly after the backend (D14) |
| Device info in the run report | `backlog.md` (P2) | **Stage 7.6 step 6.** Its blocker was "a neutral device-info accessor on `IDevice`, which is a seam decision"; D35 takes it |
| Runtime-selectable validation | `backlog.md` (P2) | **Done**, at Stage 7.6 step 12. Its backlog row is retired |

**Why step 58 stays in Stage 9**, against the first draft's recommendation to pull it forward.
`Drawable::operator<` falls through to comparing `pMesh` and `pMat` pointers, so batch order
tracks heap addresses, and ASLR reshuffles them every run. The first draft called that a flaw
in the primary instrument. It is a **latent hazard, not a gate**:

- Batching groups equal keys after sorting, so the counts — `batches`, `drawCalls` — are
  order-independent whatever order the pointers fall in.
- The images are largely order-independent too: opaque is depth-tested, and weighted-blended
  OIT is order-independent by construction.
- If it were biting, the baseline would already fail intermittently across runs. It does not.
- Two backends do not make it worse. The comparison is between two runs, and pointer order
  already differs between runs today.

It remains worth doing — it is insurance against the first order-dependent pass — but that pass
arrives in Stage 8 at the earliest, and 7.5, 7.6 and 7.7 add none. See §10 for the one place
where transparency genuinely does introduce comparison noise, which is a different mechanism.

Already done, and listed so nobody re-does them: **D10** gives clip-space handedness one site
behind `DeviceCaps::bFlipClipSpaceY`; **D11**'s curated `Rhi::Format` with `default:`-free
switches already fails the build on an unmapped format; **D12**'s Slang shaders are portable as
written; and the platform seam needs nothing, though not for the reason this paragraph first
gave. `DeviceRequirements::NativeWindowHandle` is an opaque `void*`, but what it carries is the
platform's SDL window, which the Vulkan backend casts back to create its surface — not "a native
window pointer versus an HWND", as its comment claimed. Stage 7.7 keeps it that way: the D3D12
backend asks SDL for the HWND, and the comment is corrected (§5.4).

---

## 7. Out of scope

Everything that fails the inclusion test in §1, and specifically:

- **The D3D12 backend itself.** This stage makes it possible; Stage 7.7 starts it.
- **The frame graph (step 56)** and `BarrierBatcher` (55). A second backend needs a neutral
  command list, not a graph. Building the graph against one backend bakes in that backend's
  assumptions; building it after means writing it with two in front of you.
- **Deferred destruction.** D20 explains why, and what triggers building it.
- **A fourth texture map.** D14's correction: nothing is being held back, so raising
  `TextureBinding::COUNT` is a feature. Backlog.
- **A `ShaderLibrary`.** D24. Backlog.
- **All of Stage 9 including step 58.** Arena, `FrameSnapshot`, radix sort, dirty flags, frustum
  culling, ECS, scene serialization — none of it touches the RHI seam, and ECS is on record as
  the largest-blast-radius change in the roadmap.
- **All of Stage 10.** Bindless is deferred by D14; the rest is independent. Note that steps 72
  (mipmaps) and 74 (reverse-Z) deliberately *change* the screenshot and need re-baselining, so
  running them while cross-backend pixel comparison is the primary evidence would be actively
  confusing.
- **Async compute, aliasing and multi-queue in the neutral API.** §20's row 4 constrains the
  frame graph this way already; the same constraint applies to the seam. Add a queue concept
  beyond D6's `QueueType` when a pass needs it.

---

## 8. Definition of done

`cmake/RhiBoundaryCheck.cmake` is the measure, because it is already enforced in CI and already
names the work that removes each entry. Today it holds **7 transitional headers used from 19
sites** — 17 in the first draft, 18 once Stage 7 gave the UI backend an entry of its own, and 19
once step 1 added the validation-coverage test, which needs a queue until step 2 hands one out.

The steps account for sixteen of those sites: step 2 two, step 3 one, step 5 six, step 6 one,
step 7 one, step 10 one, step 11 two, step 12 the two remaining `DebugNames.h` entries. That leaves **2
headers used from 3 sites** — or so this predicted. It ended at **4**; the fourth is recorded
below the table:

| Header | Site | Why it stays |
|---|---|---|
| `VulkanNative.h` | `engine/editor/src/VulkanUiBackend.cpp` | ImGui's Vulkan backend takes raw handles. D9 is permanent by design: a D3D12 build gets a sibling file, not an edit |
| `VulkanNative.h` | `tests/gpu/rhi/DeviceTests.cpp` | The escape hatch is what those cases assert on |
| `SwapchainUtil.h` | `tests/unit/rhi/SwapchainUtilTests.cpp` | Deliberate, and argued in the check itself: the functions are pure and device-free so they can be unit tested, and `src/vulkan/` is on a PRIVATE include path a test cannot reach |


**A fourth site outlived the stage by a few hours and is now gone.** `Engine.cpp` reached for
`GetPhysicalDevice` to query `VkFormatProperties` for a usable depth format, because the neutral
API could not be asked whether a format was usable for a purpose. It can now:
`IDevice::IsFormatSupported(Format, TextureUsage)` answers it on both backends —
`optimalTilingFeatures` on Vulkan, `D3D12_FEATURE_DATA_FORMAT_SUPPORT` on D3D12 — and needed no
new vocabulary, since `TextureUsage` already says what a texture is for. **The count is the 3
this section predicted**, and the engine names no Vulkan at all.

Note that `GetNative(ICommandList&)` survives step 12 along with the rest of the ImGui-shaped
hole — ImGui's backend takes a `VkCommandBuffer` by value, and there is no neutral shape for
that.

The second measure is `rhi_extraction_plan.md` §8's checklist: **no row still reads *Partial* or
*Deferred***. Six do today — command recording, command pool, CPU/GPU sync, descriptors,
per-draw constants and pipelines — and they are, near enough, this document's step list.

---

## 9. Open investigations

**1. ~~What does the dropped-semaphore experiment actually do today?~~ Settled 6 September
2026, and the row it came from was wrong twice over.**

`backlog.md` claimed the gpu suite asserted a synchronization dependency it could not detect,
because sync validation was off. Sync validation is not off — `validate_sync` is hardcoded
`VK_TRUE`. The obvious replacement claim, that syncval cannot see cross-submit hazards here, is
also wrong: a read-after-write on a *buffer* split across two submissions to one queue, with no
barrier, semaphore or fence between them, is reported as `vkQueueSubmit(): READ_AFTER_WRITE
hazard detected`, naming both command buffers and both submits.

**So the offscreen read has no hazard to find.** Dropping its wait semaphore leaves the suite
green because a barrier and submission order were already supplying the dependency the
semaphore was being credited with. Buffers were the discriminating case: an image carries layout
transitions, which are themselves ordered against other transitions on the same queue, so an
image test can pass for a reason unrelated to hazard detection. That is why the earlier attempt
— weakening the readback barrier's source scope — proved nothing either way.

`tests/gpu/rhi/ValidationCoverageTests.cpp` is what came out of it: the hazard above, committed
deliberately, asserting that validation *reported* it and then clearing the counters. It was
checked against its own failure — remove the hazard and the case fails — so it is a positive
control rather than another assertion that cannot fail. Every "zero validation errors" claim in
the gpu suite now rests on something.

**2. ~~What does Slang's DXIL path require?~~ Measured on 11 September 2026**, in a scratch tree
pinned to the repository's own vcpkg baseline — shader-slang 2026.7.1, directx-dxc 2026-05-27,
which is dxc 1.9.0.5191. Both ports install prebuilt binaries, so the whole probe took minutes.

- **Slang finds vcpkg's DXC with no path option**, loading `libdxcompiler.so` and `libdxil.so`
  from `tools/shader-slang/../../lib`. That relative layout only holds while `directx-dxc` is a
  host dependency, as `shader-slang` is. **Windows, where the DLLs land in `bin/`, is verified
  too**, as this entry predicted it would be — by CI, since `CompileShadersTarget` is in `ALL`: the
  merge run of #60 (run 34715242114) compiled all eight `.dxil` blobs on `ninja-debug-windows`,
  each passing `CheckDxilSignature`, on 13 September 2026.
- **`pbr.slangh` survives.** All eight stage entry points compile at `sm_6_0` under
  `-warnings-as-errors all`.
- **DXC validates and signs every compile.** Its release notes for 1.8.2505: "The compiler will now
  always use the internal validator instead of searching for an external DXIL.dll." Every blob came
  out signed, and `-Xdxc -Vd` produced an all-zero container hash — the control that makes a
  signature mean something. `dxv` is not installed by the port, and run from the release archive it
  reported "Validation succeeded" on that unsigned blob, so it cannot be the gate. §4.2 is what
  replaces `spirv-val` here.
- **No register renumbering is needed.** Every set's bindings are already unique across resource
  classes — Vulkan requires that of a set layout — so D29's respelling is one-to-one.
- **Shader model 6.9 is a full release** in the pinned DXC, not the preview its 1.8.2505 notes
  describe, so the choice was wider than §4.1 assumed. §4.2 takes `sm_6_0` regardless.
- **The per-stage split leaves the baseline pixel-identical.** Measured at step 8 on 12 September
  2026, not asserted: `baseline_test.sh` exits 0 after it, and `spirv-dis` confirms every blob
  carries exactly one `OpEntryPoint` named `"main"` once `-fvk-use-entrypoint-name` is dropped —
  the SSA id keeps the source name, which is not what Vulkan matches `pName` against.
- **D29's respelling leaves the baseline pixel-identical too**, measured at step 9 on the same
  day. Every emitted `DescriptorSet`/`Binding` pair was compared against what the attributes
  produced and not one moved, so the respelling is the one-to-one spelling change this entry
  predicted. Nothing in §9 is open any more.

---

## 10. Risks

- **Steps 5 and 8–11 touch every draw and every material.** This is the R9/R10 hazard again,
  and the RHI plan's advice applies unchanged: do not merge them, and run a baseline comparison
  between them. The twelve-step split is that advice taken further than the first draft took
  it.
- **The seam is being designed against one backend.** Some of it will be wrong in ways only
  writing the D3D12 backend reveals. Mitigate by checking each neutral description against the
  D3D12 documentation as it is written rather than inferring from the Vulkan side. Budget for
  revision rather than assuming the first shape survives. The grill found one of these before a
  line was written — D22, combined image samplers — which is evidence both that the risk is real
  and that reading the other API's documentation early is what catches it.
- **Synchronization mistakes in steps 2 and 8–11 will not fail locally.** Plausible-sounding
  synchronization compiles, renders correctly on one driver and fails intermittently on
  another. Synchronization validation is on; a clean run is necessary rather than sufficient.
- **Transparency is the noisiest place in the comparison.** Weighted-blended OIT accumulates
  additively, float addition is not associative, and `Drawable::operator<` orders by pointer, so
  three or more stacked transparent layers can differ in the low bits between runs. Known cause,
  not a regression. It is also the strongest argument for D26's tolerance applying *within* a
  backend eventually, not only across two — the current exact check holds because the test scene
  does not stack that deep, which is a property of the content rather than of the renderer.
- **Scope creep through adjacency.** Several steps open files that Stage 8 and Stage 9 also
  want to change. The inclusion test in §1 is the defence, and it only works if it is actually
  applied when the temptation arrives.
- **Twelve steps is more ceremony than six.** Twelve baseline runs, twelve review cycles. The
  trade was made deliberately: no step touches more than one idea, so when the baseline moves,
  which idea moved it is not a question.

---

## 11. Retention

**This document is kept after the stage ends.** Stage 7's plan was deleted at its stage's close
because it records how to build things that will by then be built. `rhi_extraction_plan.md` was
kept past Stage 5 because its decisions still govern a seam that outlived it. This one is the
second kind: D14–D46 say what the RHI's public API is allowed to express about recording,
binding, pipelines, submission, presentation and validation, and the D3D12 backend — and everything
written against the seam afterwards — has to respect them.

What that means in practice:

- The step sequence in §3 becomes history once the stage completes, exactly as R1–R17 did.
  Leave it; it is short, and it explains why the seam has the shape it has. §4.3's and §5.5's step
  lists follow it into history when Stages 7.6 and 7.7 complete — 7.6's already has.
- §2's decisions stay live and are the reason to open this file.
- §8's definition of done becomes the standing description of what the transitional area is
  *for*, and `cmake/RhiBoundaryCheck.cmake` stays its enforcement.
- D26 is the exception that should **not** live here permanently. It is test strategy that
  applies to any two renderers, including two D3D12 driver versions, so it also lands in the
  architecture plan's Part III and outlives this document.
- **Both plans retire together, into one permanent `docs/rhi.md`.** Decided at step 12, along
  with the decision not to do it yet. This document is not the host: it is a stage plan that
  happens to carry decisions, and so is `rhi_extraction_plan.md`. The whole D-series — D0–D46,
  less the two superseded, with their amendments — belongs in a file kept for the lifetime of the
  project, with the step lists dropped and `rhi_extraction_plan.md` §10's promotion list as the
  outline.
  The numbering continuity §2 was careful about is what makes that a merge rather than a
  rewrite.
