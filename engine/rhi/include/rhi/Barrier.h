#pragma once

#include <array>
#include <cstdint>

#include <rhi/Handles.h>
#include <rhi/RhiTypes.h>

/**
 * The neutral three-way barrier split.
 *
 * Vulkan's VK_KHR_synchronization2 splits a barrier into VkPipelineStageFlags2,
 * VkAccessFlags2 and VkImageLayout. D3D12's Enhanced Barriers splits it into
 * D3D12_BARRIER_SYNC, D3D12_BARRIER_ACCESS and D3D12_BARRIER_LAYOUT — the same
 * shape, so the RHI exposes that shape directly rather than inventing a fourth.
 *
 * The shape being shared is not the same as the enumerators being
 * interchangeable. Two consequences are recorded here because they are the
 * reason these three types convert in one direction only:
 *
 *   * Several neutral values collapse onto one Vulkan value. DepthStencil is
 *     two Vulkan stage bits; Draw and AllGraphics are both eAllGraphics;
 *     Common and UnorderedAccess are both eGeneral. A ToVk mapping is
 *     well-defined, a FromVk one is not, so the backend provides only ToVk for
 *     these three. Nothing needs the reverse: barriers are always described in
 *     neutral terms and consumed by the backend.
 *   * D3D12_BARRIER_LAYOUT has queue-type-specific variants that may only be
 *     used on a compatible queue, and requires copy-queue resources to be in
 *     COMMON. Common exists in TextureLayout to express exactly that. Moving a
 *     resource between queues is not spelled here at all, though: Vulkan needs
 *     an explicit ownership transfer and D3D12 needs a layout, so the two have
 *     no shared shape, and whichever component submits to a second queue owns
 *     that problem privately (plan D6).
 */
namespace Hikari::Rhi
{
/**
 * -> VkPipelineStageFlags2 / D3D12_BARRIER_SYNC
 *
 * A stage is a *point in the pipeline*, and these are listed roughly in the
 * order work passes through them. That ordering is the whole point of a stage
 * mask: a barrier says "everything up to stage X must finish before anything
 * from stage Y begins", so naming a stage that is too late as the source, or
 * too early as the destination, under-synchronizes.
 *
 * The stages a draw passes through, in order, and what each one covers:
 *
 *   VertexStage    the vertex shader running: reads vertex and index buffers,
 *                  and any uniform or storage buffer it samples.
 *   PixelStage     the fragment shader running: texture sampling, and reads of
 *                  anything else bound to it.
 *   DepthStencil   the depth and stencil test, and writes to the depth buffer.
 *                  Also where a depth attachment is loaded and stored.
 *   RenderTarget   the far end of the pipeline, where the shaded colour is
 *                  blended and written to the colour attachment. Also where a
 *                  colour attachment is loaded, stored and resolved.
 *
 * So PixelStage and RenderTarget are adjacent but not interchangeable: the
 * first is the shader producing a colour, the second is that colour being
 * written to memory. Sampling a texture is PixelStage + ShaderRead; rendering
 * into one is RenderTarget + RenderTargetWrite.
 *
 * None is the sync2 idiom for "no stage", and is what replaces the legacy
 * eTopOfPipe / eBottomOfPipe pair: as a source it means nothing needs to be
 * waited on, as a destination that nothing is being released to.
 */
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

/** -> VkAccessFlags2 / D3D12_BARRIER_ACCESS */
enum class AccessFlags : uint32_t
{
    None = 0,
    VertexBufferRead = 1 << 0,
    IndexBufferRead = 1 << 1,
    ConstantBufferRead = 1 << 2,
    ShaderRead = 1 << 3,
    UnorderedAccess = 1 << 4,
    RenderTargetRead = 1 << 5,
    RenderTargetWrite = 1 << 6,
    DepthStencilRead = 1 << 7,
    DepthStencilWrite = 1 << 8,
    CopySrc = 1 << 9,
    CopyDst = 1 << 10,
};
RHI_DEFINE_FLAG_OPERATORS(AccessFlags)

inline constexpr std::array kAllAccessFlags{
    AccessFlags::VertexBufferRead,  AccessFlags::IndexBufferRead,  AccessFlags::ConstantBufferRead,
    AccessFlags::ShaderRead,        AccessFlags::UnorderedAccess,  AccessFlags::RenderTargetRead,
    AccessFlags::RenderTargetWrite, AccessFlags::DepthStencilRead, AccessFlags::DepthStencilWrite,
    AccessFlags::CopySrc,           AccessFlags::CopyDst,
};

/** -> VkImageLayout / D3D12_BARRIER_LAYOUT */
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

/**
 * One texture transition: which texture, what must finish, what may then begin,
 * and the layout the texture moves between.
 *
 * The presets in <rhi/BarrierPresets.h> leave Texture invalid, which is what
 * lets them be constants rather than functions of a resource; On() pairs a
 * preset with the texture it applies to at the call site. A barrier reaching
 * the backend with an invalid Texture is a bug, and is reported rather than
 * skipped.
 *
 * The src half describes work already submitted, the dst half work not yet
 * recorded — so the defaults are the pair that synchronizes nothing, and every
 * preset sets all four.
 */
struct TextureBarrier
{
    TextureHandle Texture{};

    PipelineStage SrcStage = PipelineStage::None;
    AccessFlags SrcAccess = AccessFlags::None;
    PipelineStage DstStage = PipelineStage::None;
    AccessFlags DstAccess = AccessFlags::None;

    TextureLayout OldLayout = TextureLayout::Undefined;
    TextureLayout NewLayout = TextureLayout::Undefined;

    TextureAspect Aspect = TextureAspect::Color;

    /**
     * The subresources the barrier covers. A texture with more than one layer
     * or mip needs every one of them named, since a layout is a property of a
     * subresource rather than of the whole texture — transitioning layer 0 of a
     * cubemap and then using all six is a validation error at best and reads
     * uninitialized memory at worst.
     */
    uint32_t BaseMip = 0u;
    uint32_t MipCount = 1u;
    uint32_t BaseLayer = 0u;
    uint32_t LayerCount = 1u;

    /**
     * This transition, applied to `texture`. Returns a copy so that a preset
     * stays a constant and several barriers can share one description:
     *
     *     BarrierPresets::UndefinedToRenderTarget().On(m_OpaqueTexture)
     */
    constexpr TextureBarrier On(TextureHandle texture) const
    {
        TextureBarrier result = *this;
        result.Texture = texture;
        return result;
    }
};

/**
 * How much barrier work a stretch of recording produced: the barriers
 * themselves, and the number of commands they were issued in.
 *
 * Both, because neither alone says what it appears to. A barrier count cannot
 * tell one command carrying three barriers from three commands carrying one
 * each — and that difference is the whole point of grouping them, since each
 * command is a separate execution dependency. A call count says nothing about
 * how much was transitioned. Watching the pair is what makes a regression in
 * either visible.
 */
struct BarrierCounts
{
    uint32_t Barriers = 0u;
    uint32_t Calls = 0u;

    constexpr BarrierCounts& operator+=(const BarrierCounts& other)
    {
        Barriers += other.Barriers;
        Calls += other.Calls;
        return *this;
    }

    constexpr bool operator==(const BarrierCounts&) const = default;
};
} // namespace Hikari::Rhi
