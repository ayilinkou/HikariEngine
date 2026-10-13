#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <core/Extent3D.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Rhi
{
/**
 * Tightly packed storage, without backend staging alignment. RowCount counts
 * block rows for compressed formats. TotalBytes includes depth slices and layers.
 */
struct TextureFootprint
{
    uint64_t RowBytes = 0u;
    uint64_t RowCount = 0u;
    uint64_t SliceBytes = 0u;
    uint64_t TotalBytes = 0u;
};

/**
 * Sizes a mip's logical extent without padding or changing its dimensions.
 * Odd edges and tiny mips occupy whole blocks. Rejects empty/ambiguous input
 * and overflow before a caller can allocate a wrapped, undersized buffer.
 */
constexpr TextureFootprint ComputeTextureFootprint(Format format, Core::Extent3D extent,
                                                   uint32_t layerCount = 1u)
{
    const uint32_t blockBytes = BytesPerBlock(format);
    if (blockBytes == 0u)
        throw std::invalid_argument("Texture footprint: format has no single packed size.");
    if (extent.Width == 0u || extent.Height == 0u || extent.Depth == 0u || layerCount == 0u)
        throw std::invalid_argument("Texture footprint: extents and layer count must be nonzero.");

    const auto multiply = [](uint64_t a, uint64_t b)
    {
        if (a > std::numeric_limits<uint64_t>::max() / b)
            throw std::overflow_error("Texture footprint: byte count exceeds uint64_t.");
        return a * b;
    };
    const auto blocks = [](uint32_t size, uint32_t blockSize)
    { return (static_cast<uint64_t>(size) + blockSize - 1u) / blockSize; };

    const Core::Extent3D block = GetFormatBlockExtent(format);
    const uint64_t rowBytes = multiply(blocks(extent.Width, block.Width), blockBytes);
    const uint64_t rowCount = blocks(extent.Height, block.Height);
    const uint64_t sliceBytes = multiply(rowBytes, rowCount);
    return {rowBytes, rowCount, sliceBytes,
            multiply(multiply(sliceBytes, blocks(extent.Depth, block.Depth)), layerCount)};
}
} // namespace Hikari::Rhi
