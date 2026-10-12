#pragma once

#include <cstdint>
#include <filesystem>

#include <asset/TextureSettingsOverrides.h>

namespace Hikari::Asset
{

struct TextureCookRequest
{
    std::filesystem::path Source{};
    TextureSettingsOverrides Overrides{};
    bool bForce = false;
    // Zero leaves thread selection to Compressonator; explicit limits are 1–128.
    uint32_t EncoderThreads = 0;
    // Minimum encoding effort for reuse; force encodes at exactly this quality.
    double EncodingQuality = 1.0;
};

} // namespace Hikari::Asset
