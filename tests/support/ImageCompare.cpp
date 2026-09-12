#include "ImageCompare.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <vector>

#include <asset/ImageWriter.h>
#include <core/Log.h>

using namespace Hikari;

namespace TestSupport
{

namespace
{
constexpr Core::LogCategory LogImageCompare("Image Compare");

/** What both sides are: four 8-bit channels, no padding between rows. */
constexpr uint32_t kChannels = 4u;

/** Whether `pixels` is exactly the buffer `extent` describes. */
bool IsWellFormed(std::span<const uint8_t> pixels, Core::Extent2D extent)
{
    const uint64_t expected = static_cast<uint64_t>(extent.Width) * extent.Height * kChannels;
    return extent.Width > 0u && extent.Height > 0u && pixels.size() == expected;
}

/** The largest of a pixel's four channel differences. */
uint32_t LargestChannelDelta(const uint8_t* a, const uint8_t* b)
{
    uint32_t worst = 0u;
    for (uint32_t channel = 0u; channel < kChannels; ++channel)
    {
        const int delta = static_cast<int>(a[channel]) - static_cast<int>(b[channel]);
        worst = std::max(worst, static_cast<uint32_t>(delta < 0 ? -delta : delta));
    }

    return worst;
}

/**
 * A delta as a brightness, with the tolerance's ceiling mapped to full scale.
 *
 * A zero ceiling has no scale to map onto, so any difference at all is full
 * brightness — which is the right answer for a within-backend comparison, where
 * every differing pixel is a defect rather than a degree of one.
 */
uint8_t Amplify(uint32_t delta, uint32_t ceiling)
{
    if (delta == 0u)
        return 0u;

    if (ceiling == 0u)
        return 255u;

    return static_cast<uint8_t>(std::min<uint32_t>(255u, (delta * 255u + ceiling / 2u) / ceiling));
}
} // namespace

ImageComparison CompareImages(std::span<const uint8_t> actual, Core::Extent2D actualExtent,
                              std::span<const uint8_t> expected, Core::Extent2D expectedExtent,
                              ImageTolerance tolerance)
{
    ImageComparison result;

    if (actualExtent != expectedExtent || !IsWellFormed(actual, actualExtent) ||
        !IsWellFormed(expected, expectedExtent))
    {
        return result;
    }

    result.bComparable = true;
    result.TotalPixels = static_cast<uint64_t>(actualExtent.Width) * actualExtent.Height;

    bool bAnyDiffering = false;
    for (uint32_t y = 0u; y < actualExtent.Height; ++y)
    {
        for (uint32_t x = 0u; x < actualExtent.Width; ++x)
        {
            const size_t offset = (static_cast<size_t>(y) * actualExtent.Width + x) * kChannels;
            const uint32_t delta =
                LargestChannelDelta(actual.data() + offset, expected.data() + offset);

            if (delta == 0u)
                continue;

            ++result.DifferingPixels;

            if (delta > result.WorstChannelDelta)
            {
                result.WorstChannelDelta = delta;
                result.WorstPixelX = x;
                result.WorstPixelY = y;
            }

            if (!bAnyDiffering)
            {
                result.Bounds = DiffBounds{.MinX = x, .MinY = y, .MaxX = x, .MaxY = y};
                bAnyDiffering = true;
            }
            else
            {
                result.Bounds.MinX = std::min(result.Bounds.MinX, x);
                result.Bounds.MinY = std::min(result.Bounds.MinY, y);
                result.Bounds.MaxX = std::max(result.Bounds.MaxX, x);
                result.Bounds.MaxY = std::max(result.Bounds.MaxY, y);
            }
        }
    }

    result.bWithinTolerance = result.WorstChannelDelta <= tolerance.MaxChannelDelta &&
                              result.DifferingFraction() <= tolerance.MaxDifferingFraction;

    return result;
}

std::string Describe(const ImageComparison& comparison)
{
    if (!comparison.bComparable)
        return "not comparable: the extents differ, or a buffer is not 4 bytes per pixel";

    if (comparison.DifferingPixels == 0u)
        return std::format("identical: {} pixels, no channel differs", comparison.TotalPixels);

    return std::format("worst channel delta {} at ({}, {}); {} of {} pixels differ ({:.4f}%); "
                       "bounds ({}, {})-({}, {})",
                       comparison.WorstChannelDelta, comparison.WorstPixelX, comparison.WorstPixelY,
                       comparison.DifferingPixels, comparison.TotalPixels,
                       comparison.DifferingFraction() * 100.0, comparison.Bounds.MinX,
                       comparison.Bounds.MinY, comparison.Bounds.MaxX, comparison.Bounds.MaxY);
}

bool WriteComparisonImages(std::span<const uint8_t> actual, Core::Extent2D actualExtent,
                           std::span<const uint8_t> expected, Core::Extent2D expectedExtent,
                           ImageTolerance tolerance, const std::string& pathPrefix)
{
    const std::filesystem::path parent = std::filesystem::path(pathPrefix).parent_path();
    if (!parent.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            Core::LogMsg(Core::LogSeverity::Error, LogImageCompare, "Could not create {}: {}",
                         parent.string(), ec.message());
            return false;
        }
    }

    bool bWroteAll = Asset::WritePng(actual, actualExtent, pathPrefix + "actual.png");
    bWroteAll = Asset::WritePng(expected, expectedExtent, pathPrefix + "expected.png") && bWroteAll;

    // No diff where the extents disagree: there is no pixel-to-pixel
    // correspondence to draw, and the two images beside each other are the whole
    // of what there is to say.
    if (actualExtent != expectedExtent || !IsWellFormed(actual, actualExtent) ||
        !IsWellFormed(expected, expectedExtent))
    {
        return bWroteAll;
    }

    std::vector<uint8_t> diff(actual.size());
    for (size_t pixel = 0u; pixel < diff.size(); pixel += kChannels)
    {
        const uint8_t value =
            Amplify(LargestChannelDelta(actual.data() + pixel, expected.data() + pixel),
                    tolerance.MaxChannelDelta);

        diff[pixel] = value;
        diff[pixel + 1u] = value;
        diff[pixel + 2u] = value;
        diff[pixel + 3u] = 255u;
    }

    return Asset::WritePng(diff, actualExtent, pathPrefix + "diff.png") && bWroteAll;
}

} // namespace TestSupport
