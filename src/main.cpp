#include <span>

#include "Camera.h"
#include "CloudSystem.h"
#include "Common.h"
#include "Cubemap.h"
#include "Entity.h"
#include "FrameData.h"
#include "InstanceData.h"
#include "Lights.h"
#include "MaterialFactory.h"
#include "Model.h"
#include "ModelManager.h"
#include "PBRMaterial.h"
#include "ResourceManager.h"
#include "Texture.h"
#include "Vertex.h"
#include "XmlParser.h"

#include <core/IJobSystem.h>
#include <core/Log.h>
#include <core/SerialJobSystem.h>
#include <core/SharedQueueJobSystem.h>
#include <core/Timer.h>

#include <platform/CommandLine.h>
#include <platform/FileSystem.h>
#include <platform/HeadlessPlatform.h>
#include <platform/IPlatform.h>
#include <platform/Paths.h>
#include <platform/SdlPlatform.h>

#include <rhi/BarrierPresets.h>
#include <rhi/BufferDesc.h>
#include <rhi/DeviceDesc.h>
#include <rhi/Diagnostics.h>
#include <rhi/Handles.h>
#include <rhi/ICommandList.h>
#include <rhi/IDevice.h>
#include <rhi/IPresentTarget.h>
#include <rhi/RhiTypes.h>
#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>
#include <rhi/UniqueHandle.h>
#include <rhi/UploadContext.h>
#include <rhi/vulkan/DebugNames.h>
#include <rhi/vulkan/PipelineBuilder.h>
#include <rhi/vulkan/VulkanNative.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "ImGuiFileDialog.h"

using namespace Hikari;
using namespace Hikari::Core;
using namespace Hikari::Platform;
using namespace Hikari::Rhi::Vulkan;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>

inline void EnableAnsiColors()
{
    for (DWORD stdHandle : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE})
    {
        HANDLE handle = GetStdHandle(stdHandle);
        if (handle == INVALID_HANDLE_VALUE)
            continue;

        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode))
            continue;

        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}
#else
// For write(), which is what the signal handler prints its newline with.
#include <unistd.h>
#endif

constexpr uint32_t INITIAL_INSTANCE_CAPACITY = 1024u;
constexpr int NUM_FRAMES_IN_FLIGHT = 2;
constexpr glm::vec3 SKY_COLOR = {0.4f, 0.8f, 1.f};

constexpr LogCategory LogValidationLayer("Validation Layer");
constexpr LogCategory LogDiagnostics("Diagnostics");
constexpr LogCategory LogSDL("SDL");
constexpr LogCategory LogWindow("Window");
constexpr LogCategory LogMain("main");
constexpr LogCategory LogRenderer("Renderer");
constexpr LogCategory LogImGui("InitImGui");

std::atomic<bool> g_bShouldClose = false;

/**
 * Asks the loop to stop, for either signal that means "shut down": SIGINT from
 * Ctrl-C at a terminal, SIGTERM from a CI runner's timeout or a process
 * manager. Both leave the loop the ordinary way, so the screenshot and the run
 * report are still written — which is the whole point of handling SIGTERM,
 * since a killed run produces no artefacts to diagnose the timeout with.
 *
 * A second signal deliberately does nothing new. Escalating to _Exit would drop
 * exactly those artefacts for a user who was merely impatient; a run whose
 * shutdown is genuinely wedged is what SIGKILL is for.
 *
 * `g_bShouldClose` is a lock-free std::atomic<bool>, which a handler may touch.
 * The newline goes through write() rather than std::cout because only
 * async-signal-safe functions may be called from a handler, and formatted
 * output is not one of them: interrupting a stream mid-write and re-entering it
 * is undefined, and the failure is a corrupted or deadlocked stdout rather than
 * anything that announces itself.
 */
void HandleTerminationSignal(int)
{
    g_bShouldClose = true;

#ifdef _WIN32
    const int written = _write(1, "\n", 1);
#else
    const ssize_t written = write(STDOUT_FILENO, "\n", 1);
#endif
    // Nothing useful to do if the write fails, and a handler cannot report it.
    // Consumed because write() is declared warn_unused_result.
    (void)written;
}

struct LightData
{
    uint32_t PointLightCount;
    uint32_t DirLightCount;
    glm::vec2 Padding;
    PointLight::Data PointLights[MAX_POINT_LIGHTS];
    DirectionalLight::Data DirLights[MAX_DIR_LIGHTS];
};

struct CameraData
{
    glm::mat4 View;
    glm::mat4 Proj;
    glm::mat4 InvViewProj;
    glm::vec3 Pos;
    float NearPlane;
    glm::vec3 Padding;
    float FarPlane;
};

/**
 * Each member must start at an offset that is a multiple of its base alignment.
 * Eg. a float can start on offset 0, 4, 8 or 12.
 * glm::vec3 is 12 bytes wide by default but is 16 byte aligned.
 */
struct GlobalBuffer
{
    LightData Lights;
    CameraData CamData;
    glm::vec3 SkyColor;
    float Time;
};

#ifdef NDEBUG
constexpr bool bEnableValidationLayers = false;
#else
constexpr bool bEnableValidationLayers = true;
#endif

/**
 * Routes the RHI's validation messages into the log. Rhi::Diagnostics has
 * already counted and captured the message by the time this runs; this decides
 * only how it is presented. Called from the driver's debug callback, so it may
 * run on any thread.
 */
void HandleRhiDiagnostic(Rhi::DiagnosticSeverity severity, std::string_view message)
{
    LogSeverity logSeverity = LogSeverity::Info;
    switch (severity)
    {
        case Rhi::DiagnosticSeverity::Info:
            logSeverity = LogSeverity::Info;
            break;
        case Rhi::DiagnosticSeverity::Warning:
            logSeverity = LogSeverity::Warning;
            break;
        case Rhi::DiagnosticSeverity::Error:
            logSeverity = LogSeverity::Error;
            break;
    }

    LogMsg(logSeverity, LogValidationLayer, "{}", message);
}

struct Options
{
    std::string ScenePath;
    std::string ContentRoot;    // --content; empty = resolve automatically
    uint64_t Frames = 0;        // 0 = run until closed
    bool bFixedDt = false;      // use a fixed 1/60s timestep
    int CameraPreset = -1;      // -1 = free camera; else index into kCameraPresets
    std::string ScreenshotPath; // capture the final frame to this PNG path
    bool bScreenshotAutoPath = false;
    std::string ReportPath; // write a JSON run report to this path
    bool bReportAutoPath = false;
    bool bStrictValidation = false; // exit non-zero on any validation error
    Rhi::ValidationPolicy ValidationPolicy = Rhi::ValidationPolicy::Count;
    bool bHeadless = false; // --headless: render with no window

    /**
     * --no-ui. Suppresses the editor panel without touching anything else:
     * ImGui stays initialised and its pass still records, so a run with the
     * flag differs from one without it only in what is drawn. The baseline
     * capture uses it because the panel is a fifth of the frame and its hover
     * state depends on where the mouse was last left.
     */
    bool bNoUi = false;

    /**
     * --resolution. Zero in either half leaves the choice to the platform,
     * which sizes the window from the display it opens on.
     */
    Extent2D WindowSize{};

    /**
     * --borderless / --fullscreen. One field rather than two flags, so that
     * "borderless and exclusive at once" cannot be represented past parsing.
     */
    WindowMode StartWindowMode = WindowMode::Windowed;
    int JobCount =
        -1; // -1 = default, 0 = SerialJobSystem, N>0 = SharedQueueJobSystem with N worker threads

    /**
     * --vk-disable-extension, repeatable. Vulkan-specific by nature, which is
     * what the flag's prefix says: an optional extension exists in one backend's
     * vocabulary and nowhere else.
     */
    std::vector<std::string> DisabledVulkanExtensions;

    /**
     * --vk-force-single-queue. Same prefix for the same reason: it is queue
     * families this collapses, and only Vulkan has them.
     */
    bool bForceSingleQueue = false;
};

/**
 * Hardcoded camera transforms selected via --camera-preset <N>, for
 * deterministic screenshots/reports. Rotation is (pitch, yaw, roll) in
 * degrees, matching Transform::Rotation.
 */
struct CameraPresetData
{
    glm::vec3 Position;
    glm::vec3 Rotation;
};

constexpr CameraPresetData kCameraPresets[] = {
    {{0.f, 2.f, 10.f}, {0.f, 0.f, 0.f}},    // 0: front view, eye height
    {{10.f, 2.f, 0.f}, {0.f, 90.f, 0.f}},   // 1: side view
    {{0.f, 20.f, 0.1f}, {-89.f, 0.f, 0.f}}, // 2: top-down view
};
constexpr int kNumCameraPresets =
    static_cast<int>(sizeof(kCameraPresets) / sizeof(kCameraPresets[0]));

/**
 * Whether a screenshot can be written from a present target in `format`.
 *
 * stbi_write_png takes 8 bits per channel and nothing else, so a 16-bit float
 * target — which nothing asks for today but the neutral format list can name —
 * would need tone mapping rather than a channel swap. Rejected with a message
 * instead of silently reinterpreting the bytes, which is what a bare
 * BytesPerTexel check would allow.
 */
constexpr bool IsWritablePngFormat(Rhi::Format format)
{
    return format == Rhi::Format::BGRA8Unorm || format == Rhi::Format::RGBA8Unorm ||
           format == Rhi::Format::RGBA8Srgb;
}

void PrintUsage()
{
    std::cout << "HikariEngine\n"
                 "\n"
                 "Usage: HikariEngine [options]\n"
                 "\n"
                 "Options:\n"
                 "  --scene <path>          Load a scene (.map) on startup\n"
                 "  --content <dir>         Use <dir> as the content root\n"
                 "  --frames <N>            Exit automatically after N frames "
                 "(0 = run until closed)\n"
                 "  --fixed-dt              Use a fixed 1/60s timestep instead "
                 "of wall-clock time\n"
                 "  --camera-preset <N>     Use a hardcoded camera preset (0-" +
                     std::to_string(kNumCameraPresets - 1) +
                     ") instead of free camera\n"
                     "  --screenshot <path>     Write a PNG of the final frame "
                     "before exiting\n"
                     "  --report <path>         Write a JSON run report before "
                     "exiting\n"
                     "  --strict-validation     Exit non-zero if any Vulkan "
                     "validation error occurred\n"
                     "  --validation-policy <p> ignore | count | failfast "
                     "(default: count; failfast aborts on the first error)\n"
                     "  --resolution <W>x<H>    Start with a window of this size (default: "
                     "three quarters of\n"
                     "                          the display)\n"
                     "  --borderless            Start covering the display as a borderless "
                     "window\n"
                     "  --fullscreen            Start in exclusive fullscreen, selecting a "
                     "display mode\n"
                     "  --headless              Run without a window, rendering into an "
                     "offscreen target.\n"
                     "                          Requires --frames, and cannot be combined with "
                     "--borderless or --fullscreen\n"
                     "  --no-ui                 Suppress the editor panel. ImGui still "
                     "initialises and its\n"
                     "                          pass still runs, so only what is drawn "
                     "changes\n"
                     "  --jobs <N>              Worker thread count (0 = SerialJobSystem, "
                     "no threads; default = hardware_concurrency() - 1)\n"
                     "  --vk-disable-extension <name>\n"
                     "                          Vulkan only. Behave as though the device did not "
                     "support this\n"
                     "                          optional extension, to exercise the fallback path. "
                     "Repeatable.\n"
                     "  --vk-force-single-queue Vulkan only. Behave as though the device exposed "
                     "one queue\n"
                     "                          family, to exercise the path an integrated GPU "
                     "takes\n"
                     "  --help                  Print this message and exit\n";
}

[[noreturn]] void ExitWithUsage(int code)
{
    PrintUsage();
    std::exit(code);
}

/**
 * --borderless and --fullscreen name two different ways of covering the
 * display, so a command line carrying both asks for nothing coherent. Rejected
 * rather than settled by precedence or by order: a launcher passing both is
 * misconfigured, and honouring one of them quietly hides that. Repeating the
 * same flag is not a conflict.
 */
void RejectConflictingWindowMode(WindowMode current, WindowMode requested, const std::string& flag)
{
    if (current == WindowMode::Windowed || current == requested)
        return;

    LogMsg(LogSeverity::Error, LogMain, "{} cannot be combined with {}", flag,
           current == WindowMode::BorderlessFullscreen ? "--borderless" : "--fullscreen");
    ExitWithUsage(EXIT_FAILURE);
}

std::string GenerateTimestamp()
{
    using namespace std::chrono;
    std::time_t now = system_clock::to_time_t(system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%d_%m_%Y_%H_%M_%S"); // DD_MM_YYYY_HH_mm_SS
    return oss.str();
}

Options ParseArgs(int argc, char** argv)
{
    constexpr const char* DEFAULT_SCENE = "scenes/test_scene.map";
    constexpr uint64_t DEFAULT_FRAMES = 1000u;

    Options options;

    try
    {
        // Named rather than a temporary in the range-init: Options() hands out
        // a reference into the CommandLine, which C++20 would not keep alive
        // for the duration of the loop.
        const CommandLine commandLine(argc, argv);

        for (const CommandLineOption& option : commandLine.Options())
        {
            const std::string& flag = option.Flag;

            if (flag == "--help" || flag == "-h")
                ExitWithUsage(EXIT_SUCCESS);
            else if (flag == "--content")
                options.ContentRoot = option.RequireValue();
            else if (flag == "--scene")
                options.ScenePath = option.Value.value_or(DEFAULT_SCENE);
            else if (flag == "--frames")
                options.Frames = option.Value ? option.RequireUint64() : DEFAULT_FRAMES;
            else if (flag == "--fixed-dt")
            {
                option.RequireNoValue();
                options.bFixedDt = true;
            }
            else if (flag == "--camera-preset")
                options.CameraPreset = option.RequireInt();
            else if (flag == "--screenshot")
            {
                if (option.Value)
                    options.ScreenshotPath = *option.Value;
                else
                    options.bScreenshotAutoPath = true;
            }
            else if (flag == "--report")
            {
                if (option.Value)
                    options.ReportPath = *option.Value;
                else
                    options.bReportAutoPath = true;
            }
            else if (flag == "--strict-validation")
            {
                option.RequireNoValue();
                options.bStrictValidation = true;
            }
            else if (flag == "--validation-policy")
            {
                const std::string value = option.RequireValue();
                if (value == "ignore")
                    options.ValidationPolicy = Rhi::ValidationPolicy::Ignore;
                else if (value == "count")
                    options.ValidationPolicy = Rhi::ValidationPolicy::Count;
                else if (value == "failfast")
                    options.ValidationPolicy = Rhi::ValidationPolicy::FailFast;
                else
                {
                    LogMsg(LogSeverity::Error, LogMain,
                           "--validation-policy expects ignore, count or failfast, got: {}", value);
                    ExitWithUsage(EXIT_FAILURE);
                }
            }
            else if (flag == "--resolution")
                options.WindowSize = option.RequireExtent2D();
            else if (flag == "--borderless")
            {
                option.RequireNoValue();
                RejectConflictingWindowMode(options.StartWindowMode,
                                            WindowMode::BorderlessFullscreen, flag);
                options.StartWindowMode = WindowMode::BorderlessFullscreen;
            }
            else if (flag == "--fullscreen")
            {
                option.RequireNoValue();
                RejectConflictingWindowMode(options.StartWindowMode,
                                            WindowMode::ExclusiveFullscreen, flag);
                options.StartWindowMode = WindowMode::ExclusiveFullscreen;
            }
            else if (flag == "--headless")
            {
                option.RequireNoValue();
                options.bHeadless = true;
            }
            else if (flag == "--no-ui")
            {
                option.RequireNoValue();
                options.bNoUi = true;
            }
            else if (flag == "--jobs")
                options.JobCount = option.RequireInt();
            else if (flag == "--vk-disable-extension")
                options.DisabledVulkanExtensions.push_back(option.RequireValue());
            else if (flag == "--vk-force-single-queue")
            {
                option.RequireNoValue();
                options.bForceSingleQueue = true;
            }
            else
            {
                LogMsg(LogSeverity::Error, LogMain, "Unknown option: {}", flag);
                ExitWithUsage(EXIT_FAILURE);
            }
        }
    }
    catch (const CommandLineError& e)
    {
        LogMsg(LogSeverity::Error, LogMain, "{}", e.what());
        ExitWithUsage(EXIT_FAILURE);
    }

    // A window mode for a run with no window asks for nothing coherent.
    if (options.bHeadless && options.StartWindowMode != WindowMode::Windowed)
    {
        LogMsg(LogSeverity::Error, LogMain,
               "--headless cannot be combined with {}: a run with no window has no window mode",
               options.StartWindowMode == WindowMode::BorderlessFullscreen ? "--borderless"
                                                                           : "--fullscreen");
        ExitWithUsage(EXIT_FAILURE);
    }

    // Of the frame loop's three exits, two need a window (SDL_EVENT_QUIT and
    // ImGui's Quit button) and the third is the frame counter, which only fires
    // when Frames != 0. So a headless run without --frames ends on a signal —
    // and headless exists for CI, where nobody is there to send one. The job
    // burns its whole timeout and dies to SIGTERM, which nothing handles, so it
    // writes neither screenshot nor report.
    //
    // Ctrl-C does work interactively (the handler sets g_bShouldClose and the
    // artefacts are still written), which is what makes an unbounded run
    // defensible at a terminal — a soak, say. Rejected anyway because the
    // failure it prevents is silent and expensive in the place this mode is
    // for, and Frames is a uint64_t if someone really wants to soak.
    if (options.bHeadless && options.Frames == 0)
    {
        LogMsg(LogSeverity::Error, LogMain,
               "--headless requires --frames: with no window there is nothing that can end the "
               "run, so it would render forever and write nothing");
        ExitWithUsage(EXIT_FAILURE);
    }

    // Ignore stops errors ever being counted, so --strict-validation would pass
    // a run that had them. Rejected rather than silently preferred either way:
    // in CI that combination reads as "validation is enforced" and is not.
    if (options.bStrictValidation && options.ValidationPolicy == Rhi::ValidationPolicy::Ignore)
    {
        LogMsg(LogSeverity::Error, LogMain,
               "--strict-validation cannot be combined with --validation-policy ignore: "
               "no errors would be counted for it to act on");
        ExitWithUsage(EXIT_FAILURE);
    }

    return options;
}

/**
 * Elapsed milliseconds since `start`.
 *
 * steady_clock, not high_resolution_clock: libstdc++ makes the latter an alias
 * for system_clock, which the standard does not require to move only forwards,
 * so an NTP correction mid-run can produce a nonsense interval or a negative
 * one. core/Timer.h makes the same choice for the same reason.
 */
inline float MillisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

/** The four numbers the run report carries for a series of frame timings. */
struct TimingStats
{
    float Mean = 0.f;
    float P99 = 0.f;
    float Min = 0.f;
    float Max = 0.f;
};

/** Takes the samples by value because it sorts them to find the percentile. */
inline TimingStats ComputeTimingStats(std::vector<float> samples)
{
    if (samples.empty())
        return {};

    double sum = 0.0;
    for (const float sample : samples)
        sum += sample;

    std::sort(samples.begin(), samples.end());

    // Nearest-rank: the smallest sample at or above the 99th percentile, which
    // for fewer than 100 samples is the largest one.
    size_t index = static_cast<size_t>(std::ceil(0.99 * static_cast<double>(samples.size())));
    index = std::min(index, samples.size()) - 1;

    return TimingStats{.Mean = static_cast<float>(sum / static_cast<double>(samples.size())),
                       .P99 = samples[index],
                       .Min = samples.front(),
                       .Max = samples.back()};
}

class App
{
public:
    App(IPlatform& platform, const Paths& paths, Options options, IJobSystem& jobSystem,
        Rhi::Diagnostics& diagnostics, std::chrono::steady_clock::time_point processStart)
        : m_Platform(platform), m_Paths(paths), m_Options(std::move(options)),
          m_JobSystem(jobSystem), m_Diagnostics(diagnostics),
          m_RhiDevice(Rhi::CreateDevice(MakeDeviceDesc())),
          m_PhysicalDevice(Rhi::Vulkan::GetPhysicalDevice(*m_RhiDevice)),
          m_Device(Rhi::Vulkan::GetDevice(*m_RhiDevice)),
          m_GraphicsQueue(Rhi::Vulkan::GetGraphicsQueue(*m_RhiDevice)),
          m_QueueIndex(Rhi::Vulkan::GetGraphicsQueueFamily(*m_RhiDevice)),
          m_ProcessStart(processStart)
    {
    }
    ~App()
    {
        if (!m_bShutdown && *m_Device)
        {
            m_Device.waitIdle();
            Shutdown();
        }
    }

    void Run()
    {
        Init();

        m_Platform.Show();

        // Everything before the first frame: argument parsing, the window, the
        // device, every pipeline, and the scene's uploads.
        m_StartupMs = MillisecondsSince(m_ProcessStart);

        while (!g_bShouldClose)
        {
            const auto frameStart = std::chrono::steady_clock::now();
            m_FrameWaitMs = 0.f;

            if (!m_bIsFocused)
            {
                // Counted as waiting rather than as work: an unfocused frame is
                // idling on purpose, and charging that to the CPU cost would
                // make a backgrounded run look like a slow one.
                const auto idleStart = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                m_FrameWaitMs += MillisecondsSince(idleStart);
            }

            const auto now = frameStart;
            if (m_Options.bFixedDt)
            {
                m_DeltaTime = 1.f / 60.f;
                m_RunTime += m_DeltaTime;
            }
            else
            {
                m_DeltaTime =
                    std::chrono::duration<float, std::chrono::seconds::period>(now - m_LastTime)
                        .count();
                m_RunTime =
                    std::chrono::duration<float, std::chrono::seconds::period>(now - m_StartTime)
                        .count();
            }
            m_LastTime = now;

            // Guarded rather than abstracted: a headless run never initialised
            // SDL, so polling it is not merely pointless but a call into a
            // subsystem that does not exist. The event seam that removes the
            // direct call altogether is its own step.
            SDL_Event event;
            while (!m_Platform.IsHeadless() && SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);

                switch (event.type)
                {
                    case SDL_EVENT_MOUSE_MOTION:
                        HandleMouse(event.motion.xrel, event.motion.yrel);
                        break;
                    case SDL_EVENT_QUIT:
                        g_bShouldClose = true;
                        break;
                    case SDL_EVENT_WINDOW_RESIZED:
                        RecreateSwapchainAndRenderImages();
                        break;
                    case SDL_EVENT_WINDOW_FOCUS_GAINED:
                        m_bIsFocused = true;
                        LogMsg(LogSeverity::Info, LogWindow, "Focus gained");
                        break;
                    case SDL_EVENT_WINDOW_FOCUS_LOST:
                        m_bIsFocused = false;
                        LogMsg(LogSeverity::Info, LogWindow, "Focus lost");
                        break;
                    case SDL_EVENT_KEY_DOWN:
                        if (m_bIsFocused)
                            HandleKey(event.key.key);
                        break;
                }
            }

            m_Camera->Tick();
            HandleMovement();

            if (m_bCursorVisible && !m_Options.bNoUi)
                DrawImGuiFrame();

            ModelManager::Get()->GenerateBatches();

            const bool bIsLastFrame = g_bShouldClose || (m_Options.Frames != 0 &&
                                                         (m_FrameCounter + 1) >= m_Options.Frames);
            const bool captureScreenshot = bIsLastFrame && (!m_Options.ScreenshotPath.empty() ||
                                                            m_Options.bScreenshotAutoPath);

            DrawFrame(captureScreenshot);

            ++m_FrameCounter;
            RecordFrameTiming(frameStart);

            if (m_Options.Frames != 0 && m_FrameCounter >= m_Options.Frames)
            {
                g_bShouldClose = true;
            }
        }

        const bool bWantsScreenshot =
            !m_Options.ScreenshotPath.empty() || m_Options.bScreenshotAutoPath;

        // The in-frame decision at the top of the loop asks whether this is the
        // last frame, and a signal arriving after that line makes the answer
        // wrong: the loop exits at the top of the next iteration with nothing
        // staged, and the run ends with the "called without a captured frame"
        // error instead of a PNG. Draw one more frame and capture that instead.
        //
        // Only reachable on the interrupted path — a --frames N run stages its
        // capture on frame N-1 and never gets here. The extra frame counts as an
        // ordinary one, in the frame total and in the timing series: it is a
        // frame the app drew, and an interrupted run is not a measured one.
        if (bWantsScreenshot && !m_bScreenshotBufferReady)
        {
            const auto frameStart = std::chrono::steady_clock::now();
            m_FrameWaitMs = 0.f;

            DrawFrame(true);

            ++m_FrameCounter;
            RecordFrameTiming(frameStart);
        }

        if (bWantsScreenshot)
            WriteScreenshot();

        if (!m_Options.ReportPath.empty() || m_Options.bReportAutoPath)
            WriteReport();

        m_Device.waitIdle();
        Shutdown();
    }

private:
    /**
     * Called from the constructor's initialiser list, so it may only touch
     * members declared above m_RhiDevice.
     */
    [[nodiscard]] Rhi::DeviceDesc MakeDeviceDesc() const
    {
        Rhi::DeviceDesc desc;
        desc.ApplicationName = "HikariEngine";
        desc.bEnableValidation = bEnableValidationLayers;
        desc.pDiagnostics = &m_Diagnostics;
        // The line the whole headless path turns on: no present requirement
        // means the device creates no surface, and CreatePresentTarget hands
        // back an OffscreenTarget instead of a SwapchainTarget.
        desc.Requirements.bPresent = !m_Platform.IsHeadless();
        desc.Requirements.NativeWindowHandle = m_Platform.GetNativeWindowHandle();
        desc.DisabledOptionalExtensions = m_Options.DisabledVulkanExtensions;
        desc.bForceSingleQueue = m_Options.bForceSingleQueue;
        return desc;
    }

    void Init()
    {
        LogMsg(LogSeverity::Info, LogMain, "Init()");

        m_StartTime = std::chrono::steady_clock::now();
        m_LastTime = m_StartTime;

        InitVulkan();
        InitImGui();

        if (!m_Options.ScenePath.empty())
        {
            m_SceneGraph = XmlParser::LoadScene(m_Paths.Content(m_Options.ScenePath).string());
            if (!m_SceneGraph)
            {
                throw std::runtime_error("Failed to load scene: " + m_Options.ScenePath);
            }
            ResourceManager::PurgeCaches();
        }
        else
        {
            m_SceneGraph = std::make_unique<SceneGraph>();
        }

        // TODO: read from scene
        CubemapCreateInfo createInfo{};
        createInfo.Name = "Skybox";
        createInfo.Format = Rhi::Format::RGBA8Srgb;

        const auto skyboxFace = [this](std::string_view face)
        { return m_Paths.Content("textures/skybox/" + std::string(face)).string(); };

        createInfo.RightPath = skyboxFace("right.jpg");
        createInfo.LeftPath = skyboxFace("left.jpg");
        createInfo.TopPath = skyboxFace("top.jpg");
        createInfo.BottomPath = skyboxFace("bottom.jpg");
        createInfo.FrontPath = skyboxFace("front.jpg");
        createInfo.BackPath = skyboxFace("back.jpg");

        m_Skybox = ResourceManager::Get()->LoadCubemap(createInfo);

        m_Camera = std::make_unique<Camera>();

        if (m_Options.CameraPreset >= 0)
        {
            if (m_Options.CameraPreset >= kNumCameraPresets)
            {
                throw std::runtime_error(
                    "Invalid --camera-preset index: " + std::to_string(m_Options.CameraPreset) +
                    " (valid range: 0-" + std::to_string(kNumCameraPresets - 1) + ")");
            }

            const CameraPresetData& preset = kCameraPresets[m_Options.CameraPreset];
            m_Camera->GetTransform().Position = preset.Position;
            m_Camera->GetTransform().Rotation = preset.Rotation;
        }
        else
        {
            m_Camera->GetTransform().Position += glm::vec3(0.f, 0.f, 10.f);
        }

        LogMsg(LogSeverity::Info, LogMain, "Init() succeeded");
    }

    void InitImGui()
    {
        LogMsg(LogSeverity::Info, LogImGui, "InitImGui()");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        const vk::Format swapchainFormat = SwapchainFormat();
        vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {
            .colorAttachmentCount = 1u, .pColorAttachmentFormats = &swapchainFormat};

        // The one place that is allowed to hold raw Vulkan handles from the RHI:
        // ImGui's backend takes them by value and there is no neutral shape for
        // that, short of wrapping ImGui itself.
        const Rhi::Vulkan::NativeDevice native = Rhi::Vulkan::GetNative(*m_RhiDevice);

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = native.ApiVersion;
        initInfo.Instance = native.Instance;
        initInfo.PhysicalDevice = native.PhysicalDevice;
        initInfo.Device = native.Device;
        initInfo.QueueFamily = native.GraphicsQueueFamily;
        initInfo.Queue = native.GraphicsQueue;
        initInfo.DescriptorPool = VK_NULL_HANDLE;
        initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
        initInfo.MinImageCount = m_PresentTarget->GetImageCount();
        initInfo.MinAllocationSize = 1024 * 1024;
        initInfo.ImageCount = m_PresentTarget->GetImageCount();
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineCache = Rhi::Vulkan::GetNativePipelineCache(*m_PipelineCache);
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = nullptr;

        // The platform backend is the only half of ImGui that needs a window.
        // Skipping it leaves ImGui with no platform backend at all, which is a
        // supported configuration: what the backend supplies is io.DisplaySize
        // and io.DeltaTime, and DrawImGuiFrame sets both by hand instead.
        //
        // The renderer backend needs nothing from the window system. Everything
        // surface-shaped in it lives in the optional ImGui_ImplVulkanH_* helper
        // family, which this app has never called — it records ImGui's draws
        // into a dynamic-rendering pass of its own, against whatever image the
        // present target handed back.
        if (!m_Platform.IsHeadless())
        {
            ImGui_ImplSDL3_InitForVulkan(
                static_cast<SDL_Window*>(m_Platform.GetNativeWindowHandle()));
        }

        ImGui_ImplVulkan_Init(&initInfo);
    }

    /**
     * The present target speaks neutral types; these are the two places the
     * renderer still needs the Vulkan spelling, and it needs it often enough
     * that converting at each use site would drown the call sites.
     */
    vk::Extent2D SwapchainExtent() const
    {
        const Core::Extent2D extent = m_PresentTarget->GetExtent();
        return vk::Extent2D{extent.Width, extent.Height};
    }

    vk::Format SwapchainFormat() const
    {
        return Rhi::Vulkan::GetNativeFormat(m_PresentTarget->GetFormat());
    }

    void InitVulkan()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "InitVulkan()");

        // The device itself was created in the constructor, so that every member
        // below can assume it exists.
        //
        // First, because almost everything after it is sized to the target's
        // extent or built against its format.
        const Extent2D framebufferExtent = m_Platform.GetFramebufferExtent();
        m_PresentTarget = m_RhiDevice->CreatePresentTarget(
            Rhi::PresentTargetDesc{.Extent = {framebufferExtent.Width, framebufferExtent.Height},
                                   .FramesInFlight = NUM_FRAMES_IN_FLIGHT});

        CreateDepthResources();
        CreateDescriptorSetLayouts();
        CreateCommandPools();
        CreateTextureSampler();

        m_UploadContext = m_RhiDevice->CreateUploadContext(
            Rhi::UploadContextDesc{.DebugName = "Asset Upload Context"});

        // Before any pipeline is built, and before ImGui, which is handed the
        // same one. Paths::UserData is empty when the platform gave us nowhere
        // to write, and an empty path is how the cache is told to stay in
        // memory for the run.
        m_PipelineCache = m_RhiDevice->CreatePipelineCache(Rhi::PipelineCacheDesc{
            .Path = m_Paths.UserData("pipeline_cache.bin"), .DebugName = "Pipeline Cache"});

        ResourceManager::Init(*m_RhiDevice, *m_UploadContext, m_Paths);
        MaterialFactory::Init(*m_RhiDevice, m_TextureSampler.Get());

        CreatePipelines();
        CreateCommandBuffers();
        CreateGlobalBuffers();
        CreateInstanceBuffers(INITIAL_INSTANCE_CAPACITY);
        CreateRenderTargets();
        CreateDescriptorPool();

        // TODO: read from scene
        CloudSystemCreateInfo cloudCreateInfo{.RhiDevice = *m_RhiDevice,
                                              .PipelineCache = *m_PipelineCache,
                                              .ContentPaths = m_Paths,
                                              .GlobalSetLayout = m_GlobalBufferSetLayout,
                                              .DepthSetLayout = m_DepthSetLayout,
                                              .CommandPool = m_GenericCommandPool,
                                              // The device reports whether an async compute
                                              // queue exists (DeviceCaps::
                                              // bHasDedicatedComputeQueue); moving the cloud
                                              // dispatches onto it needs them to own their own
                                              // submission and cross-queue synchronization
                                              // first, so they share the graphics queue.
                                              .ComputeQueue = m_GraphicsQueue,
                                              .SwapchainWidth = SwapchainExtent().width,
                                              .SwapchainHeight = SwapchainExtent().height,
                                              .FramesInFlight = NUM_FRAMES_IN_FLIGHT};
        m_CloudSystem = std::make_unique<CloudSystem>(cloudCreateInfo);

        CreateDescriptorSets();
        CreateSyncObjects();
        CreateQuadBuffers();
    }

    /**
     * The stamp that names the auto-pathed files of one capture.
     *
     * Taken when the first of those files is written and reused by the rest, so
     * that a screenshot and the report describing the same moment share a name
     * — asking the clock separately puts them a second apart whenever the two
     * writes straddle a second boundary.
     */
    const std::string& CaptureTimestamp()
    {
        if (m_CaptureTimestamp.empty())
            m_CaptureTimestamp = GenerateTimestamp();

        return m_CaptureTimestamp;
    }

    void WriteReport()
    {
        const std::string DEFAULT_PATH = "tests/reports/report_";
        EnsureParentDirectoryExists(DEFAULT_PATH);

        // The last frame's counts, as the draw call and batch numbers below also
        // are. Worth knowing when comparing two reports: --screenshot captures
        // on the final frame, and the copy it inserts costs one extra barrier
        // and one extra call, so a captured run legitimately reads one higher
        // than an uncaptured one.
        const Rhi::BarrierCounts barrierCounts = FrameBarrierCounts();

        const TimingStats frame = ComputeTimingStats(m_FrameMs);
        const TimingStats cpu = ComputeTimingStats(m_CpuMs);

        std::string path =
            m_Options.bReportAutoPath ? DEFAULT_PATH + CaptureTimestamp() : m_Options.ReportPath;
        path = EnsureExtension(path, ".json");
        std::ofstream file(path);
        if (!file.is_open())
        {
            LogMsg(LogSeverity::Error, LogMain, "Failed to open report file for writing: {}", path);
            return;
        }

        const auto stats = [](const TimingStats& s)
        {
            return std::format("{{ \"mean\": {:.4f}, \"p99\": {:.4f}, \"min\": {:.4f}, "
                               "\"max\": {:.4f} }}",
                               s.Mean, s.P99, s.Min, s.Max);
        };

        // Two objects rather than one flat list, because they are read
        // differently: everything under "counters" is an expectation that must
        // match exactly, everything under "timings" is a measurement that varies
        // with the machine. A reader cannot tell those apart in a flat object,
        // and a number that looks authoritative and is not is worse than no
        // number. "run" is what makes two reports comparable at all — the same
        // scene at a different resolution, present mode or build configuration
        // is not the same measurement.
        file << "{\n"
             << "  \"frames\": " << m_FrameCounter << ",\n"
             << "  \"counters\": {\n"
             << "    \"validationErrors\": " << m_Diagnostics.ErrorCount() << ",\n"
             << "    \"validationWarnings\": " << m_Diagnostics.WarningCount() << ",\n"
             << "    \"drawCalls\": " << (m_OpaqueDrawCallCount + m_TransparentDrawCallCount)
             << ",\n"
             << "    \"batches\": " << (m_OpaqueBatchCount + m_TransparentBatchCount) << ",\n"
             << "    \"instances\": " << (m_OpaqueInstanceCount + m_TransparentInstanceCount)
             << ",\n"
             << "    \"barriers\": " << barrierCounts.Barriers << ",\n"
             << "    \"barrierCalls\": " << barrierCounts.Calls << "\n"
             << "  },\n"
             << "  \"timings\": {\n"
             << std::format("    \"startupMs\": {:.4f},\n", m_StartupMs)
             << std::format("    \"firstFrame\": {{ \"frameMs\": {:.4f}, \"cpuMs\": {:.4f} }},\n",
                            m_FirstFrameMs, m_FirstFrameCpuMs)
             << "    \"frameMs\": " << stats(frame) << ",\n"
             << "    \"cpuMs\": " << stats(cpu) << "\n"
             << "  },\n"
             << "  \"run\": {\n"
             << "    \"fixedDt\": " << (m_Options.bFixedDt ? "true" : "false") << ",\n"
             << "    \"headless\": " << (m_Platform.IsHeadless() ? "true" : "false") << ",\n"
             << "    \"noUi\": " << (m_Options.bNoUi ? "true" : "false") << ",\n"
             << "    \"width\": " << SwapchainExtent().width << ",\n"
             << "    \"height\": " << SwapchainExtent().height << ",\n"
             << "    \"jobCount\": " << m_JobSystem.WorkerCount() << ",\n"
             << "    \"presentMode\": " << PresentModeJson() << ",\n"
             << "    \"buildConfig\": \"" << HIKARI_BUILD_CONFIG << "\"\n"
             << "  }\n"
             << "}\n";
        file.close();

        LogMsg(LogSeverity::Info, LogMain, "Wrote report to {}", path);
    }

    /**
     * The present mode as a JSON value: a quoted name, or null when the target
     * does not present at all, which is what an offscreen run reports.
     */
    std::string PresentModeJson() const
    {
        const std::optional<Rhi::PresentMode> mode = m_PresentTarget->GetPresentMode();
        if (!mode)
            return "null";

        switch (*mode)
        {
            case Rhi::PresentMode::Immediate:
                return "\"immediate\"";
            case Rhi::PresentMode::Mailbox:
                return "\"mailbox\"";
            case Rhi::PresentMode::Fifo:
                return "\"fifo\"";
            case Rhi::PresentMode::FifoRelaxed:
                return "\"fifo-relaxed\"";
        }

        return "null";
    }

    /**
     * Writes the frame captured into m_ScreenshotStagingBuffer (during the
     * final frame's DrawFrame() call, before it was presented) out to disk as
     * a PNG. Used for deterministic verification via --screenshot.
     */
    void WriteScreenshot()
    {
        const std::string DEFAULT_PATH = "tests/screenshots/screenshot_";
        EnsureParentDirectoryExists(DEFAULT_PATH);

        m_Device.waitIdle();

        if (!m_bScreenshotBufferReady)
        {
            LogMsg(LogSeverity::Error, LogMain,
                   "WriteScreenshot() called without a captured frame. No "
                   "frame was drawn?");
            return;
        }

        // Driven by what the target actually chose, not by what a swapchain
        // usually chooses. A present target picks its own format — an offscreen
        // one need not agree with a surface — so a hardcoded channel order is
        // a latent bug even before a headless run exists to trip over it.
        const Rhi::Format format = m_PresentTarget->GetFormat();
        const uint32_t bytesPerPixel = Rhi::BytesPerTexel(format);
        if (!IsWritablePngFormat(format))
        {
            LogMsg(LogSeverity::Error, LogMain,
                   "Cannot write a screenshot: the present target's format is not an 8-bit "
                   "four-channel one, which is all stbi_write_png can take.");
            return;
        }

        const uint32_t width = SwapchainExtent().width;
        const uint32_t height = SwapchainExtent().height;
        const vk::DeviceSize bufferSize =
            static_cast<vk::DeviceSize>(width) * height * bytesPerPixel;

        // PNG is RGBA, so a BGRA target needs its first and third channels
        // swapped and an RGBA one needs nothing. The shader is indifferent
        // either way: it writes SV_Target component 0 and the hardware maps
        // that to whatever the format's first component is.
        const bool bSwapRedAndBlue = format == Rhi::Format::BGRA8Unorm;

        const auto* src = static_cast<const uint8_t*>(
            m_RhiDevice->GetMappedData(m_ScreenshotStagingBuffer.Get()));
        std::vector<uint8_t> pixels(static_cast<size_t>(bufferSize));
        for (size_t i = 0; i < static_cast<size_t>(width) * height; i++)
        {
            pixels[i * 4 + 0] = bSwapRedAndBlue ? src[i * 4 + 2] : src[i * 4 + 0];
            pixels[i * 4 + 1] = src[i * 4 + 1];
            pixels[i * 4 + 2] = bSwapRedAndBlue ? src[i * 4 + 0] : src[i * 4 + 2];
            pixels[i * 4 + 3] = src[i * 4 + 3];
        }

        std::string path = m_Options.bScreenshotAutoPath ? DEFAULT_PATH + CaptureTimestamp()
                                                         : m_Options.ScreenshotPath;
        path = EnsureExtension(path, ".png");
        const int writeResult =
            stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                           pixels.data(), static_cast<int>(width * bytesPerPixel));

        if (writeResult == 0)
        {
            LogMsg(LogSeverity::Error, LogMain, "Failed to write screenshot to {}", path);
        }
        else
        {
            LogMsg(LogSeverity::Info, LogMain, "Wrote screenshot to {}", path);
        }
    }

    void Shutdown()
    {
        LogMsg(LogSeverity::Info, LogMain, "Shutdown()");

        // Before ImGui, which built pipelines into the same cache, and before
        // the device that owns it goes away.
        m_PipelineCache->Save();

        m_Skybox.reset();
        m_SceneGraph.reset();
        ShutdownImGui();
        MaterialFactory::Shutdown();
        ResourceManager::Shutdown();

        m_bShutdown = true;
    }

    void ShutdownImGui()
    {
        ImGui_ImplVulkan_Shutdown();
        if (!m_Platform.IsHeadless())
            ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void HandleMouse(float x, float y)
    {
        if (!m_bCursorVisible && m_Options.CameraPreset < 0)
            m_Camera->Rotate(x, y);
    }

    void ShowCursor()
    {
        m_Platform.WarpMouse(static_cast<float>(SwapchainExtent().width / 2.f),
                             static_cast<float>(SwapchainExtent().height / 2.f));
        m_Platform.SetRelativeMouseMode(false);
        m_bCursorVisible = true;
    }

    void HideCursor()
    {
        m_Platform.SetRelativeMouseMode(true);
        m_bCursorVisible = false;
    }

    /** This includes OS key repeat delay. */
    void HandleKey(SDL_Keycode key)
    {
        switch (key)
        {
            case SDLK_ESCAPE:
                if (m_bCursorVisible)
                    HideCursor();
                else
                    ShowCursor();
                break;
            case SDLK_F9:
                m_Platform.SetWindowMode(WindowMode::Windowed);
                break;
            case SDLK_F10:
                m_Platform.SetWindowMode(WindowMode::BorderlessFullscreen);
                break;
            case SDLK_F11:
                m_Platform.SetWindowMode(WindowMode::ExclusiveFullscreen);
                break;
        }
    }

    /**
     * Checking the state of the keys every frame, bypassing OS key repeat
     * delay.
     */
    void HandleMovement()
    {
        // Headless for the same reason the event pump is guarded: SDL was never
        // initialised. Unreachable in practice today, since m_bCursorVisible
        // starts true and only a key event can clear it, but that is a property
        // of the current defaults rather than something to rely on.
        if (m_Platform.IsHeadless() || m_bCursorVisible || m_Options.CameraPreset >= 0)
            return;

        glm::vec3 camOffset = {0.f, 0.f, 0.f};
        const bool* state = SDL_GetKeyboardState(nullptr);
        if (state[SDL_SCANCODE_A])
        {
            camOffset += -m_Camera->GetRightVector() * m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_D])
        {
            camOffset += m_Camera->GetRightVector() * m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_W])
        {
            camOffset += m_Camera->GetForwardVector() * m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_S])
        {
            camOffset += -m_Camera->GetForwardVector() * m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_Q])
        {
            camOffset += glm::vec3(0.f, -1.f, 0.f) * m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_E])
        {
            camOffset += glm::vec3(0.f, 1.f, 0.f) * m_Camera->GetMoveSpeed() * m_DeltaTime;
        }

        if ((std::fabs(camOffset.x) + std::fabs(camOffset.y) + std::fabs(camOffset.z)) > 0.f)
            m_Camera->GetTransform().Position += camOffset;
    }

    /**
     * Closes out the frame that started at `frameStart`.
     *
     * Frame 0 goes to its own pair of fields rather than into the series: it
     * pays for the first use of every pipeline, the first descriptor writes and
     * the first acquire, so it is an outlier by construction and mixing it in
     * describes neither it nor a steady-state frame. The panel's readout is fed
     * from the measurement too, so that it shows what a frame cost rather than
     * what --fixed-dt told the simulation to pretend.
     */
    void RecordFrameTiming(std::chrono::steady_clock::time_point frameStart)
    {
        const float frameMs = MillisecondsSince(frameStart);

        // Clamped because the two are measured by different calls to the same
        // clock: a wait can be charged a few microseconds the frame total has
        // not yet accrued, and a negative CPU cost is worse than a zero one.
        const float cpuMs = std::max(0.f, frameMs - m_FrameWaitMs);

        const float smoothing = 0.9f;
        m_DisplayFrameTime = (m_DisplayFrameTime * smoothing) + (frameMs * (1.f - smoothing));
        if (frameMs > 0.f)
            m_DisplayFPS = (m_DisplayFPS * smoothing) + ((1000.f / frameMs) * (1.f - smoothing));

        if (m_Options.ReportPath.empty() && !m_Options.bReportAutoPath)
            return;

        if (m_FrameCounter == 1)
        {
            m_FirstFrameMs = frameMs;
            m_FirstFrameCpuMs = cpuMs;
            return;
        }

        m_FrameMs.push_back(frameMs);
        m_CpuMs.push_back(cpuMs);
    }

    void DrawFrame(bool captureScreenshot = false)
    {
        // Semaphores coordinate GPU to GPU synchronisation, for example
        // ordering work between queues. They get reset automatically after the
        // waiting operation begins.
        //
        // Fences coordinate CPU to GPU synchronisation, for times when
        // the CPU needs to know that the GPU has finished a task. Must be
        // explicitely reset by the host.

        // A recreation that was deferred means the surface had no area when it
        // was last asked. Retry it here, and skip the frame while the answer
        // has not changed: there is nothing to draw into.
        if (m_bSwapchainOutOfDate)
        {
            RecreateSwapchainAndRenderImages();
            if (m_bSwapchainOutOfDate)
                return;
        }

        FrameData& frameData = m_Frames[m_FrameIndex];

        // The frame's blocking calls are measured rather than assumed away: on a
        // FIFO surface the wait is most of the wall clock, and a CPU cost that
        // included it would read as a regression whenever the display paced us.
        const auto fenceWaitStart = std::chrono::steady_clock::now();
        auto fenceResult = m_Device.waitForFences(*frameData.DrawFence, vk::True, UINT64_MAX);
        m_FrameWaitMs += MillisecondsSince(fenceWaitStart);
        if (fenceResult != vk::Result::eSuccess)
            throw std::runtime_error("Failed to wait for fence!");

        const auto acquireStart = std::chrono::steady_clock::now();
        const Rhi::AcquiredImage image = m_PresentTarget->Acquire();
        m_FrameWaitMs += MillisecondsSince(acquireStart);
        if (image.bNeedsRecreate)
        {
            RecreateSwapchainAndRenderImages();
            return;
        }

        m_Device.resetFences(*frameData.DrawFence);

        UpdateGlobalBuffer(m_FrameIndex);
        UpdateInstanceBuffer(m_FrameIndex);

        if (captureScreenshot && !m_bScreenshotBufferReady)
        {
            const vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(SwapchainExtent().width) *
                                              SwapchainExtent().height *
                                              Rhi::BytesPerTexel(m_PresentTarget->GetFormat());
            m_ScreenshotStagingBuffer = Rhi::UniqueHandle<Rhi::BufferHandle>(
                *m_RhiDevice,
                m_RhiDevice->CreateBuffer(Rhi::BufferDesc{.Size = bufferSize,
                                                          .Usage = Rhi::BufferUsage::CopyDst,
                                                          .Access = Rhi::MemoryAccess::GpuToCpu,
                                                          .DebugName = "Screenshot Staging"}));
            m_bScreenshotBufferReady = true;
        }

        {
            // Timer recordTimer("Command buffer recording");
            m_MainThreadBarrierCounts = {};

            m_JobSystem.Submit([&] { RecordOpaqueCommandBuffer(); });
            m_JobSystem.Submit([&] { RecordTransparentCommandBuffer(); });

            // these command buffers are very small and so likely faster to
            // record on main thread
            RecordSwapImageToDrawLayout(image);
            RecordCloudsCommandBuffer();
            RecordCompositeCommandBuffer(image);
            RecordImGui(image);
            RecordSwapImageToFinalLayout(image, captureScreenshot);

            m_JobSystem.Wait();
            LogBarrierCounts();
        }

        // TODO: even when ImGui is not showing, it's being submitted
        std::array<vk::CommandBuffer, 7> commandBuffers = {
            frameData.DrawLayoutCommandBuffer,  frameData.OpaqueCommandBuffer,
            frameData.TransparentCommandBuffer, frameData.CloudCommandBuffer,
            frameData.CompositeCommandBuffer,   frameData.ImGuiCommandBuffer,
            frameData.FinalLayoutCommandBuffer};
        // Every semaphore here belongs to the present target; the submit is still
        // the renderer's, so it names them through the native accessor.
        //
        // The waits arrive as a span because how many there are is the target's
        // business: a swapchain hands back the one its acquire signalled, and a
        // headless target hands back the previous write of the same image, or
        // nothing at all on the first pass. They share one destination stage
        // because they say the same thing — this image is not safe to write yet
        // — and the first write to it is the colour attachment output.
        //
        // Fixed capacity rather than a per-frame allocation. Both targets hand
        // back at most one today; a target that grew a third would fail here
        // rather than quietly having a wait dropped.
        std::array<vk::Semaphore, 4> waitSemaphores{};
        std::array<vk::PipelineStageFlags, waitSemaphores.size()> waitStages{};
        if (image.WaitSemaphores.size() > waitSemaphores.size())
            throw std::runtime_error("The present target asked for more acquire waits than the "
                                     "frame loop can submit!");

        for (size_t i = 0; i < image.WaitSemaphores.size(); i++)
        {
            waitSemaphores[i] = Rhi::Vulkan::GetSemaphore(*m_RhiDevice, image.WaitSemaphores[i]);
            waitStages[i] = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        }

        const vk::Semaphore signalOnComplete = Rhi::Vulkan::GetSemaphore(
            *m_RhiDevice, m_PresentTarget->GetRenderCompleteSemaphore(image.Index));

        vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = static_cast<uint32_t>(image.WaitSemaphores.size()),
            .pWaitSemaphores = waitSemaphores.data(),
            .pWaitDstStageMask = waitStages.data(),
            .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
            .pCommandBuffers = commandBuffers.data(),
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &signalOnComplete};
        m_GraphicsQueue.submit(submitInfo, *frameData.DrawFence);

        const auto presentStart = std::chrono::steady_clock::now();
        const bool bPresented = m_PresentTarget->Present(image.Index);
        m_FrameWaitMs += MillisecondsSince(presentStart);
        if (!bPresented)
            RecreateSwapchainAndRenderImages();

        m_FrameIndex = (m_FrameIndex + 1) % NUM_FRAMES_IN_FLIGHT;
    }

    void DrawImGuiFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        if (m_Platform.IsHeadless())
        {
            // Standing in for the platform backend. DeltaTime is the
            // load-bearing one — NewFrame asserts it is positive, where
            // DisplaySize need only be non-negative.
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(SwapchainExtent().width),
                                    static_cast<float>(SwapchainExtent().height));
            io.DeltaTime = m_DeltaTime;
        }
        else
        {
            ImGui_ImplSDL3_NewFrame();
        }

        ImGui::NewFrame();

        if (ImGui::Begin("Menu"))
        {
            for (size_t i = 0; i < m_SceneGraph->PointLights.size(); i++)
            {
                PointLight* pPointLight = m_SceneGraph->PointLights[i];
                ImGui::PushID(static_cast<int>(i));

                ImGui::Text("Point Light");
                ImGui::DragFloat3("Position", &pPointLight->GetPosition().x, 0.5f);
                ImGui::ColorEdit3("Color##PointLight", &pPointLight->GetColor().r);
                ImGui::SliderFloat("Intensity##PointLight", &pPointLight->GetIntensity(), 0.f,
                                   1000.f);
                ImGui::PopID();

                ImGui::Dummy(ImVec2(0.f, 5.f));
            }

            for (size_t i = 0; i < m_SceneGraph->DirLights.size(); i++)
            {
                DirectionalLight* pDirLight = m_SceneGraph->DirLights[i];
                ImGui::PushID(static_cast<int>(i));

                ImGui::Text("Directional Light");
                glm::vec3 dir = pDirLight->GetDirection();
                ImGui::DragFloat3("Direction", &dir.x, 0.5f);
                if (dir != pDirLight->GetDirection())
                    pDirLight->SetDirection(dir);

                ImGui::ColorEdit3("Color##DirLight", &pDirLight->GetColor().r);
                ImGui::SliderFloat("Intensity##DirLight", &pDirLight->GetIntensity(), 0.f, 10.f);

                ImGui::PopID();
            }

            ImGui::Dummy(ImVec2(0.f, 20.f));

            ImVec2 minFileDialogSize = ImVec2(600, 400);
            if (ImGui::Button("Load Scene"))
            {
                IGFD::FileDialogConfig config;
                config.path = m_Paths.Content("scenes").string();
                ImGuiFileDialog::Instance()->OpenDialog("LoadSceneDlg", "Choose Scene to Load",
                                                        ".map", config);
            }

            if (ImGuiFileDialog::Instance()->Display("LoadSceneDlg", ImGuiWindowFlags_NoCollapse,
                                                     minFileDialogSize))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    std::string path = ImGuiFileDialog::Instance()->GetFilePathName();

                    m_Device.waitIdle();

                    // Loading the new scene before unloading current scene.
                    // Speeds up load times by not unloading resources which are
                    // used in both scenes. This does mean that you have to
                    // temporarily store both scenes in memory until the load if
                    // finished though. Can look into this in the future if it
                    // becomes a problem.
                    std::unique_ptr<SceneGraph> tempSceneGraph = XmlParser::LoadScene(path);
                    if (tempSceneGraph.get())
                    {
                        m_SceneGraph.reset();
                        m_SceneGraph = std::move(tempSceneGraph);
                        ResourceManager::PurgeCaches();
                    }
                }
                ImGuiFileDialog::Instance()->Close();
            }

            if (ImGui::Button("Save Scene"))
            {
                IGFD::FileDialogConfig config;
                config.path = m_Paths.Content("scenes").string();
                config.fileName = "new_scene.map";
                ImGuiFileDialog::Instance()->OpenDialog("SaveSceneDlg", "Save Scene As", ".map",
                                                        config);
            }

            if (ImGuiFileDialog::Instance()->Display("SaveSceneDlg", ImGuiWindowFlags_NoCollapse,
                                                     minFileDialogSize))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    std::string path = ImGuiFileDialog::Instance()->GetFilePathName();

                    XmlParser::SaveScene(m_SceneGraph, path);
                }
                ImGuiFileDialog::Instance()->Close();
            }

            if (ImGui::Button("Quit"))
            {
                g_bShouldClose = true;
            }

            ImGui::Text("Frame time: %.4fms", m_DisplayFrameTime);
            ImGui::Text("FPS: %.1f", m_DisplayFPS);
        }
        ImGui::End();

        ImGui::Render();
    }

    [[nodiscard]] vk::raii::ShaderModule
    CreateShaderModule(const std::vector<char>& shaderCode) const
    {
        vk::ShaderModuleCreateInfo createInfo{
            .codeSize = shaderCode.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t*>(shaderCode.data())};
        vk::raii::ShaderModule shaderModule(m_Device, createInfo);

        return shaderModule;
    }

    void CreateOpaquePipeline()
    {
        auto vertexBindingDesc = Vertex::GetBindingDescription();
        auto vertexAttributeDesc = Vertex::GetAttributeDescriptions();
        auto instanceBindingDesc = InstanceData::GetBindingDescription();
        auto instanceAttributeDesc = InstanceData::GetAttributeDescriptions();

        std::array<vk::VertexInputBindingDescription, 2> bindingDescs = {vertexBindingDesc,
                                                                         instanceBindingDesc};
        std::array<vk::VertexInputAttributeDescription,
                   Vertex::AttributeCount + InstanceData::AttributeCount>
            attributeDescs;
        std::ranges::copy(vertexAttributeDesc, attributeDescs.begin());
        std::ranges::copy(instanceAttributeDesc, attributeDescs.begin() + Vertex::AttributeCount);

        vk::PipelineColorBlendAttachmentState attachmentState{
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

        std::array setLayouts{*m_GlobalBufferSetLayout,
                              MaterialFactory::Get()->GetDescriptorSetLayout()};

        vk::PushConstantRange pushConstantRange{.stageFlags = vk::ShaderStageFlagBits::eFragment,
                                                .size = sizeof(PBRMaterial::MaterialData)};

        auto [opaqueLayout, opaquePipeline] =
            PipelineBuilder(m_Device)
                .Shaders(m_Paths.Shader("opaque.spv").string())
                .VertexInput(bindingDescs, attributeDescs)
                .Depth(true, true, vk::CompareOp::eLess)
                .ColorAttachments(std::array{Rhi::Vulkan::GetNativeFormat(m_OpaqueImageFormat)},
                                  std::array{attachmentState})
                .DepthAttachment(Rhi::Vulkan::GetNativeFormat(m_DepthFormat))
                .Cull(vk::CullModeFlagBits::eNone, true)
                .Layout(setLayouts, std::array{pushConstantRange})
                .DebugName("Opaque")
                .Cache(*m_PipelineCache)
                .Build();

        m_OpaquePipelineLayout = std::move(opaqueLayout);
        m_OpaquePipeline = std::move(opaquePipeline);
    }

    void CreateTransparentPipeline()
    {
        auto vertexBindingDesc = Vertex::GetBindingDescription();
        auto vertexAttributeDesc = Vertex::GetAttributeDescriptions();

        auto instanceBindingDesc = InstanceData::GetBindingDescription();
        auto instanceAttributeDesc = InstanceData::GetAttributeDescriptions();

        std::array<vk::VertexInputBindingDescription, 2> bindingDescs = {vertexBindingDesc,
                                                                         instanceBindingDesc};
        std::array<vk::VertexInputAttributeDescription,
                   Vertex::AttributeCount + InstanceData::AttributeCount>
            attributeDescs;
        std::ranges::copy(vertexAttributeDesc, attributeDescs.begin());
        std::ranges::copy(instanceAttributeDesc, attributeDescs.begin() + Vertex::AttributeCount);

        std::array<vk::Format, 2> attachmentFormats = {
            Rhi::Vulkan::GetNativeFormat(m_AccumImageFormat),
            Rhi::Vulkan::GetNativeFormat(m_RevealageImageFormat)};

        std::array<vk::PipelineColorBlendAttachmentState, 2> attachmentStates{
            {{.blendEnable = vk::True,
              .srcColorBlendFactor = vk::BlendFactor::eOne,
              .dstColorBlendFactor = vk::BlendFactor::eOne,
              .colorBlendOp = vk::BlendOp::eAdd,
              .srcAlphaBlendFactor = vk::BlendFactor::eOne,
              .dstAlphaBlendFactor = vk::BlendFactor::eOne,
              .alphaBlendOp = vk::BlendOp::eAdd,
              .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA},
             {.blendEnable = vk::True,
              .srcColorBlendFactor = vk::BlendFactor::eZero,
              .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcColor,
              .colorWriteMask = vk::ColorComponentFlagBits::eR}}};

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            m_GlobalBufferSetLayout, MaterialFactory::Get()->GetDescriptorSetLayout()};

        vk::PushConstantRange pushConstantRange{.stageFlags = vk::ShaderStageFlagBits::eFragment,
                                                .size = sizeof(PBRMaterial::MaterialData)};

        auto [transparentLayout, transparentPipeline] =
            PipelineBuilder(m_Device)
                .Shaders(m_Paths.Shader("weightedBlendedOIT.spv").string())
                .VertexInput(bindingDescs, attributeDescs)
                .Depth(true, false, vk::CompareOp::eLess)
                .ColorAttachments(attachmentFormats, attachmentStates)
                .DepthAttachment(Rhi::Vulkan::GetNativeFormat(m_DepthFormat))
                .Cull(vk::CullModeFlagBits::eNone)
                .Layout(setLayouts, std::array{pushConstantRange})
                .DebugName("Transparent")
                .Cache(*m_PipelineCache)
                .Build();

        m_TransparentPipelineLayout = std::move(transparentLayout);
        m_TransparentPipeline = std::move(transparentPipeline);
    }

    void CreateCompositePipeline()
    {
        std::array bindingDescs = {QuadVertex::GetBindingDescription()};
        std::array attributeDescs = {QuadVertex::GetAttributeDescription()};

        vk::PipelineColorBlendAttachmentState attachmentState{
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {m_GlobalBufferSetLayout,
                                                             m_CompositeSetLayout};

        auto [compositeLayout, compositePipeline] =
            PipelineBuilder(m_Device)
                .Shaders(m_Paths.Shader("composite.spv").string())
                .VertexInput(bindingDescs, attributeDescs)
                .Depth(false, false, vk::CompareOp::eLess)
                .ColorAttachments(std::array{SwapchainFormat()}, std::array{attachmentState})
                .DepthAttachment(vk::Format::eUndefined)
                .Cull(vk::CullModeFlagBits::eNone)
                .Layout(setLayouts, {})
                .DebugName("Composite")
                .Cache(*m_PipelineCache)
                .Build();

        m_CompositePipelineLayout = std::move(compositeLayout);
        m_CompositePipeline = std::move(compositePipeline);
    }

    void CreatePipelines()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreatePipelines()");

        CreateOpaquePipeline();
        CreateTransparentPipeline();
        CreateCompositePipeline();
    }

    void CreateCommandPools()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateCommandPools()");

        vk::CommandPoolCreateInfo createInfo{.flags =
                                                 vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                             .queueFamilyIndex = m_QueueIndex};
        m_GenericCommandPool = vk::raii::CommandPool(m_Device, createInfo);
        SetVkDebugName(m_Device, *m_GenericCommandPool, vk::ObjectType::eCommandPool,
                       "Generic Command Pool");

        createInfo = vk::CommandPoolCreateInfo{.queueFamilyIndex = m_QueueIndex};
        for (size_t i = 0u; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            FrameData& frame = m_Frames[i];

            frame.DrawLayoutCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.DrawLayoutCommandPool, vk::ObjectType::eCommandPool,
                           std::format("Draw Layout Command Pool Frame {}", i).c_str());

            frame.OpaqueCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.OpaqueCommandPool, vk::ObjectType::eCommandPool,
                           std::format("Opaque Command Pool Frame {}", i).c_str());

            frame.CloudCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.CloudCommandPool, vk::ObjectType::eCommandPool,
                           std::format("Cloud Command Pool Frame {}", i).c_str());

            frame.TransparentCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.TransparentCommandPool, vk::ObjectType::eCommandPool,
                           std::format("Transparent Command Pool Frame {}", i).c_str());

            frame.CompositeCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.CompositeCommandPool, vk::ObjectType::eCommandPool,
                           std::format("Composite Command Pool Frame {}", i).c_str());

            frame.ImGuiCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.ImGuiCommandPool, vk::ObjectType::eCommandPool,
                           std::format("ImGui Command Pool Frame {}", i).c_str());

            frame.FinalLayoutCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.FinalLayoutCommandPool, vk::ObjectType::eCommandPool,
                           std::format("Final Layout Command Pool Frame {}", i).c_str());
        }
    }

    void CreateCommandBuffers()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateCommandBuffers()");

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            FrameData& frame = m_Frames[i];
            vk::CommandBufferAllocateInfo allocInfo;
            vk::raii::CommandBuffer cmd({});

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.DrawLayoutCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.DrawLayoutCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.DrawLayoutCommandBuffer, vk::ObjectType::eCommandBuffer,
                           std::format("Draw Layout Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.OpaqueCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.OpaqueCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.OpaqueCommandBuffer, vk::ObjectType::eCommandBuffer,
                           std::format("Opaque Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.CloudCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.CloudCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.CloudCommandBuffer, vk::ObjectType::eCommandBuffer,
                           std::format("Cloud Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.TransparentCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.TransparentCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.TransparentCommandBuffer,
                           vk::ObjectType::eCommandBuffer,
                           std::format("Transparent Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.CompositeCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.CompositeCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.CompositeCommandBuffer, vk::ObjectType::eCommandBuffer,
                           std::format("Composite Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.ImGuiCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.ImGuiCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.ImGuiCommandBuffer, vk::ObjectType::eCommandBuffer,
                           std::format("ImGui Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.FinalLayoutCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.FinalLayoutCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.FinalLayoutCommandBuffer,
                           vk::ObjectType::eCommandBuffer,
                           std::format("Final Layout Command Buffer Frame {}", i).c_str());
        }
    }

    /**
     * The VkImageView a handle names.
     *
     * Dynamic rendering attachments and descriptor writes both take raw Vulkan
     * objects, and both still happen here — attachments until Stage 8's frame
     * graph, descriptor writes until bindless. This is the one place that
     * resolve is spelled out, so the call sites read as they did.
     */
    vk::ImageView NativeView(Rhi::TextureViewHandle handle)
    {
        return Rhi::Vulkan::GetImageView(*m_RhiDevice, handle);
    }

    /**
     * What the frame's three recording threads produced between them. Summed on
     * demand rather than accumulated into one member, because two of the three
     * are written from job threads.
     */
    Rhi::BarrierCounts FrameBarrierCounts() const
    {
        Rhi::BarrierCounts total = m_OpaqueBarrierCounts;
        total += m_TransparentBarrierCounts;
        total += m_MainThreadBarrierCounts;
        return total;
    }

    /**
     * Reports the frame's barrier counts the first time they are seen and
     * whenever they change afterwards. Logging them every frame would drown the
     * log, and never logging them would make a change in the barriers — the
     * easiest thing to get wrong here and the hardest to see — invisible
     * between runs of the report.
     */
    void LogBarrierCounts()
    {
        const Rhi::BarrierCounts counts = FrameBarrierCounts();
        if (counts == m_LoggedBarrierCounts)
            return;

        LogMsg(LogSeverity::Info, LogRenderer, "Barriers recorded this frame: {} over {} calls",
               counts.Barriers, counts.Calls);
        m_LoggedBarrierCounts = counts;
    }

    void RecordOpaqueCommandBuffer()
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.OpaqueCommandPool.reset();
        vk::raii::CommandBuffer& cmd = frame.OpaqueCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();

        const std::array openingBarriers{
            Rhi::BarrierPresets::UndefinedToDepthStencilWrite().On(frame.DepthTexture.GetHandle()),
            Rhi::BarrierPresets::UndefinedToRenderTarget().On(frame.OpaqueTexture.GetHandle())};
        m_OpaqueBarrierCounts = list->Barrier(openingBarriers);

        vk::ClearValue clearColor = vk::ClearColorValue(SKY_COLOR.r, SKY_COLOR.g, SKY_COLOR.b, 1.f);
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.f, 0);
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = NativeView(frame.OpaqueTexture.GetView()),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor};
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = NativeView(frame.DepthTexture.GetView()),
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearDepth};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = SwapchainExtent()},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo};

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_OpaquePipeline);

        cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<float>(SwapchainExtent().width),
                                        static_cast<float>(SwapchainExtent().height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), SwapchainExtent()));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_OpaquePipelineLayout, 0,
                               *frame.GlobalBufferDescriptorSet, nullptr);

        cmd.bindVertexBuffers(1, Rhi::Vulkan::GetBuffer(*m_RhiDevice, frame.InstanceBuffer.Get()),
                              {0});

        vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;

        // per mesh batch
        const std::vector<MeshBatch>& batches = ModelManager::Get()->GetOpaqueBatches();
        uint32_t instanceCount = 0;
        for (const MeshBatch& batch : batches)
        {
            vk::CullModeFlags requiredCullMode = batch.pMaterial->IsTwoSided()
                                                     ? vk::CullModeFlagBits::eNone
                                                     : vk::CullModeFlagBits::eBack;

            if (requiredCullMode != cullMode)
            {
                cmd.setCullMode(requiredCullMode);
                cullMode = requiredCullMode;
            }

            cmd.bindVertexBuffers(0, Rhi::Vulkan::GetBuffer(*m_RhiDevice, batch.VertexBuffer), {0});
            cmd.bindIndexBuffer(Rhi::Vulkan::GetBuffer(*m_RhiDevice, batch.IndexBuffer), 0,
                                vk::IndexType::eUint32);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_OpaquePipelineLayout, 1,
                                   batch.pMaterial->GetDescriptorSet(), nullptr);
            cmd.pushConstants<PBRMaterial::MaterialData>(
                m_OpaquePipelineLayout, vk::ShaderStageFlagBits::eFragment, 0u,
                *static_cast<PBRMaterial::MaterialData*>(batch.pMaterial->GetPushConstantData()));
            cmd.drawIndexed(batch.IndexCount, batch.InstanceCount, batch.FirstIndex, 0,
                            batch.FirstInstance);
            instanceCount += batch.InstanceCount;
        }
        m_OpaqueDrawCallCount = static_cast<uint32_t>(batches.size());
        m_OpaqueBatchCount = static_cast<uint32_t>(batches.size());
        m_OpaqueInstanceCount = instanceCount;

        cmd.endRendering();

        list->End();
    }

    void RecordCloudsCommandBuffer()
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.CloudCommandPool.reset();

        m_MainThreadBarrierCounts += m_CloudSystem->RecordDispatch(
            frame.CloudCommandBuffer, m_FrameIndex, frame.GlobalBufferDescriptorSet,
            frame.DepthBufferDescriptorSet);
    }

    void RecordTransparentCommandBuffer()
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.TransparentCommandPool.reset();
        vk::raii::CommandBuffer& cmd = frame.TransparentCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();

        const std::array openingBarriers{
            Rhi::BarrierPresets::UndefinedToRenderTarget().On(frame.AccumTexture.GetHandle()),
            Rhi::BarrierPresets::UndefinedToRenderTarget().On(frame.RevealageTexture.GetHandle()),
            Rhi::BarrierPresets::DepthStencilWriteToShaderResource().On(
                frame.DepthTexture.GetHandle())};
        m_TransparentBarrierCounts = list->Barrier(openingBarriers);

        vk::ClearValue accumClearColor = vk::ClearColorValue(0.f, 0.f, 0.f, 0.f);
        vk::ClearValue revealageClearColor = vk::ClearColorValue(1.f, 0.f, 0.f, 0.f);
        std::array<vk::RenderingAttachmentInfo, 2> colorAttachmentInfos = {
            {{.imageView = NativeView(frame.AccumTexture.GetView()),
              .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
              .loadOp = vk::AttachmentLoadOp::eClear,
              .storeOp = vk::AttachmentStoreOp::eStore,
              .clearValue = accumClearColor},
             {.imageView = NativeView(frame.RevealageTexture.GetView()),
              .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
              .loadOp = vk::AttachmentLoadOp::eClear,
              .storeOp = vk::AttachmentStoreOp::eStore,
              .clearValue = revealageClearColor}}};
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = NativeView(frame.DepthTexture.GetView()),
            .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eNone};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = SwapchainExtent()},
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfos.size()),
            .pColorAttachments = colorAttachmentInfos.data(),
            .pDepthAttachment = &depthAttachmentInfo};

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_TransparentPipeline);

        cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<float>(SwapchainExtent().width),
                                        static_cast<float>(SwapchainExtent().height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), SwapchainExtent()));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_TransparentPipelineLayout, 0,
                               *frame.GlobalBufferDescriptorSet, nullptr);

        cmd.bindVertexBuffers(1, Rhi::Vulkan::GetBuffer(*m_RhiDevice, frame.InstanceBuffer.Get()),
                              {0});

        // per mesh batch
        const std::vector<MeshBatch>& batches = ModelManager::Get()->GetTransparentBatches();
        uint32_t instanceCount = 0;
        for (const MeshBatch& batch : batches)
        {
            cmd.bindVertexBuffers(0, Rhi::Vulkan::GetBuffer(*m_RhiDevice, batch.VertexBuffer), {0});
            cmd.bindIndexBuffer(Rhi::Vulkan::GetBuffer(*m_RhiDevice, batch.IndexBuffer), 0,
                                vk::IndexType::eUint32);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_TransparentPipelineLayout, 1,
                                   batch.pMaterial->GetDescriptorSet(), nullptr);
            cmd.pushConstants<PBRMaterial::MaterialData>(
                m_TransparentPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0u,
                *static_cast<PBRMaterial::MaterialData*>(batch.pMaterial->GetPushConstantData()));
            cmd.drawIndexed(batch.IndexCount, batch.InstanceCount, batch.FirstIndex, 0,
                            batch.FirstInstance);
            instanceCount += batch.InstanceCount;
        }
        m_TransparentDrawCallCount = static_cast<uint32_t>(batches.size());
        m_TransparentBatchCount = static_cast<uint32_t>(batches.size());
        m_TransparentInstanceCount = instanceCount;

        cmd.endRendering();

        list->End();
    }

    void RecordCompositeCommandBuffer(const Rhi::AcquiredImage& image)
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.CompositeCommandPool.reset();
        vk::raii::CommandBuffer& cmd = frame.CompositeCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();

        const std::array openingBarriers{
            Rhi::BarrierPresets::RenderTargetToShaderResource().On(frame.OpaqueTexture.GetHandle()),
            Rhi::BarrierPresets::RenderTargetToShaderResource().On(frame.AccumTexture.GetHandle()),
            Rhi::BarrierPresets::RenderTargetToShaderResource().On(
                frame.RevealageTexture.GetHandle())};
        m_MainThreadBarrierCounts += list->Barrier(openingBarriers);

        vk::ClearValue clearColor = vk::ClearColorValue(0.f, 0.f, 0.f, 1.f);
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = NativeView(image.View),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = SwapchainExtent()},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo};

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_CompositePipeline);

        cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<float>(SwapchainExtent().width),
                                        static_cast<float>(SwapchainExtent().height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), SwapchainExtent()));

        std::array descriptorSets = {*frame.GlobalBufferDescriptorSet,
                                     *frame.CompositeDescriptorSet};
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_CompositePipelineLayout, 0u,
                               descriptorSets, nullptr);

        cmd.bindVertexBuffers(0u, Rhi::Vulkan::GetBuffer(*m_RhiDevice, m_QuadVertexBuffer.Get()),
                              {0});
        cmd.bindIndexBuffer(Rhi::Vulkan::GetBuffer(*m_RhiDevice, m_QuadIndexBuffer.Get()), 0u,
                            vk::IndexType::eUint32);

        constexpr uint32_t QUAD_INDEX_COUNT = 6u;
        cmd.drawIndexed(QUAD_INDEX_COUNT, 1u, 0u, 0, 0u);

        cmd.endRendering();

        list->End();
    }

    void RecordImGui(const Rhi::AcquiredImage& image)
    {
        m_Frames[m_FrameIndex].ImGuiCommandPool.reset();
        vk::raii::CommandBuffer& cmd = m_Frames[m_FrameIndex].ImGuiCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();

        // ImGui draws over the composited frame with loadOp eLoad, so the
        // composite pass's writes have to be visible to this pass's load.
        m_MainThreadBarrierCounts +=
            list->Barrier(Rhi::BarrierPresets::PreserveRenderTarget().On(image.Texture));

        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = NativeView(image.View),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = SwapchainExtent()},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo};

        cmd.beginRendering(renderingInfo);

        // The pass itself still records: its barrier and its render pass are what
        // a frame costs whether or not the panel is drawn, so suppressing the
        // panel alone leaves every counter in the run report untouched.
        if (m_bCursorVisible && !m_Options.bNoUi)
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *cmd);

        cmd.endRendering();
        list->End();
    }

    void RecordSwapImageToDrawLayout(const Rhi::AcquiredImage& image)
    {
        m_Frames[m_FrameIndex].DrawLayoutCommandPool.reset();
        vk::raii::CommandBuffer& cmd = m_Frames[m_FrameIndex].DrawLayoutCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();
        m_MainThreadBarrierCounts +=
            list->Barrier(Rhi::BarrierPresets::AcquiredImageToRenderTarget().On(image.Texture));
        list->End();
    }

    void RecordSwapImageToFinalLayout(const Rhi::AcquiredImage& image, bool captureScreenshot)
    {
        m_Frames[m_FrameIndex].FinalLayoutCommandPool.reset();
        vk::raii::CommandBuffer& cmd = m_Frames[m_FrameIndex].FinalLayoutCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();

        const Rhi::TextureHandle swapTexture = image.Texture;

        // Undefined means the target requires no particular layout, and then the
        // frame ends with no closing barrier at all. Only a target with a
        // presentation engine answers otherwise; the command buffer still goes
        // out, empty, because the submit takes a fixed array of seven and the
        // ImGui one is already empty whenever the panel is hidden.
        const Rhi::TextureLayout finalLayout = m_PresentTarget->GetRequiredFinalLayout();
        const bool bNeedsFinalTransition = finalLayout != Rhi::TextureLayout::Undefined;

        if (captureScreenshot)
        {
            // Copy out the composited frame while it is still safely between
            // acquire and present (see RenderTargetToCopySrc()).
            m_MainThreadBarrierCounts +=
                list->Barrier(Rhi::BarrierPresets::RenderTargetToCopySrc().On(swapTexture));

            list->CopyTextureToBuffer(
                swapTexture, m_ScreenshotStagingBuffer.Get(),
                Rhi::BufferTextureCopyRegion{
                    .Extent = {SwapchainExtent().width, SwapchainExtent().height, 1u}});

            if (bNeedsFinalTransition)
            {
                m_MainThreadBarrierCounts +=
                    list->Barrier(Rhi::BarrierPresets::CopySrcToFinal(finalLayout).On(swapTexture));
            }
        }
        else if (bNeedsFinalTransition)
        {
            m_MainThreadBarrierCounts += list->Barrier(
                Rhi::BarrierPresets::RenderTargetToFinal(finalLayout).On(swapTexture));
        }

        list->End();
    }

    void CreateSyncObjects()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateSyncObjects()");

        // The semaphores ordering acquire and present belong to the present
        // target: they have to be rebuilt in lockstep with the images they order
        // access to, and only the object owning the images knows when that is.
        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].DrawFence =
                vk::raii::Fence(m_Device, {.flags = vk::FenceCreateFlagBits::eSignaled});
            SetVkDebugName(m_Device, *m_Frames[i].DrawFence, vk::ObjectType::eFence,
                           std::format("Draw Fence_{}", i).c_str());
        }
    }

    void RecreateSwapchainAndRenderImages()
    {
        // Read here rather than beside its use at the end of the function: the
        // rebuild below is what changes it.
        const uint32_t previousImageCount = m_PresentTarget->GetImageCount();

        // A target that refuses to rebuild is the state a minimised window
        // leaves the surface in. Alt-tabbing out of exclusive fullscreen is the
        // common way to reach it: SDL minimises the window itself on focus loss
        // there, so that the desktop video mode comes back.
        //
        // Deferring rather than blocking keeps the event loop running, so the
        // window can be restored — or the application closed — while it lasts.
        // The old target is left intact, and DrawFrame skips frames until this
        // succeeds.
        const Extent2D framebufferExtent = m_Platform.GetFramebufferExtent();
        if (!m_PresentTarget->Recreate({framebufferExtent.Width, framebufferExtent.Height}))
        {
            if (!m_bSwapchainOutOfDate)
                LogMsg(LogSeverity::Info, LogRenderer,
                       "Present target cannot be rebuilt yet; deferring recreation.");

            m_bSwapchainOutOfDate = true;
            return;
        }

        // Logged after the fact because the attempt above is what can fail, and
        // a minimised window would otherwise announce a recreation every frame.
        LogMsg(LogSeverity::Info, LogRenderer, "Recreating swapchain and render images...");

        m_bSwapchainOutOfDate = false;

        // Recreate() waited for work in flight before invalidating the images,
        // so the resources rebuilt below are no longer in use either.
        m_DepthFormat = Rhi::Format::Undefined;

        CreateDepthResources();
        CreateRenderTargets();

        m_CloudSystem->Resize(SwapchainExtent().width, SwapchainExtent().height);
        UpdateDepthDescriptorSets();
        UpdateCompositeDescriptorSet();

        m_Camera->SetProjection(m_Camera->GetFOV(),
                                static_cast<float>(SwapchainExtent().width) /
                                    static_cast<float>(SwapchainExtent().height),
                                m_Camera->GetNearPlane(), m_Camera->GetFarPlane());

        const vk::Format swapchainFormat = SwapchainFormat();
        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{
            .colorAttachmentCount = 1u, .pColorAttachmentFormats = &swapchainFormat};
        ImGui_ImplVulkan_PipelineInfo pipelineInfo{};
        pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        pipelineInfo.PipelineRenderingCreateInfo = pipelineRenderingInfo;
        ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);

        // A recreate may hand back a different number of images than the last
        // one — entering or leaving fullscreen is the usual cause, because the
        // driver switches between composited and direct presentation. Nothing
        // this class owns is sized to that count (every per-frame array is
        // NUM_FRAMES_IN_FLIGHT long, and the per-image semaphores belong to the
        // target), but ImGui's Vulkan backend cached it at init, so it is the
        // one thing that has to be told.
        const uint32_t imageCount = m_PresentTarget->GetImageCount();
        if (imageCount != previousImageCount)
        {
            LogMsg(LogSeverity::Info, LogRenderer, "Swapchain image count changed: {} -> {}",
                   previousImageCount, imageCount);

            // Discards the vertex/index buffer ring the backend built for the
            // old count. The ring is rebuilt from InitInfo::ImageCount, which
            // stays at the value Init() was given — ImGui exposes no setter for
            // it — so it does not track this. That is safe rather than merely
            // tolerable: the ring is only reused once every Count frames, and
            // Count is at least 2, which is NUM_FRAMES_IN_FLIGHT. The draw
            // fence waited on at the top of a frame therefore always covers the
            // frame whose slot is about to be overwritten.
            ImGui_ImplVulkan_SetMinImageCount(imageCount);
        }
    }

    void CreateDescriptorSetLayouts()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateDescriptorSetLayouts()");

        std::array frameBindings = {vk::DescriptorSetLayoutBinding(
            0u, vk::DescriptorType::eUniformBuffer, 1u,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
                vk::ShaderStageFlagBits::eCompute,
            nullptr)};
        vk::DescriptorSetLayoutCreateInfo frameCreateInfo{
            .bindingCount = static_cast<uint32_t>(frameBindings.size()),
            .pBindings = frameBindings.data()};
        m_GlobalBufferSetLayout = vk::raii::DescriptorSetLayout(m_Device, frameCreateInfo);
        SetVkDebugName(m_Device, *m_GlobalBufferSetLayout, vk::ObjectType::eDescriptorSetLayout,
                       "Frame Uniform Buffer Descriptor Set Layout");

        std::array compositeBindings = {
            vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eSampledImage, 1u,
                                           vk::ShaderStageFlagBits::eFragment, nullptr),
            vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eSampledImage, 1u,
                                           vk::ShaderStageFlagBits::eFragment, nullptr),
            vk::DescriptorSetLayoutBinding(2u, vk::DescriptorType::eSampledImage, 1u,
                                           vk::ShaderStageFlagBits::eFragment, nullptr),
            vk::DescriptorSetLayoutBinding(3u, vk::DescriptorType::eCombinedImageSampler, 1u,
                                           vk::ShaderStageFlagBits::eFragment, nullptr)};
        vk::DescriptorSetLayoutCreateInfo compositeCreateInfo{
            .bindingCount = static_cast<uint32_t>(compositeBindings.size()),
            .pBindings = compositeBindings.data()};
        m_CompositeSetLayout = vk::raii::DescriptorSetLayout(m_Device, compositeCreateInfo);
        SetVkDebugName(m_Device, *m_CompositeSetLayout, vk::ObjectType::eDescriptorSetLayout,
                       "Composite Descriptor Set Layout");

        std::array depthBindings = {vk::DescriptorSetLayoutBinding(
            0u, vk::DescriptorType::eSampledImage, 1u,
            vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute, nullptr)};
        vk::DescriptorSetLayoutCreateInfo depthCreateInfo{
            .bindingCount = static_cast<uint32_t>(depthBindings.size()),
            .pBindings = depthBindings.data()};
        m_DepthSetLayout = vk::raii::DescriptorSetLayout(m_Device, depthCreateInfo);
        SetVkDebugName(m_Device, *m_DepthSetLayout, vk::ObjectType::eDescriptorSetLayout,
                       "Depth Descriptor Set Layout");
    }

    void CreateGlobalBuffers()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateGlobalBuffers()");

        vk::DeviceSize size = sizeof(GlobalBuffer);
        if (size % 16 != 0)
            throw std::runtime_error(
                std::format("Buffer must be 16 byte aligned! Size is {}", size));

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].GlobalBuffer = Rhi::UniqueHandle<Rhi::BufferHandle>(
                *m_RhiDevice, m_RhiDevice->CreateBuffer(Rhi::BufferDesc{
                                  .Size = size,
                                  .Usage = Rhi::BufferUsage::Uniform,
                                  .Access = Rhi::MemoryAccess::CpuToGpu,
                                  .DebugName = std::format("Global Buffer Frame {}", i)}));
        }

        m_GlobalBuffer.SkyColor = SKY_COLOR;
    }

    void UpdateGlobalBuffer(uint32_t frameIndex)
    {
        m_GlobalBuffer.Time = m_RunTime;
        m_GlobalBuffer.CamData.Pos = m_Camera->GetPosition();
        glm::mat4 view = m_Camera->GetViewMatrix();
        m_GlobalBuffer.CamData.View = glm::transpose(view);
        glm::mat4 proj = m_Camera->GetProjMatrix();
        // GLM was designed for OpenGL, which has its Y coordinate in clip
        // space inverted. Compensate for this by scaling here.
        //
        // Driven by the device capability rather than by the build, because
        // whether this is needed is a property of the graphics API: Vulkan wants
        // it, D3D12 does not. This is the only site permitted to apply it.
        if (m_RhiDevice->GetCaps().bFlipClipSpaceY)
            proj[1][1] *= -1.f;
        m_GlobalBuffer.CamData.Proj = glm::transpose(proj);
        m_GlobalBuffer.CamData.NearPlane = m_Camera->GetNearPlane();
        m_GlobalBuffer.CamData.FarPlane = m_Camera->GetFarPlane();

        m_GlobalBuffer.CamData.InvViewProj =
            glm::inverse(glm::transpose(m_GlobalBuffer.CamData.Proj) * view);

        uint32_t& pointLightCount = m_GlobalBuffer.Lights.PointLightCount;
        for (pointLightCount = 0u;
             pointLightCount < std::min(static_cast<uint32_t>(m_SceneGraph->PointLights.size()),
                                        static_cast<uint32_t>(MAX_POINT_LIGHTS));
             pointLightCount++)
        {
            m_GlobalBuffer.Lights.PointLights[pointLightCount] =
                m_SceneGraph->PointLights[pointLightCount]->GetData();
        }

        uint32_t& dirLightCount = m_GlobalBuffer.Lights.DirLightCount;
        for (dirLightCount = 0u;
             dirLightCount < std::min(static_cast<uint32_t>(m_SceneGraph->DirLights.size()),
                                      static_cast<uint32_t>(MAX_DIR_LIGHTS));
             dirLightCount++)
        {
            m_GlobalBuffer.Lights.DirLights[dirLightCount] =
                m_SceneGraph->DirLights[dirLightCount]->GetData();
        }

        if (m_SceneGraph->PointLights.size() > MAX_POINT_LIGHTS ||
            m_SceneGraph->DirLights.size() > MAX_DIR_LIGHTS)
        {
            const uint32_t pointLightTotal =
                static_cast<uint32_t>(m_SceneGraph->PointLights.size());
            const uint32_t dirLightTotal = static_cast<uint32_t>(m_SceneGraph->DirLights.size());
            LogMsg(LogSeverity::Warning, LogRenderer,
                   "Scene defines more lights than the shader supports; excess lights are "
                   "dropped. PointLights: {} (max {}), DirLights: {} (max {})",
                   pointLightTotal, MAX_POINT_LIGHTS, dirLightTotal, MAX_DIR_LIGHTS);
        }

        memcpy(m_RhiDevice->GetMappedData(m_Frames[frameIndex].GlobalBuffer.Get()), &m_GlobalBuffer,
               sizeof(m_GlobalBuffer));
    }

    void CreateDescriptorPool()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateDescriptorPool()");

        std::array framePoolSize = {vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = NUM_FRAMES_IN_FLIGHT}};
        vk::DescriptorPoolCreateInfo frameCreateInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = NUM_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(framePoolSize.size()),
            .pPoolSizes = framePoolSize.data()};

        m_FrameDescriptorPool = vk::raii::DescriptorPool(m_Device, frameCreateInfo);
        SetVkDebugName(m_Device, *m_FrameDescriptorPool, vk::ObjectType::eDescriptorPool,
                       "Frame Descriptor Pool");

        std::array compositePoolSize = {
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage,
                                   .descriptorCount = NUM_FRAMES_IN_FLIGHT * 3},
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                                   .descriptorCount = NUM_FRAMES_IN_FLIGHT * 1}};
        vk::DescriptorPoolCreateInfo compCreateInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = NUM_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(compositePoolSize.size()),
            .pPoolSizes = compositePoolSize.data()};

        m_CompositeDescriptorPool = vk::raii::DescriptorPool(m_Device, compCreateInfo);
        SetVkDebugName(m_Device, *m_CompositeDescriptorPool, vk::ObjectType::eDescriptorPool,
                       "Composite Descriptor Pool");

        std::array genericPoolSize = {vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eSampledImage, .descriptorCount = NUM_FRAMES_IN_FLIGHT}};
        vk::DescriptorPoolCreateInfo genericCreateInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = NUM_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(genericPoolSize.size()),
            .pPoolSizes = genericPoolSize.data()};

        m_GenericDescriptorPool = vk::raii::DescriptorPool(m_Device, genericCreateInfo);
        SetVkDebugName(m_Device, *m_GenericDescriptorPool, vk::ObjectType::eDescriptorPool,
                       "Generic Descriptor Pool");
    }

    void CreateDescriptorSets()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateDescriptorSets()");

        std::vector<vk::DescriptorSetLayout> globalBufferLayouts(NUM_FRAMES_IN_FLIGHT,
                                                                 *m_GlobalBufferSetLayout);
        vk::DescriptorSetAllocateInfo globalBufferAllocInfo{
            .descriptorPool = *m_FrameDescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(globalBufferLayouts.size()),
            .pSetLayouts = globalBufferLayouts.data()};
        std::vector<vk::raii::DescriptorSet> uniformDescriptorSets =
            m_Device.allocateDescriptorSets(globalBufferAllocInfo);

        std::vector<vk::DescriptorSetLayout> compSetLayouts(NUM_FRAMES_IN_FLIGHT,
                                                            *m_CompositeSetLayout);
        vk::DescriptorSetAllocateInfo compAllocInfo{
            .descriptorPool = m_CompositeDescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(compSetLayouts.size()),
            .pSetLayouts = compSetLayouts.data()};
        std::vector<vk::raii::DescriptorSet> compositeDescriptorSets =
            m_Device.allocateDescriptorSets(compAllocInfo);

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            FrameData& frame = m_Frames[i];

            frame.GlobalBufferDescriptorSet = std::move(uniformDescriptorSets[i]);
            SetVkDebugName(m_Device, *frame.GlobalBufferDescriptorSet,
                           vk::ObjectType::eDescriptorSet,
                           std::format("Main Descriptor Set Frame {}", i).c_str());

            vk::DescriptorBufferInfo bufferInfo{
                .buffer = Rhi::Vulkan::GetBuffer(*m_RhiDevice, frame.GlobalBuffer.Get()),
                .offset = 0,
                .range = sizeof(GlobalBuffer)};

            std::array globalDescriptorWrites = {
                vk::WriteDescriptorSet{.dstSet = frame.GlobalBufferDescriptorSet,
                                       .dstBinding = 0,
                                       .dstArrayElement = 0,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eUniformBuffer,
                                       .pBufferInfo = &bufferInfo}};

            m_Device.updateDescriptorSets(globalDescriptorWrites, {});

            frame.CompositeDescriptorSet = std::move(compositeDescriptorSets[i]);
            SetVkDebugName(m_Device, *frame.CompositeDescriptorSet, vk::ObjectType::eDescriptorSet,
                           std::format("Composite Descriptor Set Frame {}", i).c_str());
        }

        UpdateCompositeDescriptorSet();

        std::vector<vk::DescriptorSetLayout> depthBufferSetLayouts(1, *m_DepthSetLayout);
        vk::DescriptorSetAllocateInfo depthAllocInfo{
            .descriptorPool = m_GenericDescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(depthBufferSetLayouts.size()),
            .pSetLayouts = depthBufferSetLayouts.data()};

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].DepthBufferDescriptorSet =
                std::move(m_Device.allocateDescriptorSets(depthAllocInfo).front());
            SetVkDebugName(m_Device, *m_Frames[i].DepthBufferDescriptorSet,
                           vk::ObjectType::eDescriptorSet, "Depth Buffer Descriptor Set");
        }

        UpdateDepthDescriptorSets();
    }

    void CreateTextureSampler()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateTextureSampler()");

        // MaxAnisotropy left at 0 asks for the device maximum, which is what this
        // used to read off the physical device's limits itself.
        m_TextureSampler = Rhi::UniqueHandle<Rhi::SamplerHandle>(
            *m_RhiDevice, m_RhiDevice->CreateSampler(Rhi::SamplerDesc{
                              .bAnisotropyEnable = true, .DebugName = "Texture Sampler"}));
    }

    void CreateDepthResources()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateDepthResources()");

        m_DepthFormat = FindDepthFormat();
        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].DepthTexture = Texture(
                *m_RhiDevice,
                Rhi::TextureDesc{.Format = m_DepthFormat,
                                 .Extent = {SwapchainExtent().width, SwapchainExtent().height, 1u},
                                 .Usage = Rhi::TextureUsage::DepthStencilAttachment |
                                          Rhi::TextureUsage::Sampled,
                                 .DebugName = std::format("Frame_{} Depth Image", i)},
                Rhi::TextureViewDimension::Texture2D);
        }
    }

    Rhi::Format FindSupportedFormat(std::span<const Rhi::Format> candidates, vk::ImageTiling tiling,
                                    vk::FormatFeatureFlags features)
    {
        for (const Rhi::Format format : candidates)
        {
            const vk::FormatProperties properties =
                m_PhysicalDevice.getFormatProperties(Rhi::Vulkan::GetNativeFormat(format));

            if (tiling == vk::ImageTiling::eLinear &&
                (properties.linearTilingFeatures & features) == features)
                return format;
            if (tiling == vk::ImageTiling::eOptimal &&
                (properties.optimalTilingFeatures & features) == features)
                return format;
        }
        throw std::runtime_error("Failed to find a supported format!");
    }

    Rhi::Format FindDepthFormat()
    {
        // D16UnormS8Uint used to be a fourth candidate here and is deliberately
        // gone: it has no DXGI equivalent, so Rhi::Format cannot carry it. That
        // costs nothing, because it was unreachable. The specification's
        // mandatory format table requires VK_FORMAT_FEATURE_DEPTH_STENCIL_
        // ATTACHMENT_BIT to be "supported for at least one of
        // VK_FORMAT_D24_UNORM_S8_UINT and VK_FORMAT_D32_SFLOAT_S8_UINT"
        // (Vulkan 1.4, "Mandatory Format Support: Depth/Stencil"), and both are
        // above it in this list — so no conformant device could ever fall
        // through to a fourth candidate.
        static constexpr std::array candidates{Rhi::Format::D32Float, Rhi::Format::D32FloatS8Uint,
                                               Rhi::Format::D24UnormS8Uint};
        return FindSupportedFormat(candidates, vk::ImageTiling::eOptimal,
                                   vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    }

    void CreateInstanceBuffers(uint32_t instanceCapacity)
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateInstanceBuffers()");

        // TODO: allocating memory 3 times, can probably allocate once and
        // store offsets Can do the same with uniform buffer.
        vk::DeviceSize size = sizeof(InstanceData) * instanceCapacity;
        for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].InstanceBuffer = Rhi::UniqueHandle<Rhi::BufferHandle>(
                *m_RhiDevice, m_RhiDevice->CreateBuffer(Rhi::BufferDesc{
                                  .Size = size,
                                  .Usage = Rhi::BufferUsage::Vertex,
                                  .Access = Rhi::MemoryAccess::CpuToGpu,
                                  .DebugName = std::format("Instance Buffer Frame {}", i)}));
        }

        m_InstanceCapacity = instanceCapacity;
    }

    /**
     * Every frame's buffer is replaced at once, not just the one being filled:
     * growing them one at a time would leave the other frame short by exactly
     * the same amount and grow again on the very next frame, for two device
     * waits instead of one.
     *
     * The wait is what makes the replacement legal. These are vertex buffers
     * that frames still in flight have bound, and destroying one while a
     * submitted command buffer still refers to it is invalid
     * (VUID-vkDestroyBuffer-buffer-00922); the current frame's fence has been
     * waited on by this point, but nothing covers the others. Growth is rare
     * enough that a device-wide wait beats tracking per-buffer lifetimes.
     */
    void GrowInstanceBuffers(uint32_t neededInstances)
    {
        const uint32_t newCapacity = std::max(neededInstances, m_InstanceCapacity * 2u);

        m_RhiDevice->WaitIdle();
        CreateInstanceBuffers(newCapacity);

        LogMsg(LogSeverity::Info, LogRenderer, "Instance buffer grown to {} instances.",
               newCapacity);
    }

    void UpdateInstanceBuffer(uint32_t frameIndex)
    {
        const std::vector<InstanceData>& instanceDatas = ModelManager::Get()->GetInstanceDatas();
        if (instanceDatas.empty())
            return;

        if (instanceDatas.size() > m_InstanceCapacity)
            GrowInstanceBuffers(static_cast<uint32_t>(instanceDatas.size()));

        memcpy(m_RhiDevice->GetMappedData(m_Frames[frameIndex].InstanceBuffer.Get()),
               instanceDatas.data(), sizeof(InstanceData) * instanceDatas.size());
    }

    void CreateRenderTargets()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateRenderTargets()");

        const auto makeTarget = [this](Rhi::Format format, const std::string& name)
        {
            return Texture(
                *m_RhiDevice,
                Rhi::TextureDesc{.Format = format,
                                 .Extent = {SwapchainExtent().width, SwapchainExtent().height, 1u},
                                 .Usage = Rhi::TextureUsage::ColorAttachment |
                                          Rhi::TextureUsage::Sampled,
                                 .DebugName = name},
                Rhi::TextureViewDimension::Texture2D);
        };

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].OpaqueTexture =
                makeTarget(m_OpaqueImageFormat, std::format("Frame_{} Opaque Image", i));
            m_Frames[i].AccumTexture =
                makeTarget(m_AccumImageFormat, std::format("Frame_{} Accum Image", i));
            m_Frames[i].RevealageTexture =
                makeTarget(m_RevealageImageFormat, std::format("Frame_{} Revealage Image", i));
        }
    }

    void CreateQuadBuffers()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateQuadBuffers()");

        std::array<QuadVertex, 4> vertices = {{{.Pos = {-1.f, -1.f}, .TexCoord{0.f, 0.f}},
                                               {.Pos = {-1.f, 1.f}, .TexCoord{0.f, 1.f}},
                                               {.Pos = {1.f, 1.f}, .TexCoord{1.f, 1.f}},
                                               {.Pos = {1.f, -1.f}, .TexCoord{1.f, 0.f}}}};

        assert(vertices.size() == 4);

        std::array<uint32_t, 6> indices = {0, 1, 2, 0, 2, 3};

        const auto createUploaded =
            [this](Rhi::BufferUsage usage, auto& contents, const char* debugName)
        {
            Rhi::UniqueHandle<Rhi::BufferHandle> buffer(
                *m_RhiDevice, m_RhiDevice->CreateBuffer(
                                  Rhi::BufferDesc{.Size = std::span(contents).size_bytes(),
                                                  .Usage = usage | Rhi::BufferUsage::CopyDst,
                                                  .Access = Rhi::MemoryAccess::GpuOnly,
                                                  .DebugName = debugName}));

            m_UploadContext->UploadBuffer(buffer.Get(), 0u, std::as_bytes(std::span(contents)));
            return buffer;
        };

        m_QuadVertexBuffer =
            createUploaded(Rhi::BufferUsage::Vertex, vertices, "Quad Vertex Buffer");
        m_QuadIndexBuffer = createUploaded(Rhi::BufferUsage::Index, indices, "Quad Index Buffer");

        // Not routed through ResourceManager, so nothing else is going to flush
        // these.
        m_UploadContext->Flush();
    }

    void UpdateCompositeDescriptorSet()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "UpdateCompositeDescriptorSet()");

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            FrameData& frame = m_Frames[i];
            vk::DescriptorImageInfo opaqueImageInfo{
                .imageView = NativeView(frame.OpaqueTexture.GetView()),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo accumImageInfo{
                .imageView = NativeView(frame.AccumTexture.GetView()),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo revealageImageInfo{
                .imageView = NativeView(frame.RevealageTexture.GetView()),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo cloudsImageInfo{
                .sampler = Rhi::Vulkan::GetSampler(*m_RhiDevice, m_TextureSampler.Get()),
                .imageView = NativeView(m_CloudSystem->GetOutputView(static_cast<uint8_t>(i))),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

            std::array compDescriptorWrites = {
                vk::WriteDescriptorSet{.dstSet = frame.CompositeDescriptorSet,
                                       .dstBinding = 0u,
                                       .dstArrayElement = 0u,
                                       .descriptorCount = 1u,
                                       .descriptorType = vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &opaqueImageInfo},
                vk::WriteDescriptorSet{.dstSet = frame.CompositeDescriptorSet,
                                       .dstBinding = 1u,
                                       .dstArrayElement = 0u,
                                       .descriptorCount = 1u,
                                       .descriptorType = vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &accumImageInfo},
                vk::WriteDescriptorSet{.dstSet = frame.CompositeDescriptorSet,
                                       .dstBinding = 2u,
                                       .dstArrayElement = 0u,
                                       .descriptorCount = 1u,
                                       .descriptorType = vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &revealageImageInfo},
                vk::WriteDescriptorSet{.dstSet = frame.CompositeDescriptorSet,
                                       .dstBinding = 3u,
                                       .dstArrayElement = 0u,
                                       .descriptorCount = 1u,
                                       .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                       .pImageInfo = &cloudsImageInfo}};

            m_Device.updateDescriptorSets(compDescriptorWrites, {});
        }
    }

    void UpdateDepthDescriptorSets()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "UpdateDepthDescriptorSets()");

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            vk::DescriptorImageInfo imageInfo{
                .imageView = NativeView(m_Frames[i].DepthTexture.GetView()),
                .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};

            std::array depthDescriptorWrites = {
                vk::WriteDescriptorSet{.dstSet = m_Frames[i].DepthBufferDescriptorSet,
                                       .dstBinding = 0,
                                       .dstArrayElement = 0,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &imageInfo}};

            m_Device.updateDescriptorSets(depthDescriptorWrites, {});
        }
    }

private:
    /**
     * Declared first because the device's creation reads them, and members are
     * initialised in declaration order.
     */
    IPlatform& m_Platform;
    const Paths& m_Paths;
    Options m_Options;
    IJobSystem& m_JobSystem;

    /**
     * Owned by main() rather than by the device, because the counts are read for
     * --strict-validation after the device has been destroyed.
     */
    Rhi::Diagnostics& m_Diagnostics;

    /**
     * Owns the instance, debug messenger, surface, physical and logical devices,
     * graphics queue and the VMA allocator. Declared ahead of every GPU resource
     * below so that it is destroyed after all of them — the ordering the old
     * hand-arranged member list was maintaining by hand.
     */
    std::unique_ptr<Rhi::IDevice> m_RhiDevice;

    /**
     * After the device and before every resource that is loaded through it: the
     * context holds staging buffers of its own, and destroying it after the
     * device would release them into nothing.
     */
    std::unique_ptr<Rhi::IUploadContext> m_UploadContext;
    std::unique_ptr<Rhi::IPipelineCache> m_PipelineCache;

    /**
     * Borrowed from m_RhiDevice, which outlives them. References rather than
     * copies so that the ~100 call sites still read as they did, and so that
     * there is exactly one owner. Each of these disappears as the corresponding
     * resource type moves behind IDevice.
     */
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::Device& m_Device;
    vk::raii::Queue& m_GraphicsQueue;
    uint32_t m_QueueIndex;

    std::unique_ptr<Rhi::IPresentTarget> m_PresentTarget;
    vk::raii::PipelineLayout m_OpaquePipelineLayout = nullptr;
    vk::raii::PipelineLayout m_TransparentPipelineLayout = nullptr;
    vk::raii::PipelineLayout m_CompositePipelineLayout = nullptr;
    vk::raii::DescriptorSetLayout m_GlobalBufferSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_CompositeSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_DepthSetLayout = nullptr;
    vk::raii::Pipeline m_OpaquePipeline = nullptr;
    vk::raii::Pipeline m_TransparentPipeline = nullptr;
    vk::raii::Pipeline m_CompositePipeline = nullptr;
    vk::raii::CommandPool m_GenericCommandPool = nullptr;
    GlobalBuffer m_GlobalBuffer = {};
    Rhi::UniqueHandle<Rhi::SamplerHandle> m_TextureSampler;
    vk::raii::DescriptorPool m_FrameDescriptorPool = nullptr;
    vk::raii::DescriptorPool m_CompositeDescriptorPool = nullptr;
    vk::raii::DescriptorPool m_GenericDescriptorPool = nullptr;
    Rhi::Format m_DepthFormat = Rhi::Format::Undefined;
    static constexpr Rhi::Format m_OpaqueImageFormat = Rhi::Format::RGBA16Float;
    static constexpr Rhi::Format m_AccumImageFormat = Rhi::Format::RGBA16Float;
    static constexpr Rhi::Format m_RevealageImageFormat = Rhi::Format::R8Unorm;
    Rhi::UniqueHandle<Rhi::BufferHandle> m_QuadVertexBuffer;
    Rhi::UniqueHandle<Rhi::BufferHandle> m_QuadIndexBuffer;

    /**
     * Set when the present target could not be rebuilt yet. The frame loop
     * retries and draws nothing until it clears.
     */
    bool m_bSwapchainOutOfDate = false;

    std::array<FrameData, NUM_FRAMES_IN_FLIGHT> m_Frames;

    /**
     * Instances every frame's buffer has room for. A starting size, not a
     * ceiling — see GrowInstanceBuffers.
     */
    uint32_t m_InstanceCapacity = 0u;

    std::unique_ptr<SceneGraph> m_SceneGraph = nullptr;

    std::unique_ptr<Camera> m_Camera = nullptr;
    std::shared_ptr<Cubemap> m_Skybox = nullptr;
    std::unique_ptr<CloudSystem> m_CloudSystem = nullptr;

    uint32_t m_FrameIndex = 0;
    bool m_bIsFocused = true;
    bool m_bCursorVisible = true;
    /**
     * Set in main() before anything else runs, so startupMs covers argument
     * parsing and the window as well as device creation and the scene's
     * uploads — the two paths a windowed and a headless run differ on.
     */
    std::chrono::steady_clock::time_point m_ProcessStart;

    std::chrono::steady_clock::time_point m_StartTime;
    std::chrono::steady_clock::time_point m_LastTime;
    uint64_t m_FrameCounter = 0;
    float m_RunTime = 0.f;
    float m_DeltaTime = 0.f;
    float m_DisplayFrameTime = 0.f;
    float m_DisplayFPS = 0.f;
    bool m_bShutdown = false;

    /**
     * Empty until the first auto-pathed file of a capture is written. See
     * CaptureTimestamp().
     */
    std::string m_CaptureTimestamp;

    /**
     * Barriers recorded for the current frame, split by the thread that records
     * them: the opaque and transparent passes are recorded on job threads, so
     * each owns its own counters rather than sharing one set. Everything else
     * is recorded on the main thread and shares the third.
     */
    Rhi::BarrierCounts m_OpaqueBarrierCounts;
    Rhi::BarrierCounts m_TransparentBarrierCounts;
    Rhi::BarrierCounts m_MainThreadBarrierCounts;
    Rhi::BarrierCounts m_LoggedBarrierCounts;

    /** Used in WriteReport() */
    uint32_t m_OpaqueDrawCallCount = 0;
    uint32_t m_OpaqueBatchCount = 0;
    uint32_t m_OpaqueInstanceCount = 0;
    uint32_t m_TransparentDrawCallCount = 0;
    uint32_t m_TransparentBatchCount = 0;
    uint32_t m_TransparentInstanceCount = 0;
    /**
     * Wall clock per frame, and the same minus everything the frame spent
     * blocked. Frame 0 is held separately rather than mixed in: it pays for
     * first use of every pipeline and the first acquire, so averaging it with
     * the rest describes neither.
     */
    std::vector<float> m_FrameMs;
    std::vector<float> m_CpuMs;
    float m_FirstFrameMs = 0.f;
    float m_FirstFrameCpuMs = 0.f;
    float m_StartupMs = 0.f;

    /** Time this frame spent blocked, accumulated across its blocking calls. */
    float m_FrameWaitMs = 0.f;
    Rhi::UniqueHandle<Rhi::BufferHandle> m_ScreenshotStagingBuffer;
    bool m_bScreenshotBufferReady = false;
};

int main(int argc, char** argv)
{
    // First statement in the process, so that startupMs in the run report
    // covers argument parsing and window creation as well as device setup —
    // which is where a windowed and a headless run differ most.
    const auto processStart = std::chrono::steady_clock::now();

#ifdef _WIN32
    EnableAnsiColors();
#endif
    // Both signals that mean "stop": Ctrl-C, and the SIGTERM a CI runner sends
    // when a job outlives its timeout. Without the second, a timed-out run is
    // killed with its screenshot and report unwritten — the two things anyone
    // diagnosing the timeout would want.
    std::signal(SIGINT, HandleTerminationSignal);
    std::signal(SIGTERM, HandleTerminationSignal);

    Log::g_MinSeverity = LogSeverity::Info;

    Options options = ParseArgs(argc, argv);

    // Declared before pApp so that it outlives the device reporting into it, and
    // so its counters are still readable for the --strict-validation check
    // below, which runs after everything has been torn down.
    Rhi::Diagnostics diagnostics(
        Rhi::Diagnostics::Desc{.Policy = options.ValidationPolicy,
                               .MinSeverity = Rhi::DiagnosticSeverity::Info,
                               .OnMessage = &HandleRhiDiagnostic});

    // will be destroyed in reverse order of declaration. The platform must
    // outlive App: destroying it unloads the Vulkan library.
    //
    // IPlatform rather than SdlPlatform because which implementation this is
    // depends on --headless. SdlPlatform::ShowErrorMessageBox is static, so the
    // SDLException handler below still works without an instance.
    std::unique_ptr<IPlatform> pPlatform = nullptr;
    std::unique_ptr<Paths> pPaths = nullptr;
    std::unique_ptr<IJobSystem> pJobSystem = nullptr;
    std::unique_ptr<App> pApp = nullptr;

    try
    {
        const WindowDesc windowDesc{.Width = options.WindowSize.Width,
                                    .Height = options.WindowSize.Height};

        // One description, either implementation. A zero size means "you
        // decide" to both of them: SdlPlatform asks the display, and
        // HeadlessPlatform, having none, uses its documented constant.
        if (options.bHeadless)
            pPlatform = std::make_unique<HeadlessPlatform>(windowDesc);
        else
            pPlatform = std::make_unique<SdlPlatform>(windowDesc);

        // Before the device, so that the first swapchain is built at the size
        // the window ends up rather than at the windowed one. Where the
        // transition is asynchronous the resize still arrives late, as a
        // resize event, which costs one recreation and nothing else.
        //
        // Unreachable headless: --headless with a window-mode flag is rejected
        // during parsing, so this is always Windowed there.
        if (options.StartWindowMode != WindowMode::Windowed)
            pPlatform->SetWindowMode(options.StartWindowMode);

        if (options.JobCount == 0)
        {
            LogMsg(LogSeverity::Info, LogMain,
                   "JobSystem selected: SerialJobSystem (no worker threads)");
            pJobSystem = std::make_unique<SerialJobSystem>();
        }
        else if (options.JobCount > 0)
        {
            pJobSystem =
                std::make_unique<SharedQueueJobSystem>(static_cast<uint32_t>(options.JobCount));
            LogMsg(LogSeverity::Info, LogMain,
                   "JobSystem selected: SharedQueueJobSystem ({} worker threads)",
                   pJobSystem->WorkerCount());
        }
        else
        {
            pJobSystem = std::make_unique<SharedQueueJobSystem>();
            LogMsg(LogSeverity::Info, LogMain,
                   "JobSystem selected: SharedQueueJobSystem ({} worker threads)",
                   pJobSystem->WorkerCount());
        }

        pPaths = std::make_unique<Paths>(options.ContentRoot);

        pApp = std::make_unique<App>(*pPlatform, *pPaths, options, *pJobSystem, diagnostics,
                                     processStart);
        pApp->Run();
    }
    catch (const SDLException& e)
    {
        SdlPlatform::ShowErrorMessageBox("SDL Error", e.what());
        LogMsg(LogSeverity::Error, LogSDL, "{}", e.what());
        return EXIT_FAILURE;
    }
    catch (const vk::SystemError& e)
    {
        LogMsg(LogSeverity::Error, LogMain, "Vulkan error: {}", e.what());
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        LogMsg(LogSeverity::Error, LogMain, "Error: {}", e.what());
        return EXIT_FAILURE;
    }

    pApp.reset();
    pJobSystem.reset();
    pPaths.reset();
    pPlatform.reset();

    // Everything above is destroyed by this point, so this covers teardown
    // messages too — which is where the interesting ones tend to be, since a
    // resource freed while still in use is only detectable at destruction.
    const uint64_t validationErrors = diagnostics.ErrorCount();
    const uint64_t validationWarnings = diagnostics.WarningCount();

    if (validationErrors > 0 || validationWarnings > 0)
    {
        LogMsg(LogSeverity::Warning, LogDiagnostics, "{} error(s), {} warning(s)", validationErrors,
               validationWarnings);

        const std::vector<std::string> recent = diagnostics.RecentMessages();
        const uint64_t dropped = diagnostics.DroppedMessageCount();
        if (dropped > 0)
            LogMsg(LogSeverity::Warning, LogDiagnostics,
                   "  last {} message(s), {} earlier one(s) dropped:", recent.size(), dropped);
        else
            LogMsg(LogSeverity::Warning, LogDiagnostics, "  {} message(s):", recent.size());

        for (const std::string& message : recent)
            LogMsg(LogSeverity::Warning, LogDiagnostics, "    {}", message);
    }

    if (options.bStrictValidation && validationErrors > 0)
    {
        LogMsg(LogSeverity::Error, LogDiagnostics,
               "Strict validation failed: {} validation error(s) occurred", validationErrors);
        return EXIT_FAILURE;
    }

    LogMsg(LogSeverity::Info, LogMain, "Exiting gracefully...");
    return EXIT_SUCCESS;
}
