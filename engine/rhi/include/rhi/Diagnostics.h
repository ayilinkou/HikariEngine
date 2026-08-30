#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Hikari::Rhi
{
// Deliberately coarser than any one backend's validation severity scale. The
// backends have more levels than this (Vulkan adds a verbose tier below Info),
// and mapping those down loses nothing a caller acts on differently.
//
// The order is load-bearing: both Report() and the backend's own message filter
// compare these as a threshold, so reordering them would silently drop every
// message or none.
enum class DiagnosticSeverity : uint8_t
{
    Info,
    Warning,
    Error,
};

// What a validation message means for the run.
enum class ValidationPolicy : uint8_t
{
    // Drop everything: nothing counted, captured or forwarded. The backend's
    // debug messenger is still created and the message discarded in the
    // callback. Whether a layer writes to stdout of its own accord when no
    // messenger exists is a layer-configuration question, so filtering late is
    // the choice that cannot change what the layer prints.
    Ignore,

    // Count, capture and forward. What a normal run wants.
    Count,

    // Count, then abort on the first Error with the message printed. For CI, and
    // for bisecting a validation failure to the call that caused it — a core
    // dump at the offending call is worth more than a completed run.
    FailFast,
};

// Counts, captures and routes the backend's validation output, and owns the
// policy decision about what a validation error means for the run.
//
// Neutral because none of that is backend-specific: Vulkan reports through its
// debug messenger callback and D3D12 would through the debug layer plus
// ID3D12InfoQueue, but counting and policy are identical either way. Being
// neutral also makes it testable with no device, which matters more here than
// it looks — a clean run produces no validation messages at all, so nothing
// else in the tree would notice this path being wired up wrongly.
//
// Must outlive the device reporting into it. The device destroys its debug
// messenger near-last, after the logical device and the allocator, so messages
// still arrive while those are being torn down.
class Diagnostics
{
public:
    struct Desc
    {
        ValidationPolicy Policy = ValidationPolicy::Count;

        // Messages below this are dropped before anything else happens to them.
        DiagnosticSeverity MinSeverity = DiagnosticSeverity::Info;

        // Where messages go after being counted. The message arrives already
        // composed, so this only has to route it — taking a callback rather than
        // owning a logger keeps this module free of any opinion about how the
        // application reports things. Runs under Report()'s constraints below.
        std::function<void(DiagnosticSeverity, std::string_view)> OnMessage;
    };

    // The capture buffer keeps this many messages. Fixed rather than
    // configurable: it exists so a post-mortem has the recent history, and no
    // caller has a reason to want a different number.
    static constexpr size_t kCaptureCapacity = 64;

    Diagnostics() : Diagnostics(Desc{}) {}
    explicit Diagnostics(Desc desc);

    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;

    // Reports one message. Called from the backend's debug callback, so it may
    // run on any thread the driver chooses, re-entrantly inside a backend call,
    // and concurrently with the accessors below. Does not return under
    // FailFast when `severity` is Error.
    void Report(DiagnosticSeverity severity, std::string_view message);

    uint64_t InfoCount() const { return m_InfoCount.load(std::memory_order_relaxed); }
    uint64_t WarningCount() const { return m_WarningCount.load(std::memory_order_relaxed); }
    uint64_t ErrorCount() const { return m_ErrorCount.load(std::memory_order_relaxed); }

    // The most recent messages, oldest first, at most kCaptureCapacity of them.
    std::vector<std::string> RecentMessages() const;

    // How many counted messages fell out of the capture buffer. Read alongside
    // RecentMessages(), so a truncated capture says so rather than reading as
    // the whole story.
    uint64_t DroppedMessageCount() const
    {
        return m_DroppedMessageCount.load(std::memory_order_relaxed);
    }

    ValidationPolicy Policy() const { return m_Desc.Policy; }
    DiagnosticSeverity MinSeverity() const { return m_Desc.MinSeverity; }

    // Clears the counters and the capture buffer, leaving the policy alone. For
    // tests needing per-case isolation; nothing in a normal run resets.
    void Reset();

private:
    Desc m_Desc;

    // Atomic rather than guarded by m_CaptureMutex: a caller reading a count
    // while the driver is still reporting is normal, and should not contend with
    // message capture to do it.
    std::atomic<uint64_t> m_InfoCount{0};
    std::atomic<uint64_t> m_WarningCount{0};
    std::atomic<uint64_t> m_ErrorCount{0};
    std::atomic<uint64_t> m_DroppedMessageCount{0};

    mutable std::mutex m_CaptureMutex;
    std::vector<std::string> m_Captured;

    // Ring cursor into m_Captured, meaningful only once it has reached
    // kCaptureCapacity entries; before that, messages are simply appended.
    size_t m_CaptureNext = 0;
};
} // namespace Hikari::Rhi
