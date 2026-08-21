#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

// The RHI's neutral vocabulary: the scalar types that appear in resource
// descriptions and at the public API boundary. No Vulkan, no VMA, no D3D12.
//
// Every enum here is paired with a conversion table in the backend
// (src/vulkan/VulkanConversions.h). Adding an enumerator means adding its
// mapping in the same commit: the ToVk switches carry no `default:` label, so
// the compiler rejects the build until the mapping exists.
//
// --- Why each enum also has a kAll* array ---
//
// C++ cannot iterate an enum's enumerators, so anything that needs to visit
// every value has to be handed a list. Three things need that, and all three
// would otherwise be hand-maintained copies of the enum:
//
//   1. Converting a flags value. ToVk(BufferUsage) has to decompose a bitmask
//      into individual bits before it can map them, and looping over kAll*
//      is the only way to ask "which bits are set" without hard-coding the
//      list at the loop.
//   2. Deriving the reverse mapping. FromVk searches for the neutral value
//      whose ToVk matches, rather than being a second hand-written table that
//      could disagree with the first.
//   3. The tests. They iterate kAll* so that a new enumerator is covered the
//      moment it is added, instead of being covered only if someone remembers
//      to extend the test as well.
//
// Not every array serves all three: the scalar enums are not decomposed (1),
// and the ones with no reverse mapping skip (2). QueueType is used only by the
// tests. They exist uniformly anyway, because the cost is a line per
// enumerator and the alternative is remembering which enums have one.
//
// Forgetting to add a new enumerator to its array is the one mistake here the
// compiler cannot catch. For the flags enums it is caught at runtime instead —
// ToVk throws on a bit it could not account for, rather than silently dropping
// it (see ConvertFlags). For the rest it is a review item.
namespace Rhi
{
// Generates the bitwise operators a flags enum needs. Scoped enums have none
// by default, which is what stops a flags enum being silently mixed with an
// unrelated one.
//
// `Any` and `HasAll` exist because `flags & mask` yields the enum type, not a
// bool, and `if (flags & mask)` therefore does not compile — deliberately, so
// the intent (any bit vs every bit) has to be written down.
#define RHI_DEFINE_FLAG_OPERATORS(EnumType)                                                        \
    constexpr EnumType operator|(EnumType a, EnumType b)                                           \
    {                                                                                              \
        using U = std::underlying_type_t<EnumType>;                                                \
        return static_cast<EnumType>(static_cast<U>(a) | static_cast<U>(b));                       \
    }                                                                                              \
    constexpr EnumType operator&(EnumType a, EnumType b)                                           \
    {                                                                                              \
        using U = std::underlying_type_t<EnumType>;                                                \
        return static_cast<EnumType>(static_cast<U>(a) & static_cast<U>(b));                       \
    }                                                                                              \
    constexpr EnumType operator^(EnumType a, EnumType b)                                           \
    {                                                                                              \
        using U = std::underlying_type_t<EnumType>;                                                \
        return static_cast<EnumType>(static_cast<U>(a) ^ static_cast<U>(b));                       \
    }                                                                                              \
    constexpr EnumType operator~(EnumType a)                                                       \
    {                                                                                              \
        using U = std::underlying_type_t<EnumType>;                                                \
        return static_cast<EnumType>(~static_cast<U>(a));                                          \
    }                                                                                              \
    constexpr EnumType& operator|=(EnumType& a, EnumType b)                                        \
    {                                                                                              \
        return a = a | b;                                                                          \
    }                                                                                              \
    constexpr EnumType& operator&=(EnumType& a, EnumType b)                                        \
    {                                                                                              \
        return a = a & b;                                                                          \
    }                                                                                              \
    constexpr EnumType& operator^=(EnumType& a, EnumType b)                                        \
    {                                                                                              \
        return a = a ^ b;                                                                          \
    }                                                                                              \
    constexpr bool Any(EnumType a)                                                                 \
    {                                                                                              \
        return static_cast<std::underlying_type_t<EnumType>>(a) != 0;                              \
    }                                                                                              \
    constexpr bool HasAll(EnumType value, EnumType mask)                                           \
    {                                                                                              \
        return (value & mask) == mask;                                                             \
    }

// Pixel formats. Curated rather than a mirror of VkFormat: every entry has both
// a VkFormat and a DXGI_FORMAT equivalent, so this list is a
// portability promise and not just a convenience.
//
// Two deliberate omissions, both from the depth-format candidate list the app
// currently searches in FindDepthFormat:
//
//   * D16UnormS8Uint has no DXGI equivalent. dxgiformat.h offers stencil only
//     alongside 24-bit unorm or 32-bit float depth; the enum runs straight
//     from DXGI_FORMAT_D16_UNORM to DXGI_FORMAT_R16_UNORM. A depth+stencil
//     format at 16-bit depth cannot be expressed, so it is left out here and
//     R10 has to drop it from the candidate list or accept a promotion.
//   * The vertex-attribute formats (R32G32B32A32Sfloat and friends) are absent
//     because vertex input stays Vulkan-side for the whole of Stage 5 (D8).
//     They are portable and belong here when pipeline creation is neutralized.
enum class Format : uint32_t
{
    Undefined = 0,

    R8Unorm,
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    RGBA16Float,

    D16Unorm,
    D32Float,
    D24UnormS8Uint,
    D32FloatS8Uint,
};

inline constexpr std::array kAllFormats{
    Format::Undefined,      Format::R8Unorm,        Format::RGBA8Unorm, Format::RGBA8Srgb,
    Format::BGRA8Unorm,     Format::RGBA16Float,    Format::D16Unorm,   Format::D32Float,
    Format::D24UnormS8Uint, Format::D32FloatS8Uint,
};

// Whether `format` carries a depth component, and so needs a depth aspect
// rather than a colour one when it appears in a barrier or a view.
constexpr bool IsDepthFormat(Format format)
{
    return format == Format::D16Unorm || format == Format::D32Float ||
           format == Format::D24UnormS8Uint || format == Format::D32FloatS8Uint;
}

// Whether `format` also carries a stencil component. Separate from
// IsDepthFormat because the stencil aspect has to be named explicitly in a
// subresource range, and getting it wrong is a validation error rather than a
// visible one.
constexpr bool HasStencilComponent(Format format)
{
    return format == Format::D24UnormS8Uint || format == Format::D32FloatS8Uint;
}

// The *role* work is submitted for, not a description of a queue. Chosen to
// match D3D12's DIRECT / COMPUTE / COPY command list types, which have no
// notion of a queue family index (plan D6); Vulkan's family indices stay
// inside the backend.
//
// Nothing here says the three roles map to three distinct queues. A Vulkan
// queue family advertises a mask of capabilities, and a "universal" family
// with graphics + compute + transfer is guaranteed to exist on any device that
// supports graphics at all — so one queue may back all three roles, which is
// exactly what this application does today. The backend is free to alias them,
// and R12's point is precisely that it stops aliasing Copy once a dedicated
// transfer family is available.
//
// Presentation is deliberately not a role here. It is not a queue capability
// in Vulkan — support is a property of a (family, surface) pair, queried
// separately — and D3D12 presents from a direct queue with no notion of a
// present queue at all. So "can this queue present" is a question only the
// present path can answer, and it stays behind that seam (plan D5): Stage 6's
// IPresentTarget owns it.
enum class QueueType : uint8_t
{
    Graphics = 0,
    Compute,
    Copy,
};

inline constexpr std::array kAllQueueTypes{
    QueueType::Graphics,
    QueueType::Compute,
    QueueType::Copy,
};

// Where a resource's memory lives and which side writes it. Maps onto VMA's
// usage plus its host-access allocation flags, and onto D3D12's DEFAULT /
// UPLOAD / READBACK heap types.
enum class MemoryAccess : uint8_t
{
    // Device-local, never mapped. The destination of a staged upload.
    GpuOnly = 0,

    // Host-visible and written sequentially by the CPU, then read by the GPU:
    // staging buffers, and the persistently mapped uniform and instance
    // buffers. Sequential write is the important half — VMA may place this in
    // write-combined memory, where a read-modify-write from the CPU is
    // pathologically slow rather than merely uncached.
    CpuToGpu,

    // Host-visible and read back by the CPU after the GPU has written it: the
    // screenshot staging buffer. Random access, so not write-combined.
    GpuToCpu,
};

inline constexpr std::array kAllMemoryAccesses{
    MemoryAccess::GpuOnly,
    MemoryAccess::CpuToGpu,
    MemoryAccess::GpuToCpu,
};

// Values are the sample counts themselves, so a count can be converted to a
// bit position arithmetically rather than through another table.
enum class SampleCount : uint8_t
{
    X1 = 1,
    X2 = 2,
    X4 = 4,
    X8 = 8,
    X16 = 16,
};

inline constexpr std::array kAllSampleCounts{
    SampleCount::X1, SampleCount::X2, SampleCount::X4, SampleCount::X8, SampleCount::X16,
};

struct Extent2D
{
    uint32_t Width = 0;
    uint32_t Height = 0;

    constexpr bool operator==(const Extent2D&) const = default;
};

struct Extent3D
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Depth = 1;

    constexpr bool operator==(const Extent3D&) const = default;
};
} // namespace Rhi
