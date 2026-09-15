#pragma once

#include <format>
#include <stdexcept>
#include <string_view>

#include <rhi/TextureDesc.h>

namespace Hikari::Rhi
{
/**
 * Rejects the texture descriptions either backend would reject anyway, but with a
 * message that names the caller's field rather than a VUID or an HRESULT. Every
 * one of these is a programming error rather than a runtime condition, so they
 * throw. Shared, because the rules are the seam's rather than either API's.
 */
inline void ValidateTextureDesc(const TextureDesc& desc)
{
    const auto fail = [&desc](std::string_view why)
    {
        throw std::runtime_error(
            std::format("Rhi::IDevice::CreateTexture('{}'): {}", desc.DebugName, why));
    };

    if (desc.Format == Rhi::Format::Undefined)
        fail("no format.");

    if (desc.Extent.Width == 0u || desc.Extent.Height == 0u || desc.Extent.Depth == 0u)
        fail("every extent must be at least 1.");

    if (desc.MipLevels == 0u || desc.ArrayLayers == 0u)
        fail("MipLevels and ArrayLayers must be at least 1.");

    // Depth is the third dimension of a 3D texture and the array is the layers;
    // neither API has 3D arrays, and mixing the two is the classic way to describe
    // a cubemap as six slices deep instead of six layers wide.
    if (desc.Dimension == TextureDimension::Texture3D && desc.ArrayLayers != 1u)
        fail("a 3D texture cannot have array layers.");

    if (desc.Dimension == TextureDimension::Texture2D && desc.Extent.Depth != 1u)
        fail("a 2D texture must have a depth of 1; use ArrayLayers for slices.");

    if (desc.bCubeCompatible &&
        (desc.Dimension != TextureDimension::Texture2D || desc.ArrayLayers % 6u != 0u))
        fail("a cube-compatible texture must be 2D with a multiple of 6 array layers.");
}
} // namespace Hikari::Rhi
