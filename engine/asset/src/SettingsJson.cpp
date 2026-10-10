#include "SettingsJson.h"

#include <format>
#include <unordered_set>
#include <vector>

namespace Hikari::Asset
{

std::optional<nlohmann::json> ParseSettingsJson(std::string_view text, std::string_view path,
                                                std::string& error)
{
    using Json = nlohmann::json;
    error.clear();
    std::string currentField;
    try
    {
        // The default object parser replaces duplicate keys. A descriptor must
        // not silently accept two different paths for the same cubemap face.
        std::vector<std::unordered_set<std::string>> keys;
        std::string duplicate;
        const auto callback = [&](int, Json::parse_event_t event, Json& value)
        {
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            else if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            else if (event == Json::parse_event_t::key)
            {
                const auto key = value.get<std::string>();
                currentField = key;
                if (!keys.back().insert(key).second && duplicate.empty())
                    duplicate = key;
            }
            return true;
        };
        Json document = Json::parse(text, callback);
        if (!duplicate.empty())
        {
            error = std::format("{}: Field '{}': Duplicate field", path, duplicate);
            return std::nullopt;
        }
        if (!document.is_object())
        {
            error = std::format("{}: Texture settings must be a JSON object", path);
            return std::nullopt;
        }
        return document;
    }
    catch (const Json::exception& exception)
    {
        if (exception.id == 406 && !currentField.empty())
            error = std::format("{}: Field '{}': Number is outside the supported finite range",
                                path, currentField);
        else
            error = std::format("{}: Invalid JSON: {}", path, exception.what());
        return std::nullopt;
    }
}

} // namespace Hikari::Asset
