#include "d3d12/D3D12DebugMessages.h"

#include <array>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Hikari::Rhi::D3D12
{

namespace
{
constexpr std::array kSeverities = {
    D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR,
    D3D12_MESSAGE_SEVERITY_WARNING,    D3D12_MESSAGE_SEVERITY_INFO,
    D3D12_MESSAGE_SEVERITY_MESSAGE,
};

/**
 * The layer's five severities folded onto Diagnostics' four. Corruption is an error:
 * it means state the layer no longer trusts, which no run should pass with. INFO is
 * Verbose rather than Info, because it is the layer's announcement of every object
 * created and destroyed — some 350 messages for three frames of the test scene — and
 * MESSAGE goes with it, since D3D12 orders it below INFO.
 */
DiagnosticSeverity ToDiagnosticSeverity(D3D12_MESSAGE_SEVERITY severity)
{
    switch (severity)
    {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            return DiagnosticSeverity::Error;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            return DiagnosticSeverity::Warning;
        case D3D12_MESSAGE_SEVERITY_INFO:
        case D3D12_MESSAGE_SEVERITY_MESSAGE:
            return DiagnosticSeverity::Verbose;
    }

    return DiagnosticSeverity::Error;
}
} // namespace

D3D12DebugMessages::D3D12DebugMessages(ID3D12Device& device, Diagnostics& diagnostics)
    : m_Diagnostics(diagnostics)
{
    const HRESULT hr = device.QueryInterface(IID_PPV_ARGS(&m_InfoQueue));
    if (FAILED(hr))
    {
        throw std::runtime_error(
            std::format("The D3D12 device has no info queue (0x{:08X}), so the debug layer is not "
                        "active on it and nothing it does would be validated.",
                        static_cast<uint32_t>(hr)));
    }

    // Unlimited, because messages are read by index: at a limit, new messages
    // push old ones out of the front of the queue, which would shift every index
    // past the ones already reported. A clean run stores nothing, so what this
    // costs is memory proportional to how wrong a run is.
    m_InfoQueue->SetMessageCountLimit(static_cast<UINT64>(-1));

    // Whatever the threshold would drop is denied here instead, as the Vulkan backend
    // asks its messenger only for what the threshold admits. That saves more than the
    // formatting: the queue keeps whatever it stores for the life of the device, under
    // the unlimited count above. Denied explicitly even at the default threshold,
    // because a pushed filter replaces the one the queue starts with rather than adding
    // to it — and that one denies INFO (read back with GetStorageFilter, on the RX 580
    // and WARP under Agility SDK 1.619.5), which a filter denying nothing would undo.
    std::vector<D3D12_MESSAGE_SEVERITY> deniedSeverities;
    for (const D3D12_MESSAGE_SEVERITY severity : kSeverities)
    {
        if (ToDiagnosticSeverity(severity) < diagnostics.MinSeverity())
            deniedSeverities.push_back(severity);
    }

    D3D12_MESSAGE_ID deniedIds[] = {
        // Both say a target was cleared without the optimized clear value D3D12 lets a
        // resource declare at creation, which makes the clear "typically slower" and
        // nothing else. The seam's TextureDesc carries no clear value, so every clear
        // would warn and a D3D12 run's warning count could never match Vulkan's zero;
        // the Vulkan backend mutes its one performance hint at the layer for the same
        // reason.
        D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
        D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,

        // The layer's notice that GPU-based validation is on, muted at every threshold.
        // Its "(disabled by default)" is D3D12's default, not this engine's, and it says
        // the same whether state tracking is on or off, so EnableDebugLayer logs the
        // level actually chosen instead.
        D3D12_MESSAGE_ID_CREATEDEVICE_DEBUG_LAYER_STARTUP_OPTIONS,
    };

    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumSeverities = static_cast<UINT>(deniedSeverities.size());
    filter.DenyList.pSeverityList = deniedSeverities.data();
    filter.DenyList.NumIDs = static_cast<UINT>(std::size(deniedIds));
    filter.DenyList.pIDList = deniedIds;

    // Retrieval as well as storage, because device creation stored the startup notice
    // before this queue existed to filter it. Pushed before the first read and never
    // changed, so the indices Drain() reads by stay stable.
    m_InfoQueue->PushStorageFilter(&filter);
    m_InfoQueue->PushRetrievalFilter(&filter);
}

void D3D12DebugMessages::Drain()
{
    std::vector<std::pair<DiagnosticSeverity, std::string>> messages;

    {
        const std::lock_guard lock(m_Mutex);

        const uint64_t stored = m_InfoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (uint64_t index = m_Reported; index < stored; ++index)
        {
            SIZE_T length = 0;
            if (FAILED(m_InfoQueue->GetMessage(index, nullptr, &length)) || length == 0)
                continue;

            // D3D12_MESSAGE holds pointers, so its storage needs pointer alignment,
            // which a byte buffer does not promise.
            std::vector<uint64_t> storage((length + sizeof(uint64_t) - 1) / sizeof(uint64_t));
            auto* pMessage = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(m_InfoQueue->GetMessage(index, pMessage, &length)))
                continue;

            // DescriptionByteLength counts the terminator.
            const size_t textLength =
                pMessage->DescriptionByteLength > 0 ? pMessage->DescriptionByteLength - 1 : 0;
            messages.emplace_back(
                ToDiagnosticSeverity(pMessage->Severity),
                std::format("D3D12 [{}] {}", static_cast<int>(pMessage->ID),
                            std::string_view(pMessage->pDescription, textLength)));
        }

        m_Reported = stored;
    }

    // Outside the lock: under FailFast an error does not return, and a message
    // handler that logs should not hold up another thread's drain while it does.
    for (const auto& [severity, text] : messages)
        m_Diagnostics.Report(severity, text);
}

} // namespace Hikari::Rhi::D3D12
