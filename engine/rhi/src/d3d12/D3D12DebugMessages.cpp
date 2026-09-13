#include "d3d12/D3D12DebugMessages.h"

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Hikari::Rhi::D3D12
{

namespace
{
/**
 * The layer's five severities folded onto Diagnostics' three. Corruption is an
 * error: it means state the layer no longer trusts, which no run should pass with.
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
            return DiagnosticSeverity::Info;
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
