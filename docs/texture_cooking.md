# Texture cooking

Target Windows/Linux desktop GPUs with native BC support, checked through device/format
capabilities. Store native compressed blocks in KTX2 and load through libktx into the
existing backend-neutral RHI; upload every mip/subresource on Vulkan and D3D12, expose the
mip chain through views/samplers, and reconstruct BC5 normal Z in the shader. Existing
procedural/fallback textures need not pass through a filesystem cooker.

One standalone cooker provides single-file `cook` and recursive `cook-all` modes. A CMake
`cook-textures` target invokes batch mode once; calling CMake from code is unnecessary.
Discover PNG/JPG/DDS beneath a configurable source root, excluding generated outputs.
Code-triggered cooking uses the same logic and waits for completion when cooked output is
missing or stale. Deployment restrictions are deferred until there is a distributed app.
On-demand cooking runs synchronously on the requesting thread, launching and waiting for
Compressonator without a dedicated worker pool. Offline batch cooking processes textures
serially, using Compressonator's internal compression threads with a configurable thread
limit. Collect per-file failures, finish other batch items, and return a failing exit
status if any failed. Concurrent texture scheduling is a future optimization requiring
measurements; reuse Core's job-system implementation where useful and budget encoder
threads together with concurrent jobs. Do not occupy rendering workers with compression
process waits.

Use CompressonatorCLI for PNG/JPG compression and mip generation where its filtering is
appropriate. Normal-aware filtering and linear-light colour filtering are requirements;
verify tool capabilities rather than assuming a format flag implements them. Automate
libktx through the vcpkg `ktx` port and Compressonator through CMake and/or a custom host-tool
port. The exact Compressonator acquisition method remains an implementation investigation:
verify pinned binaries with checksums or build pinned sources, on both platforms. Offline
cooking requires no renderer/GPU initialization. Use CPU encoding initially for both
offline and on-demand cooking, avoiding graphics-context and GPU availability requirements
and GPU competition with rendering. GPU encoding is deferred until platform/format support,
quality and throughput have been measured on representative Windows/Linux systems.

### Source settings and outputs

Keep `content/textures/brick.png.json` beside `brick.png`, committing both. Generated output
lives under a separate configurable cooked root, mirroring paths and retaining the source
extension: `cooked/textures/brick.png.ktx2` and `brick.png.cook.json`. Ignore generated files
in Git; cook records need not be included in a runtime package.

Settings include `version`, `usage`, `format`, `colorSpace`, `mipmaps`, `flipNormalY`,
`alphaMode` and `alphaCutoff`. No pixel-based or other automatic usage inference. Defaults:

| Usage | Auto format | Auto colour space |
|---|---|---|
| `color` | BC7 | sRGB |
| `normal` | BC5 | linear |
| `scalar` | BC4 | linear |
| `packed` | BC7 | linear |

Unconfigured PNG/JPG defaults to colour, BC7 sRGB, full mip chain, opaque alpha mode and no
normal-Y flip. First cooking creates the settings file and warns about the assumption.
Warn for suspicious channel/settings combinations, including scalar inputs treated as
sRGB colour and channels discarded by the selected format. Warnings name the settings
file and advise modifying it and recooking. Invalid combinations fail before settings are
saved or output replaced; BC4/BC5 have no sRGB variants.

CLI exposes independent `--usage`, `--format` and `--color-space` overrides. Resolve saved
usage (or colour), apply CLI usage, resolve `auto` fields, then apply explicit overrides
and validate. Existing explicit settings remain explicit when usage changes. CLI changes
persist in the settings file; recook from the original source. Usage alone is sufficient;
`--format` changes compression only: a saved normal texture remains normal, while an
unconfigured texture remains colour. BC5 can store arbitrary two-channel data, so choosing
it does not establish normal usage or make a normal-only operation valid.

Single-file, batch and code-triggered cooking all use each texture's saved settings,
creating defaults only when its settings file is absent. Omitted CLI options preserve
saved values. Cooking checks the fingerprint and skips unchanged output; `--force` bypasses
that check without resetting settings.

`--flip-normal-y` takes no value and sets and persists `flipNormalY` to true. An enabled
flip is valid only for resolved normal usage. There is no negative flag and repeated flags
never toggle the setting. To disable a saved flip, edit `flipNormalY` to false in the JSON
and cook again; the future editor exposes this toggle.

For example, `cook brick.png --usage normal --flip-normal-y` selects normal defaults and
enables flipping. A later `cook brick.png`, `cook-all` or code-triggered cook retains that
texture's normal usage and enabled flip, and skips cooking if its fingerprint and output
are unchanged. Editing the JSON to disable flipping invalidates the fingerprint and
triggers recooking on the next cooking request.

Support new PNG/JPG encoding to BC1, BC4, BC5 and BC7. BC1 is a manual storage/quality
tradeoff, not the colour default. Explicit `alphaMode: mask` / `--alpha-mode mask` enables
binary-alpha BC1 encoding, with `alphaCutoff` / `--alpha-cutoff` defaulting to 0.5.
`--no-mips` persists `mipmaps: false` and outputs only the base level, including for DDS.
Until the alpha-mask follow-up lands, reject requests to generate masked mips, explaining
`--no-mips`; supplied DDS mips may be preserved. BC7 RGBA storage is supported, but
coverage-preserving mip processing and explicit material cutoff integration are deferred.

### DDS preservation and incremental cooking

Support BC1, BC2, BC3, BC4, BC5 and BC7 DDS repackaging/loading, preserving native format
(including signedness and colour-space interpretation), dimensions, faces, layers and
existing mip payloads. Generate/compress only missing mip levels, decoding an existing
level for filtering without recompressing retained levels. BC2/BC3 encoders are required
internally for this operation; direct PNG/JPG encoding to them is deferred. Explicit format
conversion is allowed but necessarily lossy; it must not happen silently on the preservation
path. Likewise, settings requiring pixel changes must not silently claim passthrough.

Incremental checks use a content fingerprint of source bytes, canonical effective settings,
cooker pipeline version and relevant dependency versions. Store that plus an output hash in
the generated cook record. Skip only when the fingerprint matches and the output is intact;
missing/damaged output or record, changed inputs/tools, or `--force` recooks. JSON whitespace
and source timestamps alone do not invalidate results. Use temporary outputs, validate before
publishing, replace atomically and serialize concurrent requests for the same output. An
interrupted cook must be recoverable without losing the last successful asset. Hash checks
belong to cooking requests. When the current engine/editor acquires a source asset, check
the input fingerprint and output hash, cook if necessary, wait and load the result; reuse
already-loaded assets without repeating checks on every use or frame. Direct cooked-KTX2
loading validates/loads with libktx without requiring source files or cook records.

Acceptance includes byte-identical retained DDS blocks for every supplied mip/face/layer,
equivalent important metadata, correct missing-mip generation, settings validation and
incremental invalidation, interrupted/concurrent cooking recovery, and compressed mip uploads
and sampling on both backends. Reassess/recapture the visual baseline deliberately because
compression, correct colour handling and mip sampling change pixels.
