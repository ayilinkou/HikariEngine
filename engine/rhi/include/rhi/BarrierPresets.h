#pragma once

#include <cstdint>

#include <rhi/Barrier.h>

// The transitions this renderer actually performs, named after what they are
// for rather than assembled from stage and access masks at each call site.
//
// The point is not brevity. A barrier written inline is four masks and two
// layouts that have to agree with each other and with what the surrounding
// passes do, and getting one of them wrong produces code that renders correctly
// on the driver it was written on. Naming the transition means the reasoning is
// done once, in one place, where it can be read and checked against the
// specification — and where a fix reaches every user.
//
// Each preset is a plain value, so a caller can take one and adjust a field
// rather than needing a new preset for a variation.
namespace Rhi::BarrierPresets
{
// The image just acquired from a present target, made ready to render into.
// Named for the acquire rather than for the swapchain because a headless target
// hands one back on exactly the same terms.
//
// Undefined as the old layout is a deliberate discard, not a shortcut past
// naming the real one — and specifically not because the contents are
// unreadable. They are not: reacquiring a presented image and transitioning it
// out of the present layout gives back exactly what was presented, unless
// something outside Vulkan has touched the window (Vulkan 1.4, *Presenting
// Images*). Discarding is correct anyway, because the pass that follows clears
// the whole render area, so naming the previous layout would only make the
// driver preserve pixels that are overwritten a command later.
//
// The source stage is RenderTarget rather than None, and that is load-bearing:
// an acquire hands back semaphores the submit waits on at the stage of its
// first write, and a layout transition is only ordered after a semaphore wait
// if the barrier's source stage covers the stage that was waited at. With an
// empty source scope the transition may run before the wait completes — the
// classic under-synchronized acquire, which is correct on the driver it was
// written on and a corrupt first frame elsewhere.
inline constexpr TextureBarrier AcquiredImageToRenderTarget()
{
    return TextureBarrier{
        .SrcStage = PipelineStage::RenderTarget,
        .SrcAccess = AccessFlags::None,
        .DstStage = PipelineStage::RenderTarget,
        .DstAccess = AccessFlags::RenderTargetWrite,
        .OldLayout = TextureLayout::Undefined,
        .NewLayout = TextureLayout::RenderTarget,
    };
}

// An offscreen colour target at the start of the frame that draws into it.
inline constexpr TextureBarrier UndefinedToRenderTarget(uint32_t layerCount = 1u)
{
    return TextureBarrier{
        .SrcStage = PipelineStage::None,
        .SrcAccess = AccessFlags::None,
        .DstStage = PipelineStage::RenderTarget,
        .DstAccess = AccessFlags::RenderTargetWrite,
        .OldLayout = TextureLayout::Undefined,
        .NewLayout = TextureLayout::RenderTarget,
        .LayerCount = layerCount,
    };
}

// A colour target that has been rendered into, made readable by a later pass's
// pixel shader.
inline constexpr TextureBarrier RenderTargetToShaderResource()
{
    return TextureBarrier{
        .SrcStage = PipelineStage::RenderTarget,
        .SrcAccess = AccessFlags::RenderTargetWrite,
        .DstStage = PipelineStage::PixelStage,
        .DstAccess = AccessFlags::ShaderRead,
        .OldLayout = TextureLayout::RenderTarget,
        .NewLayout = TextureLayout::ShaderResource,
    };
}

// No transition — a memory dependency between two passes that render into the
// same colour target, where the second one loads what the first wrote instead
// of clearing it.
//
// Without this the second pass's load is not ordered against the first pass's
// writes, even though both run in the same stage: separate render pass
// instances are not implicitly synchronized against each other.
inline constexpr TextureBarrier PreserveRenderTarget()
{
    return TextureBarrier{
        .SrcStage = PipelineStage::RenderTarget,
        .SrcAccess = AccessFlags::RenderTargetWrite,
        .DstStage = PipelineStage::RenderTarget,
        .DstAccess = AccessFlags::RenderTargetRead,
        .OldLayout = TextureLayout::RenderTarget,
        .NewLayout = TextureLayout::RenderTarget,
    };
}

// The finished image at the end of the frame, left in the layout its present
// target requires — IPresentTarget::GetRequiredFinalLayout(), which is
// TextureLayout::Present for a swapchain.
//
// Takes the layout rather than naming one because what the frame owes at its end
// is the target's business, not the renderer's: a target with no presentation
// engine requires nothing at all, and its caller records no barrier instead of
// calling this.
//
// Nothing waits on this transition inside the command list, which is what the
// empty destination scope says. What makes it safe is the semaphore the present
// waits on: its signal happens after every command in the submission, this one
// included.
inline constexpr TextureBarrier RenderTargetToFinal(TextureLayout finalLayout)
{
    return TextureBarrier{
        .SrcStage = PipelineStage::RenderTarget,
        .SrcAccess = AccessFlags::RenderTargetWrite,
        .DstStage = PipelineStage::None,
        .DstAccess = AccessFlags::None,
        .OldLayout = TextureLayout::RenderTarget,
        .NewLayout = finalLayout,
    };
}

// The composited swapchain image, made readable by a copy — used to capture a
// screenshot of exactly what is about to be presented.
inline constexpr TextureBarrier RenderTargetToCopySrc()
{
    return TextureBarrier{
        .SrcStage = PipelineStage::RenderTarget,
        .SrcAccess = AccessFlags::RenderTargetWrite,
        .DstStage = PipelineStage::Copy,
        .DstAccess = AccessFlags::CopySrc,
        .OldLayout = TextureLayout::RenderTarget,
        .NewLayout = TextureLayout::CopySrc,
    };
}

// The same image once the capture copy has read it, left in the layout its
// present target requires. See RenderTargetToFinal() for both the parameter and
// why nothing waits.
inline constexpr TextureBarrier CopySrcToFinal(TextureLayout finalLayout)
{
    return TextureBarrier{
        .SrcStage = PipelineStage::Copy,
        .SrcAccess = AccessFlags::CopySrc,
        .DstStage = PipelineStage::None,
        .DstAccess = AccessFlags::None,
        .OldLayout = TextureLayout::CopySrc,
        .NewLayout = finalLayout,
    };
}

// A freshly created texture, made ready to receive an upload.
inline constexpr TextureBarrier UndefinedToCopyDst(uint32_t layerCount = 1u, uint32_t mipCount = 1u,
                                                   TextureAspect aspect = TextureAspect::Color)
{
    return TextureBarrier{
        .SrcStage = PipelineStage::None,
        .SrcAccess = AccessFlags::None,
        .DstStage = PipelineStage::Copy,
        .DstAccess = AccessFlags::CopyDst,
        .OldLayout = TextureLayout::Undefined,
        .NewLayout = TextureLayout::CopyDst,
        .Aspect = aspect,
        .MipCount = mipCount,
        .LayerCount = layerCount,
    };
}

// An uploaded texture, made readable by a pixel shader.
inline constexpr TextureBarrier CopyDstToShaderResource(uint32_t layerCount = 1u,
                                                        uint32_t mipCount = 1u,
                                                        TextureAspect aspect = TextureAspect::Color)
{
    return TextureBarrier{
        .SrcStage = PipelineStage::Copy,
        .SrcAccess = AccessFlags::CopyDst,
        .DstStage = PipelineStage::PixelStage,
        .DstAccess = AccessFlags::ShaderRead,
        .OldLayout = TextureLayout::CopyDst,
        .NewLayout = TextureLayout::ShaderResource,
        .Aspect = aspect,
        .MipCount = mipCount,
        .LayerCount = layerCount,
    };
}

// The depth buffer at the start of the frame that writes it.
inline constexpr TextureBarrier UndefinedToDepthStencilWrite()
{
    return TextureBarrier{
        .SrcStage = PipelineStage::None,
        .SrcAccess = AccessFlags::None,
        .DstStage = PipelineStage::DepthStencil,
        .DstAccess = AccessFlags::DepthStencilWrite,
        .OldLayout = TextureLayout::Undefined,
        .NewLayout = TextureLayout::DepthStencilWrite,
        .Aspect = TextureAspect::Depth,
    };
}

// The depth buffer once the opaque pass has finished with it, made readable
// both by shaders sampling it and by a later pass that still depth-tests
// against it without writing.
//
// Hence the two destination accesses and the two destination stages: a
// read-only depth attachment is still consumed by the depth test, not only by
// whatever samples the texture.
inline constexpr TextureBarrier DepthStencilWriteToShaderResource()
{
    return TextureBarrier{
        .SrcStage = PipelineStage::DepthStencil,
        .SrcAccess = AccessFlags::DepthStencilWrite,
        .DstStage = PipelineStage::DepthStencil | PipelineStage::ComputeStage,
        .DstAccess = AccessFlags::ShaderRead | AccessFlags::DepthStencilRead,
        .OldLayout = TextureLayout::DepthStencilWrite,
        .NewLayout = TextureLayout::DepthStencilRead,
        .Aspect = TextureAspect::Depth,
    };
}

// A texture a compute shader is about to write through a storage binding.
inline constexpr TextureBarrier
UndefinedToUnorderedAccess(TextureAspect aspect = TextureAspect::Color)
{
    return TextureBarrier{
        .SrcStage = PipelineStage::None,
        .SrcAccess = AccessFlags::None,
        .DstStage = PipelineStage::ComputeStage,
        .DstAccess = AccessFlags::UnorderedAccess,
        .OldLayout = TextureLayout::Undefined,
        .NewLayout = TextureLayout::UnorderedAccess,
        .Aspect = aspect,
    };
}

// The result of a compute dispatch, made readable as a sampled texture.
//
// `readStage` is which shader stage does the reading: a dispatch feeding the
// main pass is read by the pixel shader, one feeding another dispatch by a
// compute shader. Naming a stage the read does not happen in is the classic way
// to under-synchronize this without any symptom on the driver it was tested on.
inline constexpr TextureBarrier
UnorderedAccessToShaderResource(PipelineStage readStage = PipelineStage::PixelStage)
{
    return TextureBarrier{
        .SrcStage = PipelineStage::ComputeStage,
        .SrcAccess = AccessFlags::UnorderedAccess,
        .DstStage = readStage,
        .DstAccess = AccessFlags::ShaderRead,
        .OldLayout = TextureLayout::UnorderedAccess,
        .NewLayout = TextureLayout::ShaderResource,
    };
}
} // namespace Rhi::BarrierPresets
