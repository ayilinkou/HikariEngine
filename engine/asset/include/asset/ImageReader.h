#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <core/Extent2D.h>

namespace Hikari::Asset
{

/**
 * Decoded pixels and the extent they describe: four 8-bit channels, tightly
 * packed with no padding between rows — the same shape WritePng takes, so a
 * write and a read round-trip without either side reinterpreting anything.
 */
struct Image
{
    std::vector<uint8_t> Pixels;
    Core::Extent2D Extent;
    // Original file channels, retained before forcing decoded pixels to RGBA.
    uint32_t SourceChannels = 0;
};

/**
 * Decodes a PNG to 8-bit RGBA, whatever channel count the file itself carries.
 *
 * Forced to four pixel channels: every caller either compares against
 * WritePng's output or re-encodes through it, and both
 * are always RGBA, so a three-channel decode would silently change the stride
 * underneath them. Returns nothing and logs on failure.
 */
[[nodiscard]] std::optional<Image> ReadPng(const std::string& path);

} // namespace Hikari::Asset
