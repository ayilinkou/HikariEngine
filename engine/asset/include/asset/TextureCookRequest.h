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
    // The CPU tool accepts 1–128 threads, with an upstream default of eight.
    uint32_t EncoderThreads = 8;
};

} // namespace Hikari::Asset
