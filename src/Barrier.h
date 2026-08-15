#pragma once

#include "vulkan/vulkan.hpp"

struct AllocatedImage;

struct ImageBarrierDesc
{
    vk::PipelineStageFlags2 srcStage;
    vk::AccessFlags2 srcAccess;
    vk::PipelineStageFlags2 dstStage;
    vk::AccessFlags2 dstAccess;
    vk::ImageLayout oldLayout;
    vk::ImageLayout newLayout;
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    uint32_t baseLayer = 0u;
    uint32_t layerCount = 1u;
    uint32_t baseMip = 0u;
    uint32_t mipCount = 1u;
};

// Barriers.h
namespace Barriers
{
inline constexpr ImageBarrierDesc AcquiredSwapchainToColorAttachment()
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccess = {},
        .dstStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccess = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .aspect = vk::ImageAspectFlagBits::eColor,
    };
}

inline constexpr ImageBarrierDesc
UndefinedToTransferDst(uint32_t layerCount = 1u, uint32_t mipCount = 1u,
                       vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccess = {},
        .dstStage = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccess = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .aspect = aspect,
        .layerCount = layerCount,
        .mipCount = mipCount,
    };
}

inline constexpr ImageBarrierDesc
TransferDstToShaderRead(uint32_t layerCount = 1u, uint32_t mipCount = 1u,
                        vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccess = vk::AccessFlagBits2::eTransferWrite,
        .dstStage = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccess = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .aspect = aspect,
        .layerCount = layerCount,
        .mipCount = mipCount,
    };
}

inline constexpr ImageBarrierDesc DepthAttachmentToShaderRead()
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
        .srcAccess = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .dstStage = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests |
                    vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccess =
            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eDepthStencilAttachmentRead,
        .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .newLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .aspect = vk::ImageAspectFlagBits::eDepth,
        .layerCount = 1u,
        .mipCount = 1u,
    };
}

inline constexpr ImageBarrierDesc UndefinedToColorAttachment(uint32_t layerCount = 1u)
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccess = {},
        .dstStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccess = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .aspect = vk::ImageAspectFlagBits::eColor,
        .layerCount = layerCount,
    };
}

inline constexpr ImageBarrierDesc UndefinedToDepthAttachment()
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccess = {},
        .dstStage = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccess = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .aspect = vk::ImageAspectFlagBits::eDepth,
    };
}

inline constexpr ImageBarrierDesc ColorAttachmentToPresent()
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccess = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStage = vk::PipelineStageFlagBits2::eBottomOfPipe,
        .dstAccess = {},
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .aspect = vk::ImageAspectFlagBits::eColor,
    };
}

inline constexpr ImageBarrierDesc
UndefinedToGeneral(vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor)
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccess = {},
        .dstStage = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccess = vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .aspect = aspect,
    };
}

inline constexpr ImageBarrierDesc ColorAttachmentToShaderRead()
{
    return ImageBarrierDesc{.srcStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            .srcAccess = vk::AccessFlagBits2::eColorAttachmentWrite,
                            .dstStage = vk::PipelineStageFlagBits2::eFragmentShader,
                            .dstAccess = vk::AccessFlagBits2::eShaderRead,
                            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                            .aspect = vk::ImageAspectFlagBits::eColor};
}

// Used to copy out the fully-composited swapchain image before presenting,
// for screenshot capture (--screenshot).
inline constexpr ImageBarrierDesc ColorAttachmentToTransferSrc()
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccess = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStage = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccess = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .aspect = vk::ImageAspectFlagBits::eColor,
    };
}

inline constexpr ImageBarrierDesc TransferSrcToPresent()
{
    return ImageBarrierDesc{
        .srcStage = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccess = vk::AccessFlagBits2::eTransferRead,
        .dstStage = vk::PipelineStageFlagBits2::eBottomOfPipe,
        .dstAccess = {},
        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .aspect = vk::ImageAspectFlagBits::eColor,
    };
}
} // namespace Barriers
