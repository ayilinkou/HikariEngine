# CLAUDE.md

HikariEngine — a cross-platform game engine (Windows / Linux) built on Vulkan,
with a D3D12 backend planned later. C++20, CMake + vcpkg, Slang shaders.

The engine is mid-refactor from a single-executable prototype into a layered library set.
Five reference documents drive that work — read the relevant section before proposing
architecture, and prefer them over inventing a design:

- `docs/architecture_plan.md` — target architecture (Part II), test strategy
  (Part III), and the **76-step incremental work order (Part IV)** that the project follows.
- `docs/backlog.md` — everything off the critical path, prioritised P1–P3 (scheduling
  priority, not severity) with what blocks each. New work that is not part of a stage goes
  here rather than into the plan, and a row is deleted when its work lands.
- `docs/suggested_work.md` — the code review that motivated the plan; open it for
  the *why* behind a known defect. Its P0–P3 scale is *severity*, a different axis from the
  backlog's priorities.
- `docs/rhi_extraction_plan.md` — **retained past Stage 5, which it drove.** Replaced Part IV
  steps 24–34 with a 17-step sequence (R1–R17) that made the RHI's public API backend-neutral
  so a D3D12 backend is possible later, and records the design decisions (D0–D13) behind that.
  Stage 5 is complete, so R1–R17 are history; the **decisions remain live**, because they
  govern what the RHI's public seam is allowed to say and Part IV's own §10 predates them —
  **except D7 and D8, superseded by Stage 7.5's D14 and D15.** Read it before touching
  anything under `engine/rhi/include/`. **It and the backend readiness plan retire together**,
  into one permanent `docs/rhi.md` holding the whole D-series and no step lists — decided at
  Stage 7.5's step 12, along with the decision not to do it yet. Its §10 is the outline for that
  merge.
- `docs/backend_readiness_plan.md` — **retained, like the RHI plan and for the same reason.**
  Stage 7.5: the four seams a second backend needs and Stage 5 did not build — submission and
  command-list ownership, rendering scope, bind groups, pipelines, and draw/dispatch recording
  — as twelve steps, plus the decisions (D14–D35) behind them. It **supersedes D7 and D8**,
  deferring bindless until after D3D12 and neutralising the binding model instead, and it
  reorders Part IV's Stage 8. Its D-numbers continue the RHI plan's series deliberately: both
  govern the same seam. It also defines **Stage 7.6** (what the backend needs around the seam —
  two of its six items widen `IDevice` deliberately) and the constraints on **Stage 7.7** (the
  D3D12 backend itself). Grilled on 6 September 2026; its §0 records what that changed, which was
  substantial. **Stage 7.6's own interview ran on 11 and 12 September 2026 and is finished**: §4.1–§4.4
  are its output — the comparison tool, the shader build, the twelve-step sequence and the decisions
  behind them — with nothing left open. Retires together with the RHI extraction plan into a single
  `docs/rhi.md` — see that document's §10.

---

## Working rules

**Follow Part IV strictly, one step at a time.** Each step is sized to end in a compiling,
running application. Do not start work outside the current stage, and do not combine steps,
without asking first. Stage 5 is complete, so Part IV is the work order again — but where
the **D-series** and Part IV disagree about the RHI's public seam, **the D-series wins**. It
spans two documents: `docs/rhi_extraction_plan.md` holds D0–D13 and
`docs/backend_readiness_plan.md` holds D14–D35, which supersede D7 and D8. Part IV was
written before the seam was neutralised, so its later stages still spell interfaces in raw
Vulkan; §10.2 is one such place. Re-express rather than copy, and amend Part IV as you go.

**Do not opportunistically refactor.** `engine/engine/src/Engine.cpp` is ~2,500 lines and is
scheduled for dismantling in Stage 8, which splits it into `Pass` classes once the frame graph
decides their shape. Touching it outside its scheduled step creates conflicts with the plan.
Fix what the step asks for; note anything else you spot rather than fixing it.

**Grill a stage's plan before starting it.** Every stage goes through `/grill-me` before its
first step — Stage 7 did, Stage 7.5 did, and the interview for 7.5 found that a plan **one day
old** already had four stale premises and three unnoticed design gaps, one of which (combined
image samplers, which D3D12 cannot express) would have been discovered mid-backend otherwise.

If a plan has already been grilled, the re-grill before starting is a quick one: four
mechanical checks — do its file and line references still resolve; have prerequisites it lists
as pending landed; do the counts and inventories it asserts still match the tree; has another
document taken a decision that now conflicts with one of its own. It may end in "nothing moved,
proceed" **only if all four come back clean**. If any of them moved, grill whatever they
touched properly rather than noting it in a commit message.

**Verify every change with `scripts/precommit.sh`** (configure + build + build tests +
`ctest -L unit` + `ctest -L gpu` + format-check) before reporting a change as done. It is a
superset of CI: everything CI enforces, plus the GPU tests, which CI's runners cannot run
because they have no Vulkan ICD. The GPU tests skip
rather than fail on a machine without an ICD, so a green precommit on such a machine has
proved less than it looks — check whether they ran before relying on them. Report failures
with the actual output — never claim a build passed without running it.

**For changes that could alter rendering, also compare against the baseline** (see
*Regression checking* below). "It still builds" is not evidence a refactor preserved
behaviour.

**Never change an expected test result without asking.** If a change makes an existing
expectation wrong — a unit or gpu test's assertion, a counter in a report, `tests/baseline/`'s
screenshot, a golden image — stop before touching the expectation. Say what moved, what in the
change caused it, and why the new value is the correct one, and get the go-ahead; then update
it. This is the one edit that turns a regression into the new normal without anyone noticing,
because afterwards the suite is green either way — a test edited to match new behaviour proves
only that the two agree, not that the behaviour is right. Adding tests for new behaviour is
ordinary work and needs no approval; only changing what an existing one expects does.

**Never guess at graphics API semantics — read the specification.** This applies to Vulkan,
Slang/SPIR-V, VMA, and D3D12 once that backend exists. If you are not certain about a
pipeline stage mask, an access mask, an image layout transition, a queue-family ownership
transfer, a required feature or extension, a struct's `pNext` chaining rules, alignment or
`std140`/`std430` layout, or what a validation message actually means — look it up and cite
what you found. Plausible-sounding synchronization is the most expensive kind of wrong here:
it compiles, it usually renders correctly on one driver, and it fails intermittently on
another. Say "I need to check the spec" rather than producing something that reads
authoritative and isn't.

Authoritative sources, local copies first. `<vcpkg>` is
`build/<preset>/vcpkg_installed/<triplet>/` — the tree a build populates, so these exist
on every platform whether or not an SDK is installed, and they match the version
`vcpkg.json` resolves to rather than whatever is installed system-wide:

| Source | Where | Use for |
|---|---|---|
| Vulkan headers | `<vcpkg>/include/vulkan/vulkan_core.h` | exact enum values, struct fields, function signatures |
| Vulkan registry | `<vcpkg>/share/vulkan/registry/vk.xml` | which extension/version a symbol belongs to, aliases, deprecations |
| Valid Usage database | `<vcpkg>/share/vulkan/registry/validusage.json` | look up a `VUID-...` from a validation message verbatim |
| Slang docs | <https://shader-slang.org/docs/> | shader language and `slangc` flags — vcpkg ships the compiler, but no docs to go with it |
| Vulkan spec | <https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html> | synchronization chapter, layout rules, the prose behind a VUID |
| VMA docs | <https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/> | allocation flags, mapping and usage patterns |
| Driver support | <https://vulkan.gpuinfo.org> | whether a feature/format/limit is realistically available |

The validation layers are the empirical check, not a substitute for the spec — a clean
validation run proves nothing was caught, not that the code is correct.

**Validation is chosen at run time, and the default is unchanged from what it always was.**
`--validation on|off` decides whether the layer is loaded at all; leaving it off the command line
means the build configuration decides, as it always did — on in Debug, off in Release. That is a
different question from `--validation-policy ignore|count|failfast`, which decides what a message
*means* once a layer is loaded, and the combinations that read as stricter than they are —
`--validation off` with a policy or with `--strict-validation` — are refused at parse time.

Synchronization validation is off by *Vulkan's* default and on in this project, through the
`VK_EXT_layer_settings` chain in `VulkanDevice::CreateInstance`. It is the expensive sub-mode —
measured on a release build of the test scene at 1.035 ms/frame against 0.802 with it off and 0.337
with no validation at all — so `--vk-sync-validation on|off` exists for the one case that needs it:
a release run that validates *and* whose timings still mean something. Vulkan-only, as the prefix
says; D3D12 has no synchronization validator. Best-practices validation is the one currently
switched off, for a layer crash — see `backlog.md`.
`grep`ping this repo for prior art is also not a source. Known-wrong places to copy from
today: `ModelData::Init` (`suggested_work.md` §1.6 — a live P0 that dereferences a null
material), `WriteScreenshot`'s hardcoded BGRA swizzle, `ChooseSwapchainFormat`'s fallback
(it can hand `FromNativeFormat` a format the neutral list cannot name), and
`Drawable::operator<`, which orders by pointer value and so is not reproducible across
processes.

**Never run git commands that change state.** No commits, branches, stashes, or pushes —
even when a task feels finished. Reading (`git status`, `git log`, `git diff`) is fine.

### Current position in the roadmap

| Stage | Steps | Status |
|---|---|---|
| 0 — Verification harness | 1–6 | ✅ done (`--frames`, `--screenshot`, `--report`, `--fixed-dt`, `--camera-preset`) |
| 1 — Build hygiene | 7–11 | ✅ done (clang-format, sanitizer presets, Catch2 + CTest in CI) |
| 2 — Header self-containment | 12–14 | ✅ done (`HeaderSelfContainment` target, enforced in CI) |
| 3 — Core library | 15–19 | ✅ done (`Engine::Core`, `IJobSystem` injected into `App`) |
| 4 — Platform library | 20–23 | ✅ done (`Engine::Platform`, `Paths` + `content/` root, `CommandLine`) |
| 5 — RHI extraction | R1–R17 | ✅ done (`Engine::RHI` — backend-neutral API, handle-based resources, batched uploads, growable descriptors, a pipeline cache, and GPU tests) |
| 6 — Headless capability | 35–40a | ✅ done (`HeadlessPlatform`, `--headless`, the present-layout seam) |
| Cleanup between 6 and 7 | — | ✅ done (`Hikari::` namespace + `namespace_check`, CI's `static-checks` job, the `counters`/`timings`/`run` report + `--no-ui`, `docs/backlog.md`) |
| 7 — Engine shell + DI | 40b, 41–47 | ✅ done (`engine/engine` + `engine/asset` + `engine/editor`, `HikariEditor` + `HikariHeadless`, injected subsystems, the event seam, and headless scene tests in CI) |
| 7.5 — Backend readiness | 1–12 | ✅ done (`ICommandAllocator`, submission and fences, rendering scope, bind groups, pipelines, draw and dispatch recording — the transitional area is 2 headers from 4 sites, down from 7 from 18) |
| 7.6 — Backend prerequisites | 1–12 | ✅ done (`HikariCompare` and the gating table, `--backend` and `rhi/Backend.h`, `DeviceInfo` and the report's `system` block, per-stage blobs with DXIL and its signature gate, `ShaderTypes.h` shared with the shaders and its layout pinned, `--validation` and `--vk-sync-validation`) |
| **7.7 — D3D12 backend** | — | **next** — stepped small, Vulkan stays the default, and it owns the Windows GPU CI job (D28). Not yet grilled |
| 8+ — Frame graph, DOD, scalability | 49–76 | not started; 49–56 partly superseded by Stage 7.5. Step 48 landed at 7.6 step 11 |

Update this table when a stage completes.

Stages 7.5, 7.6 and 7.7 are inserted rather than renumbered: a whole stage at 8 would cascade
through every cross-reference in three documents, and the fractions cost nothing. 7.5 closed the
gap between the RHI's neutral *resource* API, which Stage 5 built, and its *frame* API, which
nothing had. 7.6 builds what the backend needs around that seam — the comparison tool, the shader
build, and two deliberate widenings of `IDevice` — and 7.7 is the backend itself.

---

## Build & run

Presets are `ninja-{debug,asan,release}-{linux,windows}` plus `msvc` (VS solution).
Requires `VCPKG_ROOT` to be set. The Vulkan headers, the loader, the validation layer,
`slangc` and `spirv-val` all come from vcpkg, so **nothing needs a Vulkan SDK or
`VULKAN_SDK`**. A Debug *run* has
validation as a hard requirement rather than a degradable one (`backlog.md`), and gets it
from the layer vcpkg built: `VulkanDevice::CreateInstance` puts that on
`VK_ADD_LAYER_PATH`, so nothing has to be installed system-wide.

What vcpkg does not supply on Linux is the X11 and Wayland client libraries. `vulkan-loader`
and `vulkan-validationlayers` configure against the system's `xcb`, `x11`, `xrandr` and
`wayland-client` pkg-config modules and fail outright without them, so a Linux machine needs
those development packages before a build will configure. Installing them also commits you to
a second set: SDL3 skips its X11 extension probes entirely when libX11 is absent, and starts
hard-requiring Xcursor, Xi, Xfixes and Xtst once it is there.

A third set is autotools, which nothing in `vcpkg.json` asks for and SDL3 reaches anyway: its
Linux default features include `dbus`, which pulls `libsystemd`, which pulls `libxcrypt`, which
configures through `vcpkg-make` and so runs `autoreconf`. Two members of that set are the ones
that actually fail a build, because the rest are usually already present: `autoconf-archive`,
which most distributions leave out, and `libltdl-dev`, which libxcrypt's `configure.ac` needs
and which Debian and Ubuntu split out of `libtool` — so having libtool proves nothing there,
and `--no-install-recommends` will not drag it in. Arch ships those files inside `libtool`
itself, which is why the failure is invisible on one distribution and arbitrary-looking on the
other. On Debian/Ubuntu the whole set is

```bash
sudo apt install libxcb1-dev libx11-dev libxext-dev libxrandr-dev libwayland-dev \
                 libxcursor-dev libxi-dev libxfixes-dev libxtst-dev \
                 autoconf autoconf-archive automake libtool libltdl-dev
```

```bash
./build.sh                          # configure+build the host default (ninja-debug-linux)
./build.sh ninja-release-linux      # or any preset
cmake --workflow --preset ninja-debug-linux   # what build.sh wraps

tests/scripts/build_tests.sh        # build every test target
tests/scripts/run_unit_tests.sh     # ctest -L unit --output-on-failure
tests/scripts/run_gpu_tests.sh      # ctest -L gpu --output-on-failure (needs a Vulkan ICD)
tests/scripts/header_check.sh       # compile every header standalone, no PCH
tests/scripts/rhi_boundary_check.sh # the RHI seam: neutral headers, and who may bypass them
tests/scripts/namespace_check.sh    # every engine header opens its module's namespace
tests/scripts/format_check.sh       # dry-run, -Werror; needs no configured tree
scripts/format.sh                   # clang-format -i over src/ and engine/
scripts/precommit.sh                # all of the above, everything CI runs plus the GPU tests
```

`header_check.sh` builds the `HeaderSelfContainment` aggregate: one check target per layer
(`_App` for `src/`, one per engine module), each linking only what that layer may link.
`precommit.sh` runs it straight after the build.

**CI runs each check at the frequency its answer varies.** The source-level checks — format,
`rhi_boundary_check`, `namespace_check` — are one `static-checks` job on a bare runner, since
their verdict cannot differ between configurations and they need neither a toolchain nor a
configured tree. `HeaderSelfContainment` runs in the debug job of each OS, because its answer
*does* differ by compiler and standard library but not by configuration. Build and unit tests
run in all six. `precommit.sh` runs the same set locally in one sequence, so its ordering no
longer mirrors CI's job layout.

**The GPU and scene tests run on Linux against lavapipe**, pinned with `VK_DRIVER_FILES`
rather than discovered — enumeration order is not a stable identifier. `ctest -L gpu` runs in
all three Linux jobs including release, because `RhiTestFixture.h` enables validation
unconditionally, so a release run asserts everything a debug one does. **`ctest -L scene` runs
in all three Linux jobs too**, because `RunScene` asks for validation explicitly rather than
inheriting it from the build — the same eleven cases assert the same thing in every
configuration. Before that a release run reported zero validation errors trivially, which made
the headline assertion theatre.

Everything that *verifies* the tree lives in `tests/scripts/`; `scripts/` holds the things
that build or change it (`build.sh` at the root, `format.sh`, `precommit.sh`, and the
Windows-only `envsetup.bat`). Each script has a `.bat` equivalent beside it. Scripts resolve
`build/<preset>/` relative to the current directory, so run them from the repository root.
Build artifacts land in `build/<preset>/`; `compile_commands.json` is symlinked to the
debug-linux build for clangd.

Asset paths resolve against a content root, not the CWD, so the app runs from anywhere:

```bash
./build/ninja-debug-linux/HikariEditor --scene scenes/test_scene.map   # content-relative
./build/ninja-debug-linux/HikariEditor --content /path/to/content      # explicit root
./build/ninja-debug-linux/HikariEditor --help

# The same engine with no window, rendering into an offscreen target. --frames
# is required: nothing else can end the run.
./build/ninja-debug-linux/HikariHeadless --frames 100 --screenshot --report

# Both binaries replay an input script: key presses, resizes, captures and quit,
# delivered on the frames it names. The editor merges it with real input, so a
# scripted run that failed in CI can be watched.
./build/ninja-debug-linux/HikariHeadless --input tests/data/input/orbit.txt
./build/ninja-debug-linux/HikariEditor  --input tests/data/input/orbit.txt
```

`Paths` (in `engine/platform`) resolves the root in priority order: `--content` →
`HIKARI_CONTENT` → `<exe dir>/content` → `<source dir>/content`. An override given
explicitly must exist — a mistyped `--content` fails rather than silently falling back.
Paths handed to `Paths::Content()` are content-relative unless absolute, in which case they
are used as given.

### Regression checking

`tests/scripts/baseline_test.sh` is the whole workflow in one command: it runs the app with a
fixed timestep and a fixed camera, then compares what came out against the committed
`tests/baseline/` and exits with the comparison's status.

```bash
tests/scripts/baseline_test.sh                       # capture, then compare
tests/scripts/baseline_test.sh --update              # capture, compare, promote the baseline
tests/scripts/baseline_test.sh ninja-release-linux   # any preset; default is the host's debug
```

The run writes `tests/reports/baseline.json` and `tests/screenshots/baseline.png`, both
gitignored and both overwritten each time. **Exit codes, strongest first:**

| Code | Meaning |
|---|---|
| **3** | No verdict — a report would not read, or a field is missing from one of them or absent from the comparison's table |
| **1** | A compared signal moved |
| **2** | Nothing moved, but a signal could not be compared |
| **0** | Everything was compared, and it matched |

1 outranks 2 because a difference in a signal that *was* compared is real whatever happened to
the others. A no-verdict is not a pass: it means nothing was established.

`HikariCompare` is the tool underneath, and it works on any two runs:

```bash
./build/<preset>/HikariCompare --actual-report a.json --expected-report b.json \
                               --actual-image a.png  --expected-image b.png
```

**Each signal is compared only when the conditions it depends on match**, and a skip names the
field that caused it — `run.buildConfig` differing skips the counters, because a release build
reports zero validation errors trivially. Two classifications deliberately never gate anything:
`run.jobCount`, since a difference across job counts is a race and gating would excuse it, and
`run.headless` for pixels, which step 46 verified. The table lives in
`tests/support/ReportCompare.cpp` with the evidence for each entry beside it, and a unit test
fails the build if the report gains a field the table does not classify.

**On a pixel failure the tool writes `comparison_actual.png`, `comparison_expected.png` and an
amplified `comparison_diff.png`** beside the capture, so a reader can see *where* an image moved
rather than only by how much. The diff is scaled so that the tolerance ceiling is full
brightness; within one backend the tolerance is zero, so every differing pixel is fully bright.
CI cannot retrieve those files yet — `ci.yml` has no artefact step — which is a `backlog.md` row.

**`--update` promotes the run into `tests/baseline/`, and refuses more often than it accepts.**
It replaces both committed files together or neither. It refuses outright when the run's
conditions differ from the baseline's, so a baseline cannot quietly move to another machine or
another build configuration — a release run is rejected against a debug baseline. And it does
nothing at all when nothing moved, because PNG encoding is not reproducible and rewriting an
identical capture would still put a binary diff into git. A field the baseline simply lacks is
the one difference it is *for*, so that is not a refusal.

Read the two report signals differently, which is why they sit in separate blocks:

- **`counters`** are expectations that must match exactly. `counters.frame` — `drawCalls`,
  `batches`, `instances`, `barriers`, `barrierCalls` — describes the last frame drawn, which is
  the frame a capture shows. `counters.run` — `validationErrors`, `validationWarnings`,
  `uploadSubmissions` — accumulates over the whole run. `uploadSubmissions` is what guards the
  asset layer's batching from a distance: one scene's textures loaded inside one load scope is a
  handful of submissions, and a number that tracks the texture count means the scoping broke.
- **`timings`** — `startupMs`, `firstFrame`, and `mean`/`p99`/`min`/`max` for both `frameMs`
  (wall clock) and `cpuMs` (the same minus what the frame spent blocked) — are measurements, not
  expectations. The comparison never diffs them; read them for drift. `frameMs` is bounded below
  by the display refresh whenever the present path throttles the CPU, which is what `cpuMs`
  exists to see past. Frame 0 is reported separately as `firstFrame` and excluded from the
  series, since it pays for first use of every pipeline.

**Never byte-compare two captures.** PNG encoding is not reproducible, so `cmp` and `md5sum`
differ on a pixel-identical pair. Comparing decoded pixels by hand is also a trap worth knowing
about, because this project fell into it: PIL's `Image.getbbox()` defaults to `alpha_only=True`,
so on an RGBA pair it inspects only the alpha channel — and every capture here is fully opaque,
which made the check pass for two images of completely different scenes. That is why the
comparison is a tested tool now rather than a recipe, and why its tests include a case asserting
that a difference in colour alone, with alpha identical, is caught.

`--borderless` rather than `--resolution` alone is what pins the capture's extent: a window size
is a request the window system may refuse, and a tiling compositor always does. Without it the
capture comes out at whatever size the layout chose, and no two machines agree. The rationale is
in the script, next to the flags.

`HikariHeadless` renders into an offscreen target with no window at all. It needs something
that can end the run — `--frames`, or an `--input` script containing `quit` — and takes
`--resolution` for the target's extent rather than a window's. It has no window-mode flags, because the binary *is* the mode: the
`--headless` flag it replaced existed only so one binary could refuse its own options. The UI
still draws, so a headless capture and an editor one of the same frame come out
pixel-identical — verified at step 46 and the thing step 47's CI tests rest on.

---

## Repository layout

```
apps/editor/     # HikariEditor — SDL window, the UI attached, one main.cpp
apps/headless/   # HikariHeadless — no window, offscreen target, one main.cpp
engine/core/     # Engine::Core static lib — Log, Timer, MyMacros, SwapbackArray,
                 #   ThreadPool, IJobSystem + SerialJobSystem + SharedQueueJobSystem,
                 #   Handle + HandlePool, Extent2D + Extent3D (one definition, used
                 #   by Platform and the RHI alike).
engine/platform/ # Engine::Platform static lib — IPlatform/SdlPlatform, Paths, FileSystem,
                 #   CommandLine
engine/asset/   # Engine::Asset static lib — AssetCache, PNG encoding
engine/rhi/      # Engine::RHI static lib — the graphics abstraction.
                 #   include/rhi/         backend-neutral: IDevice, ICommandList, barriers,
                 #                        handles, descs, IUploadContext, IPipelineCache
                 #   include/rhi/vulkan/  the transitional area that may expose Vulkan —
                 #                        the native escape hatch plus what Stages 6-8 have
                 #                        not taken over yet. Frozen; see below.
                 #   src/vulkan/          the backend. Invisible outside the module.
engine/engine/   # Engine::Engine static lib — the engine, and everything not yet split out.
                 #   include/engine/      what an app sees: IEngine, RunApp, RunSpec,
                 #                        EngineConfig, RunReport, IUiBackend
                 #   src/                 the renderer, the scene and the asset types, private
                 #                        to the module until Stage 8 promotes them into
                 #                        Render and Scene modules of their own
                 #   src/shaders/         Slang source (.slang, .slangh); compiled to
                 #                        <exe dir>/shaders/*.spv
engine/editor/   # Engine::Editor static lib — the UI stack. Above Engine: an app builds
                 #   VulkanUiBackend and hands it over as an IUiBackend
cmake/           # EngineModule.cmake (engine_module), Testing.cmake (engine_test),
                 #   HeaderSelfContainment.cmake, Warnings.cmake
tests/unit/      # Catch2 tests, CTest label "unit" — no GPU, run by CI
tests/gpu/       # Catch2 tests needing a real device, CTest label "gpu" — run by CI on
                 #   Linux against lavapipe
tests/scene/     # real headless runs of the engine asserting on the RunReport they
                 #   return, CTest label "scene" — run by CI on the two Linux debug jobs
tests/data/      # a content root of its own: two hand-authored glTF cubes and the
                 #   scenes built from them, so expected counters are derivable
tests/support/   # TestSupport — the shared image and report comparison, plus the
                 #   header-only helpers (TestPaths.h, CaptureStream.h,
                 #   RhiTestFixture.h). Catch2-free, so the tool can link it too
tests/tools/     # HikariCompare — the command-line face of that comparison
content/         # runtime content root — models/ scenes/ textures/ shaders/ (.spv is gitignored)
```

### Adding files

Source lists are explicit, not globbed — a new `.cpp` will silently not build if you forget:

- `engine/engine/src/*.cpp` → append to `engine_module(Engine SOURCES ...)`, like any other
  module. There is no application source list any more: each app is one `main.cpp`.
- `engine/<module>/src/*.cpp` → append to that module's `engine_module(<Name> SOURCES ...)`
  call in `engine/<module>/CMakeLists.txt`.
- `tests/unit/**/*.cpp` → append to the matching `engine_test(...)` call in
  `tests/CMakeLists.txt` — `core_tests` for `unit/core/`, `platform_tests` for
  `unit/platform/`, `rhi_tests` for `unit/rhi/`.
- `tests/gpu/**/*.cpp` → append to `rhi_gpu_tests` in the same file. `engine_test` takes a
  `LABEL` naming the CTest label its cases get; it defaults to `unit`, and these pass `gpu`.

Headers *are* globbed (into the header checks and the format targets), so a new header is
checked automatically.

**One file is deliberately outside that rule: `engine/core/src/SanitizerShims.cpp`.** It is
its own `SanitizerShims` OBJECT library, linked directly by the app and by `engine_test()`,
and it must stay that way. Moving it into `Core`'s `SOURCES` would compile and link without
complaint and silently do nothing: the linker only extracts an archive member to resolve an
undefined symbol, and nothing here calls the libc function it interposes. The reasoning is in
the file and in `engine/core/CMakeLists.txt`.

A new engine module is `engine/<name>/` with `include/<name>/` + `src/`, one line of
`engine_module(<Name> SOURCES ... LINK_LIBRARIES ...)`, and `add_subdirectory` in the root
`CMakeLists.txt`. Header-only modules omit `SOURCES` and become INTERFACE libraries.
`engine_module` also creates that module's `HeaderSelfContainment_<Name>` check, linking
only the module itself — so a new module is header-checked with no extra wiring.

---

## Architecture rules

The target is nine layered CMake targets where **the build system enforces layering, not
discipline** — if `Core` does not link `RHI`, `Core` cannot include Vulkan headers:

```
Core ← Platform ← RHI ← {Assets, Render} ← {Scene} ← Engine ← {Editor, apps/*}
```

Two non-obvious rules that the whole test strategy rests on:

- **`Scene` must not link `RHI`.** ECS, transforms, hierarchy and serialization stay testable
  with zero Vulkan.
- **`Render` must not link `Scene`.** It consumes a POD `FrameSnapshot`, so renderer tests
  build inputs by hand.

**The RHI's public API is backend-neutral, and that is checked rather than trusted.** Nothing
under `engine/rhi/include/rhi/` may name a Vulkan or VMA type; the backend lives in
`engine/rhi/src/vulkan/`, where nothing outside the module can reach it. **And nothing in
`engine/` or `apps/` outside that module may name Vulkan at all** — checked on names rather
than includes, because a precompiled header once put the whole API in scope for a module with
no include and no allowlist entry to show for it. One file is exempt and it is listed: the ImGui
backend, permanently. A second entry would be a question about which neutral call is missing —
that is what the last temporary one turned out to be. The one exception is
`engine/rhi/include/rhi/vulkan/`, which is *frozen*: seven headers covering what Stages 6–8
have not taken over yet, and eighteen allowlisted include sites outside the module. Adding
either fails `rhi_boundary_check`, and so does leaving an allowlist entry behind after its
include goes — the list is meant to shrink to nothing. New entries are argued for in
`cmake/RhiBoundaryCheck.cmake`, next to the reason each existing one is still there.

Full target table, per-module header lists and directory layout: architecture plan §8–§9.

Design principles that should shape any new code (plan §7): everything hardware-facing sits
behind an interface with a real *and* a null/headless implementation; ownership is explicit
and constructor-injected (no new singletons — `ResourceManager`/`ModelManager`/
`MaterialFactory` are existing ones being removed in Stage 7); hot data is arrays of scalars
with 32-bit handles, not arrays of objects; anything a human judges by eye should also be a
number in the run report.

---

## Conventions

Formatting is enforced by `.clang-format` (LLVM base, Allman braces, 4-space indent, 100
columns, left-aligned `*`/`&`) — run `scripts/format.sh` rather than hand-matching.

**The clang-format version is pinned in `.clang-format-version`, and it matters.**
clang-format's output is not stable across major versions — the same `.clang-format` gives
different results from clang-format 18 and 22, with no option that reconciles them. Left to
whatever is on `PATH`, the six CI configurations run two different clang-formats and
disagree with each other. CI installs the pin; CMake warns at configure time if the local
one differs and tells you what to install:

```bash
pip install clang-format==$(cat .clang-format-version)
```

Bumping the pin means editing that file and reformatting the whole tree in the same commit.

Naming, as used throughout the codebase:

| Kind | Style | Example |
|---|---|---|
| Types, functions, methods | PascalCase | `CloudSystem`, `RecordDispatch` |
| Public/struct data members | PascalCase | `Options::ScenePath`, `LightData::Position` |
| Private members | `m_` + PascalCase | `m_SwapchainExtent` |
| Statics / globals | `s_` / `g_` | `s_Instance`, `g_bShouldClose` |
| Locals, parameters | camelCase | `frameIndex`, `createInfo` |
| `constexpr` constants | `kPascalCase` or `UPPER_SNAKE` | `kCameraPresets`, `MAX_INSTANCE_COUNT` |
| Booleans | `b` prefix | `bFixedDt`, `m_bCursorVisible` |
| Raw pointers | `p` prefix | `pWindow`, `m_pWindow` |
| Interfaces | `I` prefix | `IJobSystem` |

Planned additions (plan §19): `GpuThing` for POD GPU structs in `namespace shader`,
`ThingSystem`, `ThingPass`, `ThingHandle`.

Other rules:

- **One class per file, filename == class name.**
- **Every header must be self-contained** — `#pragma once` and include what it uses.
  `HeaderSelfContainment` compiles each `src/*.h` and each engine module's public headers
  standalone with no PCH, one target per layer, and CI fails on breakage. `src/pch.h` is
  deliberately exempt. Note that a local pass proves less than it looks: libstdc++ supplies
  `<cstdint>`, `<string>` and friends transitively, so a header missing them still compiles
  here and fails on MSVC or a newer libstdc++. Include what you use rather than relying on
  the check.
- **Warnings are errors** (`CMAKE_COMPILE_WARNING_AS_ERROR ON`, `-Wall -Wextra -Wpedantic
  -Wshadow` / `/W3`). A new warning breaks the build on all six CI configs.
- **Every engine type lives in `Hikari::<Module>`** — `Hikari::Core::Timer`,
  `Hikari::Platform::Paths`, `Hikari::Rhi::IDevice`, `Hikari::Rhi::Vulkan::SetVkDebugName`.
  The nesting is uniform and mirrors the directory: `engine/<mod>/include/<mod>/` opens
  `Hikari::<Mod>`, and a subdirectory adds a component. A header may nest deeper for its own
  grouping (`Hikari::Rhi::BarrierPresets`) but may not open a different module. The private
  sources under `engine/engine/src/` are *not* yet namespaced — they came from the old
  executable, and Stages 8–9 dismantle them into modules that already are.
- **`using namespace` is allowed in a `.cpp` and never in a header.** In a header it leaks
  into every translation unit that includes it, transitively, and breaks in whichever
  unrelated file includes it next. Engine sources qualify instead (`Core::LogMsg`), which is
  short because enclosing-namespace lookup applies inside `Hikari::`; `engine/engine/src/` and
  tests use directives, since their alternative is churn in code scheduled for demolition. Both
  halves are enforced by `tests/scripts/namespace_check.sh`.
- **Include style:** engine modules are included as `<core/Timer.h>`; a module's own private
  sources use `"Header.h"` quotes for siblings and `"lib/Header.h"` for third-party.
- **Errors:** exceptions for unrecoverable init failures; asset loading and parsing should
  log and skip rather than unwind through the frame loop.
- **RHI naming follows D3D12, not Vulkan**, wherever the two APIs name the same concept
  differently — `Copy` not `Transfer`, `CommandList` not `CommandBuffer`, `Pixel` not
  `Fragment`, `UnorderedAccess` not `Storage`. This applies to the Vulkan-side helpers too, so
  that a Vulkan term appearing in an interface reads as a mistake rather than as normal. Where
  only one API has the concept at all, its term stands. Utility headers under `rhi/vulkan/`
  take a uniform `Util` suffix (`BufferUtil.h`, `CommandListUtil.h`). Rationale and the full
  list: RHI plan D13.
- **`[[nodiscard]]` only where discarding causes real harm** — a leak, a bug, or wasted work.
  Returning a loaded resource, a RAII handle that would be destroyed immediately, or an owning
  pointer qualifies; a plain getter does not. `engine/core` and `engine/platform` have none, and
  `src/`'s handful are all on loaders that return something the caller must keep. Marking
  trivial accessors trains the reader to skip the attribute, which costs its value on the calls
  that need it.
- Comments in this codebase explain *why* (non-obvious platform quirks, ABI hazards,
  boundary-condition rules). Match that — do not narrate what the code already says.
- **Block comments document declarations; `//` is for everything else.** A comment above a
  class, function, member variable, enumerator, file-scope constant, alias, or the header's
  own subject is `/** … */` with a leading `*` on each continuation line; a one-liner may sit
  on a single line as `/** … */`. Comments inside a function body, trailing comments, section
  labels (`// --- Uploads ---`) and commented-out code stay `//`. Two boundary cases, decided:
  a macro other code invokes (`RHI_DEFINE_FLAG_OPERATORS`) is documented as a declaration,
  while a macro that configures the build (`ThreadPool.cpp`'s `WIN32_LEAN_AND_MEAN`) is not;
  and a header-level comment keeps the blank line between itself and the first declaration,
  because it documents the header rather than that declaration. The leading `*` is not decoration:
  clang-format reflows a block comment without it to a mis-indented continuation line, so the
  tree would be format-clean and visually ragged wherever a line wraps. No `@param`/`@return`
  tags — the marker is Doxygen's so that they remain possible, but a tag that restates the
  signature is the narration the rule above forbids.
- **The reasoning belongs in the source, not in a doc.** If a decision is non-obvious enough to
  need explaining, explain it where the code is, so the reader finds it without knowing a doc
  exists. Point at a doc only when the full argument is genuinely too long to sit in a comment
  — and even then, put the conclusion and the one-line reason inline and cite the doc for the
  detail, so the comment still stands on its own if the doc is retired.
- **Comments must not outlive what they describe.** When finishing a piece of work, delete the
  comments that pointed forward to it ("split out by R4", "R8 will replace this"). Keep such a
  comment only if it still tells the reader something they need, and then rewrite it to stand
  on its own: state the constraint or the rationale directly rather than citing a plan step or
  a doc, because those get retired once the work lands. Comments about genuinely outstanding
  work are fine, and should describe the intended end state rather than the ticket number.

---

## Gotchas

- **`engine/engine/src/Engine.cpp` holds the whole renderer** (~2,500 lines) — the frame loop,
  the five recorders, the descriptor sets and the pipelines. Grep before assuming something
  lives in its own file. Dismantling it is scheduled work, not incidental work. Each app is now
  one `main.cpp` under `apps/`, and neither holds anything worth grepping.
- **Shaders compile via `slangc` as a build step** into `<exe dir>/shaders/*.spv` — so
  `build/<preset>/shaders/`, one set per configuration, reached at runtime through
  `Paths::Shader()` rather than the content root. They are a build output, not content: the
  same sources compile with different flags per configuration, and a shared output directory
  had debug and release silently overwriting each other. Dependencies come from `slangc
  -depfile`, so a header edit rebuilds only the shaders that include it — `src/Common.h`
  included. Vertex/fragment entry points are `vertMain`/`fragMain`; compute is `main`, keyed
  off the `.comp.slang` suffix.
- **GPU struct layouts are declared twice by hand** — once in C++, once in Slang — with no
  `static_assert` linking them. Changing one without the other produces silent corruption.
  Unified in step 48.
- **MSVC + ASan needs `_DISABLE_STL_ANNOTATION`** to keep its STL ABI compatible with vcpkg's
  prebuilt libraries; removing it produces LNK2038 errors.
- **Windows and Linux are the platforms.** macOS was removed outright — presets, CI jobs,
  `if(APPLE)` blocks, the Metal surface path, the MoltenVK portability extensions. Don't
  reintroduce a `__APPLE__` branch as a courtesy to a port that does not exist; the git
  history has the old paths if one is ever wanted, and a port that has to be rewritten
  against a real Mac is worth more than a branch nobody can compile.
- **The instance buffer and the descriptor pools grow** — both were fixed ceilings that
  aborted, and no longer are. Growing the instance buffer reallocates storage the GPU may
  still be reading, so the wait before the swap is the load-bearing part, not the
  reallocation. Read `DescriptorAllocator::Grow` and `App::GrowInstanceBuffers` before
  changing either.
