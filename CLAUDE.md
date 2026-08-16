# CLAUDE.md

Cross-platform game engine (Windows / Linux / macOS-ARM) built on Vulkan, with a D3D12
backend planned later. C++20, CMake + vcpkg, Slang shaders.

The engine is mid-refactor from a single-executable prototype into a layered library set.
Two reference documents drive that work — read the relevant section before proposing
architecture, and prefer them over inventing a design:

- `docs/architecture_plan_14_08_2026.md` — target architecture (Part II), test strategy
  (Part III), and the **76-step incremental work order (Part IV)** that the project follows.
- `docs/suggested_work_04_08_2026.md` — the code review that motivated the plan; open it for
  the *why* behind a known defect.

---

## Working rules

**Follow Part IV strictly, one step at a time.** Each step is sized to end in a compiling,
running application. Do not start work outside the current stage, and do not combine steps,
without asking first.

**Do not opportunistically refactor.** `src/main.cpp` is ~2,600 lines and is scheduled for
dismantling across Stages 4–9. Touching it outside its scheduled step creates conflicts with
the plan. Fix what the step asks for; note anything else you spot rather than fixing it.

**Verify every change with `scripts/precommit.sh`** (configure + build + build tests + ctest
+ format-check) before reporting a change as done. That is exactly what CI enforces. Report
failures with the actual output — never claim a build passed without running it.

**For changes that could alter rendering, also compare against the baseline** (see
*Regression checking* below). "It still builds" is not evidence a refactor preserved
behaviour.

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
`grep`ping this repo for prior art is also not a source: `docs/suggested_work_04_08_2026.md`
§1.11 (unsynchronised depth read in the cloud compute pass) and §1.15 (sync objects not
recreated when the swapchain image count changes) are two known-wrong places to copy from.

**Never run git commands that change state.** No commits, branches, stashes, or pushes —
even when a task feels finished. Reading (`git status`, `git log`, `git diff`) is fine.

### Current position in the roadmap

| Stage | Steps | Status |
|---|---|---|
| 0 — Verification harness | 1–6 | ✅ done (`--frames`, `--screenshot`, `--report`, `--fixed-dt`, `--camera-preset`) |
| 1 — Build hygiene | 7–11 | ✅ done (clang-format, sanitizer presets, Catch2 + CTest in CI) |
| 2 — Header self-containment | 12–14 | ✅ done (`HeaderSelfContainment` target, enforced in CI) |
| 3 — Core library | 15–19 | ✅ done (`Engine::Core`, `IJobSystem` injected into `App`) |
| **4 — Platform library** | **20–23** | **in progress — step 20 done (`Engine::Platform`, `IPlatform`/`SdlPlatform`); ← next: 21 `Paths`, 22 `content/` root, 23 `CommandLine.h`** |
| 5 — RHI extraction | 24–34 | not started |
| 6 — Headless capability | 35–40 | not started |
| 7 — Engine shell + DI | 41–47 | not started — **CI goal met at step 47** |
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

tests/scripts/build_tests.sh        # build the core_tests target only
tests/scripts/run_unit_tests.sh     # ctest -L unit --output-on-failure
scripts/format.sh                   # clang-format -i over src/ and engine/
scripts/format_check.sh             # dry-run, -Werror
scripts/precommit.sh                # all of the above, in CI order
```

Windows equivalents are the `.bat` files in `scripts/`. Build artifacts land in
`build/<preset>/`; `compile_commands.json` is symlinked to the debug-linux build for clangd.

Run the app from the **repo root** — asset paths are currently CWD-relative (fixed in step 22):

```bash
./build/ninja-debug-linux/VulkanApp --scene scenes/test_scene.map
./build/ninja-debug-linux/VulkanApp --help
```

### Regression checking

`tests/scripts/headless.sh` runs the app with fixed timestep and a fixed camera, writing a
PNG and a JSON report:

```bash
tests/scripts/headless.sh    # --scene (default scenes/test_scene.map) --frames (default 1000)
                             # --fixed-dt --camera-preset 1 --screenshot --report
```

Output goes to `tests/screenshots/` and `tests/reports/` (both gitignored). Compare against
the committed `tests/baseline/`. The report is the primary signal — it carries
`validationErrors`, `validationWarnings`, `drawCalls`, `batches`, `instances`,
`meanFrameTimeMs`, `p99FrameTimeMs`. Validation errors must stay at 0.

Note `--headless` is parsed but not yet implemented (Stage 6); today this still opens a window.

---

## Repository layout

```
src/           # the application — one class per file, plus main.cpp (App + everything unmoved)
src/shaders/   # Slang source (.slang, .slangh); compiled to shaders/*.spv at build time
engine/core/   # Engine::Core static lib — Log, Timer, MyMacros, SwapbackArray,
               #   ThreadPool, IJobSystem + SerialJobSystem + SharedQueueJobSystem
cmake/         # EngineModule.cmake (engine_module), Testing.cmake (engine_test), Warnings.cmake
tests/unit/    # Catch2 tests, CTest label "unit"
tests/support/ # shared test helpers (TestPaths.h, CaptureStream.h)
scenes/ models/ textures/   # content; moves under content/ in step 22
```

### Adding files

Source lists are explicit, not globbed — a new `.cpp` will silently not build if you forget:

- `src/*.cpp` → append to `SOURCES` in the root `CMakeLists.txt`.
- `engine/core/src/*.cpp` → append to the `engine_module(Core SOURCES ...)` call in
  `engine/core/CMakeLists.txt`.
- `tests/unit/**/*.cpp` → append to the `engine_test(core_tests SOURCES ...)` call in
  `tests/CMakeLists.txt`.

Headers *are* globbed (into `HeaderSelfContainment` and the format targets), so a new header
is checked automatically.

A new engine module is `engine/<name>/` with `include/<name>/` + `src/`, one line of
`engine_module(<Name> SOURCES ... LINK_LIBRARIES ...)`, and `add_subdirectory` in the root
`CMakeLists.txt`. Header-only modules omit `SOURCES` and become INTERFACE libraries.

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

Naming, as used throughout the codebase:

| Kind | Style | Example |
|---|---|---|
| Types, functions, methods | PascalCase | `CloudSystem`, `RecordDispatch` |
| Public/struct data members | PascalCase | `Options::ScenePath`, `LightData::Position` |
| Private members | `m_` + PascalCase | `m_SwapchainExtent` |
| Statics / globals | `s_` / `g_` | `s_Instance`, `g_ValidationErrorCount` |
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
  `HeaderSelfContainment` compiles each `src/*.h` standalone with no PCH and CI fails on
  breakage. `src/pch.h` is deliberately exempt.
- **Warnings are errors** (`CMAKE_COMPILE_WARNING_AS_ERROR ON`, `-Wall -Wextra -Wpedantic
  -Wshadow` / `/W3`). A new warning breaks the build on all nine CI configs.
- **Include style:** engine modules are included as `<core/Timer.h>`; `src/` files use
  `"Header.h"` quotes for siblings and `"lib/Header.h"` for third-party.
- **Errors:** exceptions for unrecoverable init failures; asset loading and parsing should
  log and skip rather than unwind through the frame loop.
- Comments in this codebase explain *why* (non-obvious platform quirks, ABI hazards,
  boundary-condition rules). Match that — do not narrate what the code already says.

---

## Gotchas

- **`src/main.cpp` holds `App` and the whole renderer** (~2,600 lines). Grep before assuming
  something lives in its own file. Dismantling it is scheduled work, not incidental work.
- **Shaders compile via `slangc` as a build step** into `shaders/*.spv` (gitignored). The
  dependency tracking is coarse — every shader depends on every `.slangh`, so a header edit
  rebuilds all of them. Vertex/fragment entry points are `vertMain`/`fragMain`; compute is
  `main`, keyed off the `.comp.slang` suffix.
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
- **Fixed ceilings that abort rather than grow:** `MAX_INSTANCE_COUNT` 1024, and a
  100-material descriptor pool limit. Growable replacements land in steps 31–32.
