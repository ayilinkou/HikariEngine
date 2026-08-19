#include <rhi/Diagnostics.h>

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace Rhi
{
Diagnostics::Diagnostics(Desc desc) : m_Desc(std::move(desc))
{
    m_Captured.reserve(kCaptureCapacity);
}

void Diagnostics::Report(DiagnosticSeverity severity, std::string_view message)
{
    if (m_Desc.Policy == ValidationPolicy::Ignore)
        return;

    if (severity < m_Desc.MinSeverity)
        return;

    switch (severity)
    {
        case DiagnosticSeverity::Info:
            m_InfoCount.fetch_add(1, std::memory_order_relaxed);
            break;
        case DiagnosticSeverity::Warning:
            m_WarningCount.fetch_add(1, std::memory_order_relaxed);
            break;
        case DiagnosticSeverity::Error:
            m_ErrorCount.fetch_add(1, std::memory_order_relaxed);
            break;
    }

    {
        const std::lock_guard lock(m_CaptureMutex);

        if (m_Captured.size() < kCaptureCapacity)
        {
            m_Captured.emplace_back(message);
        }
        else
        {
            m_Captured[m_CaptureNext] = message;
            m_CaptureNext = (m_CaptureNext + 1) % kCaptureCapacity;
            m_DroppedMessageCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Deliberately outside the lock above. This callback is supplied by the
    // application and runs on a driver thread; invoking it while holding the
    // capture lock would make any path from it back into Diagnostics a
    // self-deadlock.
    if (m_Desc.OnMessage)
        m_Desc.OnMessage(severity, message);

    if (m_Desc.Policy == ValidationPolicy::FailFast && severity == DiagnosticSeverity::Error)
    {
        // Aborting rather than throwing, for the same reason FromVk gives for not
        // throwing out of the severity mapping: this runs inside the driver's C
        // callback, and unwinding through C is undefined. Flush first because the
        // log writes through stdio, which abort() does not drain.
        std::fprintf(stderr, "FailFast: aborting on validation error: %.*s\n",
                     static_cast<int>(message.size()), message.data());
        std::fflush(nullptr);
        std::abort();
    }
}

std::vector<std::string> Diagnostics::RecentMessages() const
{
    const std::lock_guard lock(m_CaptureMutex);

    // Below capacity nothing has wrapped, so insertion order is already oldest
    // first. Once full, the cursor points at the oldest entry.
    if (m_Captured.size() < kCaptureCapacity)
        return m_Captured;

    std::vector<std::string> ordered;
    ordered.reserve(m_Captured.size());
    ordered.insert(ordered.end(), m_Captured.begin() + static_cast<std::ptrdiff_t>(m_CaptureNext),
                   m_Captured.end());
    ordered.insert(ordered.end(), m_Captured.begin(),
                   m_Captured.begin() + static_cast<std::ptrdiff_t>(m_CaptureNext));
    return ordered;
}

void Diagnostics::Reset()
{
    m_InfoCount.store(0, std::memory_order_relaxed);
    m_WarningCount.store(0, std::memory_order_relaxed);
    m_ErrorCount.store(0, std::memory_order_relaxed);
    m_DroppedMessageCount.store(0, std::memory_order_relaxed);

    const std::lock_guard lock(m_CaptureMutex);
    m_Captured.clear();
    m_CaptureNext = 0;
}
} // namespace Rhi
