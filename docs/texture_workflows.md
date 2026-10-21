# Texture cooking workflows

The asset pipeline stores native BC blocks in KTX2. It supports PNG/JPG, supported DDS
and explicit six-face cubemap descriptors. The renderer uploads all supplied mips through
the RHI; the cooker itself requires no window or graphics device.

## Build the shared Release cooker

From the repository root, configure and build normally:

```sh
./build.sh ninja-debug-linux
```

On Windows use `build.bat ninja-debug-windows`. Every engine configuration provisions
the same Release host tool at `build/texture-cooker/HikariTextureCooker` (`.exe` on Windows),
including Debug and ASan applications. A build lock serializes access to that tool tree.
Use a separate `HIKARI_TEXTURE_COOKER_BUILD_DIR` for incompatible toolchains/generators.

After configuration, `cmake --build --preset ninja-debug-linux --target HikariTextureCooker`
builds only the host tool. Its independent project links Core, Platform and Asset, without
renderer, backend or shader targets. Dependencies and encoder binaries are pinned by
vcpkg and the texture tool checks; no system encoder on PATH is substituted.

## Offline and single-file cooking

The following examples use the Linux executable path; append `.exe` on Windows.

```sh
build/texture-cooker/HikariTextureCooker cook textures/brick.png --source-root content
build/texture-cooker/HikariTextureCooker cook-all --source-root content --quality 1
build/texture-cooker/HikariTextureCooker cook textures/brick.png --source-root content --dry-run
```

Regular CLI/CMake cooking defaults to quality `1.0`. Auto colour uses BC7; normal, scalar
and packed usages default to BC5, BC4 and BC7 respectively. DDS preserves native compressed
formats/blocks unless authored operations require conversion or missing-mip completion.
The default cooked root is the resolved source root's sibling `cooked/`, and
`--cooked-root <dir>` overrides it. Paths retain their source extensions, for example
`cooked/textures/brick.png.ktx2` and `brick.png.cook.json`.

Each source uses `<image>.texture.json`. First cooking creates missing settings and warns
about assumed colour usage. PNG/JPG defaults to opaque; DDS can supply or leave alpha
interpretation unresolved. Review and commit these sidecars with the sources. For
example, this single-file override authors normal usage and persists the Y flip:

```sh
build/texture-cooker/HikariTextureCooker cook textures/normal.png --source-root content --usage normal --flip-normal-y
```

Omitted settings are preserved. Explicit single-file settings overrides are saved;
numeric quality, force and thread limits are operational options. Batch cooking rejects
per-texture overrides. `--force` recooks at the exact requested quality, whereas ordinary
requests reuse compatible higher-quality outputs. `--threads 0` selects automatic encoder
threading; explicit limits are `1..128`. `--quiet` suppresses single-file success/unchanged
summaries while keeping errors and runtime cooking progress.

To create templates without source images, use `init-texture brick.png` or
`init-cubemap sky` from an existing directory. Complete the cubemap's six face paths before
cooking it. Templates preserve existing files unless `--overwrite` or `--force` is given.

CMake exposes the same offline batch workflow:

```sh
cmake --preset ninja-debug-linux -DHIKARI_TEXTURE_ENCODING_QUALITY=1.0
cmake --build --preset ninja-debug-linux --target cook-textures
```

Its other options are `HIKARI_TEXTURE_SOURCE_ROOT`, `HIKARI_TEXTURE_COOKED_ROOT` and
`HIKARI_TEXTURE_ENCODER_THREADS`. Cooking is explicit rather than part of every build.

## Runtime and code acquisition

Both applications accept `--cooked-root` and `--texture-quality <0..1>` (default `1.0`).
Runtime prefers BC1 for authored auto colour pixel inputs with known opaque alpha,
including descriptor cubemaps. Explicit formats, non-colour usages, masks, unknown DDS
alpha and native compressed DDS retain their existing rules.

| Source request | Result |
|---|---|
| Matching intact cooked output meets the minimum quality | Reused; eligible runtime requests also accept the offline BC7 recipe |
| Missing/stale output, existing settings | Synchronous Release cook using saved settings and runtime policy |
| Source exists, no cooked output or settings | Created authored defaults with `format: auto`, then cooked with runtime policy |
| Missing source, invalid settings or failed cook | Logged failure and cached magenta fallback; retried on later acquisition |
| Direct `.ktx2` request | Loaded from the cooked root without source files, records or cooker |

The default sidecar keeps `auto`; runtime does not save BC1 into that field. A later
offline BC7 cook upgrades runtime BC1 from source, and runtime can reuse that offline
result without downgrading it. Format participates in the fingerprint; the native KTX2
header identifies actual storage. The record includes actual quality, dependency/encoder
identity and the output hash. Legacy records recook. Per-output locking and recoverable
publication protect concurrent/interrupted requests.

When a runtime cook really begins, the engine immediately shows a message such as:

```text
[Texture Cooker] Cooking texture /content/textures/brick.png at runtime: BC1, quality 1
```

The message follows freshness checks under the cook lock. Warm skips, compatible BC7 reuse,
direct cooked loads and live registry-cache hits do not claim to cook. A failed encoding
attempt can still have a start message; its error follows through the usual diagnostics.
Fallback 2D/cubemap textures are generated as magenta pixels in memory and are never cooked.

Code uses the same services:

```cpp
// Offline CPU cooking, with saved settings and the usual BC7 colour default.
Hikari::Asset::TextureCooker cooker(contentRoot, cookedRoot);
auto result = cooker.Cook({.Source = "textures/brick.png", .EncodingQuality = 1.0});

// Runtime acquisition launches the shared Release executable synchronously.
Hikari::Asset::TextureSource source(contentRoot, cookedRoot, releaseCookerPath, 1.0);
std::string error;
std::vector<std::string> warnings;
auto texture = source.Read("textures/brick.png", error, warnings);

// A package can acquire KTX2 directly, without a source/record/cooker dependency.
auto packaged = source.Read("textures/brick.png.ktx2", error, warnings);
```

Check failure results before using them. The registry owns live resource caching and
fallbacks; `TextureSource` alone returns data or an error. Packaging KTX2 files does not
rewrite scene/model source references: requests must explicitly name the cooked KTX2
to use source-free acquisition.

## Delivery verification and baseline approval

`python scripts/precommit.py` runs the build, header/static checks,
unit tests, GPU tests, scene tests and formatting. The `texture_delivery` unit gate runs
the real shared Release tool with display variables cleared and an invalid Vulkan ICD.
It covers clean batch output, warm skips, quality upgrades, native DDS preservation,
runtime BC7 reuse, source-free acquisition and runtime log forwarding/error capture.

```sh
ctest --test-dir build/ninja-debug-linux -L texture-delivery --no-tests=error --output-on-failure
```

Use `build/ninja-debug-windows` on Windows. CI includes this gate in every Linux/Windows
Release, Debug and ASan unit run. Vulkan GPU/scene coverage runs on lavapipe; Windows
D3D12 coverage runs on pinned WARP with full GPU validation. CI requires a usable device,
and missing test labels fail. Local skips without an ICD do not establish GPU correctness.

CI scene smoke tests set `HIKARI_SCENE_TEXTURE_QUALITY=0`, using the lowest encoding effort
for the full-size shipped assets. This test-only override reaches in-process scenes and
launched headless runs through the application's normal quality option. Production and
local scene-test defaults remain `1.0`. Eligible runtime colours already use BC1; explicit
formats and normal/scalar/packed semantics are preserved. The small codec, default-quality
and upgrade tests retain their requested qualities, including real BC7 at `1.0`.

Shipped scene KTX2 files and records are cached per OS between CI runs. Every restored
asset still passes the usual source/settings/tool fingerprint, minimum-quality and output
hash checks. Only successful scene runs save these outputs; lock/transaction files are
excluded. Unit/delivery fixtures use isolated temporary roots and still cook from scratch.
The delivery gate uses one-pixel colour inputs and base-only cubemap faces to limit encoder
process launches; dedicated filtering/upload tests cover full mip chains.

## Runtime acquisition cost

The engine loads `textures/skybox/sky.cubemap.json` even when no scene is selected. That
shipped descriptor explicitly selects BC7: six 2048×2048 faces with all mips upload about
32 MiB. Its source cooking, freshness checks, loading and GPU upload happen after the UI
initialization message. ImGui's font textures are managed by ImGui rather than this asset
cooker. A cook-start message distinguishes a recook from a warm load. Authored
`format: auto` would allow runtime BC1, but the existing explicit BC7 choice is preserved.

TC18 profiling found that repeated range insertion while assembling native blocks was
expensive in Debug builds. The KTX2 reader now allocates the payload once and bulk-copies
each face/mip, retaining container and subresource bounds validation. On the local software
Vulkan run, the existing 32 MiB skybox's KTX2 read dropped from about 1.5 s to 37 ms;
the complete warm skybox load dropped from 1.8 s to 328 ms without changing its BC7 blocks.
These measurements describe that local run. The temporary profiling timers and skybox
loading message were removed after diagnosis.

## Hardware verification

On a Windows hardware machine, after building, run:

```powershell
python scripts/precommit.py ninja-debug-windows
python tests/scripts/backend_compare.py ninja-debug-windows
```

The backend comparison requires both Vulkan and D3D12 on the same adapter. Preserve
`tests/reports/backend_*.json` and `tests/screenshots/backend_*.png` for review.

For the committed baseline, use `python3 tests/scripts/baseline_test.py` on the original Linux
GPU/display, with its matching build configuration. The committed capture uses 1,000
frames, fixed timestep/camera and a 1920×1080 borderless editor window. Inspect the new
image/diff and explain intended compression, mip-sampling and normal-decoding changes
before approving promotion. Keep cold-runtime BC1 comparisons separate from offline BC7
references so the cache's existing format does not obscure what was measured. A software
GPU or a different frame count cannot approve this hardware baseline. Do not pass
`--update` until the concrete comparison has been reviewed and approved.

The stage's completion evidence and remaining gates are tracked in
[texture_cooking.md](texture_cooking.md#tc18-cross-platform-delivery-and-completion-gate).
Known follow-ups include DDS alpha-aware missing-mip completion, coverage-preserving masked
mips/material cutoff integration, editor controls, async acquisition, HDR/BC6H and advisory
cubemap grouping hints; see [the backlog](backlog.md). Unresolved DDS alpha still produces
the documented ordinary-filtering warning and remains a correctness follow-up.
