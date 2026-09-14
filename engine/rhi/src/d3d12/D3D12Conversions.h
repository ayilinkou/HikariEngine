#pragma once

#include <directx/d3d12.h>
#include <directx/dxgiformat.h>

#include <rhi/Barrier.h>
#include <rhi/Pipeline.h>
#include <rhi/RhiTypes.h>
#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>

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

/**
 * A layout as an enhanced barrier's. One-to-one but for DepthStencilRead, which is a
 * depth attachment tested read-only and sampled at once: DEPTH_STENCIL_READ admits no
 * shader read, so it maps to DIRECT_QUEUE_GENERIC_READ, the one layout the Enhanced
 * Barriers specification lists as compatible with both — legal because every list that
 * records a barrier here is a direct list.
 */
D3D12_BARRIER_LAYOUT ToBarrierLayout(TextureLayout layout);

/**
 * Accesses as an enhanced barrier's. No access at all is NO_ACCESS, never COMMON, which
 * means every access the layout allows and, as a before-access, every write.
 */
D3D12_BARRIER_ACCESS ToBarrierAccess(AccessFlags access);

/** Pipeline stages as an enhanced barrier's synchronization scope. */
D3D12_BARRIER_SYNC ToBarrierSync(PipelineStage stage);

/**
 * A blend factor for the colour channels, or — `bAlpha` — for the alpha channel. D3D12
 * forbids a colour factor on alpha; Vulkan allows it and means the alpha component, so
 * a colour factor on alpha becomes its alpha twin, the same factor.
 */
D3D12_BLEND ToBlend(BlendFactor factor, bool bAlpha);
D3D12_BLEND_OP ToBlendOp(BlendOp op);
D3D12_CULL_MODE ToCullMode(CullMode mode);
D3D12_COMPARISON_FUNC ToComparisonFunc(CompareOp op);

/** A sampler description as the descriptor D3D12 writes into its sampler heap. */
D3D12_SAMPLER_DESC ToD3D12Sampler(const SamplerDesc& desc);

/** A view as the shader-resource descriptor a shader reads the texture through. */
D3D12_SHADER_RESOURCE_VIEW_DESC ToShaderResourceView(const TextureViewDesc& desc);

/** A view as the unordered-access descriptor a shader writes through. Throws for a cube view. */
D3D12_UNORDERED_ACCESS_VIEW_DESC ToUnorderedAccessView(const TextureViewDesc& desc);
} // namespace Hikari::Rhi::D3D12
