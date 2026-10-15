#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <asset/TextureCookBatchEntry.h>

namespace Hikari::Asset
{

struct TextureCookBatchResult
{
    std::vector<TextureCookBatchEntry> Entries;
    /** Discovery/configuration errors are separate from per-texture failures. */
    std::vector<std::string> Errors;

    bool Succeeded() const
    {
        return Errors.empty() &&
               std::none_of(Entries.begin(), Entries.end(), [](const auto& entry)
                            { return entry.Result.Status == TextureCookStatus::Failed; });
    }
};

} // namespace Hikari::Asset
