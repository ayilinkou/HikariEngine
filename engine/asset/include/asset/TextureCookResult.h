#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hikari::Asset
{

enum class TextureCookStatus
{
    Failed,
    Cooked,
    Skipped
};

struct TextureCookResult
{
    TextureCookStatus Status = TextureCookStatus::Failed;
    std::filesystem::path Output;
    std::string Error;
    std::vector<std::string> Warnings;
};

} // namespace Hikari::Asset
