#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Hikari::Rhi
{
/**
 * Whether `adapterName` contains `requested`, ignoring ASCII case.
 *
 * Shared by both backends so that DeviceDesc::Gpu means the same test whichever
 * one reads it, and neutral so that neither backend reaches into the other's
 * sources for it. Case-insensitive because the request is typed by hand on a
 * command line and the name is whatever the driver chose to capitalise. An
 * empty request matches everything, which is what leaving the field alone means.
 */
inline bool AdapterNameMatches(std::string_view adapterName, std::string_view requested)
{
    const auto folded = [](char c) { return std::tolower(static_cast<unsigned char>(c)); };
    const auto found = std::ranges::search(adapterName, requested, {}, folded, folded);
    return requested.empty() || !found.empty();
}

/**
 * The adapters a backend considered, one per line, for a refusal to list.
 * Each entry is a name followed by what became of it.
 */
inline std::string DescribeAdapters(const std::vector<std::string>& adapters)
{
    std::string text;
    for (const std::string& adapter : adapters)
        text += "\n  " + adapter;

    return adapters.empty() ? std::string("\n  (none)") : text;
}
} // namespace Hikari::Rhi
