# CLAUDE.md

HikariEngine — a cross-platform game engine (Windows / Linux / macOS-ARM) built on Vulkan,
with a D3D12 backend planned later. C++20, CMake + vcpkg, Slang shaders.

The engine is mid-refactor from a single-executable prototype into a layered library set.
Three reference documents drive that work — read the relevant section before proposing
architecture, and prefer them over inventing a design:

- `docs/architecture_plan.md` — target architecture (Part II), test strategy
  (Part III), and the **76-step incremental work order (Part IV)** that the project follows.
- `docs/suggested_work.md` — the code review that motivated the plan; open it for
  the *why* behind a known defect.
- `docs/rhi_extraction_plan.md` — **retained past Stage 5, which it drove.** Replaced Part IV
  steps 24–34 with a 17-step sequence (R1–R17) that made the RHI's public API backend-neutral
  so a D3D12 backend is possible later, and records the design decisions (D0–D12) behind that.
  Stage 5 is complete, so R1–R17 are history; the **decisions remain live**, because they
  govern what the RHI's public seam is allowed to say and Part IV's own §10 predates them.
  Read it before touching anything under `engine/rhi/include/`. Its §10 lists what should
  eventually be promoted into the architecture plan; retiring it is a deliberate future
  decision, not a step in the roadmap.

---

## Working rules

**Follow Part IV strictly, one step at a time.** Each step is sized to end in a compiling,
running application. Do not start work outside the current stage, and do not combine steps,
without asking first. Stage 5 is complete, so Part IV is the work order again — but where
`docs/rhi_extraction_plan.md`'s decisions (D0–D12) and Part IV disagree about the RHI's
public seam, **the RHI plan still wins**. Part IV was written before the seam was
neutralised, so its later stages still spell interfaces in raw Vulkan; §10.2 is one such
place. Re-express rather than copy, and amend Part IV as you go.

**Do not opportunistically refactor.** `src/main.cpp` is ~2,600 lines and is scheduled for
dismantling across Stages 4–9. Touching it outside its scheduled step creates conflicts with
the plan. Fix what the step asks for; note anything else you spot rather than fixing it.

**Verify every change with `scripts/precommit.sh`** (configure + build + build tests +
`ctest -L unit` + `ctest -L gpu` + format-check) before reporting a change as done. It is a
superset of CI: everything CI enforces, plus two things CI does not run — the GPU tests
(CI's runners have no Vulkan ICD) and `rhi_boundary_check` (an oversight, tracked in the
plan's independent-work table; CI does still enforce header *neutrality* through
`HeaderSelfContainment_RHI_Neutral`, but not the allowlist ratchet). The GPU tests skip
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

Authoritative sources, local copies first (they match the installed SDK version):

| Source | Where | Use for |
|---|---|---|
| Vulkan headers | `$VULKAN_SDK/include/vulkan/vulkan_core.h` | exact enum values, struct fields, function signatures |
| Vulkan registry | `$VULKAN_SDK/share/vulkan/registry/vk.xml` | which extension/version a symbol belongs to, aliases, deprecations |
| Valid Usage database | `$VULKAN_SDK/share/vulkan/registry/validusage.json` | look up a `VUID-...` from a validation message verbatim |
| Slang docs | `$VULKAN_SDK/share/doc/slang/` (`user-guide/`, `language-reference/`, `command-line-slangc-reference.md`) | shader language and `slangc` flags |
| Vulkan spec | <https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html> | synchronization chapter, layout rules, the prose behind a VUID |
| VMA docs | <https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/> | allocation flags, mapping and usage patterns |
| Driver support | <https://vulkan.gpuinfo.org> | whether a feature/format/limit is realistically available |

The validation layers are the empirical check, not a substitute for the spec — a clean
validation run proves nothing was caught, not that the code is correct. Synchronization
validation in particular is off by default and worth enabling when touching barriers.
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
| **7 — Engine shell + DI** | **40b, 41–47** | **next** — the cleanup series first (`docs/cleanup_plan.md`); **CI goal met at step 47** |
| 8+ — Frame graph, DOD, scalability | 48–76 | not started |

Update this table when a stage completes.

---

## Build & run

Presets are `ninja-{debug,asan,release}-{linux,windows,macos}` plus `msvc` (VS solution).
Requires `VULKAN_SDK` and `VCPKG_ROOT` to be set; `slangc` must be on `PATH`.

```bash
./build.sh                          # configure+build the host default (ninja-debug-linux)
./build.sh ninja-release-linux      # or any preset
cmake --workflow --preset ninja-debug-linux   # what build.sh wraps

tests/scripts/build_tests.sh        # build every test target
tests/scripts/run_unit_tests.sh     # ctest -L unit --output-on-failure
tests/scripts/run_gpu_tests.sh      # ctest -L gpu --output-on-failure (needs a Vulkan ICD)
tests/scripts/header_check.sh       # compile every header standalone, no PCH
tests/scripts/rhi_boundary_check.sh # the RHI seam: neutral headers, and who may bypass them
tests/scripts/format_check.sh       # dry-run, -Werror
scripts/format.sh                   # clang-format -i over src/ and engine/
scripts/precommit.sh                # all of the above, CI's checks in CI's order
```

`header_check.sh` builds the `HeaderSelfContainment` aggregate: one check target per layer
(`_App` for `src/`, one per engine module), each linking only what that layer may link.
`precommit.sh` runs it straight after the build, matching CI's ordering.

Everything that *verifies* the tree lives in `tests/scripts/`; `scripts/` holds the things
that build or change it (`build.sh` at the root, `format.sh`, `precommit.sh`, and the
Windows-only `envsetup.bat`). Each script has a `.bat` equivalent beside it. Scripts resolve
`build/<preset>/` relative to the current directory, so run them from the repository root.
Build artifacts land in `build/<preset>/`; `compile_commands.json` is symlinked to the
debug-linux build for clangd.

Asset paths resolve against a content root, not the CWD, so the app runs from anywhere:

```bash
./build/ninja-debug-linux/HikariEngine --scene scenes/test_scene.map   # content-relative
./build/ninja-debug-linux/HikariEngine --content /path/to/content      # explicit root
./build/ninja-debug-linux/HikariEngine --help
```

`Paths` (in `engine/platform`) resolves the root in priority order: `--content` →
`HIKARI_CONTENT` → `<exe dir>/content` → `<source dir>/content`. An override given
explicitly must exist — a mistyped `--content` fails rather than silently falling back.
Paths handed to `Paths::Content()` are content-relative unless absolute, in which case they
are used as given.

### Regression checking

`tests/scripts/baseline_test.sh` runs the app with fixed timestep and a fixed camera, writing
a PNG and a JSON report:

```bash
tests/scripts/baseline_test.sh   # --scene (default scenes/test_scene.map) --frames (default 1000)
                                 # --fixed-dt --camera-preset 1 --screenshot --report
                                 # --resolution 1920x1080 --borderless
```

Output goes to `tests/screenshots/` and `tests/reports/` (both gitignored). Compare against
the committed `tests/baseline/`. Two signals, and **both are usable**:

- **The report** carries `validationErrors`, `validationWarnings`, `drawCalls`, `batches`,
  `instances`, `barriers`, `barrierCalls`. Validation errors must stay at 0. Ignore
  `meanFrameTimeMs`/`p99FrameTimeMs` — under `--fixed-dt` the app records the *timestep*
  rather than measured cost, so both read exactly 16.6667 regardless of performance. That is
  a known defect, tracked in the plan's independent-work table.
- **A pixel diff of the screenshot**, which is the stronger check and is now reliable: the
  script forces `--resolution 1920x1080 --borderless`, so captures come out at a fixed extent
  instead of at whatever size the window manager chose. **Never byte-compare** — PNG encoding
  is not reproducible, so `cmp`/`md5sum` on a pixel-identical pair still differs. Compare
  decoded pixels (`PIL.ImageChops.difference(a, b).getbbox() is None`).

`--borderless` rather than `--resolution` alone is what makes that work: a window size is a
request the window system may refuse, and a tiling compositor always does. The rationale is
in the script, next to the flags.

`--headless` renders into an offscreen target with no window at all. It requires
`--frames` — nothing else can end the run — and cannot be combined with `--borderless` or
`--fullscreen`. ImGui still draws, so a headless capture and a windowed one of the same frame
come out pixel-identical.

---

## Repository layout

```
src/             # the application — one class per file, plus main.cpp (App + everything unmoved)
src/shaders/     # Slang source (.slang, .slangh); compiled to <exe dir>/shaders/*.spv
engine/core/     # Engine::Core static lib — Log, Timer, MyMacros, SwapbackArray,
                 #   ThreadPool, IJobSystem + SerialJobSystem + SharedQueueJobSystem,
                 #   Handle + HandlePool. Extent2D/Extent3D move here (planned) — they
                 #   exist twice today, as ::Extent2D in Platform and Rhi::Extent2D.
engine/platform/ # Engine::Platform static lib — IPlatform/SdlPlatform, Paths, FileSystem,
                 #   CommandLine
engine/rhi/      # Engine::RHI static lib — the graphics abstraction.
                 #   include/rhi/         backend-neutral: IDevice, ICommandList, barriers,
                 #                        handles, descs, IUploadContext, IPipelineCache
                 #   include/rhi/vulkan/  the transitional area that may expose Vulkan —
                 #                        the native escape hatch plus what Stages 6-8 have
                 #                        not taken over yet. Frozen; see below.
                 #   src/vulkan/          the backend. Invisible outside the module.
cmake/           # EngineModule.cmake (engine_module), Testing.cmake (engine_test),
                 #   HeaderSelfContainment.cmake, Warnings.cmake
tests/unit/      # Catch2 tests, CTest label "unit" — no GPU, run by CI
tests/gpu/       # Catch2 tests needing a real device, CTest label "gpu" — not run by CI
tests/support/   # shared test helpers (TestPaths.h, CaptureStream.h, RhiTestFixture.h)
content/         # runtime content root — models/ scenes/ textures/ shaders/ (.spv is gitignored)
```

### Adding files

Source lists are explicit, not globbed — a new `.cpp` will silently not build if you forget:

- `src/*.cpp` → append to `SOURCES` in the root `CMakeLists.txt`.
- `engine/<module>/src/*.cpp` → append to that module's `engine_module(<Name> SOURCES ...)`
  call in `engine/<module>/CMakeLists.txt`.
- `tests/unit/**/*.cpp` → append to the matching `engine_test(...)` call in
  `tests/CMakeLists.txt` — `core_tests` for `unit/core/`, `platform_tests` for
  `unit/platform/`, `rhi_tests` for `unit/rhi/`.
- `tests/gpu/**/*.cpp` → append to `rhi_gpu_tests` in the same file. `engine_test` takes a
  `LABEL` naming the CTest label its cases get; it defaults to `unit`, and these pass `gpu`.

Headers *are* globbed (into the header checks and the format targets), so a new header is
checked automatically.

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
`engine/rhi/src/vulkan/`, where nothing outside the module can reach it. The one exception is
`engine/rhi/include/rhi/vulkan/`, which is *frozen*: seven headers covering what Stages 6–8
have not taken over yet, and sixteen allowlisted include sites outside the module. Adding
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
whatever is on `PATH`, the nine CI configurations run three different clang-formats and
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
  -Wshadow` / `/W3`). A new warning breaks the build on all nine CI configs.
- **Include style:** engine modules are included as `<core/Timer.h>`; `src/` files use
  `"Header.h"` quotes for siblings and `"lib/Header.h"` for third-party.
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

- **`src/main.cpp` holds `App` and the whole renderer** (~2,600 lines). Grep before assuming
  something lives in its own file. Dismantling it is scheduled work, not incidental work.
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
- **macOS pins `find_package(Vulkan)` to the SDK**, not vcpkg's loader; a loader/layer version
  mismatch causes infinite recursion in `vkGetDeviceProcAddr` during ImGui init. Don't
  "simplify" that block in the root `CMakeLists.txt`.
- **MSVC + ASan needs `_DISABLE_STL_ANNOTATION`** to keep its STL ABI compatible with vcpkg's
  prebuilt libraries; removing it produces LNK2038 errors.
- **macOS is tagged experimental** — it builds in CI but is exercised far less than
  Linux/Windows.
- **The instance buffer and the descriptor pools grow** — both were fixed ceilings that
  aborted, and no longer are. Growing the instance buffer reallocates storage the GPU may
  still be reading, so the wait before the swap is the load-bearing part, not the
  reallocation. Read `DescriptorAllocator::Grow` and `App::GrowInstanceBuffers` before
  changing either.
