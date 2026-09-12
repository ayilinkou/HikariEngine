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
};

/**
 * Decodes a PNG to 8-bit RGBA, whatever channel count the file itself carries.
 *
 * Forced to four channels rather than reporting the file's own: every caller
 * either compares against WritePng's output or re-encodes through it, and both
 * are always RGBA, so a three-channel decode would silently change the stride
 * underneath them. Returns nothing and logs on failure.
 */
[[nodiscard]] std::optional<Image> ReadPng(const std::string& path);

} // namespace Hikari::Asset
