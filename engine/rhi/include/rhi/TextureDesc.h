#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <rhi/RhiTypes.h>

namespace Hikari::Rhi
{
// The shape of the underlying resource, not of a view onto it. A cubemap is a
// 2D texture with six array layers and bCubeCompatible set — which is what both
// APIs actually store, Vulkan needing eCubeCompatible at creation and D3D12
// treating "cube" purely as a view dimension over a 6-slice array.
enum class TextureDimension : uint8_t
{
    Texture2D = 0,
    Texture3D,
};

inline constexpr std::array kAllTextureDimensions{
    TextureDimension::Texture2D,
    TextureDimension::Texture3D,
};

enum class TextureUsage : uint32_t
{
    None = 0,
    Sampled = 1 << 0,
    Storage = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthStencilAttachment = 1 << 3,
    CopySrc = 1 << 4,
    CopyDst = 1 << 5,
};
RHI_DEFINE_FLAG_OPERATORS(TextureUsage)

inline constexpr std::array kAllTextureUsages{
    TextureUsage::Sampled,         TextureUsage::Storage,
    TextureUsage::ColorAttachment, TextureUsage::DepthStencilAttachment,
    TextureUsage::CopySrc,         TextureUsage::CopyDst,
};

struct TextureDesc
{
    TextureDimension Dimension = TextureDimension::Texture2D;

    // Type qualified because the member and its type share a name: after this
    // declaration, unqualified `Format` inside the struct names the member.
    Rhi::Format Format = Rhi::Format::Undefined;

    // Depth is the third dimension for a Texture3D and must stay 1 otherwise;
    // an array of 2D slices is described by ArrayLayers instead. The two are
    // separate concepts in both APIs and are not interchangeable.
    Extent3D Extent{};

    uint32_t MipLevels = 1;
    uint32_t ArrayLayers = 1;
    SampleCount Samples = SampleCount::X1;
    TextureUsage Usage = TextureUsage::None;

    // Requires ArrayLayers == 6 and Dimension == Texture2D. Set at creation
    // because Vulkan needs VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT on the image
    // itself, before any view exists.
    bool bCubeCompatible = false;

    std::string DebugName;
};
} // namespace Hikari::Rhi
