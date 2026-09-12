#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <platform/IPlatform.h>

#include <rhi/Backend.h>
#include <rhi/Diagnostics.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Engine
{

/** The four numbers the run report carries for a series of frame timings. */
struct TimingStats
{
    float Mean = 0.f;
    float P99 = 0.f;
    float Min = 0.f;
    float Max = 0.f;
};

/**
 * What one run measured, as data rather than as a file. The engine fills it and
 * returns it; an app decides whether it becomes JSON, an assertion, or nothing.
 *
 * The four groups are separate because they are read differently. Counters are
 * expectations that must match a committed baseline exactly. Timings are
 * measurements that vary with the machine, so they are read for drift rather
 * than diffed. Run is what was asked for — the same scene at a different
 * resolution, present mode or build configuration is not the same measurement.
 * System is what answered, which is neither an expectation nor a measurement
 * nor a request: it is the machine and the backend that produced the rest.
 */
struct RunReport
{
    /**
     * Counts from the last frame drawn, which is the frame a capture shows.
     */
    struct FrameCounters
    {
        uint32_t DrawCalls = 0;
        uint32_t Batches = 0;
        uint32_t Instances = 0;
        uint32_t Barriers = 0;
        uint32_t BarrierCalls = 0;
    };

    /**
     * Counts accumulated over the whole run, which is a different question from
     * the frame counts and was previously mixed in with them: a validation error
     * on frame 3 belongs to the run, and the draw calls of frame 3 do not.
     */
    struct RunCounters
    {
        uint64_t ValidationErrors = 0;
        uint64_t ValidationWarnings = 0;

        /**
         * Queue submissions the upload context made. The number the asset
         * layer's batching is visible in: one scene's worth of textures loaded
         * inside one load scope is a handful of submissions, and one submission
         * per texture means the scoping broke.
         */
        uint64_t UploadSubmissions = 0;
    };

    /** The two scopes, kept apart so that a reader knows which is which. */
    struct Counters
    {
        FrameCounters Frame;
        RunCounters Run;
    };

    /**
     * Frame 0 is held apart from the series rather than mixed into it: it pays
     * for first use of every pipeline and the first acquire, so averaging it
     * with the rest describes neither.
     */
    struct FirstFrameTimings
    {
        float FrameMs = 0.f;
        float CpuMs = 0.f;
    };

    /** Wall clock per frame, and the same minus what the frame spent blocked. */
    struct RunTimings
    {
        float StartupMs = 0.f;
        FirstFrameTimings FirstFrame;
        TimingStats FrameMs;
        TimingStats CpuMs;
    };

    /**
     * What produced the numbers, as opposed to what was asked of it.
     *
     * Apart from `run` deliberately: nothing in here ever gates a counter
     * comparison, because two backends or two machines disagreeing about a draw
     * call count is a bug in one of them rather than a difference to excuse
     * (plan D26). Everything in `run` gates something.
     */
    struct SystemInfo
    {
        /** The backend that ran, not the one that was asked for. */
        Rhi::Backend Backend = Rhi::Backend::Vulkan;

        std::string Gpu;
        std::string Driver;

        /** Opaque backend text: a Vulkan version, or a D3D12 feature level. */
        std::string ApiVersion;

        /**
         * The build's target rather than a runtime query — the OS *version*
         * would need a platform call, and `gpu` and `driver` already tell apart
         * the machines it would tell apart.
         */
        std::string Os;
        std::string Arch;
    };

    /**
     * The conditions the numbers above were measured under — what was *asked
     * for*, as against the `System` block's what answered.
     *
     * Every field here is one a comparison of two runs consults before deciding
     * whether a signal is worth comparing at all. Three things are deliberately
     * absent: the content root, which differs per machine while ScenePath
     * already identifies the scene; the editor's window size, already covered by
     * the extent; and --strict-validation, which changes the exit code and
     * nothing that is measured.
     */
    struct RunInfo
    {
        bool bFixedDt = false;
        bool bHeadless = false;
        bool bNoUi = false;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t JobCount = 0;

        /** Absent where the target does not present at all, as offscreen ones do not. */
        std::optional<Rhi::PresentMode> PresentMode;
        std::string BuildConfig;

        /** As given on the command line: a comparison matches the strings. */
        std::string ScenePath;

        /** Index into the presets; -1 is a free camera. */
        int CameraPreset = -1;

        /** Empty when the run was not scripted. */
        std::string InputScriptPath;

        /** Which frame the capture shows, or nothing where none was taken. */
        std::optional<uint64_t> CaptureFrame;

        /**
         * Whether the backend's validation layer was loaded at all, which
         * decides whether the validation counters mean anything. Derived from
         * the build configuration until it becomes selectable.
         */
        bool bValidationEnabled = false;

        Rhi::ValidationPolicy ValidationPolicy = Rhi::ValidationPolicy::Count;

        /**
         * Whether synchronization validation was on. Vulkan-only, and on
         * whenever validation is, until it gains a switch of its own.
         */
        bool bSyncValidation = false;

        std::vector<std::string> DisabledVulkanExtensions;
        bool bForceSingleQueue = false;

        uint32_t FramesInFlight = 0;

        /**
         * The mode the window ended the run in, asked of the window system
         * rather than remembered from the request. Absent for a headless run,
         * which has no window, as PresentMode is absent for an offscreen target.
         */
        std::optional<Platform::WindowMode> WindowMode;
    };

    /**
     * When the run started, ISO 8601 in UTC — "2026-09-12T18:55:03Z".
     *
     * In the report because it is no longer in the filename: the baseline's two
     * files took fixed names so that a capture and the comparison after it agree
     * on where they went, and the stamp had to land somewhere.
     *
     * UTC rather than local time, because two reports being compared often come
     * from two machines, and "16:47" against "18:47" is unreadable without
     * knowing both their offsets. The trailing Z says so rather than leaving a
     * reader to assume.
     */
    std::string StartedAt;

    uint64_t Frames = 0;
    Counters Counters;
    RunTimings Timings;
    RunInfo Run;
    SystemInfo System;
};

} // namespace Hikari::Engine
