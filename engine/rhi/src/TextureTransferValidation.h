#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>

#include <rhi/ICommandList.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureFootprint.h>
#include <rhi/UploadContext.h>

namespace Hikari::Rhi
{
/** Validates before staging allocation or native command recording can have side effects. */
inline TextureFootprint ValidateTextureCopy(const TextureDesc& desc,
                                            const BufferTextureCopyRegion& region,
                                            std::string_view operation)
{
    const auto fail = [&](std::string_view why)
    { throw std::runtime_error(std::format("Rhi::{}('{}'): {}", operation, desc.DebugName, why)); };

    if (IsBlockCompressed(desc.Format))
        fail("compressed texture transfers are not implemented yet.");
    if (BytesPerTexel(desc.Format) == 0u)
        fail("format has no single packed copy size; combined depth/stencil copies are "
             "unsupported.");
    if (desc.Samples != SampleCount::X1)
        fail("buffer copies require a single-sample texture.");
    if (region.Aspect != DefaultAspect(desc.Format))
        fail("copy aspect does not match the format.");
    if (region.MipLevel >= desc.MipLevels || region.MipLevel >= 32u || region.LayerCount == 0u ||
        region.BaseLayer >= desc.ArrayLayers ||
        region.LayerCount > desc.ArrayLayers - region.BaseLayer)
        fail("mip or layer range is outside the texture.");

    const Core::Extent3D mip{std::max(desc.Extent.Width >> region.MipLevel, 1u),
                             std::max(desc.Extent.Height >> region.MipLevel, 1u),
                             std::max(desc.Extent.Depth >> region.MipLevel, 1u)};
    if (region.Extent.Width > mip.Width || region.Extent.Height > mip.Height ||
        region.Extent.Depth > mip.Depth)
        fail("copy extent exceeds the selected mip.");
    const TextureFootprint footprint =
        ComputeTextureFootprint(desc.Format, region.Extent, region.LayerCount);
    if (region.BufferOffset % BytesPerTexel(desc.Format) != 0u)
        fail("buffer offset must be a multiple of the texel size.");
    if (region.BufferOffset > std::numeric_limits<uint64_t>::max() - footprint.TotalBytes)
        fail("buffer offset plus copy size overflows uint64_t.");
    return footprint;
}

/**
 * Both the four-byte copy-queue rule and the texel-size rule must hold (including
 * RGB32): VUID-vkCmdCopyBufferToImage-commandBuffer-07737 and dstImage-07975.
 */
inline uint64_t TextureStagingAlignment(Format format)
{
    return std::lcm(uint64_t{4u}, static_cast<uint64_t>(BytesPerTexel(format)));
}

/** All mips/layers must be present exactly once because uploading discards the whole texture. */
inline uint64_t ValidateTextureUploads(const TextureDesc& desc,
                                       std::span<const TextureUpload> subresources)
{
    if (subresources.empty())
        return 0u;
    const auto fail = [&](std::string_view why)
    { throw std::runtime_error(std::format("Rhi::UploadTexture('{}'): {}", desc.DebugName, why)); };
    if (!HasAll(desc.Usage, TextureUsage::CopyDst | TextureUsage::Sampled))
        fail("uploads require CopyDst and Sampled usage.");
    if (IsDepthFormat(desc.Format))
        fail("depth/stencil uploads are unsupported.");

    uint64_t total = 0u;
    uint64_t covered = 0u;
    for (size_t index = 0u; index < subresources.size(); ++index)
    {
        const TextureUpload& subresource = subresources[index];
        const TextureFootprint footprint =
            ValidateTextureCopy(desc,
                                {.Aspect = subresource.Aspect,
                                 .MipLevel = subresource.MipLevel,
                                 .BaseLayer = subresource.BaseLayer,
                                 .LayerCount = subresource.LayerCount,
                                 .Extent = subresource.Extent},
                                "UploadTexture");
        const Core::Extent3D mip{std::max(desc.Extent.Width >> subresource.MipLevel, 1u),
                                 std::max(desc.Extent.Height >> subresource.MipLevel, 1u),
                                 std::max(desc.Extent.Depth >> subresource.MipLevel, 1u)};
        if (subresource.Extent != mip)
            fail("each upload must cover the full logical mip extent.");
        if (subresource.Data.size_bytes() != footprint.TotalBytes)
            fail("payload size must equal the tightly packed subresource size.");
        for (size_t previous = 0u; previous < index; ++previous)
        {
            const TextureUpload& other = subresources[previous];
            if (other.MipLevel == subresource.MipLevel &&
                other.BaseLayer < subresource.BaseLayer + subresource.LayerCount &&
                subresource.BaseLayer < other.BaseLayer + other.LayerCount)
                fail("subresource layer ranges overlap.");
        }
        covered += subresource.LayerCount;
        const uint64_t alignment = TextureStagingAlignment(desc.Format);
        const uint64_t padding = (alignment - total % alignment) % alignment;
        if (total > std::numeric_limits<uint64_t>::max() - padding ||
            total + padding > std::numeric_limits<uint64_t>::max() - footprint.TotalBytes)
            fail("staging size overflows uint64_t.");
        total += padding + footprint.TotalBytes;
    }
    if (covered != static_cast<uint64_t>(desc.MipLevels) * desc.ArrayLayers)
        fail("all mips and layers must be supplied in one upload.");
    return total;
}
} // namespace Hikari::Rhi
