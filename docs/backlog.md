# HikariEngine — Backlog

Work that is off the critical path. The architecture plan is what happens next; this is
everything else, and it is deliberately broad — an item belongs here whether or not it can be
picked up today.

**Priority** is P1 (most important), P2, P3, and it means *how soon this should happen*, not
how broken the thing is. It is a different axis from `suggested_work.md`'s P0–P3, which is
severity assigned once by a review — a P2 design-debt item there can perfectly well be a P1
here. Both documents name their axis, "Severity tags" there and `Priority` here, because the
letters overlap and the scales do not.

**Blocked by** is blank for most rows, and a blank means what this list used to promise for
every row: pick it up any time. Where it names something, the item is still worth recording
here rather than buried in the step that unblocks it — the frame-time defect sat in the
architecture plan's §14 prose through five stages before anyone tracked it.

Each item is verified by "output unchanged unless noted, zero validation errors". Completed
items are deleted rather than struck through, along with any expanded note below the table;
git history is the record.

Rows marked *(cleanup: `branch`)* are carried by the series in `cleanup_plan.md`, which holds
the decided approach for each. The notes below the table describe the defect, not the fix.

| Priority | Item | Where | Size | Blocked by |
|---|---|---|---|---|
| P1 | Correctness fixes from `suggested_work.md` §1.6 and §3.1 — §3.2 (batched uploads) is done | various | S–M each | |
| P1 | `SIGTERM` goes unhandled, so a CI timeout kills even a bounded run before it writes its screenshot and report — `SIGINT` is handled and `SIGTERM` should be too *(cleanup: `fix/signals`)* | `main.cpp:2562` | XS | |
| P1 | Ctrl-C with `--screenshot` usually writes no PNG, only the "called without a captured frame" error. `WriteScreenshot` does run — `HandleSIGINT` leaves the loop the ordinary way — but whether anything was captured is a race: the capture is recorded in-frame, decided at `main.cpp:569` from `g_bShouldClose`, so a signal arriving after that line in the last iteration exits at the top of the next one with nothing staged. Capture on the way out instead when the flag was asked for and `m_bScreenshotBufferReady` is false *(cleanup: `fix/signals`)* | `main.cpp:493-575` | S | |
| P1 | `--no-ui`, and re-baseline onto it — the baseline captures a fifth of the frame as ImGui, whose hover state depends on where the mouse was left *(cleanup: `test/baseline`)* | `main.cpp`, `tests/scripts/baseline_test.sh`, `tests/baseline/` | S | |
| P1 | Frame-time counters record the timestep, not wall clock, under `--fixed-dt` *(cleanup: `test/baseline`)* | `App::Run` | XS | |
| P1 | Move `Extent2D` and `Extent3D` into `Engine::Core` — one type instead of `::Extent2D` and `Rhi::Extent2D` *(cleanup: `engine/core`)* | `core/`, `platform/`, `rhi/` | S | |
| P1 | `ChooseSwapchainFormat`'s fallback hands `FromNativeFormat` something it may not be able to name *(cleanup: `engine/rhi`)* | `rhi/vulkan/SwapchainUtil.h` | XS | |
| P1 | Delete `App::m_Surface` — bound, never read, and the only caller of `Rhi::Vulkan::GetSurface` *(cleanup: `engine/rhi`)* | `main.cpp`, `rhi/vulkan/VulkanNative.h` | XS | |
| P1 | `rhi_boundary_check` runs in precommit but not in CI *(cleanup: `test/ci`)* | `.github/workflows/ci.yml` | XS | |
| P1 | `SdlPlatform`'s explicit `SDL_Vulkan_LoadLibrary`/`UnloadLibrary` pair is redundant — a `SDL_WINDOW_VULKAN` window loads and unloads the library itself *(cleanup: `platform/sdl`)* | `platform/SdlPlatform.cpp`, `SdlPlatform.h` | XS | |
| P2 | `--present-mode <immediate\|mailbox\|fifo\|fifo-relaxed>`, defaulting to mailbox; an explicit mode that the surface does not offer is a hard error | `rhi/IPresentTarget.h`, `SwapchainUtil.h`, `main.cpp` | S | |
| P2 | Document the matrix convention once and apply it consistently | `opaque.slang` header comment | S | |
| P2 | `.map` format `version` attribute | `XmlParser` | XS | |
| P2 | Record the GPU name, driver version, Vulkan API version, OS and architecture in the run report — two reports from different machines are otherwise comparable-looking and not comparable | `main.cpp`, `rhi/IDevice.h` | S | a neutral device-info accessor on `IDevice`, which is a seam decision |
| P2 | A baseline comparison script — decode both PNGs, report the diff bounding box, and diff the report's `counters`. Today `CLAUDE.md` has to tell a human to drive PIL by hand | `tests/scripts/` | S | |
| P2 | Namespace `src/`'s remaining types under `Hikari::` | `src/` | M | Stages 7–9, which move them into engine modules a piece at a time |
| P3 | The ImGui panel has no regression coverage: the baseline is captured with `--no-ui`, deliberately, because a UI capture's hover highlight follows wherever the mouse was left | `tests/`, editor | M | Stage 7's `EditorLayer`, which can be driven without a mouse |
| P3 | Expose cloud push-constants in ImGui (`m_CloudData` is pushed but never written) | `CloudSystem` + editor UI | S | |
| P3 | `surface.slangh` to de-duplicate ~130 lines across the two surface shaders | `shaders/` | M | |
| P3 | Split `pbr.slangh` into `brdf`/`tonemap`/`phase` | `shaders/` | S | |
| P3 | `CubemapCreateInfo` → `std::array<std::string,6> FacePaths`, delete the 6-case switch | `CubemapLoader.cpp` | S | |
| P3 | Finish the skybox (loaded at `main.cpp:598`, never rendered) and reuse it for IBL | new pass | M–L | |

Five of these are worth expanding on, because they are latent defects or carry a decision:

- **`Extent2D` in two places.** `::Extent2D` (Platform) and `Rhi::Extent2D` are the same two
  `uint32_t`s; only the RHI's has `operator==`. `Core` is what both modules already link, so
  it is where the type belongs, and `Extent3D` goes with it rather than being stranded alone
  in `RhiTypes.h`. Delete the `Rhi::` spellings rather than aliasing them — one type reachable
  under two names is what makes a reader stop and check whether they differ. Roughly five call
  sites.
- **The format fallback.** `ChooseSwapchainFormat` returns `formats[0]` when its preference is
  absent, and the very next line calls `FromNativeFormat`, which throws on anything the
  curated `Rhi::Format` list cannot name. `Rhi::Format` has `BGRA8Unorm` but no `BGRA8Srgb` —
  and on an X11 surface with RADV the *only* two formats offered are `B8G8R8A8_SRGB` and
  `B8G8R8A8_UNORM`, so `formats[0]` is exactly the unnameable one. The "fallback" is therefore
  a trap: it hands the next line something that aborts startup. Either restrict it to formats
  the list covers, or fail there with a message naming what the surface offered.
- **Frame times under `--fixed-dt`.** `App::Run` computes `currentFrameTime` from
  `m_DeltaTime`, which `--fixed-dt` *sets* to 1/60 — so `m_FrameTimesMs` records the timestep
  rather than the cost, and `meanFrameTimeMs`/`p99FrameTimeMs` read exactly 16.6667 whatever
  the frame actually took. `baseline_test.sh` always passes `--fixed-dt`, so those two
  counters can never detect a regression in the one mode that is supposed to detect
  regressions. The architecture plan's §14 already says the fix: measure wall clock
  separately from the simulation timestep, and report raw values. Do this before any step
  starts making performance claims.
- **`SdlPlatform`'s explicit Vulkan loader calls.** The constructor calls
  `SDL_Vulkan_LoadLibrary(nullptr)` and the destructor `SDL_Vulkan_UnloadLibrary()`, and
  neither is needed. SDL 3.4's `SDL_CreateWindow` documents that *"if the window is created
  with any of the `SDL_WINDOW_OPENGL` or `SDL_WINDOW_VULKAN` flags, then the corresponding
  LoadLibrary function … is called and the corresponding UnloadLibrary function is called by
  `SDL_DestroyWindow()`"* (`SDL3/SDL_video.h`), and `SdlPlatform` always passes
  `SDL_WINDOW_VULKAN`. The two SDL entry points that need the library loaded —
  `SDL_Vulkan_GetInstanceExtensions` and `SDL_Vulkan_CreateSurface` — both run after the
  window exists, and both are skipped entirely when `bPresent` is false. Nothing else consumes
  SDL's loader: `SDL_Vulkan_GetVkGetInstanceProcAddr` is never called, because
  `vk::raii::Context`'s default constructor builds its dispatcher from vulkan.hpp's own
  `vk::detail::DynamicLoader`, which opens `vulkan-1.dll` / `libvulkan.so.1` itself. Removing
  the pair also removes an asymmetry: a throw from `SDL_CreateWindow` skips the destructor, so
  today's explicit load goes unpaired until `SDL_Quit`.

  Two things not to lose with it. The explicit load is what produces *"Failed to load Vulkan
  library!"* on a machine with no driver, where `SDL_CreateWindow` would fail with *"Failed to
  create window!"* instead — `SDL_GetError()` still names the real cause, so the message that
  survives should say so rather than blaming the window. And `SdlPlatform.h`'s class comment
  ("Because the destructor calls `SDL_Vulkan_UnloadLibrary()`, an `SdlPlatform` must outlive
  every object holding a Vulkan handle") needs rewriting rather than deleting: the ordering
  constraint is real, but its cause is `SDL_DestroyWindow` invalidating the surface, not the
  unload. The loader was never the reason — the app's own `DynamicLoader` holds a reference to
  the library whatever SDL does with its own.
- **`--present-mode`, and why the two failure policies differ.** The default stays what it is
  today: prefer mailbox, fall back to FIFO. **An explicitly requested mode that the surface
  does not offer is a hard error naming what was asked for and listing what is available** —
  never a silent downgrade. The whole reason to pass the flag is to test a specific mode, and
  a run that quietly measured a different one is worse than a run that refused: it produces a
  number that looks valid and is not.

  That is deliberately the opposite policy from `DeviceDesc::DisabledOptionalExtensions`,
  which reports and ignores a name it does not recognise. The cases differ: disabling an
  extension that was never present still achieves the intent, whereas asking for immediate
  and getting FIFO means the measurement is of something else.

  Two constraints on the implementation. **The default must stay a preference**, because only
  FIFO is guaranteed by the spec — mailbox is not, and a strict default would refuse to launch
  on a surface without it. And the *mode* is neutral vocabulary under D13 ("where only one API
  has the concept at all, its term stands"): Vulkan names these, D3D12 spells the same
  behaviour as `SyncInterval` plus `ALLOW_TEARING`, so this is `--present-mode` rather than
  `--vk-present-mode`.

  Reject `--present-mode` together with `--headless`, alongside the borderless/fullscreen
  check step 40a adds — an offscreen target does not present, so there is no mode to choose.

  **Log the mode that was actually chosen**, so a fallback is visible rather than inferred.
  The place for it is the existing one-line summary at the end of `SwapchainTarget::Create` —
  `"Swapchain: {}x{}, {} images"` — which becomes `"Swapchain: {}x{}, {} images, {}"`. Not
  surface creation: the surface exists before any mode is chosen, and `ChoosePresentMode` runs
  against `getSurfacePresentModesKHR` during swapchain creation, so the surface has nothing to
  report yet. `Create` is also called from `Recreate`, so the line already fires on every
  resize and fullscreen toggle and already carries an extent that changes each time — the mode
  rides along at no extra noise, and a mode that changed across a recreate shows up without a
  second log site or a "did it change" comparison.

  That one line covers both paths. An explicit mode that is unavailable throws before this
  point, naming what the surface offers; the default path cannot throw, so printing what it
  settled on is the only way a mailbox→FIFO fallback is ever visible.

  Worth pairing with the frame-time fix above: once the report carries real wall-clock
  timings, it should also carry the present mode, because two reports taken under different
  modes are not comparable.
