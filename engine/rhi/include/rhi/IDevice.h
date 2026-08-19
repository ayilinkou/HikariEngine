#pragma once

#include <memory>

#include <rhi/DeviceDesc.h>
#include <rhi/Diagnostics.h>

namespace Rhi
{
// The GPU device, and eventually the sole owner of every GPU resource.
//
// Abstract rather than a compile-time typedef to the backend type, because a
// null/recording implementation has to be able to coexist with a real one in a
// single test binary, and because selecting a backend at runtime should not
// require rewriting call sites. The cost is a vtable dispatch on calls that are
// already crossing into a driver, which is why resource creation lives here and
// per-draw recording does not.
class IDevice
{
public:
    virtual ~IDevice() = default;

    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;
    IDevice(IDevice&&) = delete;
    IDevice& operator=(IDevice&&) = delete;

    virtual const DeviceCaps& GetCaps() const = 0;

    // The device's validation counters and policy. Always valid: a device given
    // no Diagnostics creates its own rather than returning null.
    virtual Diagnostics& GetDiagnostics() = 0;

    // Blocks until the device has finished everything submitted to it. A
    // shutdown and resize tool, not a synchronisation primitive — anything in
    // the frame loop wanting to wait should wait on a fence instead.
    virtual void WaitIdle() = 0;

protected:
    IDevice() = default;
};

// Creates the device for whichever backend this build was compiled with.
// Throws on failure rather than returning null: there is no useful degraded
// mode, and every caller would otherwise have to check.
[[nodiscard]] std::unique_ptr<IDevice> CreateDevice(const DeviceDesc& desc);
} // namespace Rhi
