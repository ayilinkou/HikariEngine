#include <rhi/Backend.h>

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>

#include <rhi/IDevice.h>

#include "vulkan/VulkanDeviceFactory.h"

namespace Hikari::Rhi
{

namespace
{
/** One spelling per backend, so the command line and the run report share it. */
struct Spelling
{
    Backend Value;
    std::string_view Name;
};

constexpr std::array kSpellings = {
    Spelling{Backend::Vulkan, "vulkan"},
    Spelling{Backend::D3D12, "d3d12"},
};

/**
 * What this build was compiled with.
 *
 * Driven by the source list rather than by the platform: a Windows build
 * configured without the D3D12 backend has to report that truthfully, or
 * --backend's error message becomes a lie in exactly the configuration someone
 * is debugging. Stage 7.7 adds the backend's sources and the definition of
 * HIKARI_RHI_D3D12 together.
 */
constexpr std::array kAvailable = {
    Backend::Vulkan,
#ifdef HIKARI_RHI_D3D12
    Backend::D3D12,
#endif
};
} // namespace

std::span<const Backend> AvailableBackends()
{
    return kAvailable;
}

std::string_view ToString(Backend backend)
{
    for (const Spelling& spelling : kSpellings)
    {
        if (spelling.Value == backend)
            return spelling.Name;
    }

    return "unknown";
}

std::optional<Backend> BackendFromString(std::string_view name)
{
    for (const Spelling& spelling : kSpellings)
    {
        if (spelling.Name == name)
            return spelling.Value;
    }

    return std::nullopt;
}

std::unique_ptr<IDevice> CreateDevice(const DeviceDesc& desc)
{
    // A precondition rather than a message for a user: an unavailable backend
    // reaching here means a caller bypassed the command line, which is a
    // programming error. The flag's own refusal happens at parse time, before
    // the platform or the content root exist, and it is the one that lists what
    // the build has.
    const std::span<const Backend> available = AvailableBackends();
    if (std::ranges::find(available, desc.Backend) == available.end())
    {
        throw std::runtime_error(
            std::format("Backend not in this build: {}", ToString(desc.Backend)));
    }

    switch (desc.Backend)
    {
        case Backend::Vulkan:
            return Vulkan::CreateVulkanDevice(desc);

        case Backend::D3D12:
            // Unreachable: a build without it does not list it as available, and
            // a build with it replaces this with the call (Stage 7.7).
            break;
    }

    throw std::runtime_error(std::format("No factory for backend: {}", ToString(desc.Backend)));
}

} // namespace Hikari::Rhi
