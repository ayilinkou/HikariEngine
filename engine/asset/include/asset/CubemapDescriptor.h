#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <asset/TextureSettings.h>

namespace Hikari::Asset
{

// DDS/KTX2 face order, with no filename-based orientation inference.
inline constexpr std::array<std::string_view, 6> kCubemapFaceNames{
    "positiveX", "negativeX", "positiveY", "negativeY", "positiveZ", "negativeZ"};

struct CubemapDescriptor
{
    TextureSettings Settings;
    // Paths are relative to the descriptor's directory.
    std::array<std::filesystem::path, 6> Faces;
};

std::optional<CubemapDescriptor> ParseCubemapDescriptor(std::string_view text,
                                                        std::string_view path, std::string& error);
std::optional<std::string> SerializeCubemapDescriptor(const CubemapDescriptor& descriptor,
                                                      std::string_view path, std::string& error);
// An editable template with all six face paths unset. Strict descriptor parsing
// and serialization still require nonempty paths for actual cooking requests.
std::optional<std::string> SerializeCubemapTemplate(const TextureSettings& settings,
                                                    std::string_view path, std::string& error);

} // namespace Hikari::Asset
