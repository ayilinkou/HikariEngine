# TC17 encoding quality review

Linux and Windows measurements have been reviewed. On 15 October 2026 the user confirmed
that the Windows rendered comparisons looked acceptable and selected BC1 at quality `1.0`
for eligible runtime opaque colour textures/cubemaps. Offline colour cooking retains BC7
as its auto default. Authored explicit formats and alpha policy remain authoritative.

The manual tools are `tests/scripts/texture_quality_benchmark.py` and
`HikariTextureLoadProbe`. They use isolated source/settings copies and cooked roots,
comparing auto format at quality `1.0` and `0.5`, plus explicit BC1 at quality `1.0` for
authored opaque colour textures/cubemaps. The shipped source settings are preserved.
BC5 normals and packed material data do not participate in the BC1 comparison.

## Linux observations

Saved on 13 October 2026 using the shared Release cooker, 16 logical CPUs and an
RX 580 through RADV Mesa 26.2.4. Each source is 2048×2048; the cubemap has six such faces.
One cold cook per variant and three warm checks were recorded. No OS cache flushing was
performed. These observations are not a statistical benchmark.

| Asset | Format / quality | Cold cook + CPU/GPU load | Warm check median | KTX2 MiB | Base RGB PSNR |
|---|---|---:|---:|---:|---:|
| Colour | BC7 / 1.0 | 11.468 s | 47.25 ms | 5.33 | 60.32 dB |
| Colour | BC7 / 0.5 | 4.311 s | 43.25 ms | 5.33 | 55.69 dB |
| Colour | BC1 / 1.0 | 2.092 s | 28.95 ms | 2.67 | 46.20 dB |
| Normal | BC5 / 1.0 | 2.102 s | 45.22 ms | 5.33 | 55.05 dB |
| Normal | BC5 / 0.5 | 2.071 s | 44.44 ms | 5.33 | 52.98 dB |
| Packed | BC7 / 1.0 | 25.597 s | 62.88 ms | 5.33 | 48.66 dB |
| Packed | BC7 / 0.5 | 8.826 s | 60.57 ms | 5.33 | 48.53 dB |
| Cubemap | BC7 / 1.0 | 113.916 s | 195.48 ms | 32.00 | 57.05 dB |
| Cubemap | BC7 / 0.5 | 45.988 s | 194.69 ms | 32.00 | 53.16 dB |
| Cubemap | BC1 / 1.0 | 26.667 s | 114.20 ms | 16.00 | 45.47 dB |

BC7 quality `0.5` reduced cold time substantially on these assets. BC5 normal time changed
little; mean normal angular error was 0.105° at `1.0` and 0.128° at `0.5`. BC1 was faster
and halved compressed storage for these opaque colour assets, with lower RGB PSNR.
Visual acceptance still requires inspection; these numbers alone do not choose a policy.

Cold-to-GPU is the CLI cooking wall time plus CPU reading and GPU creation/upload/flush
measured in a separate process. Device startup, decoding previews and rendering review
images are excluded. It is not an engine startup measurement. A warm check measures
fingerprint/output validation, not a scene reload.

The interactive local page is `tests/reports/tc17-linux-quality/review.html`. It compares
variants with a split slider, native-resolution image links, downsampling choices and all
six cubemap faces. Decoded previews show stored mips 0, 2 and 4. Rendered views draw the
complete texture at native, quarter and sixteenth resolution, using implicit derivatives
and the production anisotropic sampler; cubemap filtering can blend neighbouring levels.
Normal views use the shared decoder and a fixed diffuse light. They are texture review
renders rather than full PBR scene captures. Colour/lit-normal renders are encoded for
sRGB display; packed channels are displayed numerically.

Rendered evidence was added on 14 October using software Vulkan (lavapipe), separately
from the original RX 580 load measurements. All ten variants produced 75 images with
zero validation errors or warnings. The original 126 measurement/input/output files
were checked by SHA-256 before and after rendering and were unchanged. The per-variant
`render-probe.json` files and `render-results.json` identify the rendering device.
Generated reports and images remain ignored by Git.

## Windows observations

The supplied `tests/reports/tc17-windows-quality/review.html` embeds measurements from
14 October 2026: Windows 10, 16 logical CPUs, RX 580, D3D12 driver 31.0.21925.1001 and the
shared Release cooker. Its generated image folders were not copied to this workspace;
visual acceptance was confirmed by the user after viewing them on Windows. All ten
variants report zero validation errors/warnings and one upload submission. Their decoded
RGB/normal quality metrics match the Linux measurements.

| Asset | Format / quality | Cold cook + CPU/GPU load | Warm check median | KTX2 MiB | Base RGB PSNR |
|---|---|---:|---:|---:|---:|
| Colour | BC7 / 1.0 | 18.107 s | 93.17 ms | 5.33 | 60.32 dB |
| Colour | BC7 / 0.5 | 11.318 s | 98.06 ms | 5.33 | 55.69 dB |
| Colour | BC1 / 1.0 | 7.416 s | 73.38 ms | 2.67 | 46.20 dB |
| Normal | BC5 / 1.0 | 7.367 s | 95.81 ms | 5.33 | 55.05 dB |
| Normal | BC5 / 0.5 | 7.673 s | 104.01 ms | 5.33 | 52.98 dB |
| Packed | BC7 / 1.0 | 31.800 s | 117.02 ms | 5.33 | 48.66 dB |
| Packed | BC7 / 0.5 | 13.573 s | 115.94 ms | 5.33 | 48.53 dB |
| Cubemap | BC7 / 1.0 | 162.122 s | 356.88 ms | 32.00 | 57.05 dB |
| Cubemap | BC7 / 0.5 | 82.946 s | 353.40 ms | 32.00 | 53.16 dB |
| Cubemap | BC1 / 1.0 | 63.779 s | 213.15 ms | 16.00 | 45.47 dB |

BC1 reduced colour and cubemap cold cook/load time by about 59% and 61%, respectively,
while halving compressed storage. These single samples support the chosen default but
do not establish timing guarantees. Lowering BC5 quality did not improve normal cooking
time here. The same timing exclusions and warm-check limits described above apply.

## Run on Windows

From the repository root in PowerShell, with the normal Windows build prerequisites
installed (`VCPKG_ROOT` and Python 3 on PATH):

```powershell
.\build.bat ninja-debug-windows
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
python tests/scripts/texture_quality_benchmark.py `
  --cooker build/texture-cooker/HikariTextureCooker.exe `
  --probe build/ninja-debug-windows/HikariTextureLoadProbe.exe `
  --backend D3D12 `
  --output tests/reports/tc17-windows-quality
```

Use a new output directory; existing measurements are never overwritten by a fresh run.
The script cooks colour, normal, packed and six-face cubemap cases and opens no window.
The build always supplies the shared Release cooker even when the probe is Debug.
Probe validation is enabled, including full D3D12 GPU validation. Device startup and
review rendering are outside the recorded load timing. The report identifies whether
the chosen adapter is hardware or WARP; do not equate their GPU load timings.
Send `results.json` for numerical comparison, or the complete output directory to include
`review.html` and its images. Keep the directory structure when sharing the HTML.

## Run on Linux or add rendered views to saved results

```sh
./build.sh ninja-debug-linux
python3 tests/scripts/texture_quality_benchmark.py \
  --cooker build/texture-cooker/HikariTextureCooker \
  --probe build/ninja-debug-linux/HikariTextureLoadProbe \
  --backend Vulkan --output tests/reports/tc17-linux-quality-new
```

To render previously measured outputs without rerunning cooking or replacing their
original timing/decoded-preview reports:

```sh
python3 tests/scripts/texture_quality_benchmark.py \
  --probe build/ninja-debug-linux/HikariTextureLoadProbe \
  --backend Vulkan --output tests/reports/tc17-linux-quality --render-existing
```

This writes separate `render-probe.json`/`render-results.json` reports, rendered images,
and an updated `review.html`. It keeps `results.json`, original per-case reports,
source/settings copies, cooked outputs and recorded timing logs intact. An explicit
`VK_DRIVER_FILES` can select a software ICD for image generation when no GPU is available;
do not present that load time as the original hardware's result.

## Agreed runtime policy

Runtime defaults to quality `1.0`; both applications accept `--texture-quality <0..1>`.
The shared request's `bRuntime` and the single-file CLI's `--runtime` select BC1 only for
authored `format: auto`, colour usage and known opaque alpha when encoding pixel inputs.
This includes ordinary 2D images, descriptor cubemaps and opaque pixel DDS. Native
compressed DDS blocks retain their format. Explicit formats, normals, scalar/packed data
and masked/unknown-alpha inputs continue to use their existing rules. Regular CLI and
CMake cooks still default to quality `1.0` and BC7 for auto colour.

Runtime format and numeric quality are operational options. Existing sidecars/descriptors
are preserved byte-for-byte unless the caller supplies an explicit settings override.
When settings are absent, first cooking still creates the usual authored defaults,
including `format: auto`; it never saves the runtime BC1 choice into that field. Saving
application-wide preferences belongs to the deferred editor settings work.

The effective format is part of the cook fingerprint, and KTX2's native format identifies
the stored blocks. The versioned record also retains actual quality, encoder/dependency
identity and the output hash. Eligible runtime requests accept either their BC1 recipe
or the compatible offline BC7 recipe when the output is intact and meets the requested
minimum quality. Offline BC7 requests recook runtime BC1 from source. Force requests use
their exact format/quality; legacy records remain invalid for reuse. Both paths use the
same cook lock, freshness checks and safe publication.

TC18 retains cross-platform verification of this runtime implementation and baseline
review/promotion. It also includes the requested informational runtime-cooking log:
identify the source, actual selected format and quality when cooking really occurs;
unchanged checks and direct cooked loads must not claim to cook a texture.

## Implementation verification

The 15 October Linux Debug precommit passed 463 unit checks, 50 Vulkan GPU checks and
13 scene checks with no skips, using software Vulkan (llvmpipe) because the workspace
has no physical GPU. Formatting also passed the repository's pinned clang-format 22.1.8.
New cases cover eligibility, native DDS preservation, missing-settings creation, quality
validation/forwarding, offline upgrades, higher-quality BC7 reuse, force, damaged output
and concurrent runtime/offline requests. The existing descriptor-cubemap upload check
now expects BC1 and its eight-byte blocks under the approved runtime policy.

An isolated 1920×1080, three-frame fixed-camera scene comparison is saved under
`tests/reports/tc17-runtime-regression/`. One run reused offline BC7; the other removed
only seven eligible colour outputs from a copied cooked root and cooked BC1 at quality
`1.0`. Both reports have zero validation errors/warnings and matching counters. The
expected image difference is 62,260 pixels (3.0025%), with maximum channel delta 9,
confined to the textured models. The source sidecars and original cooked tree were checked
by SHA-256 and are unchanged. The generated comparison images are available for review.

The committed-baseline comparison returned a skipped verdict because the capture used
three frames rather than the baseline's 1,000. It is not a baseline pass, and no baseline
was promoted. TC18 retains a matching baseline capture/review and Windows execution of
the newly added runtime policy tests.
