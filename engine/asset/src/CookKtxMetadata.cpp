#include "CookKtxMetadata.h"

#include <string_view>

#include <KHR/khr_df.h>

namespace Hikari::Asset
{

bool SetCookKtxMetadata(ktxTexture2* texture, const ResolvedTextureSettings& settings,
                        DdsAlphaMode alpha, std::string& error)
{
    const auto metadata = [&](const char* key, std::string_view value)
    {
        const std::string terminated(value);
        const auto status = ktxHashList_AddKVPair(&texture->kvDataHead, key,
                                                  static_cast<unsigned int>(terminated.size() + 1),
                                                  terminated.c_str());
        if (status != KTX_SUCCESS)
            error = ktxErrorString(status);
        return status == KTX_SUCCESS;
    };
    if (!metadata(KTX_ORIENTATION_KEY, "rd") ||
        !metadata("HikariTextureUsage", ToString(settings.Authored.Usage)))
        return false;
    std::string_view name;
    switch (alpha)
    {
        case DdsAlphaMode::Unknown:
            break;
        case DdsAlphaMode::Straight:
            name = "straight";
            break;
        case DdsAlphaMode::Premultiplied:
            name = "premultiplied";
            break;
        case DdsAlphaMode::Opaque:
            name = "opaque";
            break;
        case DdsAlphaMode::Custom:
            name = "custom";
            break;
    }
    if (!name.empty() && !metadata("HikariDdsAlphaMode", name))
        return false;
    if (alpha == DdsAlphaMode::Premultiplied)
        KHR_DFDSETVAL(texture->pDfd + 1, FLAGS, KHR_DF_FLAG_ALPHA_PREMULTIPLIED);
    return true;
}

} // namespace Hikari::Asset
