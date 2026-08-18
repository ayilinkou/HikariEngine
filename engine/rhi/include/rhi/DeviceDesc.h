#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Rhi
{
// How much a device is required to be able to do. Separated from DeviceDesc
// because presentation is the one requirement that is about to become optional:
// a headless run wants everything here except a window, and keeping the split
// explicit now means that change is a flag rather than a new code path.
struct DeviceRequirements
{
    // When false, no surface is created and no queue family is required to
    // support presentation. Nothing sets this yet — it exists so that the
    // present and non-present paths are already distinct.
    bool bPresent = true;

    // Opaque platform window handle, needed only when bPresent. Opaque rather
    // than typed because the two backends want unrelated things from it (a
    // native window pointer versus an HWND), and neither type belongs in a
    // neutral header.
    void* NativeWindowHandle = nullptr;
};

// Deliberately coarser than any one backend's validation severity scale. The
// backends have more levels than this (Vulkan adds a verbose tier below Info),
// and mapping those down loses nothing a caller acts on differently.
enum class DiagnosticSeverity : uint8_t
{
    Info,
    Warning,
    Error,
};

struct DeviceDesc
{
    std::string ApplicationName = "VulkanApp";

    DeviceRequirements Requirements;

    // Turns on the backend's validation/debug layer. Costs real performance, so
    // the caller decides rather than this defaulting to the build type.
    bool bEnableValidation = false;

    // Messages below this are dropped before the callback is invoked.
    DiagnosticSeverity MinDiagnosticSeverity = DiagnosticSeverity::Info;

    // Invoked from the backend's debug callback, which means it can be called
    // from any thread the driver chooses and re-entrantly during a device call.
    // Keep implementations short and thread-safe.
    //
    // The message is already composed; the caller's job is to route it. Taking
    // a callback rather than owning a logger keeps this module free of any
    // opinion about how the application reports things.
    std::function<void(DiagnosticSeverity, std::string_view)> OnDiagnosticMessage;
};

// What a device turned out to be able to do, as opposed to what was asked of
// it. Read this rather than testing the backend or the platform: that is the
// whole point of it existing.
struct DeviceCaps
{
    // True when the API's clip space has Y pointing down relative to the
    // convention GLM produces, so a projection matrix needs its Y row negated.
    // Vulkan needs this; D3D12 does not. Exactly one site in the renderer may
    // read it — anything that recomputes a projection matrix must consult this
    // flag rather than repeating the constant, or the two sites will disagree
    // the first time a second backend exists.
    bool bFlipClipSpaceY = false;

    // False when the device was created without presentation support, whether
    // because it was not asked for or because nothing suitable was found.
    bool bPresentSupported = false;
};
} // namespace Rhi
