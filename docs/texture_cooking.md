# Texture cooking

**Stage 7.8 — TC1 implemented; Linux verification complete, native Windows verification
pending the manual Windows boot. TC2 is next.** Runs after the
completed Stage 7.7 and before Stage 8. The numbered design decisions record the agreed
behaviour. TC1 is a dependency/capability gate: verify acquisition and filtering support
before downstream implementation, and reopen the design if those facts require a change to
the agreed scope. Stage-local TC numbers do not renumber the architecture plan's steps
48–76.

## Contents

1. [Design decisions](#1-design-decisions)
2. [Stage 7.8 incremental work order](#stage-78-incremental-work-order)
3. [Follow-ups outside Stage 7.8](#follow-ups-outside-stage-78)

---

## 1. Design decisions

Decision identifiers TC-D1–TC-D14 are stable references for the tasks below.

**TC-D1 — Native BC KTX2 through the neutral RHI.**

Target Windows/Linux desktop GPUs with native BC support, checked through device/format
capabilities. Store native compressed blocks in KTX2 and load through libktx into the
existing backend-neutral RHI; upload every mip/subresource on Vulkan and D3D12, expose the
mip chain through views/samplers, and reconstruct BC5 normal Z in the shader. Existing
procedural/fallback textures need not pass through a filesystem cooker.

Acceptance includes byte-identical retained DDS blocks for every supplied mip/face/layer,
equivalent important metadata, correct missing-mip generation, settings validation and
incremental invalidation, interrupted/concurrent cooking recovery, and compressed mip
uploads and sampling on both backends. Reassess/recapture the visual baseline deliberately
because compression, correct colour handling and mip sampling change pixels.

**TC-D2 — Source settings and separate cooked outputs.**

Keep `content/textures/brick.png.json` beside `brick.png`, committing both. Generated
output lives under a separate configurable cooked root, mirroring paths and retaining the
source extension: `cooked/textures/brick.png.ktx2` and `brick.png.cook.json`. Ignore
generated files in Git; cook records need not be included in a runtime package.

The default cooked root is a sibling of the resolved content root:
`<content-root-parent>/cooked/`. Thus `HikariEngine/content/` maps to
`HikariEngine/cooked/`, regardless of the working directory. The cooker and engine accept
`--cooked-root` to override it and use the same source-relative output mapping. Cooking
into an unwritable root fails with an actionable diagnostic; automatic user-cache
selection is deferred. Distinct content roots sharing a parent also share this default
output root; use explicit cooked roots to avoid collisions in that layout.

Settings include `version`, `usage`, `format`, `colorSpace`, `mipmaps`, `flipNormalY`,
`alphaMode` and `alphaCutoff`. No pixel-based or other automatic usage inference.
Defaults:

| Usage | Auto format | Auto colour space |
|---|---|---|
| `color` | BC7 | sRGB |
| `normal` | BC5 | linear |
| `scalar` | BC4 | linear |
| `packed` | BC7 | linear |

Unconfigured PNG/JPG defaults to colour, BC7 sRGB, full mip chain, opaque alpha mode and
no normal-Y flip. First cooking creates the settings file and warns about the assumption.
Warn for suspicious channel/settings combinations, including scalar inputs treated as sRGB
colour and channels discarded by the selected format. Warnings name the settings file and
advise modifying it and recooking. Invalid combinations fail before settings are saved or
output replaced; BC4/BC5 have no sRGB variants.

**TC-D3 — Preserve saved settings and apply explicit single-file overrides.**

CLI exposes independent `--usage`, `--format` and `--color-space` overrides. Resolve saved
usage (or colour), apply CLI usage, resolve `auto` fields, then apply explicit overrides
and validate. Existing explicit settings remain explicit when usage changes. CLI changes
persist in the settings file; recook from the original source. Usage alone is sufficient;
`--format` changes compression only: a saved normal texture remains normal, while an
unconfigured texture remains colour. BC5 can store arbitrary two-channel data, so choosing
it does not establish normal usage or make a normal-only operation valid.

Single-file, batch and code-triggered cooking all use each texture's saved settings,
creating defaults only when its settings file is absent. Omitted CLI options preserve
saved values. Cooking checks the fingerprint and skips unchanged output; `--force`
bypasses that check without resetting settings.

`--flip-normal-y` takes no value and sets and persists `flipNormalY` to true. An enabled
flip is valid only for resolved normal usage. There is no negative flag and repeated flags
never toggle the setting. To disable a saved flip, edit `flipNormalY` to false in the JSON
and cook again; the future editor exposes this toggle.

For example, `cook brick.png --usage normal --flip-normal-y` selects normal defaults and
enables flipping. A later `cook brick.png`, `cook-all` or code-triggered cook retains that
texture's normal usage and enabled flip, and skips cooking if its fingerprint and output
are unchanged. Editing the JSON to disable flipping invalidates the fingerprint and
triggers recooking on the next cooking request.

**TC-D4 — CPU encoding, serial batches and synchronous on-demand cooking.**

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

**TC-D5 — Preserve compressed DDS and accept common 8-bit pixel layouts.**

Support BC1, BC2, BC3, BC4, BC5 and BC7 DDS repackaging/loading, preserving native format
(including signedness and colour-space interpretation), dimensions, faces, layers and
existing mip payloads. Generate/compress only missing mip levels, decoding an existing
level for filtering without recompressing retained levels. BC2/BC3 encoders are required
internally for this operation; direct PNG/JPG encoding to them is deferred.

Also accept common uncompressed 8-bit RGB/BGR/RGBA/BGRA DDS layouts, including supported
cubemap inputs. Decode channel layout and row pitch into canonical pixels, then reuse the
image cooking/filtering/compression path with authored settings. Uncompressed payloads are
not covered by the compressed-block preservation guarantee. Reject unusual channel masks,
unsupported layouts, higher-precision and HDR DDS explicitly. Include layout, row-pitch
and malformed-input fixtures rather than attempting every uncompressed variant.

**TC-D6 — Content fingerprints, output hashes and recoverable publication.**

Incremental checks use a content fingerprint of source bytes, canonical effective
settings, cooker pipeline version and relevant dependency versions. Store that plus an
output hash in the generated cook record. Skip only when the fingerprint matches and the
output is intact; missing/damaged output or record, changed inputs/tools, or `--force`
recooks. JSON whitespace and source timestamps alone do not invalidate results. Use
temporary outputs, validate before publishing, replace atomically and serialize concurrent
requests for the same output. An interrupted cook must be recoverable without losing the
last successful asset. Hash checks belong to cooking requests. When the current
engine/editor acquires a source asset, check the input fingerprint and output hash, cook
if necessary, wait and load the result; reuse already-loaded assets without repeating
checks on every use or frame. Direct cooked-KTX2 loading validates/loads with libktx
without requiring source files or cook records.

**TC-D7 — Explicit masked cooking without generated masked mips.**

Support new PNG/JPG encoding to BC1, BC4, BC5 and BC7. BC1 is a manual storage/quality
tradeoff, not the colour default. Explicit `alphaMode: mask` / `--alpha-mode mask` enables
binary-alpha BC1 encoding, with `alphaCutoff` / `--alpha-cutoff` defaulting to 0.5.
`--no-mips` persists `mipmaps: false` and outputs only the base level, including for DDS.
Until the alpha-mask follow-up lands, reject requests to generate masked mips, explaining
`--no-mips`; supplied DDS mips may be preserved. BC7 RGBA storage is supported, but
coverage-preserving mip processing and explicit material cutoff integration are deferred.

**TC-D8 — Explicit cubemap descriptors and cross-face mip filtering.**

Support ordinary 2D textures and single cubemaps in this stage. Texture arrays, cubemap
arrays and volumes are deferred; reject those DDS/KTX2 shapes explicitly. Cubemap cooking
must preserve face order/orientation and supplied face/mip payloads. Accept cubemap DDS
and a cubemap JSON descriptor listing six explicitly identified face paths plus shared
texture settings. A descriptor produces one KTX2 cubemap; its input fingerprint covers all
six source images and effective descriptor settings. Recursive discovery includes cubemap
descriptors named `*.cubemap.json`, with face paths relative to the descriptor's
directory. Use the descriptor path as the source asset identity: cooking
`content/textures/skybox/sky.cubemap.json` produces
`cooked/textures/skybox/sky.cubemap.ktx2` and `sky.cubemap.cook.json`. Source-based
runtime loading requests the content-relative descriptor path; direct cooked loading
requests the corresponding cooked-relative KTX2 path. No separate name field is required.

Discovery first validates descriptors and collects referenced face paths, then discovers
standalone images while excluding those references. References from a failed descriptor
remain excluded rather than silently cooking as unrelated 2D textures. Explicit
single-file cooking can still request a face as a 2D texture. Without a descriptor, face
images are ordinary 2D inputs; no grouping is inferred. Author the existing skybox
descriptor during content migration before the first batch cook and update its loader to
request that asset. Descriptors use `faces` keys `positiveX`, `negativeX`, `positiveY`,
`negativeY`, `positiveZ`, `negativeZ`, with the same top-level version/texture settings
fields as 2D settings. Store faces in DDS/KTX2 order +X, -X, +Y, -Y, +Z, -Z and use
standard sampler-compatible face orientation (KTX `rd`: columns right, rows down). No
filename-based front/back convention is imposed on new assets. Preserve the current skybox
assignment during migration: right → +X, left → -X, top → +Y, bottom → -Y, back → +Z,
front → -Z. Verify per-face orientation using labelled directional images on both
backends; no automatic image rotations or flips are inferred. Newly generated cubemap mip
levels require cross-face filtering with consistent face orientation. Retain supplied DDS
levels byte-for-byte; cross-face filtering does not repair seams in those retained levels.

**TC-D9 — Texture settings are authoritative over material-slot expectations.**

Texture settings remain authoritative when a material slot expects a different usage. Warn
with the texture path, settings path, expected usage and recooking guidance, then continue
loading with the saved settings. Do not infer usage or override settings from the slot.
Validate semantic compatibility rather than enforcing its default compression: a linear
normal texture explicitly encoded as BC7 can still be valid in a normal slot. Invalid
settings or unsupported formats remain cooking/loading failures; this warning does not
waive those validations. During migration, author correct settings for shipped normal and
packed textures instead of relying on material-slot inference.

**TC-D10 — Positive-Z tangent-space normals with vector-filtered mips.**

Normal usage supports tangent-space normals with positive Z. Newly encoded BC5 normals use
UNORM XY: interpret source RGB in [0, 1] as signed XYZ in [-1, 1], filter and normalize
vectors when generating mips, and encode XY back to UNORM. At sampling, unpack XY and
reconstruct `Z = sqrt(max(0, 1 - X*X - Y*Y))`; normalize the resulting direction as
needed. An explicit BC7 normal override retains XYZ and uses three-channel unpacking.
Preserve signed BC5 DDS as SNORM, sampling signed XY without the UNORM remap before
reconstructing Z. Object-space and negative-Z normal representations are outside this
stage's scope.

For normal mips, average decoded vectors over the mip filter footprint, then normalize. If
the averaged vector's length is effectively zero, use the flat tangent-space normal `(0,
0, 1)` instead of normalizing an unstable vector. Guard normalization against non-finite
results. Normal-variance-based roughness adjustment and more advanced filters are
deferred.

**TC-D11 — Explicit DDS conversion and metadata interpretation.**

Explicit format conversion is allowed but necessarily lossy; it must not happen silently
on the preservation path. An explicitly selected different format or pixel-changing
operation such as `flipNormalY: true`, whether saved in settings or supplied by CLI,
authorizes decoding and recompression without a second confirmation flag. Warn that
existing levels will be recompressed and are no longer covered by the byte-preservation
guarantee. Usage alone does not authorize recompression; it guides filtering of missing
levels. Fingerprints record effective operations so an unchanged converted output is not
repeatedly cooked.

For DDS with explicit colour-space metadata, preserve that interpretation by default. For
legacy DDS without it, resolve colour space from configured usage (`color` → sRGB;
`normal`, `scalar`, `packed` → linear) and warn about the assumption. An explicit
`colorSpace` override takes precedence. Changing linear/sRGB interpretation alone does not
decode or recompress blocks; update the container format/metadata while preserving
payloads. Still reject colour-space/format combinations that have no valid native format.

**Known alpha limitation — must be resolved:** when DDS alpha usage is unspecified,
generate missing mips using ordinary filtering rather than skipping completion. This can
break masked foliage silhouettes: averaging alpha can shrink or remove leaves at distance.
Warn prominently whenever generating levels for an alpha-capable DDS whose alpha use is
unresolved. Name the source/settings file and state that ordinary filtering does not
preserve alpha-test coverage; advise explicit mask settings and `--no-mips` if needed. Do
not label this path alpha-correct or infer material semantics from pixel contents.
Preserve supplied blocks and explicit alpha metadata without inventing missing metadata.
Explicit mask usage still rejects generated mips until coverage-preserving processing
lands. Retained levels may already contain this defect; preserving them does not repair
it.

**TC-D12 — Cooking failures use fallback textures.**

If on-demand cooking fails, log an error and return the existing fallback texture so scene
loading can continue. Keep the last successful cooked file on disk, but do not load it
silently when known to be stale. Offline single-file/batch commands return failure through
their exit codes. A failed cook never updates the record to claim fresh output.

**TC-D13 — Automatically provision pinned cooking dependencies.**

Use CompressonatorCLI for PNG/JPG compression and mip generation where its filtering is
appropriate. Normal-aware filtering and linear-light colour filtering are requirements;
verify tool capabilities rather than assuming a format flag implements them. Automate
libktx through the vcpkg `ktx` port and Compressonator through CMake and/or a custom
host-tool port. Normal CMake/vcpkg project setup automatically provisions pinned
Compressonator; no separate manual installation or tool-setup target is required before
offline or on-demand cooking. The exact acquisition method remains an implementation
investigation: verify pinned binaries with checksums or build pinned sources, on both
platforms. Offline cooking requires no renderer/GPU initialization. Use CPU encoding
initially for both offline and on-demand cooking, avoiding graphics-context and GPU
availability requirements and GPU competition with rendering. GPU encoding is deferred
until platform/format support, quality and throughput have been measured on representative
Windows/Linux systems.

**TC-D14 — Strict schemas, bounded batch options and unchanged dimensions.**

Validate texture settings and cubemap descriptors strictly: reject unknown fields, invalid
values and unsupported schema versions with a diagnostic naming the file and offending
field/version. Recognized optional fields use documented defaults. Do not silently ignore
misspellings or interpret a newer schema as the current version.

Preserve source dimensions. Reject inputs unsupported by the selected compression tool,
native format or target backends, naming the constraint; do not resize or pad
automatically. Cubemap descriptors require six square faces with matching dimensions.
Account for whole compressed blocks in tiny mip payloads without changing their logical
dimensions.

`cook-all` accepts operational options only: source/cooked roots, `--force` and encoder
thread limits. Reject per-texture overrides (`--usage`, `--format`, `--color-space`,
`--flip-normal-y`, alpha options and `--no-mips`) in batch mode. Apply those through
single-file `cook` or authored settings instead of overwriting a mixed tree's settings.

---

## Stage 7.8 incremental work order

Each step leaves a compiling, running application and has its own verification. Keep
source loading active until TC14; earlier steps exercise cooked loading through tests.
Read RHI D11 before extending formats: neutral format entries and both backend mappings
land together. Documentation-only edits need text/link review and `git diff --check`;
implementation uses the repository's precommit and rendering checks. Baseline promotion
requires explicit approval, even though rendering changes are expected in this stage.

### TC1. Resolve tool capabilities and dependency acquisition

Implemented and verified on Linux. Native Windows verification is pending manual testing.

- **Do:** Use libktx from the pinned vcpkg baseline and pin Compressonator; automate
  host-tool acquisition/build on Windows
  and Linux as part of normal project setup, and establish CPU encode/decode coverage for
  the selected BC formats. Probe mip filtering, signed formats, supported dimensions and
  alpha handling. Keep capability constraints in the relevant tests and code. Keep tools
  out of the RHI.
- **Verify:** Clean dependency setup on both hosts; cook/decode tiny representative
  fixtures without starting the engine or initializing graphics. Existing application
  unchanged.
- **Size:** M · **Needs:** TC-D13 investigation.

### TC2. Texture settings schema and validation

- **Do:** Add versioned settings parsing, effective-default resolution, independent
  overrides, persistent positive-only flip semantics and actionable warnings/errors.
  Preserve original source channel information for validation. Define batch override and
  schema policies.
- **Verify:** Cases cover saved defaults, every usage, colour-space incompatibilities,
  normal flip validation, alpha/cutoff validation, malformed JSON and persisted settings.
  Invalid requests do not mutate settings or replace output.
- **Size:** M · **Needs:** TC1; TC-D14.

### TC3. Cooker service, paths and CLI shell

- **Do:** Add CPU-only cooking service contracts in the asset/tool layer and a standalone
  `HikariTextureCooker` entry point. Implement single-file path mapping, source-root
  bounds, settings creation and CLI parsing; keep the service callable without renderer
  dependencies. Accept six-image cubemap descriptors with shared settings and explicit
  face identities.
- **Verify:** Help/exit codes, source-extension collision avoidance, missing inputs and
  output roots. CLI and code resolve identical requests. No engine loading behaviour
  changes.
- **Size:** M · **Needs:** TC2; TC-D8, TC-D12.

### TC4. Fingerprints, cook records and safe publication

- **Do:** Hash source bytes, canonical effective settings and tool/pipeline versions.
  Implement output verification, forced recooking, per-output locking, temporary-file
  validation and recoverable publication of output/record. Account for interruption
  between the two writes. For descriptor cubemaps, hash all six source files and their
  face assignments.
- **Verify:** Content/settings/tool changes invalidate; timestamps and JSON whitespace do
  not. Missing/corrupt output, stale record, interrupted publication and simultaneous
  requests recover without claiming stale output is current. Failed cooks retain the last
  good file.
- **Size:** M · **Needs:** TC3; TC-D12.

### TC5. DDS parsing and lossless KTX2 repackaging

- **Do:** Parse supported legacy/DX10 DDS headers and bounded subresource payloads, map
  native formats/metadata and write KTX2 with libktx. Reject unsupported or ambiguous
  cases according to agreed policies. Preserve compressed blocks without passing them
  through an encoder.
- **Verify:** For every supported format/shape, compare retained payload bytes per
  mip/face/ layer and important metadata after reopening KTX2. Include truncated/malformed
  DDS and `--no-mips` base-level preservation cases.
- **Size:** M–L · **Needs:** TC4; TC-D8, TC-D11.

### TC6. Opaque colour and material-data encoding

- **Do:** Encode PNG/JPG to BC1/BC4/BC5/BC7 as allowed by usage/settings. Establish scalar
  channel mapping, packed-channel retention, linear-light colour mip filtering and
  complete chains through 1×1. Use Compressonator where verified; prepare mips ourselves
  where needed. Decode supported uncompressed 8-bit DDS layouts/row pitch and reuse this
  pixel path for 2D and cubemap inputs; reject unusual layouts and higher-precision/HDR
  formats.
- **Verify:** Reopen outputs with libktx; check format, colour space, level extents and
  payload sizes, and use known pixel fixtures to assess filtering rather than only encoder
  success.
- **Size:** M–L · **Needs:** TC4; TC-D13, TC-D14.

### TC7. Normal preparation, filtering and encoding

- **Do:** Implement the agreed normal representation, optional Y flip, normal-aware mip
  generation and compression overrides. Establish cooked metadata for correct shader
  decoding.
- **Verify:** Known flat/tilted normals, Y flip, signedness and non-unit/degenerate filter
  cases; verify direction and reconstructed Z, including explicitly overridden BC7
  normals.
- **Size:** M · **Needs:** TC6; TC-D10.

### TC8. DDS missing-mip completion and explicit conversion

- **Do:** Generate only absent mip levels in the native source format, including BC2/BC3;
  retain supplied levels, including all six cubemap faces. Implement cross-face filtering
  for newly generated cubemap mips. Implement explicit format/pixel conversion according
  to TC-D11, making recompression clear. Use shared colour/data/normal filtering.
- **Verify:** Partial-chain fixtures become complete while every retained block remains
  identical. Full chains pass through; masked generation is rejected while supplied masked
  chains are preserved. Test conversion separately from preservation. Use directional
  cubemap fixtures to verify face orientation and continuity across every shared
  edge/corner in new levels, while retained DDS levels stay byte-identical. Include an
  unresolved-alpha DDS fixture: missing levels are generated, retained levels are
  unchanged and the coverage-risk warning identifies the affected asset/settings.
- **Size:** M–L · **Needs:** TC5–TC7; TC-D8, TC-D11.

### TC9. Base-only masked texture cooking

- **Do:** Implement explicit opaque/mask modes, cutoff validation, BC1 binary-alpha
  encoding and `--no-mips`. Keep BC7 varying-alpha storage. Identify material-cutoff
  limitations without implementing coverage-preserving mips in this stage.
- **Verify:** Alpha values around the cutoff produce expected encoded opacity; accidental
  alpha loss warns/errors as specified; requests to generate masked mips fail clearly.
- **Size:** S–M · **Needs:** TC6, TC8.

### TC10. Recursive batch cooking and CMake target

- **Do:** Discover supported source files recursively, preserve each settings file or
  create defaults, check fingerprints and cook serially. Exclude generated roots,
  aggregate failures, expose encoder thread limits and add the CMake `cook-textures`
  target. Discover cubemap descriptors and process their six images as one cubemap cooking
  request.
- **Verify:** Mixed source tree, saved normal flips, no-op second run, one-file
  invalidation, failed item followed by successful items, and nonzero aggregate exit
  status. Run without GPU.
- **Size:** M · **Needs:** TC3–TC9; TC-D14.

### TC11. Neutral BC formats and block-aware footprints

- **Do:** Extend the curated format enum and both conversion tables together; add block
  extent/bytes and checked subresource-size helpers. Audit upload/readback validation and
  every `BytesPerTexel` caller; reject unsupported operations instead of inventing a texel
  size. Add actual device-format capability checks and actionable diagnostics.
- **Verify:** Format mappings, overflow, odd dimensions and tiny mips occupying whole
  blocks; existing uncompressed copy sizes and tests stay correct on both backends.
- **Size:** M · **Needs:** TC1; TC-D8, TC-D10, TC-D14.

### TC12. Compressed uploads on Vulkan and D3D12

- **Do:** Implement tightly packed block uploads, backend staging alignment and D3D12 row
  footprints. Submit all mips/layers in one `UploadTexture` call as required by the
  existing interface. Cover compressed readback where supported by the agreed seam.
- **Verify:** Known block data, non-power-of-two extents, small final mips, arrays/faces
  within agreed scope and staging-budget boundaries on both backends, with zero validation
  errors.
- **Size:** M–L · **Needs:** TC11.

### TC13. libktx decoding into neutral texture data

- **Do:** Read/validate supported KTX2 metadata and payloads in the asset layer, map to
  neutral formats and enumerate subresources. Reject unsupported
  supercompression/transcoding or shapes explicitly; do not use libktx's Vulkan upload
  path to bypass the RHI.
- **Verify:** Valid cooked fixtures and malformed format/level/payload metadata; upload
  through TC12 and sample known mips on both backends. Existing source loading remains
  active.
- **Size:** M · **Needs:** TC5, TC12; TC-D8, TC-D10.

### TC14. Synchronous source acquisition and existing-content migration

- **Do:** Integrate shared incremental cooking into asset acquisition, plus direct KTX2
  loading. Warn on material/metadata usage conflicts while loading with saved settings;
  establish cache identity, author settings for existing normal and packed textures
  without usage inference, and apply the agreed failure/root policy. Preserve
  procedural/fallback paths and keep Stage 8 module relocations out of this step.
- **Verify:** Cold cook/load, warm skip, edited source/settings, missing cooker and failed
  cook; repeated acquisition reuses the cache. Verify every shipped material's effective
  usage.
- **Size:** M–L · **Needs:** TC10, TC13; TC-D9, TC-D12.

### TC15. Mip sampling and normal shader integration

- **Do:** Expose full mip chains through texture views and samplers; select correct normal
  decoding in both surface shaders, preserving explicitly overridden normal formats. Audit
  cubemap loading only within the agreed shape scope. Record expected visual differences.
- **Verify:** Minification selects smaller mips, base-only textures remain valid, normal
  orientation agrees between backends, and representative colour/data/masked textures
  render correctly. Existing material alpha cutoff remains a documented follow-up
  constraint.
- **Size:** M · **Needs:** TC7, TC14; TC-D10.

### TC16. Cross-platform delivery and completion gate

- **Do:** Wire meaningful cooker unit/integration and compressed GPU coverage into the
  existing Windows/Linux checks. Document offline/single-file/code workflows and clean
  acquisition. Obtain approval for baseline updates and promote them only after inspecting
  intended changes.
- **Verify:** Clean build/cook on both hosts; CPU-only batch works without graphics; DDS
  preservation and invalidation suites pass; Vulkan and D3D12 upload/render checks pass
  with zero validation errors. All open decisions resolved or explicitly deferred with
  their blockers.
- **Size:** M · **Needs:** TC1–TC15.

---

## Follow-ups outside Stage 7.8

Editor settings/recooking controls, alpha-coverage-preserving mips and explicit material
alpha mode/cutoff, asynchronous cooking with fallback replacement, and HDR/BC6H remain
backlog items. Automatic usage inference is excluded. GPU encoding and concurrent batch
scheduling require performance/quality measurements before a separate design decision.

Track advisory cubemap grouping hints as a follow-up: inspect directories for plausible
face-name groups (for example left/right/top/bottom/front/back) and log a warning
suggesting an explicit cubemap descriptor. These hints must not automatically group
images, write descriptors, exclude inputs or change settings. Explicit descriptors remain
authoritative; this feature is separate from automatic pixel-based texture-usage
inference.

DDS alpha-aware mip completion is a required correctness follow-up, tracked separately in
the backlog and tied to coverage-preserving masked mips/material cutoff integration.
Replace the unresolved-alpha ordinary-filtering limitation with explicit, tested alpha
handling; cover masked silhouettes, blended/premultiplied colour and source alpha
interpretation as applicable. Keep the warning until the affected path is resolved. This
is not an optional quality optimization: distant foliage disappearing is an expected
failure of the initial ordinary-filtering path and can otherwise be difficult to trace
back to cooking.
