#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <core/Extent2D.h>

namespace TestSupport
{

/**
 * How far two images may differ and still count as the same picture (plan D26).
 *
 * Two caps rather than one, each catching what the other misses: the ceiling
 * catches a single catastrophically wrong pixel, which an average would hide,
 * and the fraction catches an image that has drifted slightly everywhere.
 *
 * Both default to zero, and zero is the only setting Stage 7.6 has. The same
 * backend on the same device has no reason to differ at all, so a comparison
 * within a backend stays exactly as strict as a byte-for-byte one; the
 * cross-backend limits cannot be chosen until 7.7 has two backends to measure
 * between. These are committed constants when they arrive, not a knob: raising
 * one is changing an expected test result.
 */
struct ImageTolerance
{
    uint32_t MaxChannelDelta = 0u;
    double MaxDifferingFraction = 0.0;
};

/**
 * The smallest rectangle containing every differing pixel, inclusive at both
 * corners. Meaningless when nothing differs, which DifferingPixels says.
 */
struct DiffBounds
{
    uint32_t MinX = 0u;
    uint32_t MinY = 0u;
    uint32_t MaxX = 0u;
    uint32_t MaxY = 0u;
};

/**
 * What a comparison measured, reported whether it passed or failed.
 *
 * The measurement is the point rather than the verdict: "worst channel delta 3,
 * 0.4% of pixels differ" against limits of 8 and 2% shows drift while there is
 * still headroom, where a bare pass hides the approach to the cliff and the day
 * it fails nobody can tell whether it fell or walked there (D26).
 */
struct ImageComparison
{
    /**
     * False when the two images cannot be compared at all — different extents,
     * or a pixel buffer that is not exactly four bytes per pixel of its extent.
     * Every field below is meaningless then, including bWithinTolerance.
     */
    bool bComparable = false;

    bool bWithinTolerance = false;

    /** The largest single-channel difference found, and where it was found. */
    uint32_t WorstChannelDelta = 0u;
    uint32_t WorstPixelX = 0u;
    uint32_t WorstPixelY = 0u;

    /** A pixel counts as differing if any of its four channels does. */
    uint64_t DifferingPixels = 0u;
    uint64_t TotalPixels = 0u;

    DiffBounds Bounds;

    double DifferingFraction() const
    {
        return TotalPixels == 0u
                   ? 0.0
                   : static_cast<double>(DifferingPixels) / static_cast<double>(TotalPixels);
    }
};

/**
 * Compares two tightly packed 8-bit RGBA images.
 *
 * All four channels are compared. Captures are opaque, so alpha costs nothing
 * to look at and a capture that stops being opaque is caught rather than
 * hidden — which is not hypothetical: the PIL recipe this replaces inspected
 * the alpha channel *only*, and therefore passed for any two images at all.
 */
[[nodiscard]] ImageComparison CompareImages(std::span<const uint8_t> actual,
                                            Hikari::Core::Extent2D actualExtent,
                                            std::span<const uint8_t> expected,
                                            Hikari::Core::Extent2D expectedExtent,
                                            ImageTolerance tolerance = {});

/** One line naming what a comparison measured, for a test failure or a tool. */
[[nodiscard]] std::string Describe(const ImageComparison& comparison);

/**
 * Writes `<pathPrefix>actual.png`, `<pathPrefix>expected.png` and, when the two
 * extents match, an amplified `<pathPrefix>diff.png`. Creates the prefix's
 * parent directory. Returns false and logs if any write fails.
 *
 * The diff is greyscale over each pixel's largest channel delta, scaled so that
 * the tolerance's ceiling is full brightness — so at the zero tolerance of a
 * within-backend comparison every differing pixel is fully bright. Amplified
 * rather than raw because the differences worth seeing are usually a handful of
 * levels, which is invisible against black.
 */
bool WriteComparisonImages(std::span<const uint8_t> actual, Hikari::Core::Extent2D actualExtent,
                           std::span<const uint8_t> expected, Hikari::Core::Extent2D expectedExtent,
                           ImageTolerance tolerance, const std::string& pathPrefix);

} // namespace TestSupport
