#pragma once

#include <cstdint>
#include <memory>

#include <rhi/BufferDesc.h>
#include <rhi/DeviceDesc.h>
#include <rhi/Diagnostics.h>
#include <rhi/Handles.h>

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

    // --- Buffers ---
    //
    // The device owns the storage and hands back a handle rather than an object
    // (plan D2). Throws on allocation failure rather than returning an invalid
    // handle: a caller that cannot have its buffer has nothing useful to do
    // with the failure, and every one of them would otherwise have to check.
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;

    // Frees the buffer and invalidates every outstanding copy of `handle`.
    // Destroying an already-destroyed or never-valid handle is reported through
    // Diagnostics rather than ignored — that report is the use-after-free
    // detection the handle model exists to buy, so it is worth reading.
    virtual void Destroy(BufferHandle handle) = 0;

    // The CPU-visible pointer for a host-visible buffer, or nullptr for a
    // GpuOnly one or a stale handle.
    //
    // There is no matching Unmap. Host-visible allocations here are mapped for
    // as long as they live, because the buffers that need a mapping — the
    // per-frame uniform and instance buffers — are written every frame, and
    // mapping is not free. A pair of Map/Unmap calls would therefore be a
    // fiction: the pointer is valid from creation to destruction either way.
    virtual void* GetMappedData(BufferHandle handle) = 0;

    // Buffers currently alive. Exists to be asserted on at shutdown, where
    // anything other than zero is a leak.
    virtual uint32_t GetLiveBufferCount() const = 0;

protected:
    IDevice() = default;
};

// Creates the device for whichever backend this build was compiled with.
// Throws on failure rather than returning null: there is no useful degraded
// mode, and every caller would otherwise have to check.
[[nodiscard]] std::unique_ptr<IDevice> CreateDevice(const DeviceDesc& desc);
} // namespace Rhi
