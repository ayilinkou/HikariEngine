#pragma once

#include <optional>
#include <stdexcept>

#include <rhi/RhiTypes.h>

namespace Hikari::Rhi::D3D12
{
inline PresentMode ChoosePresentMode(std::optional<PresentMode> requestedMode,
                                     bool bTearingSupported)
{
    const PresentMode mode = requestedMode.value_or(PresentMode::Mailbox);
    switch (mode)
    {
        case PresentMode::Mailbox:
        case PresentMode::Fifo:
            return mode;
        case PresentMode::Immediate:
            if (bTearingSupported)
                return mode;
            throw std::runtime_error(
                "Requested present mode 'immediate' is unavailable. Available: mailbox, fifo.");
    }

    throw std::runtime_error("D3D12: unknown requested present mode.");
}
} // namespace Hikari::Rhi::D3D12
