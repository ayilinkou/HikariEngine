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

| Priority | Item | Where | Size | Blocked by |
|---|---|---|---|---|
| P1 | Correctness fixes from `suggested_work.md` §1.6 and §3.1 — §3.2 (batched uploads) is done | various | S–M each | |
| P1 | Restore `validate_best_practices` in `VulkanDevice.cpp`, commented out on 2026-09-05. vulkan-validationlayers 1.4.357.0 reads an image's last-used queue family in a maintenance9-gated branch of `BestPractices::ValidateImageInQueue` without checking it against `VK_QUEUE_FAMILY_IGNORED`, so the first use of any image in a submit segfaults inside the layer — every Debug run and two GPU tests. Fixed upstream by Vulkan-ValidationLayers PR #12922, merged after the 1.4.357.0 tag was cut, so no version vcpkg offers yet contains it — still 1.4.357.0 when last checked on 2026-09-12. **Not a one-line uncomment when the day comes.** Best practices emits performance *warnings*, so `counters.run.validationWarnings` moves off zero and the baseline has to be recaptured deliberately; nothing here has ever run with it on, so budget a triage pass over what it says and expect more `message_id_filter` entries beside the one already kept for it. Decide at the same time whether it is unconditional as sync validation is, or gets `--vk-best-practices on|off` beside Stage 7.6's `--vk-sync-validation` — and if it is flagged it needs a `run` report field and a gating-table entry, or the comparison tool compares `validationWarnings` across runs that differ in it | `VulkanDevice.cpp`, `vcpkg-configuration.json`, `tests/baseline/` | S | a vcpkg baseline offering vulkan-validationlayers newer than 1.4.357.0 |
| P2 | One capture per run, and a name that cannot hold two. `DrawFrame` stages a capture only while `m_bScreenshotBufferReady` is false, so a script asking for `screenshot` twice gets one file and no warning about the other; `Engine::Run` returns a single `CapturedFrame` and the app writes it once. The naming compounds it: `GenerateTimestamp()` is second-resolution and the PNG and the report share one stamp, so two runs a second apart overwrite each other, and per-capture files would collide the moment more than one is written. Wants a captures list keyed by frame, a name that includes the frame, and a dropped request that says so | `Engine.cpp`, `RunApp.cpp` | M | |
| P2 | `--present-mode <immediate\|mailbox\|fifo\|fifo-relaxed>`, defaulting to the preference chain; an explicit mode that the surface does not offer is a hard error | `rhi/IPresentTarget.h`, `SwapchainUtil.h`, `RunSpec` | S | |
| P2 | Document the matrix convention once and apply it consistently | `opaque.slang` header comment | S | |
| P2 | `.map` format `version` attribute | `XmlParser` | XS | |
| P2 | Record the GPU name, driver version, API version, OS, architecture **and which backend produced it** in the run report — two reports from different machines are otherwise comparable-looking and not comparable, and with `--backend` two from the same machine are too | `Engine.cpp`, `rhi/IDevice.h` | S | nothing — its blocker was "a seam decision", and Stage 7.5 is complete. `backend_readiness_plan.md` §6 places it in Stage 7.6 |
| P1 | A baseline comparison script — decode both PNGs, report the diff bounding box, and diff the report's `counters`. Today `CLAUDE.md` has to tell a human to drive PIL by hand, and that is not theoretical: the documented recipe compared RGBA images with `getbbox()`, which defaults to `alpha_only=True` and therefore inspected only the alpha channel. Every capture is fully opaque, so the check passed for any two images at all until it was corrected on 2026-09-05 | `tests/scripts/` | S–M | **scheduled in Stage 7.6**, which needs it with per-channel tolerance caps built in (backend readiness plan D26) — do it there rather than twice |
| P2 | A Debug build cannot start where `VK_LAYER_KHRONOS_validation` is not installed: validation goes into `requiredLayers`, so `VulkanDevice.cpp:604` throws `Required layer not supported` at instance creation rather than logging and continuing without it. Distinct from `--validation`, which landed in Stage 7.6 step 12 and decides whether the layer is *asked for* — this is what should happen when one that was asked for is simply absent, as it is on a fresh clone without the SDK's layers | `VulkanDevice.cpp` | S | |
| P3 | We declare a Vulkan 1.4 application but accept a 1.3 device. `VkApplicationInfo::apiVersion` is `VK_API_VERSION_1_4` (`VulkanDevice.h:254`, used at `VulkanDevice.cpp:1252`) while `IsPhysicalDeviceSuitable` accepts anything reporting 1.3 or higher (`VulkanDevice.cpp:1433`). Legal — the declared version is the highest the app is written against, and only the instance must support it — but nothing stops a 1.4-only core call being made on a 1.3 device, where it is a null function pointer rather than a diagnostic. No symptom today, and it would fail loudly rather than corrupt quietly, which is why this is P3 rather than higher. The fix is a choice rather than a correction: raise the suitability floor to 1.4 and give up 1.3 hardware, or declare 1.3 and treat 1.4 core as off-limits. Decide it against vulkan.gpuinfo.org rather than by preference. **The version decides limits, not only features:** the spec's Required Limits table raises `maxBoundDescriptorSets` from 4 to 7 and `maxPushConstantsSize` from 128 to 512 for a 1.4 implementation, so declaring 1.4 while accepting 1.3 means only the core floors can be relied on. Measured on 2026-09-12, an RX 580 on RADV advertises 1.4.354 and reports `maxPushConstantsSize` 256 — below the 1.4 floor — so the rule either way is to read the limit rather than infer it from the version | `VulkanDevice.cpp`, `VulkanDevice.h` | XS | |
| P3 | Check a pipeline layout's requested push-constant size and bind group count against the device's limits as it is created, and fail naming the limit, the request and what the device offers. Validation already rejects an over-limit layout, so in a Debug run this is only a better message; with validation off it is undefined behaviour with no diagnostic at all, which is the window this closes. Nothing can fail today — the push-constant blocks are far under the universal 128-byte floor and four bind groups sit inside the core guarantee of four sets — so it is insurance against a fifth bind group or a fatter block. Belongs at layout creation, not at physical device selection: selection does not yet know what will be asked for, so the numbers there would be constants typed beside the real usage and would drift from it | `VulkanDevice.cpp` | XS | |
| P3 | Record the OS *version* in the run report's `system` block, not only the compile-time platform name. Stage 7.6 step 6 writes `"Windows"`/`"Linux"` from a compile definition, which tells two machines of the same family apart not at all. Left out deliberately: `gpu` and `driver` already distinguish those machines, and the driver string is what actually changes rendering behaviour, so the OS version is a proxy for it at best — and identity never gates a comparison, so this field is read by a human and nothing else. Needs an `IPlatform` getter with a real implementation per platform: SDL's `SDL_GetPlatform()` returns the name only, so the version means `uname` on Linux and `RtlGetVersion` on Windows, the latter with the manifest caveat that makes the obvious call lie | `IPlatform`, `SdlPlatform`, `HeadlessPlatform`, `Engine.cpp` | S | its trigger, not a date: someone comparing two reports and unable to tell why they differ |
| P2 | `cmake/Format.cmake` globs only `src/` and `engine/*/{include,src}`, so `tests/` and `apps/` are formatted by neither `scripts/format.sh` nor `tests/scripts/format_check.sh`, and CI's green format job is silent about them. 21 of the 38 files under those two directories did not match the pinned clang-format when this was measured on 2026-09-12. Extending the glob is one line; the cost is that the same commit reformats 21 files, and a formatting commit that large wants to be on its own rather than riding along with a change someone has to review | `cmake/Format.cmake` | S | |
| P3 | `WriteRunReport` and `WriteCapturePng` both call `EnsureParentDirectoryExists` with the *default* output path rather than the one being written, so `--report /a/new/dir/x.json` fails to open the file instead of creating the directory, and the run reports the failure after it has already done the work. The default paths keep working, which is why nothing has noticed. Pass the path being written | `RunApp.cpp` | XS | |
| P3 | Extend step 48's reflection test to compare resource registers, spaces and kinds against the bind group layouts, not only field offsets and vertex inputs. The hazard is D29's: reorder two declarations in a shader and its registers shift while `Engine.cpp`'s `BindGroupLayoutBinding` arrays keep the old slots. Most of it is caught already — a type mismatch or a missing binding fails pipeline creation on both backends' validation, and two same-type resources swapped shows up at once in the baseline pixel comparison — so this buys a precise message rather than new coverage, which is why it is P3 and why step 48 was kept narrow. Cheap only once the layout arrays are reachable from a unit test | `tests/unit/`, `engine/engine/src/` | S | Stage 8, which splits `Engine.cpp` into passes that own their layouts |
| P2 | Namespace `engine/engine/src/`'s remaining types under `Hikari::Engine` | `engine/engine/src/` | M | Stages 8–9, which split them into modules a piece at a time |
| P2 | An engine config file feeding `EngineConfig`, with flags overriding it. Format, precedence, where the file is found, and how the effective values reach the run report are all open — design it when a setting actually needs persisting, rather than defaulting into a shape | `engine/engine` | M | `EngineConfig` existing (step 41) |
| P3 | Async compute: create a queue on the dedicated compute family so `QueueType::Compute` submits to it rather than resolving to the graphics queue. Today no compute queue object exists, so every dispatch is serialised with rendering. Not a defect — `GetQueueFamily` and `GetQueue` agree, and `Submit` rejects a list from the wrong allocator — but the overlap is unrealised. What follows is the cost: the cloud noise volume is written by compute and sampled by graphics, so it needs queue-family ownership transfers of the kind `VulkanUploadContext` already does for copies, plus cross-queue waits wired into the frame loop. `architecture_plan.md` §20 row 4 keeps multi-queue out of the neutral API until a pass needs it, and this is that decision | `VulkanDevice.cpp`, `CloudSystem` | L | **a measurement**: whether the cloud dispatch is a big enough slice of a frame to be worth overlapping. Nobody has taken it |
| P3 | `--gpu <name-substring>` device selection. Device creation takes the first suitable device in enumeration order, so a machine with both lavapipe and a real GPU gets whichever the loader lists first; CI pins the ICD with `VK_DRIVER_FILES` instead. Enumeration order is not a stable identifier, so a bare index is not the answer | `rhi/DeviceDesc.h`, `VulkanDevice.cpp` | S–M | |
| P3 | The ImGui panel has no regression coverage: the baseline is captured with `--no-ui`, deliberately, because a UI capture's hover highlight follows wherever the mouse was left | `tests/`, editor | M | Stage 7's `EditorLayer`, which can be driven without a mouse |
| P3 | Expose cloud push-constants in ImGui (`m_CloudData` is pushed but never written) | `CloudSystem` + editor UI | S | |
| P3 | `surface.slangh` to de-duplicate ~130 lines across the two surface shaders | `shaders/` | M | |
| P3 | Split `pbr.slangh` into `brdf`/`tonemap`/`phase` | `shaders/` | S | |
| P3 | `CubemapCreateInfo` → `std::array<std::string,6> FacePaths`, delete the 6-case switch | `CubemapLoader.cpp` | S | |
| P3 | Finish the skybox (loaded but never rendered) and reuse it for IBL | new pass | M–L | |
| P3 | A `ShaderLibrary` owning the shader name-to-bytes mapping, module caching and module lifetime, asking the device for its format. ~120–150 lines replacing about eight three-line load sequences, so barely shorter and only meaningfully cleaner once it owns a second responsibility. Deliberately not built in Stage 7.5 (D24): the engine-side helper that step 6 leaves behind is roughly sixty lines short of being it, so promoting it later is a rename and a move | `engine/engine/src/` | S–M | its trigger, not a date: the second shader responsibility — variants, permutations, or hot reload |
| P3 | Emissive and occlusion texture maps: raise `TextureBinding::COUNT` past 3. Nothing is currently held back — neither map is parsed by the loader, present in `MaterialData`, or referenced by a shader — so this is a feature rather than a deferred fix, which is why Stage 7.5 excluded it and why it is no longer a rider on step 70 as `suggested_work.md` §2.6 proposed. After Stage 7.5 step 5 it is an enum value, a layout entry, a `PBRMaterial` write and two shaders, with no descriptor-pool consequences. **Changes the baseline deliberately** | `Texture.h`, `PBRMaterial`, surface shaders | S–M | nothing; cheapest after Stage 7.5 step 5 |
| P3 | DXVK on Linux — run the D3D12 backend over Vulkan, so the second backend is exercisable without Windows. A stretch goal: it tests our D3D12 code against a translation layer's interpretation rather than against a real D3D12 runtime, so a pass proves less than it looks and a failure may be DXVK's | build, CI | L | Stage 7.7 existing at all |
| P2 | `tests/scripts/build_tests.sh` and its `.bat` build four of the seven test targets — `core_tests`, `platform_tests`, `rhi_tests`, `rhi_gpu_tests` — and miss `asset_tests`, `engine_tests` and `scene_tests`, although `CLAUDE.md` describes the script as building every test target. `scripts/precommit.sh` is unaffected only because `build.sh` has already built everything by the time it runs. On its own, the script leaves those three binaries as they were, so `run_scene_tests.sh` afterwards runs a stale `scene_tests` — or, if it was never built, finds no tests and exits 0, because `ctest` reports "No tests were found!!!" as success. An explicit list goes stale whenever a test target is added, so this wants something a new `engine_test` joins automatically rather than three more names; and the run scripts should treat finding no tests as a failure | `tests/scripts/`, `cmake/Testing.cmake` | XS | |
| P3 | CI throws away the evidence when a comparison fails. Stage 7.6's `TestSupport` writes actual, expected and an amplified diff PNG on a pixel mismatch, and `ci.yml` has no `upload-artifact` step at all, so on a runner those three files die with the job and a red scene test is readable only as text. Wants them uploaded from the Linux debug and ASan jobs when `ctest -L scene` fails | `.github/workflows/ci.yml` | S | Stage 7.6 step 2, which writes the images |

One of these is worth expanding on, because it carries a decision:

- **`--present-mode`, and why the two failure policies differ.** The default stays what it is
  today: the chain in `ChoosePresentMode` — mailbox, then immediate, then FIFO. **An explicitly
  requested mode that the surface does not offer is a hard error naming what was asked for and
  listing what is available** — never a silent downgrade. The whole reason to pass the flag is
  to test a specific mode, and a run that quietly measured a different one is worse than a run
  that refused: it produces a number that looks valid and is not.

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
