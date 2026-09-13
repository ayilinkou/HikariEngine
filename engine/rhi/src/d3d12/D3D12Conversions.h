#pragma once

#include <directx/d3d12.h>
#include <directx/dxgiformat.h>

#include <rhi/Barrier.h>
#include <rhi/RhiTypes.h>
#include <rhi/TextureDesc.h>

namespace Hikari::Rhi::D3D12
{
/**
 * Neutral vocabulary to D3D12's. Every switch here names every enumerator and has no
 * `default:`, so a value added to the seam fails the build until it is mapped here.
 */

/** The format a view or a copy names. */
DXGI_FORMAT ToDxgi(Format format);

/**
 * The format a texture's resource is created with. A depth format that is also
 * sampled has to be created typeless: D3D12 gives a fully typed depth resource no
 * shader view of another format, and the depth-stencil view and the shader view of
 * one texture name different formats (D32_FLOAT and R32_FLOAT).
 */
DXGI_FORMAT ToDxgiResourceFormat(Format format, TextureUsage usage);

/**
 * The format a shader reads a texture of `format` through: the depth channel's, for
 * a depth format.
 */
DXGI_FORMAT ToDxgiShaderViewFormat(Format format);

D3D12_RESOURCE_FLAGS ToResourceFlags(TextureUsage usage);

/**
 * A layout as a legacy barrier's resource state. Undefined has no state of its own —
 * it means any — so it maps to COMMON here and a barrier from it resolves the real
 * state separately.
 *
 * ShaderResource is both shader-resource states, since the seam does not say which
 * stages read it; both are read-only, so they combine. DepthStencilRead adds them too,
 * because that layout is also what a shader samples depth through.
 */
D3D12_RESOURCE_STATES ToLegacyState(TextureLayout layout);
} // namespace Hikari::Rhi::D3D12
