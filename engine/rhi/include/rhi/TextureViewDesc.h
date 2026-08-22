#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <rhi/Handles.h>
#include <rhi/RhiTypes.h>

namespace Rhi
{
// How a view interprets the texture underneath it.
//
// Separate from TextureDimension because the two genuinely differ: a cubemap is
// stored as a 2D texture with six array layers (TextureDesc's comment says why)
// and only the view calls it a cube. D3D12 makes the same split — the resource
// is a Texture2D array, the SRV dimension is TEXTURECUBE — so this is the shape
// both APIs already have rather than one invented here.
enum class TextureViewDimension : uint8_t
{
    Texture2D = 0,
    Texture2DArray,
    TextureCube,
    Texture3D,
};

inline constexpr std::array kAllTextureViewDimensions{
    TextureViewDimension::Texture2D,
    TextureViewDimension::Texture2DArray,
    TextureViewDimension::TextureCube,
    TextureViewDimension::Texture3D,
};

struct TextureViewDesc
{
    TextureHandle Texture{};

    TextureViewDimension Dimension = TextureViewDimension::Texture2D;

    // Undefined means the texture's own format, which is what all but a
    // reinterpreting view wants. Type qualified because the member and its type
    // share a name, as in TextureDesc.
    Rhi::Format Format = Rhi::Format::Undefined;

    // None means the aspect the format implies — DefaultAspect(). Worth the
    // sentinel: a colour default would silently produce a view of the wrong
    // aspect for every depth texture, and that is a validation error at
    // creation on a good day and a black shadow lookup on a bad one.
    TextureAspect Aspect = TextureAspect::None;

    uint32_t BaseMip = 0u;
    uint32_t MipCount = 1u;
    uint32_t BaseLayer = 0u;
    uint32_t LayerCount = 1u;

    std::string DebugName;
};
} // namespace Rhi
