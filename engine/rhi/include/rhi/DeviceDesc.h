#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rhi/Backend.h>
#include <rhi/Diagnostics.h>

namespace Hikari::Rhi
{
/**
 * How much of a D3D12 debug layer's validation runs on the GPU, where validation
 * runs at all.
 *
 * D3D12's term, because D3D12 is the backend that needs it: a D3D12 descriptor
 * names no resource state, so the layer on the CPU knows what is bound but not
 * what a shader reads. Vulkan checks the equivalent on the CPU, because a
 * descriptor write there names the layout.
 */
enum class GpuBasedValidation : uint8_t
{
    Off,

    /**
     * Shaders are checked for what they read — uninitialized or incompatible
     * descriptors and samplers, descriptors naming deleted resources, reads past
     * the heap — and resource states are not tracked. The tracking is nearly all of
     * the cost: on the RX 580 a test-scene frame is 3.8 ms here against 22.8 ms at
     * Full, and 1.3 ms Off.
     */
    Descriptors,

    /**
     * Descriptors, and also whether each resource a shader reads is in a state it
     * may be read in, including implicit promotion and decay — the check a mistake
     * in a backend's own state tracking shows up in. What the tests ask for.
     */
    Full,
};

/**
 * The level's name, and the only spelling of it: --d3d12-gpu-based-validation
 * parses these words and a run report prints them.
 */
constexpr std::string_view ToString(GpuBasedValidation level)
{
    switch (level)
    {
        case GpuBasedValidation::Off:
            return "off";
        case GpuBasedValidation::Descriptors:
            return "descriptors";
        case GpuBasedValidation::Full:
            return "full";
    }

    return "unknown";
}

/** The inverse, returning nothing for a word that names no level. */
constexpr std::optional<GpuBasedValidation> GpuBasedValidationFromString(std::string_view name)
{
    if (name == "off")
        return GpuBasedValidation::Off;

    if (name == "descriptors")
        return GpuBasedValidation::Descriptors;

    if (name == "full")
        return GpuBasedValidation::Full;

    return std::nullopt;
}

/**
 * How much a device is required to be able to do. Separated from DeviceDesc
 * because presentation is the one requirement that is about to become optional:
 * a headless run wants everything here except a window, and keeping the split
 * explicit now means that change is a flag rather than a new code path.
 */
struct DeviceRequirements
{
    /**
     * When false, no surface is created, no surface or swapchain extension is
     * requested, and no queue family is required to support presentation. The
     * GPU tests run this way, since a test binary has no window; the
     * application's own headless mode is what Stage 6 builds on top of it.
     */
    bool bPresent = true;

    /**
     * Opaque platform window handle, needed only when bPresent. Opaque rather
     * than typed because the two backends want unrelated things from it (a
     * native window pointer versus an HWND), and neither type belongs in a
     * neutral header.
     */
    void* NativeWindowHandle = nullptr;
};

struct DeviceDesc
{
    std::string ApplicationName = "HikariEngine";

    /**
     * Which implementation to build the device from. A field here rather than a
     * parameter of CreateDevice because the fields around it — the validation
     * switch, the disabled extensions, the single-queue lever — are all read
     * differently depending on which backend reads them, so one object
     * describes the whole request; and because Vulkan being permanently the
     * default (plan D25) is then a struct default rather than a convention
     * every call site has to observe.
     */
    Rhi::Backend Backend = Rhi::Backend::Vulkan;

    DeviceRequirements Requirements;

    /**
     * Part of the name of the adapter to run on, matched without regard to case
     * against the backend's own name for each one. Empty keeps each backend's
     * rule: the first suitable adapter in enumeration order.
     *
     * A name rather than an index because enumeration order is not a stable
     * identifier — it differs between machines and can differ between boots.
     * The cost is that a name means something only within one backend, since
     * each API spells adapter names its own way; and it cannot tell two copies
     * of one adapter apart, such as the WARP Windows ships from the WARP deployed
     * beside the executable, where deployment decides which one answers.
     *
     * An adapter that matches but does not meet the backend's requirements is
     * refused rather than skipped for the next one, and no match at all refuses
     * with the adapters that were found: asking for a GPU and silently getting
     * another would measure the wrong machine.
     */
    std::string Gpu;

    /**
     * Turns on the backend's validation/debug layer. Costs real performance, so
     * the caller decides rather than this defaulting to the build type.
     */
    bool bEnableValidation = false;

    /**
     * Where the backend reports validation messages. Not owned, and must outlive
     * the device: the debug messenger is destroyed after the logical device and
     * the allocator, so messages arrive during teardown. The caller also usually
     * wants the counts after the device is gone — a non-zero exit for
     * --strict-validation is decided once everything has been torn down.
     *
     * Null is allowed and means the device makes its own, so that GetDiagnostics()
     * is always valid; a caller that never reads the counts need not care.
     */
    Diagnostics* pDiagnostics = nullptr;

    /**
     * Backend extension names to pretend this device does not support.
     *
     * Purely a testing lever, and one that has no substitute: an optional
     * extension changes which of two code paths runs, and the path taken on
     * hardware *without* the extension is otherwise unreachable on hardware
     * with it. That is the wrong way round — the fallback is the path most
     * hardware in the field takes, so it is the one that most needs exercising.
     *
     * Names are backend-specific ("VK_KHR_maintenance9"), which is why this is a
     * list of strings rather than an enum: nothing neutral could name them. A
     * name the backend does not recognise as one of its *optional* extensions is
     * reported and ignored, so this can never turn a working device into a
     * failing one.
     */
    std::vector<std::string> DisabledOptionalExtensions;

    /**
     * Resolve every queue role to one queue, as though the device exposed a
     * single universal family.
     *
     * The other half of the testing lever above, and needed because the two
     * reach different code. Disabling an extension changes what a device
     * promises; this changes its *shape* — and a device with no separate copy
     * family neither hands resources over nor has a second queue to submit them
     * on, which is a third arrangement rather than a variation on the first two.
     * It is what an integrated GPU looks like, so the path is real hardware's
     * and not a contrivance.
     *
     * Neutral because both backends have somewhere to put it: Vulkan collapses
     * the family selection, and D3D12 would submit everything on the direct
     * queue. A backend that cannot honour it must say so rather than pretend.
     */
    bool bForceSingleQueue = false;

    /**
     * Whether the validation layer's synchronization checks run, where
     * bEnableValidation turned validation on at all.
     *
     * On by default: it catches the class of defect that is hardest to find any
     * other way, and it is off by *Vulkan's* default, so leaving it alone would
     * quietly give up the check. It is also the expensive sub-mode, which is the
     * only reason to expose it.
     *
     * A backend with no synchronization validator ignores this, as one with no
     * optional extensions ignores the list above — the field describes what to
     * ask for, and what a backend can honour is the backend's business.
     */
    bool bSyncValidation = true;

    /**
     * How much the debug layer also validates on the GPU, where bEnableValidation
     * turned validation on at all. Its output arrives after the GPU executes rather
     * than inside the offending call. Ignored by a backend with nothing to switch.
     *
     * Descriptors by default rather than Full, because resource-state tracking
     * costs a debug frame several times over and an interactive run pays that
     * every frame; the tests ask for Full, so the state checks still run under
     * every gate. Type qualified because the member and its type share a name.
     */
    Rhi::GpuBasedValidation GpuBasedValidation = Rhi::GpuBasedValidation::Descriptors;

    /**
     * How many resource descriptors — constant buffers, textures, unordered-access
     * textures — and how many sampler descriptors every bind group alive at once may
     * hold between them, on a backend that binds from fixed heaps.
     *
     * D3D12 does: at most one heap of each kind can be bound at a time, switching can
     * stall, and ImGui keeps raw handles into it, so each heap is created once at this
     * size and never grows. A scene past the capacity is refused naming the field. The
     * sampler default is the 2,048 D3D12 guarantees every adapter; identical samplers
     * share their descriptors, so a scene rarely needs more than a handful. A backend
     * whose pools grow — Vulkan — ignores both.
     */
    uint32_t ResourceDescriptorCapacity = 65'536u;
    uint32_t SamplerDescriptorCapacity = 2'048u;
};

/**
 * What a device turned out to be able to do, as opposed to what was asked of
 * it. Read this rather than testing the backend or the platform: that is the
 * whole point of it existing.
 *
 * **Caps are branched on; DeviceInfo below is reported and never branched on.**
 * That is the whole of the split, and it is why the GPU's name is not here: a
 * caller holding that string has everything it needs to write the driver check
 * this struct exists to prevent.
 */
struct DeviceCaps
{
    /**
     * True when the API's clip space has Y pointing down relative to the
     * convention GLM produces, so a projection matrix needs its Y row negated.
     * Vulkan needs this; D3D12 does not. Exactly one site in the renderer may
     * read it — anything that recomputes a projection matrix must consult this
     * flag rather than repeating the constant, or the two sites will disagree
     * the first time a second backend exists.
     */
    bool bFlipClipSpaceY = false;

    /**
     * False when the device was created without presentation support, whether
     * because it was not asked for or because nothing suitable was found.
     */
    bool bPresentSupported = false;

    /**
     * Whether the device exposes a queue for this kind of work that is separate
     * from the graphics queue — an async compute engine and a DMA engine, in
     * hardware terms. False means the graphics queue is the only one available
     * for it: always capable of the work, but unable to overlap it with
     * rendering.
     *
     * These describe the device, not where the RHI currently submits. Both are
     * false on an integrated GPU exposing a single universal family, which is
     * the case the rest of the engine has to keep working for.
     */
    bool bHasDedicatedComputeQueue = false;
    bool bHasDedicatedCopyQueue = false;

    /**
     * The file extension of the compiled shader bytes this backend reads --
     * "spv" here, "dxil" on D3D12.
     *
     * A string rather than an enum because the only thing anyone does with it is
     * build a filename, and the build emits one blob per stage under a uniform
     * name so that resolving it is the same on both backends (plan D24).
     */
    const char* ShaderExtension = "";
};

/**
 * Which device produced a run, as opposed to what it can do.
 *
 * **Info is reported and never branched on; DeviceCaps above is what a caller
 * branches on.** Nothing in the engine should read these strings for anything
 * but printing them: the moment one is compared against a known driver name,
 * the capability seam above has been routed around. `ShaderExtension` is the
 * exception that proves the rule rather than the precedent that dissolves it —
 * it exists so that callers need *not* know the backend, which is the opposite
 * of what a GPU name gets used for.
 *
 * The device's half only. A run report also names the operating system and the
 * architecture, and those are properties of the process rather than of the
 * device, so they do not come from here.
 */
struct DeviceInfo
{
    /** Which backend actually built this device, not which one was requested. */
    Rhi::Backend Backend = Rhi::Backend::Vulkan;

    /** The adapter's own name for itself. */
    std::string Gpu;

    /**
     * Whatever the backend can say about its driver, as opaque text. Never
     * parsed: its only use is telling two machines apart in a run report.
     */
    std::string Driver;

    /**
     * What the device *supports*, not what the run asked for — the requested
     * version is a constant in our source and identical on every machine, so it
     * would say nothing about the machine a report describes.
     *
     * Opaque text, because the two APIs do not answer the same question:
     * Vulkan has an API version ("1.4.321") and D3D12 has a feature level
     * ("feature level 12_2") instead. The value rather than the field name
     * carries the disambiguation, so a feature level never reads as a version.
     */
    std::string ApiVersion;

    /**
     * The adapter's PCI vendor and device identifiers.
     *
     * The one piece of identity both APIs spell alike, because both report the
     * PCI identifiers themselves — which is what lets two reports from different
     * backends be recognised as the same adapter, where the names above cannot
     * be matched across APIs. They name the chip rather than the card: two cards
     * of one model share them. Software rasterizers carry vendor identifiers of
     * their own.
     */
    uint32_t VendorId = 0;
    uint32_t DeviceId = 0;
};
} // namespace Hikari::Rhi
