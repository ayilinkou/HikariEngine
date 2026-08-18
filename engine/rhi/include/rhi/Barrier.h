#pragma once

#include <array>
#include <cstdint>

#include <rhi/RhiTypes.h>

// The neutral three-way barrier split.
//
// Vulkan's VK_KHR_synchronization2 splits a barrier into VkPipelineStageFlags2,
// VkAccessFlags2 and VkImageLayout. D3D12's Enhanced Barriers splits it into
// D3D12_BARRIER_SYNC, D3D12_BARRIER_ACCESS and D3D12_BARRIER_LAYOUT — the same
// shape, so the RHI exposes that shape directly rather than inventing a fourth.
//
// The shape being shared is not the same as the enumerators being
// interchangeable. Two consequences are recorded here because they are the
// reason these three types convert in one direction only:
//
//   * Several neutral values collapse onto one Vulkan value. DepthStencil is
//     two Vulkan stage bits; Draw and AllGraphics are both eAllGraphics;
//     Common and UnorderedAccess are both eGeneral. A ToVk mapping is
//     well-defined, a FromVk one is not, so the backend provides only ToVk for
//     these three. Nothing needs the reverse: barriers are always described in
//     neutral terms and consumed by the backend.
//   * D3D12_BARRIER_LAYOUT has queue-type-specific variants that may only be
//     used on a compatible queue, and requires copy-queue resources to be in
//     COMMON. Common exists in TextureLayout to express exactly that, and is
//     what R12's queue-family ownership transfer will reach for.
namespace Rhi
{
// -> VkPipelineStageFlags2 / D3D12_BARRIER_SYNC
//
// A stage is a *point in the pipeline*, and these are listed roughly in the
// order work passes through them. That ordering is the whole point of a stage
// mask: a barrier says "everything up to stage X must finish before anything
// from stage Y begins", so naming a stage that is too late as the source, or
// too early as the destination, under-synchronizes.
//
// The stages a draw passes through, in order, and what each one covers:
//
//   VertexStage    the vertex shader running: reads vertex and index buffers,
//                  and any uniform or storage buffer it samples.
//   PixelStage     the fragment shader running: texture sampling, and reads of
//                  anything else bound to it.
//   DepthStencil   the depth and stencil test, and writes to the depth buffer.
//                  Also where a depth attachment is loaded and stored.
//   RenderTarget   the far end of the pipeline, where the shaded colour is
//                  blended and written to the colour attachment. Also where a
//                  colour attachment is loaded, stored and resolved.
//
// So PixelStage and RenderTarget are adjacent but not interchangeable: the
// first is the shader producing a colour, the second is that colour being
// written to memory. Sampling a texture is PixelStage + ShaderRead; rendering
// into one is RenderTarget + RenderTargetWrite.
//
// None is the sync2 idiom for "no stage", and is what replaces the legacy
// eTopOfPipe / eBottomOfPipe pair: as a source it means nothing needs to be
// waited on, as a destination that nothing is being released to.
enum class PipelineStage : uint32_t
{
    None = 0,
    Draw = 1 << 0,
    VertexStage = 1 << 1,
    PixelStage = 1 << 2,
    ComputeStage = 1 << 3,
    DepthStencil = 1 << 4,
    RenderTarget = 1 << 5,
    Copy = 1 << 6,
    Resolve = 1 << 7,
    AllGraphics = 1 << 8,
    All = 1 << 9,
};
RHI_DEFINE_FLAG_OPERATORS(PipelineStage)

inline constexpr std::array kAllPipelineStages{
    PipelineStage::Draw,         PipelineStage::VertexStage,  PipelineStage::PixelStage,
    PipelineStage::ComputeStage, PipelineStage::DepthStencil, PipelineStage::RenderTarget,
    PipelineStage::Copy,         PipelineStage::Resolve,      PipelineStage::AllGraphics,
    PipelineStage::All,
};

// -> VkAccessFlags2 / D3D12_BARRIER_ACCESS
enum class AccessFlags : uint32_t
{
    None = 0,
    VertexBufferRead = 1 << 0,
    IndexBufferRead = 1 << 1,
    ConstantBufferRead = 1 << 2,
    ShaderRead = 1 << 3,
    UnorderedAccess = 1 << 4,
    RenderTargetWrite = 1 << 5,
    DepthStencilRead = 1 << 6,
    DepthStencilWrite = 1 << 7,
    CopySrc = 1 << 8,
    CopyDst = 1 << 9,
};
RHI_DEFINE_FLAG_OPERATORS(AccessFlags)

inline constexpr std::array kAllAccessFlags{
    AccessFlags::VertexBufferRead, AccessFlags::IndexBufferRead,   AccessFlags::ConstantBufferRead,
    AccessFlags::ShaderRead,       AccessFlags::UnorderedAccess,   AccessFlags::RenderTargetWrite,
    AccessFlags::DepthStencilRead, AccessFlags::DepthStencilWrite, AccessFlags::CopySrc,
    AccessFlags::CopyDst,
};

// -> VkImageLayout / D3D12_BARRIER_LAYOUT
enum class TextureLayout : uint32_t
{
    Undefined = 0,
    Common,
    RenderTarget,
    ShaderResource,
    UnorderedAccess,
    DepthStencilWrite,
    DepthStencilRead,
    CopySrc,
    CopyDst,
    Present,
};

inline constexpr std::array kAllTextureLayouts{
    TextureLayout::Undefined,        TextureLayout::Common,
    TextureLayout::RenderTarget,     TextureLayout::ShaderResource,
    TextureLayout::UnorderedAccess,  TextureLayout::DepthStencilWrite,
    TextureLayout::DepthStencilRead, TextureLayout::CopySrc,
    TextureLayout::CopyDst,          TextureLayout::Present,
};

// Which parts of a texture a barrier or a view refers to. Kept separate from
// Format because a depth/stencil format has two aspects and an operation
// usually names one of them.
enum class TextureAspect : uint32_t
{
    None = 0,
    Color = 1 << 0,
    Depth = 1 << 1,
    Stencil = 1 << 2,
};
RHI_DEFINE_FLAG_OPERATORS(TextureAspect)

inline constexpr std::array kAllTextureAspects{
    TextureAspect::Color,
    TextureAspect::Depth,
    TextureAspect::Stencil,
};

// The aspect mask a barrier or view should use for `format`, so that the
// depth/stencil decision is made in one place rather than at each call site.
constexpr TextureAspect DefaultAspect(Format format)
{
    if (!IsDepthFormat(format))
        return TextureAspect::Color;

    return HasStencilComponent(format) ? (TextureAspect::Depth | TextureAspect::Stencil)
                                       : TextureAspect::Depth;
}
} // namespace Rhi
