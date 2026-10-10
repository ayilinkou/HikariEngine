#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Hikari::Asset
{

// Share strict JSON syntax and duplicate-key rejection across settings/descriptors.
std::optional<nlohmann::json> ParseSettingsJson(std::string_view text, std::string_view path,
                                                std::string& error);

} // namespace Hikari::Asset
