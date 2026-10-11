#pragma once

#include <filesystem>

#include <asset/TextureCookResult.h>

namespace Hikari::Asset
{

struct TextureCookBatchEntry
{
    std::filesystem::path Source;
    TextureCookResult Result;
};

} // namespace Hikari::Asset
