# Between Stage 6 and Stage 7 — Cleanup: Implementation Plan

> **Temporary document.** It carries the cleanup work that sits between Stage 6 and Stage 7
> of the architecture plan, and is scheduled for deletion when Stage 7 begins at step 40b.
> See [§8 Retirement](#8-retirement) for what to keep and what to throw away.

**Created:** 30 August 2026 · **Expands:** `architecture_plan.md`, *Between Stage 6 and
Stage 7 — a cleanup PR* · **Status:** in progress — see the [progress table](#progress)

---

## Table of contents

1. [Purpose and authority](#1-purpose-and-authority)
2. [Scope](#2-scope)
3. [The eight PRs](#3-the-eight-prs) — [progress table](#progress)
4. [Decisions that span PRs](#4-decisions-that-span-prs)
5. [The run report's new shape](#5-the-run-reports-new-shape)
6. [Backlog changes](#6-backlog-changes)
7. [Deferred work, and what each deferral blocks](#7-deferred-work-and-what-each-deferral-blocks)
8. [Retirement](#8-retirement)

---

## 1. Purpose and authority

The architecture plan says one small PR sits between Stage 6 and Stage 7. That turned out to
be eight, because the items differ enough in kind that one diff would be unreviewable, and
because two of them — the namespace sweep and the CI restructure — are better done *before*
the rest rather than alongside it.

This document is the decision record for that work. Where it disagrees with the architecture
plan's cleanup section, this document wins; it was written later and with the code open.
Everything it decides about the RHI's public seam still defers to `rhi_extraction_plan.md`'s
D0–D12.

Nothing here blocks Stage 7 *starting* except `--no-ui`, which step 46's comparison is
defined on rather than merely helped by.

## 2. Scope

Ten items, taken from the architecture plan's cleanup section and the independent-work table,
plus two structural changes that surfaced while planning them.

**In:** `--no-ui` and the re-baseline · frame-time counters recording the timestep ·
`Extent2D`/`Extent3D` into `Engine::Core` · `ChooseSwapchainFormat`'s fallback ·
`rhi_boundary_check` in CI · `App::m_Surface` · `OffscreenTarget::Readback` ·
`SdlPlatform`'s redundant Vulkan loader pair · `SIGTERM` · the Ctrl-C capture race ·
the `Hikari::` namespace sweep · the CI job restructure.

**Out, deliberately:** `--present-mode` — it is the only item that adds a user-facing flag
with its own policy surface, and it has no deadline. The open P0/P1 correctness fixes from
`suggested_work.md` (§1.6, §3.1) — a fix that changes rendering does not belong in a series
whose job is to re-establish what the baseline means. Both keep their backlog rows.

## 3. The eight PRs

They land in this order. Each is branch-from-`main`, squash-merge, matching every PR in the
repository's history. Authored sequentially, none needs a rebase; the pairs most likely to
collide if worked in parallel are `test/baseline` ↔ `fix/signals` (shared report-writing call
sites) and `engine/rhi` ↔ `engine/core` (`RhiTypes.h`).

### Progress

| # | Branch | Title | Status |
|---|---|---|---|
| 1 | `docs/backlog` | Moved independent work into a backlog document | done |
| 2 | `engine/namespace` | Moved the engine into the Hikari namespace | not started |
| 3 | `test/ci` | Split platform-independent checks into their own CI job | not started |
| 4 | `test/baseline` | Baseline captures without UI and reports measured frame times | not started |
| 5 | `fix/signals` | Added SIGTERM handling and screenshot capture on exit | not started |
| 6 | `engine/rhi` | Curated the swapchain format fallback and removed unused RHI code | not started |
| 7 | `engine/core` | Moved Extent2D and Extent3D into Engine/Core | not started |
| 8 | `platform/sdl` | Removed the redundant SDL Vulkan loader calls | not started |

---

### 1 · `docs/backlog` — Moved independent work into a backlog document

Pure prose, no code, so it cannot break a build and it gives every later PR exactly one
documentation obligation: delete the backlog row it just completed.

- Create `docs/backlog.md` from the architecture plan's §37, and delete §37. It is the last
  numbered section before the appendices, so nothing renumbers; the table of contents loses
  entry 37.
- Add a `Priority` column (P1 most important, P2, P3) and a `Blocked by` column. A blank
  blocker means "pick up any time", which is what the old section promised for every row.
- Delete the `[DONE]` rows. Git history is the record. The expanded paragraph on the
  screenshot swizzle goes with its row: `WriteScreenshot` already carries that reasoning as a
  comment, which is where the conventions say it belongs.
- Repoint the cross-references: `architecture_plan.md` ×6, `CLAUDE.md` ×2,
  `suggested_work.md` ×1.
- Update `CLAUDE.md`'s "three reference documents" to four, describing what the backlog is for.

**The priority scales are deliberately not unified.** `suggested_work.md`'s P0–P2 is
*severity*, assigned once by a review that happened, and its letters are baked into section
anchors that other documents link to. The backlog's P1–P3 is *scheduling priority*, and it
changes as priorities change. The column headers name the axis — `Severity` there, `Priority`
here — and no cell in the backlog carries the review's P-numbers in its prose: the row for the
open correctness fixes reads "correctness fixes from `suggested_work.md` §1.6, §3.1".

Not in this PR: the namespace convention text, which belongs with the sweep that makes it true.

### 2 · `engine/namespace` — Moved the engine into the Hikari namespace

Everything under `engine/` gets uniform per-module nesting: `Hikari::Core`, `Hikari::Platform`,
`Hikari::Rhi` (with `Hikari::Rhi::Vulkan` beneath it). `src/` is **not** swept.

The reasoning, since it is the largest structural decision here:

- `rhi_extraction_plan.md` §D already made this decision for one module — PascalCase to match
  `Log` and `JobSystemDetail`, and "mandatory rather than optional" because `Rhi::Texture`,
  `Rhi::Format` and `Rhi::Buffer` are collision-prone. `Handle`, `Timer`, `Extent2D` and every
  `ThingSystem`/`ThingPass` the plan's §19 naming scheme will generate are the same kind of
  name. This is that decision in its general form.
- **Now, not later.** Three modules today; eight once Stages 8–10 add Assets, Render, Scene,
  Engine and Editor. Stage 7 is specifically the stage that moves `main.cpp`'s contents *into*
  engine modules, and code arriving in a namespaced module has to be namespaced on arrival —
  so sweeping first turns fifteen-odd namespace decisions into fifteen plain moves.
- **First, not last.** None of the other PRs is written yet, so sweeping first means they are
  authored in the final world and `Extent2D` lands in its permanent home once.
- **Uniform nesting rather than promoting Core to `Hikari::` directly.** The tie-breaker is
  enforceability: `directory == target == namespace` is a rule a script can check, whereas
  "Core's vocabulary sits at the top level" can only be enforced by discipline, and this
  codebase's philosophy is that the build system enforces layering rather than discipline.
  Inside `namespace Hikari::Rhi`, enclosing-namespace lookup means a header still writes
  `Core::Extent3D`, so the verbosity lands only in app- and consumer-side headers.
- **Engine only.** The consumer-facing argument is about what a library exposes, and `src/`
  exposes nothing — it is an executable being deleted a piece at a time, whose types are
  scheduled to be renamed, split or dropped (`ModelManager` and `Drawable` become
  `RenderScene`/`DrawListBuilder`/`FrameSnapshot`; `Node` disappears into the importer).
  `src/*.cpp` take one `using namespace Hikari;` each; `src/*.h` qualify explicitly. The mixed
  state has a natural end date: `src/main.cpp` ceases to exist at step 46.

**The rule:** `using namespace` is allowed in `.cpp` files inside the engine and the app, and
**never** in a header. Document it in `CLAUDE.md`'s conventions.

**Ship the check with the sweep.** `cmake/NamespaceCheck.cmake`, in the same shape as
`RhiBoundaryCheck.cmake` — one CMake script shared by a `.sh` and a `.bat`, source-only, no
build required. Two line-level assertions, both with essentially no false-positive surface:

1. Every public header under `engine/<mod>/include/<mod>/` opens `namespace Hikari::<Mod>`.
2. No header anywhere contains `using namespace`. This is the half that earns the script: a
   using-directive in a header leaks into every translation unit that includes it, and nothing
   else in the tree catches it.

The script and its `precommit.sh` entry land here; the **CI wiring lands in `test/ci`**, so
that the step is added once in its final home rather than added to nine matrix jobs and
immediately deleted. Between the two PRs the rule is enforced by precommit only.

### 3 · `test/ci` — Split platform-independent checks into their own CI job

CI runs a nine-config matrix in which every job repeats every check. The checks are not
equally portable, and the honest tiering is three tiers:

| Tier | Checks | Why |
|---|---|---|
| Once | `rhi_boundary_check`, `NamespaceCheck`, format check | Source-level. clang-format is version-pinned precisely so all nine agree |
| Once per OS | `HeaderSelfContainment` | Compiles headers with the platform's compiler and standard library — that difference *is* the check. Config-independent, so the debug job per OS suffices |
| Once per config | Build, unit tests | Debug, release and asan genuinely differ |

- A `static-checks` job on a bare `ubuntu-latest`: checkout, then the source-level checks. It
  reports in about a minute instead of in the eleventh minute of nine parallel builds, and it
  still reports when the build is broken.
- `format-check` needs a non-CMake entry point first, since it is currently a target inside the
  configured tree and a configure needs vcpkg and the Vulkan SDK. Move the implementation into
  `cmake/FormatCheck.cmake` and have both the `format-check` target and the scripts invoke it,
  keeping one implementation — the pattern `RhiBoundaryCheck.cmake` already establishes.
- Drop `HeaderSelfContainment` from six of the nine jobs, keeping the debug job per OS.
  **Do not** reduce it to one job: `CLAUDE.md` explains why — libstdc++ supplies `<cstdint>`
  and `<string>` transitively, so a header missing them passes on Linux and fails on MSVC.
- Keep the existing `continue-on-error` + aggregate-failure-step pattern so one failing check
  does not mask another.

`CLAUDE.md` currently states that CI's step order mirrors `precommit.sh`'s. That stops being
true for the extracted checks; say so rather than leaving the claim standing.

### 4 · `test/baseline` — Baseline captures without UI, and reports measured frame times

Two items, joined because they are the only two that move `tests/baseline/`, and because they
share one thesis: the baseline currently measures something other than what it claims to.

**`--no-ui` suppresses the panel, not the pass.** A new option gating the two sites
`m_bCursorVisible` already gates — the `DrawImGuiFrame()` call (`main.cpp:564`) and
`ImGui_ImplVulkan_RenderDrawData` (`main.cpp:1881`) — independent of cursor state. ImGui stays
initialised, `RecordImGui` still records its `PreserveRenderTarget` barrier and its
`beginRendering`/`endRendering`, and the seventh command buffer is still submitted. The cost is
that a `--no-ui` run still pays for an empty editor pass, which Stage 7 deletes for free when
`RecordImGui` becomes `render/passes/ImGuiPass.cpp`; buying that now would mean structural
surgery on the submit path plus a second reason for the promoted report to move.

Do **not** overload `m_bCursorVisible`: it also drives relative-mouse capture and is read at
`main.cpp:986` and `:1035` alongside `CameraPreset` and headless.

The flag is spelled `--no-ui`, and `--headless` does **not** imply it. Step 46 needs both
captures taken with the flag explicitly, and 40a deliberately keeps ImGui rendering headless so
step 47 still exercises it.

**Frame times become wall clock.** Use `Timer`/`steady_clock`. `App::Run` currently times with
`std::chrono::high_resolution_clock` (`main.cpp:497`, `m_StartTime` at `:616`), which
`core/Timer.h:26` documents as an alias for the non-monotonic `system_clock` on libstdc++.

The fix is not only "use wall clock". Today a sample is `now - m_LastTime` computed at the *top*
of the iteration, so the value recorded on iteration *N* is the duration of iteration *N-1*, and
on iteration 0 it is approximately init time wearing a frame's name. Each interval must be
attributed to the frame that spent it, or `startupMs` and `firstFrameMs` double-count.

**One warm-up frame is excluded**, and reported separately rather than dropped, so nothing is
hidden and the exclusion explains itself. `startupMs` covers `main()` entry to the first loop
iteration — from `main()` rather than from `App::Init` because that is the only version that
captures `SdlPlatform`'s construction, which is exactly where the windowed and headless paths
differ. `App` is told when the process started; one constructor parameter, which Stage 7 turns
into part of `RunSpec`.

Schema in [§5](#5-the-run-reports-new-shape).

**`baseline_test.sh` always passes `--no-ui`, and the baseline holds one scene-only pair.**
Committing a second, UI-bearing capture was rejected: nothing warps the cursor at startup, so
it carries a hover highlight on whichever widget the mouse was last over and would produce
false diffs. A check people learn to ignore is already gone. The gap is real and goes on the
backlog at P3, blocked on Stage 7's `EditorLayer`, which can be driven deterministically
without a mouse.

**Promotion.** The screenshot cannot be verified the usual way — it differs from the old
baseline by construction. The check is that the bounding box of differing pixels between a
`--no-ui` run and a UI run at the same frame count and camera preset lies entirely inside the
panel's rectangle; anything outside it means the flag changed the scene. Armand verifies this
by hand, and `tests/baseline/` is promoted only after that. What must **not** move:
`validationErrors` 0, `drawCalls` 22, `batches` 22, `instances` 23, `barriers` 14,
`barrierCalls` 9 — none of them touch ImGui.

### 5 · `fix/signals` — Added SIGTERM handling and screenshot capture on exit

- Register `SIGTERM` with the same handler as `SIGINT`. A CI timeout kills even a bounded run
  before it writes its artefacts.
- Replace the handler's `std::cout << "\n"`, which is not async-signal-safe, with a `write` /
  `_write`. The flag itself is fine: `g_bShouldClose` is `std::atomic<bool>`, lock-free on
  every target, and a handler may touch a lock-free atomic.
- **Capture on the way out.** The capture decision is taken inside the frame at
  `main.cpp:569`, before `DrawFrame`, so a signal arriving after that line leaves the loop with
  nothing staged and `WriteScreenshot` reports "called without a captured frame". After the
  loop, if a screenshot was requested and `m_bScreenshotBufferReady` is false, render one more
  frame with capture enabled. It only ever happens on the interrupted path — a `--frames N` run
  still captures on frame *N-1*.

  Rejected: staging a copy every frame while `--screenshot` is set. It would move `barriers`
  and `barrierCalls` for every capture run and add a per-frame copy to exactly the mode used
  for measurement, contaminating the frame times PR 4 exists to make honest.
- The capture frame counts as an ordinary frame: included in `frames`, contributing a timing
  sample. The interrupted path is not a measured run, and a special case buys nothing.
- A second signal does nothing new. If shutdown wedges, `SIGKILL` is the answer; escalating to
  `std::_Exit` would lose the artefacts for a merely impatient user.

### 6 · `engine/rhi` — Curated the swapchain format fallback, and removed unused RHI code

**`ChooseSwapchainFormat` gets a curated preference order:** `BGRA8Unorm`, then `RGBA8Unorm`,
both with `SRGB_NONLINEAR`; anything else throws, naming what was asked for and listing what
the surface offered.

The plan's write-up frames this as a naming hazard — `formats[0]` handed to `FromNativeFormat`,
which throws on anything the curated `Rhi::Format` list cannot name, and the list has
`BGRA8Unorm` but no `BGRA8Srgb`. It is also a *rendering* hazard, which is what rules out the
permissive fix: falling back from a UNORM format to an SRGB one changes what the hardware does
on write, so the same shader output lands as different pixels and a baseline comparison fails
for a reason unrelated to the change under test. Every format in the curated order is nameable
*and* colour-equivalent, so "we fell back" and "the picture is unchanged" are both guaranteed.
`WriteScreenshot` already handles the channel-order difference, since step 39 drove it from
`GetFormat()`.

**Delete `App::m_Surface`**, bound and never read, together with `Rhi::Vulkan::GetSurface` —
its only caller — and that allowlist entry in `cmake/RhiBoundaryCheck.cmake`. The ratchet
working as designed.

**Delete `OffscreenTarget::Readback`.** Its only callers are five sites in
`tests/gpu/rhi/PresentTargetTests.cpp`, which reaches it through the RHI's private
`src/vulkan/OffscreenTarget.h`; the app does not use it, having its own screenshot staging
buffer at `main.cpp:1105`. The cases move onto `tests/support/GpuReadback.h`.

**The helper must take the wait semaphore explicitly.** `Readback` is not a byte copy: it waits
on the target's render-complete semaphore for that image index, and `PresentTargetTests.cpp:318`
says why that matters — "A stray `WaitIdle` here would hide a `Readback` that established no
dependency at all". A helper that reaches for `WaitIdle` turns that test into one that passes
vacuously.

### 7 · `engine/core` — Moved Extent2D and Extent3D into Engine/Core

One type instead of `::Extent2D` (`platform/Extent2D.h`, no comparison operator) and
`Rhi::Extent2D`/`Extent3D` (`RhiTypes.h:301`, with a defaulted `operator==`).

- Lands as `core/Extent2D.h` and `core/Extent3D.h`, one type per header, matching Core's
  existing layout (`Handle.h`, `HandlePool.h`), in `namespace Hikari::Core` after PR 2.
- Keeps `operator==`.
- **Delete the `Rhi::` spellings rather than aliasing them.** One type reachable under two
  names is what makes a reader stop and check whether they differ.
- Roughly five call sites, plus the RHI headers that name the type.

### 8 · `platform/sdl` — Removed the redundant SDL Vulkan loader calls

`SdlPlatform`'s constructor calls `SDL_Vulkan_LoadLibrary(nullptr)` and its destructor
`SDL_Vulkan_UnloadLibrary()`; neither is needed. SDL 3.4's `SDL_CreateWindow` documents that a
window created with `SDL_WINDOW_VULKAN` loads the library itself and `SDL_DestroyWindow`
unloads it, and `SdlPlatform` always passes that flag. The full argument, including why the
app's own `vk::detail::DynamicLoader` means nothing else depends on SDL's loader, is in the
architecture plan's expanded note — carry it into the backlog row, then delete it with the row.

Two things not to lose:

- The explicit load is what produces *"Failed to load Vulkan library!"* on a machine with no
  driver, where `SDL_CreateWindow` would fail with *"Failed to create window!"*. `SDL_GetError()`
  still names the real cause, so the surviving message must say so rather than blaming the
  window.
- `SdlPlatform.h`'s class comment needs rewriting, not deleting. The ordering constraint is
  real, but its cause is `SDL_DestroyWindow` invalidating the surface, not the unload.

Removing the pair also removes an asymmetry: a throw from `SDL_CreateWindow` skips the
destructor, so today's explicit load goes unpaired until `SDL_Quit`.

## 4. Decisions that span PRs

- **Every PR deletes the backlog row it completes.** `[DONE]` rows are not kept.
- **Verification** is `scripts/precommit.sh` before any PR is reported done, plus a baseline
  comparison for anything that could alter rendering. PRs 4, 6 and 7 are in that category.
- **Branch naming** stays `<category>/<topic>`, lowercase, one-word topic, as every merged PR
  in the repository uses. Titles stay past-tense sentence case.

## 5. The run report's new shape

Nothing machine-reads the report today — `src/main.cpp` writes it and no script, test or CI job
parses it — so this is the cheapest moment to change its shape. Step 47 is the first consumer
that would have to care.

```json
{
  "frames": 1000,
  "counters": { "validationErrors": 0, "validationWarnings": 0, "drawCalls": 22,
                "batches": 22, "instances": 23, "barriers": 14, "barrierCalls": 9 },
  "timings": {
    "startupMs": 812.4,
    "firstFrame": { "frameMs": 41.2, "cpuMs": 38.9 },
    "frameMs":    { "mean": 16.7, "p99": 18.1, "min": 15.9, "max": 22.4 },
    "cpuMs":      { "mean": 3.2,  "p99": 4.8,  "min": 2.9,  "max": 9.1 }
  },
  "run": { "fixedDt": true, "headless": false, "noUi": true, "width": 1920, "height": 1080,
           "jobCount": 8, "presentMode": "mailbox", "buildConfig": "debug" }
}
```

**Why `counters` and `timings` are separate objects.** Everything under `counters` is an
expectation that must match exactly; everything under `timings` is a measurement that varies
with the machine. In one flat object a reader cannot tell `validationErrors: 0` from a number
that is merely informative, which is the same failure the plan objects to elsewhere — an
artefact that produces a value looking valid and is not.

**Why both `frameMs` and `cpuMs`.** End-to-end pace is bounded below by the display refresh
whenever the present path throttles the CPU — on a FIFO surface a hard floor at 16.67ms — so a
renderer regression that stays under budget would be invisible and the counters would still be
constants, just for a different reason than today. CPU-side cost alone has the opposite blind
spot: a GPU-bound run reads fast and smooth. The pair distinguishes "we got slower" from "we
waited longer", which is the first question anyone asks of a regression. Both carry the same
four statistics; trimming the series that is *not* refresh-bounded would give back the exact
blindness this fix exists to remove.

**No `samples` field.** With exactly one frame excluded it is always `frames - 1`, and a
derivable field is one that can go stale.

**`run` exists because two reports are only comparable under the same conditions.**
`baseline_test.sh` takes the preset as `$1` and defaults to `ninja-debug-linux`, so a report
produced by `baseline_test.sh ninja-release-linux` is identically shaped and off by an order of
magnitude with nothing in the file saying so. `presentMode` needs a small accessor on the
present target — the reporting half of the deferred `--present-mode` item, and worth having
anyway because a mailbox→FIFO fallback is otherwise invisible. `buildConfig` is derived from
`$<CONFIG>` plus a suffix when `ENABLE_SANITIZERS` is on, giving `debug`, `release`,
`debug+asan`: correct under the MSVC multi-config generator, and correct for a tree configured
without a preset, neither of which a preset name would be.

GPU name, driver version, OS and architecture are deliberately *not* here — see §7.

## 6. Backlog changes

PR 1 creates the file. The priority assignment, agreed row by row:

| Priority | Item |
|---|---|
| P1 | `suggested_work.md` correctness fixes §1.6, §3.1 |
| P1 | The nine items carried by PRs 4–8 (each deleted as its PR lands) |
| P2 | `--present-mode` |
| P2 | Document the matrix convention |
| P2 | `.map` format `version` attribute |
| P2 | Device, driver, OS and architecture in the run report — *blocked by: a neutral device-info accessor on `IDevice`* |
| P2 | A baseline comparison script — *blocked by: nothing; step 47 is where CI needs one* |
| P2 | Namespace `src/`'s remaining types — *blocked by: Stages 7–9, which move them into engine modules* |
| P3 | ImGui panel has no regression coverage — *blocked by: Stage 7's `EditorLayer`* |
| P3 | Expose cloud push-constants in ImGui |
| P3 | `surface.slangh` de-duplication |
| P3 | Split `pbr.slangh` |
| P3 | `CubemapCreateInfo` → `std::array<std::string,6>` |
| P3 | Finish the skybox + IBL |

The non-obvious ones: §1.6 is P1 despite being kept out of this series, because priority and
scheduling are different axes — which is the whole reason for the column. Everything touching
the ImGui panel is P3 because Stage 7 extracts it into `EditorLayer`, so work on it now is work
done twice; the same argument puts `CubemapCreateInfo` at P3, since Appendix A moves
`CubemapLoader` into `engine/assets/src/importers/`.

The backlog's admission rule is **broad**: anything off the critical path, blocked or not. The
alternative — a strictly prerequisite-free list, with blocked items written into the stage text
that unblocks them — has already failed once here: the frame-time defect sat in §14's prose
through five stages before reaching the table. Revisit if the file gets out of hand.

## 7. Deferred work, and what each deferral blocks

| Deferred | Blocks | Note |
|---|---|---|
| `--present-mode` | Nothing | The report carries the chosen mode regardless |
| `suggested_work.md` §1.6, §3.1 | Nothing here | §1.6 is a live crash path; nothing in this series touches it |
| Baseline comparison script | Nothing now | Step 47 is where CI needs one; designing it now means designing it without its consumer |
| Device/driver/OS in the report | Cross-machine report comparison | Needs a neutral device-info accessor on `IDevice` — a seam decision, and seam decisions belong in a PR about the seam |
| ImGui panel regression coverage | Nothing | Blocked on `EditorLayer`; the gap is on the backlog rather than papered over with an artefact that diffs on mouse position |
| Namespacing `src/` | Nothing | Happens type by type as Stages 7–9 move code into engine modules |

## 8. Retirement

Delete this document when Stage 7 begins at step 40b. Before deleting, check that these have
landed somewhere durable:

- **The namespace convention** (`Hikari::<Module>`, uniform nesting, `using namespace` in
  `.cpp` only) → `CLAUDE.md`'s conventions, plus `cmake/NamespaceCheck.cmake` enforcing it.
- **The report schema and why it is shaped that way** → a comment on `WriteReport`, and
  `CLAUDE.md`'s regression-checking section, which currently tells the reader to ignore
  `meanFrameTimeMs`/`p99FrameTimeMs` and will not need to.
- **The CI tiering rationale** → a comment in `.github/workflows/ci.yml` next to the
  `static-checks` job, and the correction to `CLAUDE.md`'s claim that CI mirrors precommit's
  order.
- **The baseline's UI-coverage gap** → the backlog row, which outlives this document.
- **Anything still not started** → back to `docs/backlog.md` with a priority, rather than
  disappearing with the file.
