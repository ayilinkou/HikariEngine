#include "d3d12/D3D12Conversions.h"

#include <stdexcept>

namespace Hikari::Rhi::D3D12
{

DXGI_FORMAT ToDxgi(Format format)
{
    switch (format)
    {
        case Format::Undefined:
            return DXGI_FORMAT_UNKNOWN;
        case Format::R8Unorm:
            return DXGI_FORMAT_R8_UNORM;
        case Format::RGBA8Unorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8Srgb:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::BGRA8Unorm:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::RGBA16Float:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case Format::RG32Float:
            return DXGI_FORMAT_R32G32_FLOAT;
        case Format::RGB32Float:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case Format::RGBA32Float:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::D16Unorm:
            return DXGI_FORMAT_D16_UNORM;
        case Format::D32Float:
            return DXGI_FORMAT_D32_FLOAT;
        case Format::D24UnormS8Uint:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32FloatS8Uint:
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    }

    return DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT ToDxgiResourceFormat(Format format, TextureUsage usage)
{
    if (!IsDepthFormat(format) || (usage & TextureUsage::Sampled) == TextureUsage::None)
        return ToDxgi(format);

    switch (format)
    {
        case Format::D16Unorm:
            return DXGI_FORMAT_R16_TYPELESS;
        case Format::D32Float:
            return DXGI_FORMAT_R32_TYPELESS;
        case Format::D24UnormS8Uint:
            return DXGI_FORMAT_R24G8_TYPELESS;
        case Format::D32FloatS8Uint:
            return DXGI_FORMAT_R32G8X24_TYPELESS;
        case Format::Undefined:
        case Format::R8Unorm:
        case Format::RGBA8Unorm:
        case Format::RGBA8Srgb:
        case Format::BGRA8Unorm:
        case Format::RGBA16Float:
        case Format::RG32Float:
        case Format::RGB32Float:
        case Format::RGBA32Float:
            break;
    }

    return ToDxgi(format);
}

DXGI_FORMAT ToDxgiShaderViewFormat(Format format)
{
    switch (format)
    {
        case Format::D16Unorm:
            return DXGI_FORMAT_R16_UNORM;
        case Format::D32Float:
            return DXGI_FORMAT_R32_FLOAT;
        case Format::D24UnormS8Uint:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case Format::D32FloatS8Uint:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case Format::Undefined:
        case Format::R8Unorm:
        case Format::RGBA8Unorm:
        case Format::RGBA8Srgb:
        case Format::BGRA8Unorm:
        case Format::RGBA16Float:
        case Format::RG32Float:
        case Format::RGB32Float:
        case Format::RGBA32Float:
            break;
    }

    return ToDxgi(format);
}

D3D12_RESOURCE_STATES ToLegacyState(TextureLayout layout)
{
    switch (layout)
    {
        case TextureLayout::Undefined:
        case TextureLayout::Common:
            return D3D12_RESOURCE_STATE_COMMON;
        case TextureLayout::RenderTarget:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case TextureLayout::ShaderResource:
            return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        case TextureLayout::UnorderedAccess:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case TextureLayout::DepthStencilWrite:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case TextureLayout::DepthStencilRead:
            return D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        case TextureLayout::CopySrc:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case TextureLayout::CopyDst:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case TextureLayout::Present:
            return D3D12_RESOURCE_STATE_PRESENT;
    }

    return D3D12_RESOURCE_STATE_COMMON;
}

D3D12_BARRIER_LAYOUT ToBarrierLayout(TextureLayout layout)
{
    switch (layout)
    {
        case TextureLayout::Undefined:
            return D3D12_BARRIER_LAYOUT_UNDEFINED;
        case TextureLayout::Common:
            return D3D12_BARRIER_LAYOUT_COMMON;
        case TextureLayout::RenderTarget:
            return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        case TextureLayout::ShaderResource:
            return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
        case TextureLayout::UnorderedAccess:
            return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
        case TextureLayout::DepthStencilWrite:
            return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
        case TextureLayout::DepthStencilRead:
            return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ;
        case TextureLayout::CopySrc:
            return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
        case TextureLayout::CopyDst:
            return D3D12_BARRIER_LAYOUT_COPY_DEST;
        case TextureLayout::Present:
            return D3D12_BARRIER_LAYOUT_PRESENT;
    }

    return D3D12_BARRIER_LAYOUT_UNDEFINED;
}

D3D12_BARRIER_ACCESS ToBarrierAccess(AccessFlags access)
{
    D3D12_BARRIER_ACCESS result = D3D12_BARRIER_ACCESS_COMMON;
    for (const AccessFlags flag : kAllAccessFlags)
    {
        if (!Any(access & flag))
            continue;

        switch (flag)
        {
            case AccessFlags::None:
                break;
            case AccessFlags::VertexBufferRead:
                result |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
                break;
            case AccessFlags::IndexBufferRead:
                result |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
                break;
            case AccessFlags::ConstantBufferRead:
                result |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
                break;
            case AccessFlags::ShaderRead:
                result |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
                break;
            case AccessFlags::UnorderedAccess:
                result |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
                break;
            // D3D12 has one render-target access for reading and writing alike.
            case AccessFlags::RenderTargetRead:
            case AccessFlags::RenderTargetWrite:
                result |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
                break;
            case AccessFlags::DepthStencilRead:
                result |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
                break;
            case AccessFlags::DepthStencilWrite:
                result |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
                break;
            case AccessFlags::CopySrc:
                result |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
                break;
            case AccessFlags::CopyDst:
                result |= D3D12_BARRIER_ACCESS_COPY_DEST;
                break;
        }
    }

    return result == D3D12_BARRIER_ACCESS_COMMON ? D3D12_BARRIER_ACCESS_NO_ACCESS : result;
}

D3D12_BARRIER_SYNC ToBarrierSync(PipelineStage stage)
{
    D3D12_BARRIER_SYNC result = D3D12_BARRIER_SYNC_NONE;
    for (const PipelineStage flag : kAllPipelineStages)
    {
        if (!Any(stage & flag))
            continue;

        switch (flag)
        {
            case PipelineStage::None:
                break;
            case PipelineStage::Draw:
            // Every graphics stage, which DRAW supersedes.
            case PipelineStage::AllGraphics:
                result |= D3D12_BARRIER_SYNC_DRAW;
                break;
            case PipelineStage::VertexStage:
                result |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
                break;
            case PipelineStage::PixelStage:
                result |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
                break;
            case PipelineStage::ComputeStage:
                result |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
                break;
            case PipelineStage::DepthStencil:
                result |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
                break;
            case PipelineStage::RenderTarget:
                result |= D3D12_BARRIER_SYNC_RENDER_TARGET;
                break;
            case PipelineStage::Copy:
                result |= D3D12_BARRIER_SYNC_COPY;
                break;
            case PipelineStage::Resolve:
                result |= D3D12_BARRIER_SYNC_RESOLVE;
                break;
            case PipelineStage::All:
                result |= D3D12_BARRIER_SYNC_ALL;
                break;
        }
    }

    return result;
}

namespace
{
D3D12_FILTER_TYPE ToFilterType(Filter filter)
{
    switch (filter)
    {
        case Filter::Nearest:
            return D3D12_FILTER_TYPE_POINT;
        case Filter::Linear:
            return D3D12_FILTER_TYPE_LINEAR;
    }

    return D3D12_FILTER_TYPE_LINEAR;
}

D3D12_FILTER_TYPE ToFilterType(MipmapMode mode)
{
    switch (mode)
    {
        case MipmapMode::Nearest:
            return D3D12_FILTER_TYPE_POINT;
        case MipmapMode::Linear:
            return D3D12_FILTER_TYPE_LINEAR;
    }

    return D3D12_FILTER_TYPE_LINEAR;
}

D3D12_TEXTURE_ADDRESS_MODE ToAddressMode(AddressMode mode)
{
    switch (mode)
    {
        case AddressMode::Repeat:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case AddressMode::MirroredRepeat:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case AddressMode::ClampToEdge:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case AddressMode::ClampToBorder:
            return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    }

    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

D3D12_COMPARISON_FUNC ToComparison(CompareOp op)
{
    switch (op)
    {
        case CompareOp::Never:
            return D3D12_COMPARISON_FUNC_NEVER;
        case CompareOp::Less:
            return D3D12_COMPARISON_FUNC_LESS;
        case CompareOp::Equal:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareOp::LessOrEqual:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareOp::Greater:
            return D3D12_COMPARISON_FUNC_GREATER;
        case CompareOp::NotEqual:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case CompareOp::GreaterOrEqual:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case CompareOp::Always:
            return D3D12_COMPARISON_FUNC_ALWAYS;
    }

    return D3D12_COMPARISON_FUNC_ALWAYS;
}
} // namespace

D3D12_BLEND ToBlend(BlendFactor factor, bool bAlpha)
{
    switch (factor)
    {
        case BlendFactor::Zero:
            return D3D12_BLEND_ZERO;
        case BlendFactor::One:
            return D3D12_BLEND_ONE;
        case BlendFactor::OneMinusSrcColor:
            return bAlpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    }

    return D3D12_BLEND_ONE;
}

D3D12_BLEND_OP ToBlendOp(BlendOp op)
{
    switch (op)
    {
        case BlendOp::Add:
            return D3D12_BLEND_OP_ADD;
    }

    return D3D12_BLEND_OP_ADD;
}

D3D12_CULL_MODE ToCullMode(CullMode mode)
{
    switch (mode)
    {
        case CullMode::None:
            return D3D12_CULL_MODE_NONE;
        case CullMode::Front:
            return D3D12_CULL_MODE_FRONT;
        case CullMode::Back:
            return D3D12_CULL_MODE_BACK;
    }

    return D3D12_CULL_MODE_NONE;
}

D3D12_COMPARISON_FUNC ToComparisonFunc(CompareOp op)
{
    return ToComparison(op);
}

D3D12_SAMPLER_DESC ToD3D12Sampler(const SamplerDesc& desc)
{
    // Comparison is a filter reduction on D3D12 rather than a separate enable, and
    // anisotropy replaces the three filter types rather than joining them.
    const D3D12_FILTER_REDUCTION_TYPE reduction = desc.bCompareEnable
                                                      ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON
                                                      : D3D12_FILTER_REDUCTION_TYPE_STANDARD;

    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter =
        desc.bAnisotropyEnable
            ? D3D12_ENCODE_ANISOTROPIC_FILTER(reduction)
            : D3D12_ENCODE_BASIC_FILTER(ToFilterType(desc.MinFilter), ToFilterType(desc.MagFilter),
                                        ToFilterType(desc.MipmapFilter), reduction);
    sampler.AddressU = ToAddressMode(desc.AddressU);
    sampler.AddressV = ToAddressMode(desc.AddressV);
    sampler.AddressW = ToAddressMode(desc.AddressW);
    sampler.MipLODBias = desc.MipLodBias;
    sampler.MaxAnisotropy = desc.bAnisotropyEnable ? static_cast<UINT>(desc.MaxAnisotropy) : 1u;
    sampler.ComparisonFunc =
        desc.bCompareEnable ? ToComparison(desc.Compare) : D3D12_COMPARISON_FUNC_NEVER;
    sampler.MinLOD = desc.MinLod;
    sampler.MaxLOD = desc.MaxLod;

    // The float and int border colours are the same colours to D3D12, which has no
    // integer border of its own.
    const float white =
        desc.Border == BorderColor::OpaqueWhiteFloat || desc.Border == BorderColor::OpaqueWhiteInt
            ? 1.f
            : 0.f;
    const float alpha = desc.Border == BorderColor::TransparentBlackFloat ||
                                desc.Border == BorderColor::TransparentBlackInt
                            ? 0.f
                            : 1.f;
    sampler.BorderColor[0] = white;
    sampler.BorderColor[1] = white;
    sampler.BorderColor[2] = white;
    sampler.BorderColor[3] = alpha;

    return sampler;
}

D3D12_SHADER_RESOURCE_VIEW_DESC ToShaderResourceView(const TextureViewDesc& desc)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = ToDxgiShaderViewFormat(desc.Format);
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    switch (desc.Dimension)
    {
        case TextureViewDimension::Texture2D:
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MostDetailedMip = desc.BaseMip;
            view.Texture2D.MipLevels = desc.MipCount;
            break;
        case TextureViewDimension::Texture2DArray:
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MostDetailedMip = desc.BaseMip;
            view.Texture2DArray.MipLevels = desc.MipCount;
            view.Texture2DArray.FirstArraySlice = desc.BaseLayer;
            view.Texture2DArray.ArraySize = desc.LayerCount;
            break;
        case TextureViewDimension::TextureCube:
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            view.TextureCube.MostDetailedMip = desc.BaseMip;
            view.TextureCube.MipLevels = desc.MipCount;
            break;
        case TextureViewDimension::Texture3D:
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            view.Texture3D.MostDetailedMip = desc.BaseMip;
            view.Texture3D.MipLevels = desc.MipCount;
            break;
    }

    return view;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC ToUnorderedAccessView(const TextureViewDesc& desc)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
    view.Format = ToDxgi(desc.Format);

    switch (desc.Dimension)
    {
        case TextureViewDimension::Texture2D:
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipSlice = desc.BaseMip;
            break;
        case TextureViewDimension::Texture2DArray:
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipSlice = desc.BaseMip;
            view.Texture2DArray.FirstArraySlice = desc.BaseLayer;
            view.Texture2DArray.ArraySize = desc.LayerCount;
            break;
        case TextureViewDimension::Texture3D:
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            view.Texture3D.MipSlice = desc.BaseMip;
            view.Texture3D.FirstWSlice = 0;
            // Every depth slice of the mip.
            view.Texture3D.WSize = static_cast<UINT>(-1);
            break;
        case TextureViewDimension::TextureCube:
            throw std::runtime_error("A cube view cannot be written through unordered access.");
    }

    return view;
}

D3D12_RESOURCE_FLAGS ToResourceFlags(TextureUsage usage)
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

    if ((usage & TextureUsage::ColorAttachment) != TextureUsage::None)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    if ((usage & TextureUsage::Storage) != TextureUsage::None)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if ((usage & TextureUsage::DepthStencilAttachment) != TextureUsage::None)
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        // Telling the runtime a depth target is never read lets it skip what
        // sampling would need.
        if ((usage & TextureUsage::Sampled) == TextureUsage::None)
            flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }

    return flags;
}

} // namespace Hikari::Rhi::D3D12
