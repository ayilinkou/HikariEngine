#pragma once

#include <cstdint>
#include <limits>

#include <asset/TextureMetadata.h>
#include <rhi/SamplerDesc.h>

#include "shaders/ShaderTypes.h"

namespace Hikari::Engine
{
/** One sampler serves textures with different chain lengths; views limit each chain. */
inline Rhi::SamplerDesc SurfaceSamplerDesc()
{
    return {.MaxLod = std::numeric_limits<float>::max(),
            .bAnisotropyEnable = true,
            .DebugName = "Texture Sampler"};
}

/** Saved encoding wins; metadata-free native BC5 still has only two normal channels. */
inline uint32_t NormalShaderEncoding(const Asset::TextureMetadata& metadata, Rhi::Format format)
{
    if (metadata.NormalEncoding)
    {
        switch (*metadata.NormalEncoding)
        {
            case Asset::TextureNormalEncoding::XyzUnorm:
                return NormalEncodingXyzUnorm;
            case Asset::TextureNormalEncoding::XyUnormPositiveZ:
                return NormalEncodingXyUnormPositiveZ;
            case Asset::TextureNormalEncoding::XySnormPositiveZ:
                return NormalEncodingXySnormPositiveZ;
            case Asset::TextureNormalEncoding::YxUnormPositiveZ:
                return NormalEncodingYxUnormPositiveZ;
            case Asset::TextureNormalEncoding::AgUnormPositiveZ:
                return NormalEncodingAgUnormPositiveZ;
        }
    }
    if (metadata.DdsSwizzle == Asset::DdsChannelSwizzle::SwapRedGreen)
        return NormalEncodingYxUnormPositiveZ;
    if (metadata.DdsSwizzle == Asset::DdsChannelSwizzle::SwapRedAlpha)
        return NormalEncodingAgUnormPositiveZ;
    if (format == Rhi::Format::BC5Unorm)
        return NormalEncodingXyUnormPositiveZ;
    if (format == Rhi::Format::BC5Snorm)
        return NormalEncodingXySnormPositiveZ;
    return NormalEncodingXyzUnorm;
}
} // namespace Hikari::Engine
