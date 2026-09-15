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
 * Both default to zero, which is what a comparison within a backend uses: the
 * same backend on the same device has no reason to differ at all, so it stays
 * exactly as strict as a byte-for-byte comparison. Across backends the limits are
 * the build type's measured pair below, committed constants rather than a knob:
 * raising one is changing an expected test result.
 */
struct ImageTolerance
{
    uint32_t MaxChannelDelta = 0u;
    double MaxDifferingFraction = 0.0;
};

/**
 * How far a Vulkan capture and a D3D12 capture of the same frame on the same
 * adapter may differ in a debug build (plan D26, amended). Exactly the measured
 * values, with no headroom: the pair is deterministic, so the difference moves only
 * when something changes, and a margin would decide which changes pass unexplained.
 * Each build type has its own pair, because a release build's shaders differ in a
 * few more pixels; the ASan build compiles its shaders as debug does and uses this one.
 *
 * Measured on 14 September 2026 on an AMD Radeon RX 580 under Adrenalin 26.5.2,
 * rendering test_scene.map at camera preset 1, 1920x1080, 100 frames of a fixed
 * timestep and no UI: 11,434 of 2,073,600 pixels differ, 62% of them by 1 and 93% by
 * 8 or less, and the worst by 120. The sky the cloud pass draws, about half the
 * frame, is identical. Two mechanisms account for every differing pixel, each shown
 * by a control run that removed it:
 *
 * - Clip-space Y reaches the framebuffer through opposite conventions. Vulkan is
 *   given the negated projection and maps y_f = (p_y / 2) y_d + o_y; D3D maps
 *   Y_rt = (1 - Y) * Height * 0.5 + TopLeftY. Both round in float32 and keep 8
 *   subpixel bits, and the two roundings put some vertices on neighbouring
 *   subpixels. That moves which pixels an edge covers — the car's underside, the
 *   suitcases, the wheel — and the values interpolated at pixel centres, which the
 *   one-pixel specular highlights on thin geometry turn into large deltas: the
 *   worst is on a suitcase rim, and the window trim's highlight shifts along its
 *   length. The clouds beside those edges move with them, because the cloud pass
 *   reads the depth buffer. Snapping vertices to NDC Y values both mappings
 *   represent exactly removed all of it, with X left alone; clip coordinates made
 *   identical on both backends removed none, so the vertex shaders are not involved.
 * - Anisotropic filtering, which Vulkan's specification leaves open ("The particular
 *   scheme for anisotropic texture filtering is implementation-dependent") and D3D's
 *   functional specification lets approximate its footprint. The driver's two paths
 *   sample one grazing sliver of trim differently, by up to 9; with anisotropy off as
 *   well, the two captures were identical.
 */
inline constexpr ImageTolerance kCrossBackendToleranceDebug{
    .MaxChannelDelta = 120u,
    // The measured count over the measured extent, so a capture differing in exactly
    // as many pixels passes: the comparison divides the same two doubles.
    .MaxDifferingFraction = 11434.0 / 2073600.0,
};

/**
 * The same, for a release build, measured from the same frame on the same day and
 * driver. It differs in every pixel the debug pair does, by the same amounts, and in
 * four more: sky pixels at (286, 38), (287, 39), (577, 78) and (580, 80), each by 1.
 * Release compiles shaders at -O3, and the cloud raymarch's accumulation over its
 * steps then rounds differently on the two backends there — the one mechanism a debug
 * build's unoptimised shaders do not show.
 */
inline constexpr ImageTolerance kCrossBackendToleranceRelease{
    .MaxChannelDelta = 120u,
    .MaxDifferingFraction = 11438.0 / 2073600.0,
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
 * The diff is greyscale over each pixel's largest channel delta, on a logarithmic
 * curve that puts the worst delta in the image at full brightness and no differing
 * pixel below 32. Scaled to what was measured rather than to a tolerance, which is
 * zero within a backend and would leave every differing pixel equally bright; and
 * logarithmic rather than straight, because most differences worth seeing are a
 * level or two beside a worst of a hundred, and a straight line draws those black.
 */
bool WriteComparisonImages(std::span<const uint8_t> actual, Hikari::Core::Extent2D actualExtent,
                           std::span<const uint8_t> expected, Hikari::Core::Extent2D expectedExtent,
                           const std::string& pathPrefix);

} // namespace TestSupport
