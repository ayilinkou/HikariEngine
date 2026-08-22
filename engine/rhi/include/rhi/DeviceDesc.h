#pragma once

#include <string>
#include <vector>

#include <rhi/Diagnostics.h>

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

struct DeviceDesc
{
    std::string ApplicationName = "VulkanApp";

    DeviceRequirements Requirements;

    // Turns on the backend's validation/debug layer. Costs real performance, so
    // the caller decides rather than this defaulting to the build type.
    bool bEnableValidation = false;

    // Where the backend reports validation messages. Not owned, and must outlive
    // the device: the debug messenger is destroyed after the logical device and
    // the allocator, so messages arrive during teardown. The caller also usually
    // wants the counts after the device is gone — a non-zero exit for
    // --strict-validation is decided once everything has been torn down.
    //
    // Null is allowed and means the device makes its own, so that GetDiagnostics()
    // is always valid; a caller that never reads the counts need not care.
    Diagnostics* pDiagnostics = nullptr;

    // Backend extension names to pretend this device does not support.
    //
    // Purely a testing lever, and one that has no substitute: an optional
    // extension changes which of two code paths runs, and the path taken on
    // hardware *without* the extension is otherwise unreachable on hardware
    // with it. That is the wrong way round — the fallback is the path most
    // hardware in the field takes, so it is the one that most needs exercising.
    //
    // Names are backend-specific ("VK_KHR_maintenance9"), which is why this is a
    // list of strings rather than an enum: nothing neutral could name them. A
    // name the backend does not recognise as one of its *optional* extensions is
    // reported and ignored, so this can never turn a working device into a
    // failing one.
    std::vector<std::string> DisabledOptionalExtensions;
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

    // Whether the device exposes a queue for this kind of work that is separate
    // from the graphics queue — an async compute engine and a DMA engine, in
    // hardware terms. False means the graphics queue is the only one available
    // for it: always capable of the work, but unable to overlap it with
    // rendering.
    //
    // These describe the device, not where the RHI currently submits. Both are
    // false on an integrated GPU exposing a single universal family, which is
    // the case the rest of the engine has to keep working for.
    bool bHasDedicatedComputeQueue = false;
    bool bHasDedicatedCopyQueue = false;
};
} // namespace Rhi
