#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <rhi/RhiTypes.h>

/**
 * The pure questions the neutral format list can answer without a device.
 *
 * These need no GPU, which is the point: sizing a copy, choosing an aspect and
 * telling depth from colour are decisions the renderer makes on every path, and
 * a wrong answer shows up as a corrupt image rather than as a failure. Checking
 * them here means they are checked by CI, which has no Vulkan ICD.
 */
using namespace Hikari::Rhi;

/**
 * Spelled out per format rather than derived, deliberately. A test that
 * computed the expected size the same way the implementation does would agree
 * with it whatever either of them said.
 */
TEST_CASE("BytesPerTexel sizes every format it can", "[rhi][format]")
{
    CHECK(BytesPerTexel(Format::R8Unorm) == 1u);
    CHECK(BytesPerTexel(Format::D16Unorm) == 2u);
    CHECK(BytesPerTexel(Format::RGBA8Unorm) == 4u);
    CHECK(BytesPerTexel(Format::RGBA8Srgb) == 4u);
    CHECK(BytesPerTexel(Format::BGRA8Unorm) == 4u);
    CHECK(BytesPerTexel(Format::D32Float) == 4u);
    CHECK(BytesPerTexel(Format::RGBA16Float) == 8u);
}

/**
 * Zero is the answer to a question with no single answer, not an oversight, so
 * it is asserted rather than left to be discovered by a caller that allocates
 * nothing and copies nothing.
 */
TEST_CASE("BytesPerTexel reports no single size where there is none", "[rhi][format]")
{
    CHECK(BytesPerTexel(Format::Undefined) == 0u);

    // Both have a size per aspect and the two differ, so a caller has to name
    // the aspect before the question means anything.
    CHECK(BytesPerTexel(Format::D24UnormS8Uint) == 0u);
    CHECK(BytesPerTexel(Format::D32FloatS8Uint) == 0u);
}

/**
 * The compiler catches a *missing* enumerator through -Wswitch. What it cannot
 * catch is one added to the enum and to kAllFormats but given no size, since
 * the switch would still be exhaustive if it were grouped under the zero label
 * by accident. Iterating is what covers that: a new colour format arrives here
 * as a failure rather than as a silently empty copy.
 */
TEST_CASE("Every format is either sized or explicitly unsizeable", "[rhi][format]")
{
    for (const Format format : kAllFormats)
    {
        const uint32_t size = BytesPerTexel(format);

        const bool bHasNoSingleSize = format == Format::Undefined ||
                                      (IsDepthFormat(format) && HasStencilComponent(format));

        if (bHasNoSingleSize)
            CHECK(size == 0u);
        else
            CHECK(size > 0u);
    }
}

/**
 * Usable in a constant expression, which is what lets a caller size a
 * compile-time buffer or static_assert against a layout.
 */
TEST_CASE("BytesPerTexel is a constant expression", "[rhi][format]")
{
    static_assert(BytesPerTexel(Format::BGRA8Unorm) == 4u);
    static_assert(BytesPerTexel(Format::Undefined) == 0u);
    SUCCEED();
}

/**
 * DefaultAspect and the two predicates it is built from, checked over the whole
 * list for the same reason: a depth texture given a colour aspect is a
 * validation error at view creation on a good day and a black shadow lookup on
 * a bad one, and that is exactly what the sentinel in TextureViewDesc exists to
 * prevent.
 */
TEST_CASE("DefaultAspect names the aspects the format actually has", "[rhi][format]")
{
    for (const Format format : kAllFormats)
    {
        const TextureAspect aspect = DefaultAspect(format);

        if (!IsDepthFormat(format))
        {
            CHECK(aspect == TextureAspect::Color);
            CHECK_FALSE(HasStencilComponent(format));
            continue;
        }

        CHECK(HasAll(aspect, TextureAspect::Depth));
        CHECK_FALSE(HasAll(aspect, TextureAspect::Color));
        CHECK(HasAll(aspect, TextureAspect::Stencil) == HasStencilComponent(format));
    }
}
