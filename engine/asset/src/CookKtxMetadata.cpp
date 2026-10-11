#include "CookKtxMetadata.h"

#include <format>
#include <string_view>

#include <KHR/khr_df.h>

namespace Hikari::Asset
{

bool SetCookKtxMetadata(ktxTexture2* texture, const ResolvedTextureSettings& settings,
                        DdsAlphaMode alpha, std::string& error, const DdsTextureInfo* source)
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
    // The cooking policy is separate from DDS alpha representation. Omit it
    // when usage is unresolved rather than publishing an opaque assumption.
    if (settings.bAlphaUsageKnown)
    {
        if (!metadata("HikariTextureAlphaMode", ToString(settings.AlphaMode)))
            return false;
        if (settings.AlphaMode == TextureAlphaMode::Mask &&
            !metadata("HikariTextureAlphaCutoff", std::format("{}", settings.Authored.AlphaCutoff)))
            return false;
    }
    if (settings.Authored.Usage == TextureUsage::Normal)
    {
        // Signed BC5 samples XY directly; UNORM requires remapping to [-1, 1].
        // Both reconstruct positive Z. RGB formats retain all three components.
        std::string_view encoding =
            settings.Format == TextureCompression::BC5Signed ? "xy-snorm-positive-z"
            : settings.Format == TextureCompression::BC5     ? "xy-unorm-positive-z"
                                                             : "xyz-unorm";
        if (source && source->IsCompressed())
        {
            if (source->ChannelSwizzle == DdsChannelSwizzle::SwapRedGreen)
                encoding = "yx-unorm-positive-z";
            else if (source->ChannelSwizzle == DdsChannelSwizzle::SwapRedAlpha)
                encoding = "ag-unorm-positive-z";
        }
        if (!metadata("HikariNormalEncoding", encoding))
            return false;
    }
    if (source && source->bNormalMap && !metadata("HikariDdsNormalMap", "true"))
        return false;
    if (source && source->ChannelSwizzle != DdsChannelSwizzle::None &&
        !metadata("HikariDdsSwizzle",
                  source->ChannelSwizzle == DdsChannelSwizzle::SwapRedGreen ? "A2XY" : "A2D5"))
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
