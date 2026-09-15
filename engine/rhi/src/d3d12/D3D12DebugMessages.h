#pragma once

#include <cstdint>
#include <mutex>

#include <directx/d3d12.h>
#include <directx/d3d12sdklayers.h>
#include <wrl/client.h>

#include <rhi/Diagnostics.h>

namespace Hikari::Rhi::D3D12
{
/**
 * Moves the debug layer's messages into Diagnostics.
 *
 * Polled, because nothing can call back: ID3D12InfoQueue1, the interface that
 * registers a message callback, is unavailable on both adapters of the Windows 10
 * machine this backend is built on, even with the Agility SDK's layers loaded.
 * So every backend method that can produce a message ends by calling Drain(),
 * which is what puts a FailFast abort inside the method whose call was wrong —
 * with the engine's calling line on the stack, which is the frame Diagnostics
 * promises. A method that forgets to drain points the abort at whichever call
 * drains next.
 *
 * Messages the layer reports only when the GPU executes — GPU-based validation's,
 * and errors it detects at ExecuteCommandLists — arrive at the next drain after
 * that, so a run's last drain has to follow a wait for the GPU.
 */
class D3D12DebugMessages
{
public:
    /** Throws when the device has no info queue, which means the layer is not active. */
    D3D12DebugMessages(ID3D12Device& device, Diagnostics& diagnostics);

    D3D12DebugMessages(const D3D12DebugMessages&) = delete;
    D3D12DebugMessages& operator=(const D3D12DebugMessages&) = delete;

    /**
     * Reports every message stored since the last call. Cheap when there are none:
     * one query of the stored count.
     */
    void Drain();

private:
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_InfoQueue;
    Diagnostics& m_Diagnostics;

    /** Serialises drains, which recorders on job-system threads make concurrently. */
    std::mutex m_Mutex;

    /**
     * How many stored messages have already been reported. Messages are read by
     * index and never cleared: clearing after a read would discard whatever
     * another thread's call stored between the count and the clear.
     */
    uint64_t m_Reported = 0;
};
} // namespace Hikari::Rhi::D3D12
