#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <rhi/RhiTypes.h>

namespace Hikari::Rhi
{
enum class Filter : uint8_t
{
    Nearest = 0,
    Linear,
};

inline constexpr std::array kAllFilters{
    Filter::Nearest,
    Filter::Linear,
};

enum class MipmapMode : uint8_t
{
    Nearest = 0,
    Linear,
};

inline constexpr std::array kAllMipmapModes{
    MipmapMode::Nearest,
    MipmapMode::Linear,
};

enum class AddressMode : uint8_t
{
    Repeat = 0,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

inline constexpr std::array kAllAddressModes{
    AddressMode::Repeat,
    AddressMode::MirroredRepeat,
    AddressMode::ClampToEdge,
    AddressMode::ClampToBorder,
};

enum class CompareOp : uint8_t
{
    Never = 0,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

inline constexpr std::array kAllCompareOps{
    CompareOp::Never,   CompareOp::Less,     CompareOp::Equal,          CompareOp::LessOrEqual,
    CompareOp::Greater, CompareOp::NotEqual, CompareOp::GreaterOrEqual, CompareOp::Always,
};

/**
 * The colour returned when AddressMode::ClampToBorder samples outside the
 * image. Both the float and the integer families are listed because Vulkan
 * keeps them as distinct enumerators and expects the choice to match whether
 * the sampled format is integer; D3D12 takes a float4 (with a matching UINT
 * variant for static samplers) and so can express either.
 */
enum class BorderColor : uint8_t
{
    TransparentBlackFloat = 0,
    OpaqueBlackFloat,
    OpaqueWhiteFloat,
    TransparentBlackInt,
    OpaqueBlackInt,
    OpaqueWhiteInt,
};

inline constexpr std::array kAllBorderColors{
    BorderColor::TransparentBlackFloat, BorderColor::OpaqueBlackFloat,
    BorderColor::OpaqueWhiteFloat,      BorderColor::TransparentBlackInt,
    BorderColor::OpaqueBlackInt,        BorderColor::OpaqueWhiteInt,
};

struct SamplerDesc
{
    Filter MagFilter = Filter::Linear;
    Filter MinFilter = Filter::Linear;
    MipmapMode MipmapFilter = MipmapMode::Linear;

    AddressMode AddressU = AddressMode::Repeat;
    AddressMode AddressV = AddressMode::Repeat;
    AddressMode AddressW = AddressMode::Repeat;

    float MipLodBias = 0.f;
    float MinLod = 0.f;
    float MaxLod = 0.f;

    /**
     * MaxAnisotropy is ignored unless bAnisotropyEnable is set, and must not
     * exceed the device's maxSamplerAnisotropy limit. A value of 0 means "use
     * the device maximum", so callers do not have to plumb the limit through
     * just to ask for the best available filtering.
     */
    bool bAnisotropyEnable = false;
    float MaxAnisotropy = 0.f;

    /**
     * A comparison sampler, used for shadow-map style lookups that return a
     * filtered pass/fail rather than a depth value.
     */
    bool bCompareEnable = false;
    CompareOp Compare = CompareOp::Always;

    BorderColor Border = BorderColor::OpaqueBlackInt;

    std::string DebugName;
};
} // namespace Hikari::Rhi
