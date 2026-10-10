#include <asset/CubemapDescriptor.h>

#include <algorithm>
#include <format>

#include "SettingsJson.h"

namespace Hikari::Asset
{

std::optional<CubemapDescriptor> ParseCubemapDescriptor(std::string_view text,
                                                        std::string_view path, std::string& error)
{
    auto document = ParseSettingsJson(text, path, error);
    if (!document)
        return std::nullopt;
    if (!document->contains("faces") || !document->at("faces").is_object())
    {
        error = std::format("{}: Field 'faces': Required object with six named face paths", path);
        return std::nullopt;
    }
    CubemapDescriptor descriptor;
    const auto& faces = document->at("faces");
    for (const auto& [key, value] : faces.items())
    {
        (void)value;
        if (std::find(kCubemapFaceNames.begin(), kCubemapFaceNames.end(), key) ==
            kCubemapFaceNames.end())
        {
            error = std::format("{}: Field 'faces.{}': Unknown cubemap face", path, key);
            return std::nullopt;
        }
    }
    for (size_t i = 0; i < descriptor.Faces.size(); ++i)
    {
        const std::string key(kCubemapFaceNames[i]);
        if (!faces.contains(key) || !faces.at(key).is_string() ||
            faces.at(key).get<std::string>().empty())
        {
            error = std::format("{}: Field 'faces.{}': Required nonempty relative image path", path,
                                key);
            return std::nullopt;
        }
        descriptor.Faces[i] = faces.at(key).get<std::string>();
        if (descriptor.Faces[i].has_root_path() ||
            descriptor.Faces[i].generic_string().find('\0') != std::string::npos)
        {
            error = std::format("{}: Field 'faces.{}': Required relative path without null bytes",
                                path, key);
            return std::nullopt;
        }
    }
    document->erase("faces");
    const auto settings = ParseTextureSettings(document->dump(), path, error);
    if (!settings)
        return std::nullopt;
    descriptor.Settings = *settings;
    return descriptor;
}

std::optional<std::string> SerializeCubemapDescriptor(const CubemapDescriptor& descriptor,
                                                      std::string_view path, std::string& error)
{
    const auto settings = SerializeTextureSettings(descriptor.Settings, path, error);
    if (!settings)
        return std::nullopt;
    auto document = nlohmann::json::parse(*settings);
    for (size_t i = 0; i < descriptor.Faces.size(); ++i)
    {
        if (descriptor.Faces[i].empty() || descriptor.Faces[i].has_root_path() ||
            descriptor.Faces[i].generic_string().find('\0') != std::string::npos)
        {
            error = std::format("{}: Field 'faces.{}': Required relative image path", path,
                                kCubemapFaceNames[i]);
            return std::nullopt;
        }
        document["faces"][std::string(kCubemapFaceNames[i])] = descriptor.Faces[i].generic_string();
    }
    return document.dump(2) + '\n';
}

} // namespace Hikari::Asset
