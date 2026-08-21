#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <rhi/RhiTypes.h>

namespace Rhi
{
// What a buffer may be bound as. Every entry maps to a VkBufferUsageFlagBits
// value; on D3D12 most of these are properties of the view rather than the
// resource, which is why the neutral enum describes intent instead of mirroring
// either API's bit layout.
enum class BufferUsage : uint32_t
{
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,

    // Named for the copy direction rather than Vulkan's TransferSrc/TransferDst
    // so that the barrier vocabulary (AccessFlags::CopySrc / CopyDst) and the
    // usage vocabulary agree.
    CopySrc = 1 << 4,
    CopyDst = 1 << 5,
};
RHI_DEFINE_FLAG_OPERATORS(BufferUsage)

inline constexpr std::array kAllBufferUsages{
    BufferUsage::Vertex,  BufferUsage::Index,   BufferUsage::Uniform,
    BufferUsage::Storage, BufferUsage::CopySrc, BufferUsage::CopyDst,
};

struct BufferDesc
{
    uint64_t Size = 0;
    BufferUsage Usage = BufferUsage::None;
    MemoryAccess Access = MemoryAccess::GpuOnly;

    // Attached to the resource via VK_EXT_debug_utils in debug builds, and the
    // D3D12 equivalent is ID3D12Object::SetName. Worth setting on everything:
    // it is what turns a validation message or a captured frame from a handle
    // value into something readable.
    std::string DebugName;
};
} // namespace Rhi
