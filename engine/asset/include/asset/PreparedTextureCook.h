#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <asset/CubemapDescriptor.h>
#include <asset/DdsTextureInfo.h>
#include <asset/ImageInfo.h>
#include <asset/ResolvedTextureSettings.h>
#include <asset/TextureCookPaths.h>
#include <asset/TextureCookRequest.h>

namespace Hikari::Asset
{

struct PreparedTextureCook
{
    TextureCookRequest Request;
    TextureCookPaths Paths;
    ResolvedTextureSettings Settings;
    // One image, or six images in +X, -X, +Y, -Y, +Z, -Z order.
    std::vector<std::filesystem::path> Inputs;
    std::vector<ImageInfo> ImageMetadata;
    std::optional<DdsTextureInfo> Dds;
    std::optional<CubemapDescriptor> Descriptor;
    std::optional<std::string> OriginalSettings;
    bool bWriteSettings = false;
};

} // namespace Hikari::Asset
