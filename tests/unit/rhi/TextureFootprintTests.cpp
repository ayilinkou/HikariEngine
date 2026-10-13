#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <rhi/TextureFootprint.h>

#include "TextureTransferValidation.h"
#include "TextureValidation.h"

using namespace Hikari::Rhi;

TEST_CASE("BC footprints round storage to whole blocks", "[rhi][format][bc]")
{
    constexpr std::array kFormats{Format::BC1Unorm, Format::BC1Srgb,  Format::BC2Unorm,
                                  Format::BC2Srgb,  Format::BC3Unorm, Format::BC3Srgb,
                                  Format::BC4Unorm, Format::BC4Snorm, Format::BC5Unorm,
                                  Format::BC5Snorm, Format::BC7Unorm, Format::BC7Srgb};
    constexpr std::array kBytes{8u, 8u, 16u, 16u, 16u, 16u, 8u, 8u, 16u, 16u, 16u, 16u};
    for (size_t i = 0u; i < kFormats.size(); ++i)
    {
        const Format format = kFormats[i];
        CAPTURE(format);
        CHECK(IsBlockCompressed(format));
        CHECK(BytesPerTexel(format) == 0u);
        CHECK(BytesPerBlock(format) == kBytes[i]);
        CHECK(GetFormatBlockExtent(format) == Hikari::Core::Extent3D{4u, 4u, 1u});
        const TextureFootprint odd = ComputeTextureFootprint(format, {7u, 5u, 1u});
        CHECK(odd.RowBytes == 2u * kBytes[i]);
        CHECK(odd.RowCount == 2u);
        CHECK(odd.SliceBytes == 4u * kBytes[i]);
        CHECK(odd.TotalBytes == 4u * kBytes[i]);
        CHECK(ComputeTextureFootprint(format, {3u, 2u, 1u}).TotalBytes == kBytes[i]);
        CHECK(ComputeTextureFootprint(format, {1u, 1u, 1u}, 6u).TotalBytes == 6u * kBytes[i]);
    }
    static_assert(ComputeTextureFootprint(Format::BC7Srgb, {1u, 1u, 1u}).TotalBytes == 16u);
}

TEST_CASE("Uncompressed footprints retain texel row slice and layer sizes", "[rhi][format]")
{
    const TextureFootprint packed = ComputeTextureFootprint(Format::RGBA8Unorm, {7u, 5u, 3u}, 2u);
    CHECK(packed.RowBytes == 28u);
    CHECK(packed.RowCount == 5u);
    CHECK(packed.SliceBytes == 140u);
    CHECK(packed.TotalBytes == 840u);
    CHECK(ComputeTextureFootprint(Format::R8Unorm, {3u, 2u, 1u}).TotalBytes == 6u);
    CHECK(ComputeTextureFootprint(Format::RGB32Float, {3u, 2u, 1u}).TotalBytes == 72u);
    CHECK(GetFormatBlockExtent(Format::D32Float) == Hikari::Core::Extent3D{1u, 1u, 1u});
    static_assert(ComputeTextureFootprint(Format::D16Unorm, {3u, 2u, 1u}).TotalBytes == 12u);
}

TEST_CASE("Footprints reject ambiguous empty and overflowing inputs", "[rhi][format]")
{
    for (const Format format : {Format::Undefined, Format::D24UnormS8Uint, Format::D32FloatS8Uint})
        CHECK_THROWS_AS(ComputeTextureFootprint(format, {4u, 4u, 1u}), std::invalid_argument);
    CHECK_THROWS_AS(ComputeTextureFootprint(Format::BC1Unorm, {0u, 4u, 1u}), std::invalid_argument);
    CHECK_THROWS_AS(ComputeTextureFootprint(Format::BC1Unorm, {4u, 0u, 1u}), std::invalid_argument);
    CHECK_THROWS_AS(ComputeTextureFootprint(Format::BC1Unorm, {4u, 4u, 0u}), std::invalid_argument);
    CHECK_THROWS_AS(ComputeTextureFootprint(Format::BC1Unorm, {4u, 4u, 1u}, 0u),
                    std::invalid_argument);
    constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
    CHECK(ComputeTextureFootprint(Format::BC1Unorm, {kMax, 1u, 1u}).RowBytes == 8589934592ull);
    CHECK_THROWS_AS(ComputeTextureFootprint(Format::RGBA32Float, {kMax, kMax, 1u}),
                    std::overflow_error);
    CHECK_THROWS_AS(ComputeTextureFootprint(Format::BC1Unorm, {kMax, kMax, 3u}),
                    std::overflow_error);
    CHECK_THROWS_AS(ComputeTextureFootprint(Format::BC7Unorm, {kMax, 4u, 1u}, kMax),
                    std::overflow_error);
}

TEST_CASE("Uploads validate complete payloads before staging", "[rhi][upload]")
{
    TextureDesc desc{.Format = Format::RGBA8Unorm,
                     .Extent = {2u, 2u, 1u},
                     .MipLevels = 2u,
                     .Usage = TextureUsage::Sampled | TextureUsage::CopyDst};
    const std::array<std::byte, 16u> pixels{};
    std::array uploads{
        TextureUpload{.Data = pixels, .Extent = {2u, 2u, 1u}},
        TextureUpload{.Data = std::span(pixels).first(4), .MipLevel = 1u, .Extent = {1u, 1u, 1u}}};
    CHECK(ValidateTextureUploads(desc, uploads) == 20u);
    CHECK_THROWS(ValidateTextureUploads(desc, std::span(uploads).first(1)));
    SECTION("wrong byte count")
    {
        uploads[1].Data = pixels;
    }
    SECTION("overlap")
    {
        uploads[1] = uploads[0];
    }
    SECTION("bad mip")
    {
        uploads[1].MipLevel = 32u;
    }
    SECTION("bad layer")
    {
        uploads[1].LayerCount = 2u;
    }
    SECTION("partial mip")
    {
        uploads[0].Extent.Width = 1u;
    }
    SECTION("wrong aspect")
    {
        uploads[0].Aspect = TextureAspect::Depth;
    }
    SECTION("missing usage")
    {
        desc.Usage = TextureUsage::Sampled;
    }
    CHECK_THROWS(ValidateTextureUploads(desc, uploads));
}

TEST_CASE("Staging accounts for tiny mips and non-power-of-two texel alignment", "[rhi][upload]")
{
    const std::array<std::byte, 12u> pixels{};
    TextureDesc desc{.Format = Format::R8Unorm,
                     .Extent = {3u, 1u, 1u},
                     .MipLevels = 2u,
                     .Usage = TextureUsage::Sampled | TextureUsage::CopyDst};
    const std::array uploads{
        TextureUpload{.Data = std::span(pixels).first(3), .Extent = {3u, 1u, 1u}},
        TextureUpload{.Data = std::span(pixels).first(1), .MipLevel = 1u, .Extent = {1u, 1u, 1u}}};
    CHECK(ValidateTextureUploads(desc, uploads) == 5u);
    CHECK(TextureStagingAlignment(Format::RGB32Float) == 12u);
    CHECK(TextureStagingAlignment(Format::RGBA32Float) == 16u);
}

TEST_CASE("Texture copy validation rejects unsupported formats and overflowing offsets",
          "[rhi][format][bc]")
{
    TextureDesc desc{.Format = Format::RGBA8Unorm, .Extent = {8u, 8u, 1u}};
    BufferTextureCopyRegion region{.Extent = {4u, 4u, 1u}};
    CHECK(ValidateTextureCopy(desc, region, "test").TotalBytes == 64u);
    SECTION("misaligned compressed block")
    {
        desc.Format = Format::BC1Unorm;
        region.BufferOffset = 4u;
    }
    SECTION("depth stencil")
    {
        desc.Format = Format::D24UnormS8Uint;
    }
    SECTION("overflow")
    {
        region.BufferOffset = std::numeric_limits<uint64_t>::max() - 3u;
    }
    SECTION("misaligned")
    {
        region.BufferOffset = 1u;
    }
    SECTION("extent")
    {
        region.Extent.Width = 9u;
    }
    CHECK_THROWS(ValidateTextureCopy(desc, region, "test"));
}

TEST_CASE("BC descriptions enforce the agreed shape scope", "[rhi][format][bc]")
{
    TextureDesc desc{.Format = Format::BC7Srgb,
                     .Extent = {12u, 12u, 1u},
                     .MipLevels = 4u,
                     .ArrayLayers = 6u,
                     .Usage = TextureUsage::Sampled,
                     .bCubeCompatible = true};
    CHECK_NOTHROW(ValidateTextureDesc(desc));
    SECTION("cube array")
    {
        desc.ArrayLayers = 12u;
    }
    SECTION("2D array")
    {
        desc.bCubeCompatible = false;
    }
    SECTION("volume")
    {
        desc.Dimension = TextureDimension::Texture3D;
    }
    SECTION("multisample")
    {
        desc.Samples = SampleCount::X4;
    }
    SECTION("too many mips")
    {
        desc.MipLevels = 5u;
    }
    CHECK_THROWS(ValidateTextureDesc(desc));
}

TEST_CASE("BC copy regions respect block boundaries and logical mip edges", "[rhi][format][bc]")
{
    for (const Format format : {Format::BC1Unorm, Format::BC7Unorm})
    {
        const uint32_t blockBytes = BytesPerBlock(format);
        TextureDesc desc{.Format = format,
                         .Extent = {12u, 12u, 1u},
                         .MipLevels = 4u,
                         .ArrayLayers = 6u,
                         .bCubeCompatible = true};
        CHECK(TextureStagingAlignment(format) == blockBytes);
        CHECK(ValidateTextureCopy(desc,
                                  {.BufferOffset = 3u * blockBytes,
                                   .BaseLayer = 2u,
                                   .LayerCount = 3u,
                                   .Extent = {8u, 4u, 1u}},
                                  "test")
                  .TotalBytes == 6u * blockBytes);
        CHECK(ValidateTextureCopy(desc, {.MipLevel = 1u, .Extent = {6u, 4u, 1u}}, "test")
                  .TotalBytes == 2u * blockBytes);
        CHECK(ValidateTextureCopy(desc, {.MipLevel = 2u, .Extent = {3u, 3u, 1u}}, "test")
                  .TotalBytes == blockBytes);
        CHECK(ValidateTextureCopy(desc, {.MipLevel = 3u, .Extent = {1u, 1u, 1u}}, "test")
                  .TotalBytes == blockBytes);
        CHECK_THROWS(ValidateTextureCopy(desc, {.Extent = {6u, 4u, 1u}}, "test"));
        CHECK_THROWS(ValidateTextureCopy(desc, {.Extent = {4u, 6u, 1u}}, "test"));
        CHECK_THROWS(ValidateTextureCopy(
            desc, {.BufferOffset = blockBytes / 2u, .Extent = {4u, 4u, 1u}}, "test"));
        CHECK_THROWS(ValidateTextureCopy(
            desc,
            {.BufferOffset = std::numeric_limits<uint64_t>::max() - blockBytes + 1u,
             .Extent = {8u, 4u, 1u}},
            "test"));
        CHECK_THROWS(ValidateTextureCopy(desc, {.Extent = {0u, 4u, 1u}}, "test"));
    }
}

TEST_CASE("BC upload validation requires exact blocks and complete face coverage",
          "[rhi][upload][bc]")
{
    const TextureDesc desc{.Format = Format::BC1Unorm,
                           .Extent = {4u, 4u, 1u},
                           .MipLevels = 3u,
                           .ArrayLayers = 6u,
                           .Usage = TextureUsage::Sampled | TextureUsage::CopyDst,
                           .bCubeCompatible = true};
    const std::array<std::byte, 48u> blocks{};
    std::array uploads{
        TextureUpload{.Data = blocks, .MipLevel = 2u, .LayerCount = 6u, .Extent = {1u, 1u, 1u}},
        TextureUpload{.Data = blocks, .MipLevel = 0u, .LayerCount = 6u, .Extent = {4u, 4u, 1u}},
        TextureUpload{.Data = std::span(blocks).first(16u),
                      .MipLevel = 1u,
                      .BaseLayer = 4u,
                      .LayerCount = 2u,
                      .Extent = {2u, 2u, 1u}},
        TextureUpload{.Data = std::span(blocks).first(32u),
                      .MipLevel = 1u,
                      .BaseLayer = 0u,
                      .LayerCount = 4u,
                      .Extent = {2u, 2u, 1u}}};
    CHECK(ValidateTextureUploads(desc, uploads) == 144u);
    SECTION("truncated tiny mip block")
    {
        uploads[0].Data = std::span(blocks).first(47u);
    }
    SECTION("extra bytes")
    {
        uploads[2].Data = blocks;
    }
    SECTION("missing face")
    {
        uploads[0].LayerCount = 5u;
        uploads[0].Data = std::span(blocks).first(40u);
    }
    SECTION("overlapping face groups")
    {
        uploads[2].BaseLayer = 3u;
    }
    SECTION("partial mip")
    {
        uploads[1].Extent = {2u, 2u, 1u};
    }
    CHECK_THROWS(ValidateTextureUploads(desc, uploads));
}
