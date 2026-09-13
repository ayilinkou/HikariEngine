#include "d3d12/D3D12Conversions.h"

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
