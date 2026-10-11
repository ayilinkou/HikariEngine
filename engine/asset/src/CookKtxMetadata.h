#pragma once

#include <string>

#include <ktx.h>

#include <asset/DdsTextureInfo.h>
#include <asset/ResolvedTextureSettings.h>

namespace Hikari::Asset
{

bool SetCookKtxMetadata(ktxTexture2* texture, const ResolvedTextureSettings& settings,
                        DdsAlphaMode alpha, std::string& error);

} // namespace Hikari::Asset
