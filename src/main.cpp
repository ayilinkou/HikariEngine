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
#include <rhi/RhiTypes.h>
#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>
#include <rhi/UniqueHandle.h>
#include <rhi/UploadContext.h>
#include <rhi/vulkan/DebugNames.h>
#include <rhi/vulkan/PipelineBuilder.h>
#include <rhi/vulkan/SwapchainUtil.h>
#include <rhi/vulkan/VulkanNative.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "ImGuiFileDialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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

void HandleSIGINT(int)
{
    g_bShouldClose = true;
    std::cout << "\n";
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

// Each member must start at an offset that is a multiple of its base alignment.
// Eg. a float can start on offset 0, 4, 8 or 12.
// glm::vec3 is 12 bytes wide by default but is 16 byte aligned.
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

// Routes the RHI's validation messages into the log. Rhi::Diagnostics has
// already counted and captured the message by the time this runs; this decides
// only how it is presented. Called from the driver's debug callback, so it may
// run on any thread.
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
    bool bHeadless = false; // TODO
    int JobCount =
        -1; // -1 = default, 0 = SerialJobSystem, N>0 = SharedQueueJobSystem with N worker threads

    // --vk-disable-extension, repeatable. Vulkan-specific by nature, which is
    // what the flag's prefix says: an optional extension exists in one backend's
    // vocabulary and nowhere else.
    std::vector<std::string> DisabledVulkanExtensions;

    // --vk-force-single-queue. Same prefix for the same reason: it is queue
    // families this collapses, and only Vulkan has them.
    bool bForceSingleQueue = false;
};

// Hardcoded camera transforms selected via --camera-preset <N>, for
// deterministic screenshots/reports. Rotation is (pitch, yaw, roll) in
// degrees, matching Transform::Rotation.
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

void PrintUsage()
{
    std::cout << "VulkanApp\n"
                 "\n"
                 "Usage: VulkanApp [options]\n"
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
                     "  --headless              Run without a window "
                     "(reserved, not yet implemented)\n"
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
            else if (flag == "--headless")
            {
                option.RequireNoValue();
                options.bHeadless = true;
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

class App
{
public:
    App(IPlatform& platform, const Paths& paths, Options options, IJobSystem& jobSystem,
        Rhi::Diagnostics& diagnostics)
        : m_Platform(platform), m_Paths(paths), m_Options(std::move(options)),
          m_JobSystem(jobSystem), m_Diagnostics(diagnostics),
          m_RhiDevice(Rhi::CreateDevice(MakeDeviceDesc())),
          m_PhysicalDevice(Rhi::Vulkan::GetPhysicalDevice(*m_RhiDevice)),
          m_Device(Rhi::Vulkan::GetDevice(*m_RhiDevice)),
          m_Surface(Rhi::Vulkan::GetSurface(*m_RhiDevice)),
          m_GraphicsQueue(Rhi::Vulkan::GetGraphicsQueue(*m_RhiDevice)),
          m_QueueIndex(Rhi::Vulkan::GetGraphicsQueueFamily(*m_RhiDevice))
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

        while (!g_bShouldClose)
        {
            if (!m_bIsFocused)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto now = std::chrono::high_resolution_clock::now();
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

            const float smoothing = 0.9f;
            float currentFrameTime = m_DeltaTime * 1000.f;
            m_DisplayFrameTime =
                (m_DisplayFrameTime * smoothing) + (currentFrameTime * (1.f - smoothing));

            if (!m_Options.ReportPath.empty() || m_Options.bReportAutoPath)
                m_FrameTimesMs.push_back(currentFrameTime);

            float currentFPS = 1.f / m_DeltaTime;
            m_DisplayFPS = (m_DisplayFPS * smoothing) + (currentFPS * (1.f - smoothing));

            SDL_Event event;
            while (SDL_PollEvent(&event))
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

            if (m_bCursorVisible)
                DrawImGuiFrame();

            ModelManager::Get()->GenerateBatches();

            const bool bIsLastFrame = g_bShouldClose || (m_Options.Frames != 0 &&
                                                         (m_FrameCounter + 1) >= m_Options.Frames);
            const bool captureScreenshot = bIsLastFrame && (!m_Options.ScreenshotPath.empty() ||
                                                            m_Options.bScreenshotAutoPath);

            DrawFrame(captureScreenshot);

            ++m_FrameCounter;
            if (m_Options.Frames != 0 && m_FrameCounter >= m_Options.Frames)
            {
                g_bShouldClose = true;
            }
        }

        if (!m_Options.ScreenshotPath.empty() || m_Options.bScreenshotAutoPath)
            WriteScreenshot();

        if (!m_Options.ReportPath.empty() || m_Options.bReportAutoPath)
            WriteReport();

        m_Device.waitIdle();
        Shutdown();
    }

private:
    // Called from the constructor's initialiser list, so it may only touch
    // members declared above m_RhiDevice.
    [[nodiscard]] Rhi::DeviceDesc MakeDeviceDesc() const
    {
        Rhi::DeviceDesc desc;
        desc.ApplicationName = "Vulkan App";
        desc.bEnableValidation = bEnableValidationLayers;
        desc.pDiagnostics = &m_Diagnostics;
        desc.Requirements.bPresent = true;
        desc.Requirements.NativeWindowHandle = m_Platform.GetNativeWindowHandle();
        desc.DisabledOptionalExtensions = m_Options.DisabledVulkanExtensions;
        desc.bForceSingleQueue = m_Options.bForceSingleQueue;
        return desc;
    }

    void Init()
    {
        LogMsg(LogSeverity::Info, LogMain, "Init()");

        m_StartTime = std::chrono::high_resolution_clock::now();
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

        vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {
            .colorAttachmentCount = 1u,
            .pColorAttachmentFormats = &m_SwapchainSurfaceFormat.format};

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
        initInfo.MinImageCount = m_MinImageCount;
        initInfo.MinAllocationSize = 1024 * 1024;
        initInfo.ImageCount = static_cast<uint32_t>(m_SwapTextures.size());
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineCache = Rhi::Vulkan::GetNativePipelineCache(*m_PipelineCache);
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = nullptr;

        ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window*>(m_Platform.GetNativeWindowHandle()));
        ImGui_ImplVulkan_Init(&initInfo);
    }

    void InitVulkan()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "InitVulkan()");

        // The device itself was created in the constructor, so that every member
        // below can assume it exists.
        CreateSwapchain();
        CreateSwapchainImageViews();
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
                                              .SwapchainWidth = m_SwapchainExtent.width,
                                              .SwapchainHeight = m_SwapchainExtent.height,
                                              .FramesInFlight = NUM_FRAMES_IN_FLIGHT};
        m_CloudSystem = std::make_unique<CloudSystem>(cloudCreateInfo);

        CreateDescriptorSets();
        CreateSyncObjects();
        CreateQuadBuffers();
    }

    // The stamp that names the auto-pathed files of one capture.
    //
    // Taken when the first of those files is written and reused by the rest, so
    // that a screenshot and the report describing the same moment share a name
    // — asking the clock separately puts them a second apart whenever the two
    // writes straddle a second boundary.
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

        uint64_t validationErrors = m_Diagnostics.ErrorCount();
        uint64_t validationWarnings = m_Diagnostics.WarningCount();
        uint32_t drawCalls = m_OpaqueDrawCallCount + m_TransparentDrawCallCount;
        uint32_t batches = m_OpaqueBatchCount + m_TransparentBatchCount;
        uint32_t instances = m_OpaqueInstanceCount + m_TransparentInstanceCount;
        // The last frame's counts, as the draw call and batch numbers above also
        // are. Worth knowing when comparing two reports: --screenshot captures
        // on the final frame, and the copy it inserts costs one extra barrier
        // and one extra call, so a captured run legitimately reads one higher
        // than an uncaptured one.
        Rhi::BarrierCounts barrierCounts = FrameBarrierCounts();

        float meanFrameTimeMs = 0.f;
        float p99FrameTimeMs = 0.f;
        if (!m_FrameTimesMs.empty())
        {
            double sum = 0.0;
            for (float t : m_FrameTimesMs)
                sum += t;
            meanFrameTimeMs = static_cast<float>(sum / m_FrameTimesMs.size());

            std::vector<float> sorted = m_FrameTimesMs;
            std::sort(sorted.begin(), sorted.end());
            size_t index =
                static_cast<size_t>(std::ceil(0.99 * static_cast<double>(sorted.size())));
            index = std::min(index, sorted.size()) - 1;
            p99FrameTimeMs = sorted[index];
        }

        std::string path =
            m_Options.bReportAutoPath ? DEFAULT_PATH + CaptureTimestamp() : m_Options.ReportPath;
        path = EnsureExtension(path, ".json");
        std::ofstream file(path);
        if (!file.is_open())
        {
            LogMsg(LogSeverity::Error, LogMain, "Failed to open report file for writing: {}", path);
            return;
        }

        file << "{\n"
             << "  \"frames\": " << m_FrameCounter << ",\n"
             << "  \"validationErrors\": " << validationErrors << ",\n"
             << "  \"validationWarnings\": " << validationWarnings << ",\n"
             << "  \"drawCalls\": " << drawCalls << ",\n"
             << "  \"batches\": " << batches << ",\n"
             << "  \"instances\": " << instances << ",\n"
             << "  \"barriers\": " << barrierCounts.Barriers << ",\n"
             << "  \"barrierCalls\": " << barrierCounts.Calls << ",\n"
             << "  \"meanFrameTimeMs\": " << meanFrameTimeMs << ",\n"
             << "  \"p99FrameTimeMs\": " << p99FrameTimeMs << "\n"
             << "}\n";
        file.close();

        LogMsg(LogSeverity::Info, LogMain, "Wrote report to {}", path);
    }

    // Writes the frame captured into m_ScreenshotStagingBuffer (during the
    // final frame's DrawFrame() call, before it was presented) out to disk as
    // a PNG. Used for deterministic verification via --screenshot.

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

        const uint32_t width = m_SwapchainExtent.width;
        const uint32_t height = m_SwapchainExtent.height;
        constexpr uint32_t bytesPerPixel = 4;
        const vk::DeviceSize bufferSize =
            static_cast<vk::DeviceSize>(width) * height * bytesPerPixel;

        // The swapchain format is BGRA; swizzle to RGBA before writing.
        const auto* src = static_cast<const uint8_t*>(
            m_RhiDevice->GetMappedData(m_ScreenshotStagingBuffer.Get()));
        std::vector<uint8_t> pixels(static_cast<size_t>(bufferSize));
        for (size_t i = 0; i < static_cast<size_t>(width) * height; i++)
        {
            pixels[i * 4 + 0] = src[i * 4 + 2]; // R <- B
            pixels[i * 4 + 1] = src[i * 4 + 1]; // G <- G
            pixels[i * 4 + 2] = src[i * 4 + 0]; // B <- R
            pixels[i * 4 + 3] = src[i * 4 + 3]; // A <- A
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
        m_Platform.WarpMouse(static_cast<float>(m_SwapchainExtent.width / 2.f),
                             static_cast<float>(m_SwapchainExtent.height / 2.f));
        m_Platform.SetRelativeMouseMode(false);
        m_bCursorVisible = true;
    }

    void HideCursor()
    {
        m_Platform.SetRelativeMouseMode(true);
        m_bCursorVisible = false;
    }

    // This includes OS key repeat delay.
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

    // Checking the state of the keys every frame, bypassing OS key repeat
    // delay.
    void HandleMovement()
    {
        if (m_bCursorVisible || m_Options.CameraPreset >= 0)
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

    void DrawFrame(bool captureScreenshot = false)
    {
        // Semaphores coordinate GPU to GPU synchronisation, for example
        // ordering work between queues. They get reset automatically after the
        // waiting operation begins.
        //
        // Fences coordinate CPU to GPU synchronisation, for times when
        // the CPU needs to know that the GPU has finished a task. Must be
        // explicitely reset by the host.

        FrameData& frameData = m_Frames[m_FrameIndex];
        auto fenceResult = m_Device.waitForFences(*frameData.DrawFence, vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess)
            throw std::runtime_error("Failed to wait for fence!");

        auto [result, imageIndex] =
            m_Swapchain.acquireNextImage(UINT64_MAX, *frameData.PresentCompleteSemaphore, nullptr);

        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            RecreateSwapchainAndRenderImages();
            return;
        }
        else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
        {
            assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
            throw std::runtime_error("Failed to acquire next swapchain image!");
        }

        m_Device.resetFences(*frameData.DrawFence);

        UpdateGlobalBuffer(m_FrameIndex);
        UpdateInstanceBuffer(m_FrameIndex);

        if (captureScreenshot && !m_bScreenshotBufferReady)
        {
            const vk::DeviceSize bufferSize =
                static_cast<vk::DeviceSize>(m_SwapchainExtent.width) * m_SwapchainExtent.height * 4;
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
            RecordSwapImageToDrawLayout(imageIndex);
            RecordCloudsCommandBuffer();
            RecordCompositeCommandBuffer(imageIndex);
            RecordImGui(imageIndex);
            RecordSwapImageToPresentLayout(imageIndex, captureScreenshot);

            m_JobSystem.Wait();
            LogBarrierCounts();
        }

        // TODO: even when ImGui is not showing, it's being submitted
        std::array<vk::CommandBuffer, 7> commandBuffers = {
            frameData.DrawLayoutCommandBuffer,   frameData.OpaqueCommandBuffer,
            frameData.TransparentCommandBuffer,  frameData.CloudCommandBuffer,
            frameData.CompositeCommandBuffer,    frameData.ImGuiCommandBuffer,
            frameData.PresentLayoutCommandBuffer};
        vk::PipelineStageFlags waitDestinationStageFlags(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submitInfo{.waitSemaphoreCount = 1u,
                                  .pWaitSemaphores = &*frameData.PresentCompleteSemaphore,
                                  .pWaitDstStageMask = &waitDestinationStageFlags,
                                  .commandBufferCount =
                                      static_cast<uint32_t>(commandBuffers.size()),
                                  .pCommandBuffers = commandBuffers.data(),
                                  .signalSemaphoreCount = 1u,
                                  .pSignalSemaphores = &*m_RenderCompleteSemaphores[imageIndex]};
        m_GraphicsQueue.submit(submitInfo, *frameData.DrawFence);

        const vk::PresentInfoKHR presentInfo{.waitSemaphoreCount = 1,
                                             .pWaitSemaphores =
                                                 &*m_RenderCompleteSemaphores[imageIndex],
                                             .swapchainCount = 1,
                                             .pSwapchains = &*m_Swapchain,
                                             .pImageIndices = &imageIndex};

        result = m_GraphicsQueue.presentKHR(presentInfo);
        if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR))
        {
            RecreateSwapchainAndRenderImages();
        }
        else if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to present image!");
        }

        m_FrameIndex = (m_FrameIndex + 1) % NUM_FRAMES_IN_FLIGHT;
    }

    void DrawImGuiFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
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

    void CreateSwapchain()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateSwapchain()");

        vk::SurfaceCapabilitiesKHR capabilities =
            m_PhysicalDevice.getSurfaceCapabilitiesKHR(*m_Surface);
        const std::vector<vk::SurfaceFormatKHR> formats =
            m_PhysicalDevice.getSurfaceFormatsKHR(*m_Surface);
        m_SwapchainSurfaceFormat = ChooseSwapchainFormat(formats);
        const std::vector<vk::PresentModeKHR> presentModes =
            m_PhysicalDevice.getSurfacePresentModesKHR(*m_Surface);
        const Extent2D framebufferExtent = m_Platform.GetFramebufferExtent();
        m_SwapchainExtent = ChooseSwapchainExtent(
            capabilities, vk::Extent2D{framebufferExtent.Width, framebufferExtent.Height});

        LogMsg(LogSeverity::Info, LogRenderer, "Swapchain Extent: {}x{}", m_SwapchainExtent.width,
               m_SwapchainExtent.height);

        m_MinImageCount = ChooseSwapMinImageCount(capabilities);
        vk::SwapchainCreateInfoKHR createInfo{
            .surface = *m_Surface,
            .minImageCount = m_MinImageCount,
            .imageFormat = m_SwapchainSurfaceFormat.format,
            .imageColorSpace = m_SwapchainSurfaceFormat.colorSpace,
            .imageExtent = m_SwapchainExtent,
            .imageArrayLayers = 1,
            .imageUsage =
                vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,

            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = ChoosePresentMode(presentModes),
            .clipped = true,
            .oldSwapchain = nullptr};

        m_Swapchain = vk::raii::SwapchainKHR(m_Device, createInfo);
        SetVkDebugName(m_Device, *m_Swapchain, vk::ObjectType::eSwapchainKHR, "Swapchain");

        // Every swapchain texture and view is described in neutral terms, so a
        // surface offering nothing Rhi::Format can name is an unrecoverable init
        // failure rather than something to fall back from — that is the deal the
        // curated format list makes. ChooseSwapchainFormat asks for BGRA8Unorm
        // first, which every desktop surface offers.
        const Rhi::Format swapchainFormat =
            Rhi::Vulkan::FromNativeFormat(m_SwapchainSurfaceFormat.format);

        // Registered rather than created: the images belong to the presentation
        // engine, and a handle is only how the rest of the RHI names one.
        const std::vector<vk::Image> images = m_Swapchain.getImages();
        assert(m_SwapTextures.empty());
        m_SwapTextures.reserve(images.size());
        for (size_t i = 0; i < images.size(); i++)
        {
            const Rhi::TextureDesc desc{
                .Format = swapchainFormat,
                .Extent = {m_SwapchainExtent.width, m_SwapchainExtent.height, 1u},
                .Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::CopySrc,
                .DebugName = std::format("Swapchain Image_{}", i)};
            m_SwapTextures.emplace_back(
                *m_RhiDevice, Rhi::Vulkan::RegisterExternalTexture(*m_RhiDevice, images[i], desc));
        }

        LogMsg(LogSeverity::Info, LogRenderer, "Swapchain image count: {}", m_SwapTextures.size());
    }

    void CreateSwapchainImageViews()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateSwapchainImageViews()");

        assert(m_SwapImageViews.empty());
        m_SwapImageViews.reserve(m_SwapTextures.size());
        for (size_t i = 0; i < m_SwapTextures.size(); i++)
        {
            m_SwapImageViews.emplace_back(
                *m_RhiDevice, m_RhiDevice->CreateTextureView(Rhi::TextureViewDesc{
                                  .Texture = m_SwapTextures[i].Get(),
                                  .DebugName = std::format("Swapchain Image View_{}", i)}));
        }
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
                .ColorAttachments(std::array{m_SwapchainSurfaceFormat.format},
                                  std::array{attachmentState})
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

            frame.PresentLayoutCommandPool = vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(m_Device, *frame.PresentLayoutCommandPool, vk::ObjectType::eCommandPool,
                           std::format("Present Layout Command Pool Frame {}", i).c_str());
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

            allocInfo = vk::CommandBufferAllocateInfo{.commandPool = frame.PresentLayoutCommandPool,
                                                      .level = vk::CommandBufferLevel::ePrimary,
                                                      .commandBufferCount = 1u};
            cmd = std::move(vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.PresentLayoutCommandBuffer = std::move(cmd);
            SetVkDebugName(m_Device, *frame.PresentLayoutCommandBuffer,
                           vk::ObjectType::eCommandBuffer,
                           std::format("Present Layout Command Buffer Frame {}", i).c_str());
        }
    }

    // The VkImageView a handle names.
    //
    // Dynamic rendering attachments and descriptor writes both take raw Vulkan
    // objects, and both still happen here — attachments until Stage 8's frame
    // graph, descriptor writes until bindless. This is the one place that
    // resolve is spelled out, so the call sites read as they did.
    vk::ImageView NativeView(Rhi::TextureViewHandle handle)
    {
        return Rhi::Vulkan::GetImageView(*m_RhiDevice, handle);
    }

    // What the frame's three recording threads produced between them. Summed on
    // demand rather than accumulated into one member, because two of the three
    // are written from job threads.
    Rhi::BarrierCounts FrameBarrierCounts() const
    {
        Rhi::BarrierCounts total = m_OpaqueBarrierCounts;
        total += m_TransparentBarrierCounts;
        total += m_MainThreadBarrierCounts;
        return total;
    }

    // Reports the frame's barrier counts the first time they are seen and
    // whenever they change afterwards. Logging them every frame would drown the
    // log, and never logging them would make a change in the barriers — the
    // easiest thing to get wrong here and the hardest to see — invisible
    // between runs of the report.
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
            .renderArea = {.offset = {0, 0}, .extent = m_SwapchainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo};

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_OpaquePipeline);

        cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<float>(m_SwapchainExtent.width),
                                        static_cast<float>(m_SwapchainExtent.height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapchainExtent));
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
            .renderArea = {.offset = {0, 0}, .extent = m_SwapchainExtent},
            .layerCount = 1,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfos.size()),
            .pColorAttachments = colorAttachmentInfos.data(),
            .pDepthAttachment = &depthAttachmentInfo};

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_TransparentPipeline);

        cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<float>(m_SwapchainExtent.width),
                                        static_cast<float>(m_SwapchainExtent.height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapchainExtent));
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

    void RecordCompositeCommandBuffer(uint32_t imageIndex)
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
            .imageView = NativeView(m_SwapImageViews[imageIndex].Get()),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = m_SwapchainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo};

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_CompositePipeline);

        cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<float>(m_SwapchainExtent.width),
                                        static_cast<float>(m_SwapchainExtent.height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapchainExtent));

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

    void RecordImGui(uint32_t imageIndex)
    {
        m_Frames[m_FrameIndex].ImGuiCommandPool.reset();
        vk::raii::CommandBuffer& cmd = m_Frames[m_FrameIndex].ImGuiCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();

        // ImGui draws over the composited frame with loadOp eLoad, so the
        // composite pass's writes have to be visible to this pass's load.
        m_MainThreadBarrierCounts += list->Barrier(
            Rhi::BarrierPresets::PreserveRenderTarget().On(m_SwapTextures[imageIndex].Get()));

        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = NativeView(m_SwapImageViews[imageIndex].Get()),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = m_SwapchainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo};

        cmd.beginRendering(renderingInfo);

        if (m_bCursorVisible)
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *cmd);

        cmd.endRendering();
        list->End();
    }

    void RecordSwapImageToDrawLayout(uint32_t imageIndex)
    {
        m_Frames[m_FrameIndex].DrawLayoutCommandPool.reset();
        vk::raii::CommandBuffer& cmd = m_Frames[m_FrameIndex].DrawLayoutCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();
        m_MainThreadBarrierCounts +=
            list->Barrier(Rhi::BarrierPresets::AcquiredSwapchainToRenderTarget().On(
                m_SwapTextures[imageIndex].Get()));
        list->End();
    }

    void RecordSwapImageToPresentLayout(uint32_t imageIndex, bool captureScreenshot)
    {
        m_Frames[m_FrameIndex].PresentLayoutCommandPool.reset();
        vk::raii::CommandBuffer& cmd = m_Frames[m_FrameIndex].PresentLayoutCommandBuffer;
        std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(*m_RhiDevice, *cmd);
        list->Begin();

        const Rhi::TextureHandle swapTexture = m_SwapTextures[imageIndex].Get();

        if (captureScreenshot)
        {
            // Copy out the composited frame while it is still safely between
            // acquire and present (see RenderTargetToCopySrc()).
            m_MainThreadBarrierCounts +=
                list->Barrier(Rhi::BarrierPresets::RenderTargetToCopySrc().On(swapTexture));

            list->CopyTextureToBuffer(
                swapTexture, m_ScreenshotStagingBuffer.Get(),
                Rhi::BufferTextureCopyRegion{
                    .Extent = {m_SwapchainExtent.width, m_SwapchainExtent.height, 1u}});

            m_MainThreadBarrierCounts +=
                list->Barrier(Rhi::BarrierPresets::CopySrcToPresent().On(swapTexture));
        }
        else
        {
            m_MainThreadBarrierCounts +=
                list->Barrier(Rhi::BarrierPresets::RenderTargetToPresent().On(swapTexture));
        }

        list->End();
    }

    void CreateSyncObjects()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateSyncObjects()");

        for (size_t i = 0; i < m_SwapTextures.size(); i++)
        {
            m_RenderCompleteSemaphores.emplace_back(
                vk::raii::Semaphore(m_Device, vk::SemaphoreCreateInfo()));
            SetVkDebugName(m_Device, *m_RenderCompleteSemaphores.back(), vk::ObjectType::eSemaphore,
                           std::format("Render Complete Semaphore_{}", i).c_str());
        }

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].PresentCompleteSemaphore =
                vk::raii::Semaphore(m_Device, vk::SemaphoreCreateInfo());
            SetVkDebugName(m_Device, *m_Frames[i].PresentCompleteSemaphore,
                           vk::ObjectType::eSemaphore,
                           std::format("Present Complete Semaphore_{}", i).c_str());

            m_Frames[i].DrawFence =
                vk::raii::Fence(m_Device, {.flags = vk::FenceCreateFlagBits::eSignaled});
            SetVkDebugName(m_Device, *m_Frames[i].DrawFence, vk::ObjectType::eFence,
                           std::format("Draw Fence_{}", i).c_str());
        }
    }

    void RecreateSwapchainAndRenderImages()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "Recreating swapchain and render images...");

        // Blocks while the window is minimised — a zero-sized framebuffer is
        // not a legal swapchain extent.
        Extent2D framebufferExtent = m_Platform.GetFramebufferExtent();
        while (framebufferExtent.Width == 0 || framebufferExtent.Height == 0)
        {
            framebufferExtent = m_Platform.GetFramebufferExtent();
            SDL_Event event;
            SDL_WaitEvent(&event);
        }

        m_Device.waitIdle();

        m_SwapImageViews.clear();
        m_SwapTextures.clear();
        m_Swapchain = nullptr;

        m_DepthFormat = Rhi::Format::Undefined;

        m_Device.waitIdle();

        CreateSwapchain();
        CreateSwapchainImageViews();

        CreateDepthResources();
        CreateRenderTargets();

        m_CloudSystem->Resize(m_SwapchainExtent.width, m_SwapchainExtent.height);
        UpdateDepthDescriptorSets();
        UpdateCompositeDescriptorSet();

        m_Camera->SetProjection(m_Camera->GetFOV(),
                                static_cast<float>(m_SwapchainExtent.width) /
                                    static_cast<float>(m_SwapchainExtent.height),
                                m_Camera->GetNearPlane(), m_Camera->GetFarPlane());

        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{.colorAttachmentCount = 1u,
                                                              .pColorAttachmentFormats =
                                                                  &m_SwapchainSurfaceFormat.format};
        ImGui_ImplVulkan_PipelineInfo pipelineInfo{};
        pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        pipelineInfo.PipelineRenderingCreateInfo = pipelineRenderingInfo;
        ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);
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
                                 .Extent = {m_SwapchainExtent.width, m_SwapchainExtent.height, 1u},
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

    // Every frame's buffer is replaced at once, not just the one being filled:
    // growing them one at a time would leave the other frame short by exactly
    // the same amount and grow again on the very next frame, for two device
    // waits instead of one.
    //
    // The wait is what makes the replacement legal. These are vertex buffers
    // that frames still in flight have bound, and destroying one while a
    // submitted command buffer still refers to it is invalid
    // (VUID-vkDestroyBuffer-buffer-00922); the current frame's fence has been
    // waited on by this point, but nothing covers the others. Growth is rare
    // enough that a device-wide wait beats tracking per-buffer lifetimes.
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
                                 .Extent = {m_SwapchainExtent.width, m_SwapchainExtent.height, 1u},
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
    // Declared first because the device's creation reads them, and members are
    // initialised in declaration order.
    IPlatform& m_Platform;
    const Paths& m_Paths;
    Options m_Options;
    IJobSystem& m_JobSystem;

    // Owned by main() rather than by the device, because the counts are read for
    // --strict-validation after the device has been destroyed.
    Rhi::Diagnostics& m_Diagnostics;

    // Owns the instance, debug messenger, surface, physical and logical devices,
    // graphics queue and the VMA allocator. Declared ahead of every GPU resource
    // below so that it is destroyed after all of them — the ordering the old
    // hand-arranged member list was maintaining by hand.
    std::unique_ptr<Rhi::IDevice> m_RhiDevice;

    // After the device and before every resource that is loaded through it: the
    // context holds staging buffers of its own, and destroying it after the
    // device would release them into nothing.
    std::unique_ptr<Rhi::IUploadContext> m_UploadContext;
    std::unique_ptr<Rhi::IPipelineCache> m_PipelineCache;

    // Borrowed from m_RhiDevice, which outlives them. References rather than
    // copies so that the ~100 call sites still read as they did, and so that
    // there is exactly one owner. Each of these disappears as the corresponding
    // resource type moves behind IDevice.
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::Device& m_Device;
    vk::raii::SurfaceKHR& m_Surface;
    vk::raii::Queue& m_GraphicsQueue;
    uint32_t m_QueueIndex;

    vk::raii::SwapchainKHR m_Swapchain = nullptr;
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

    vk::SurfaceFormatKHR m_SwapchainSurfaceFormat;
    vk::Extent2D m_SwapchainExtent;
    // The presentation engine owns the images; these handles only name them, so
    // releasing one frees nothing. The views on top of them are the device's,
    // and are declared second so that they are released first.
    std::vector<Rhi::UniqueHandle<Rhi::TextureHandle>> m_SwapTextures;
    std::vector<Rhi::UniqueHandle<Rhi::TextureViewHandle>> m_SwapImageViews;
    uint32_t m_MinImageCount = 0;

    std::array<FrameData, NUM_FRAMES_IN_FLIGHT> m_Frames;

    // Instances every frame's buffer has room for. A starting size, not a
    // ceiling — see GrowInstanceBuffers.
    uint32_t m_InstanceCapacity = 0u;
    std::vector<vk::raii::Semaphore> m_RenderCompleteSemaphores;

    std::unique_ptr<SceneGraph> m_SceneGraph = nullptr;

    std::unique_ptr<Camera> m_Camera = nullptr;
    std::shared_ptr<Cubemap> m_Skybox = nullptr;
    std::unique_ptr<CloudSystem> m_CloudSystem = nullptr;

    uint32_t m_FrameIndex = 0;
    bool m_bIsFocused = true;
    bool m_bCursorVisible = true;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_LastTime;
    uint64_t m_FrameCounter = 0;
    float m_RunTime = 0.f;
    float m_DeltaTime = 0.f;
    float m_DisplayFrameTime = 0.f;
    float m_DisplayFPS = 0.f;
    bool m_bShutdown = false;

    // Empty until the first auto-pathed file of a capture is written. See
    // CaptureTimestamp().
    std::string m_CaptureTimestamp;

    // Barriers recorded for the current frame, split by the thread that records
    // them: the opaque and transparent passes are recorded on job threads, so
    // each owns its own counters rather than sharing one set. Everything else
    // is recorded on the main thread and shares the third.
    Rhi::BarrierCounts m_OpaqueBarrierCounts;
    Rhi::BarrierCounts m_TransparentBarrierCounts;
    Rhi::BarrierCounts m_MainThreadBarrierCounts;
    Rhi::BarrierCounts m_LoggedBarrierCounts;

    // Used in WriteReport()
    uint32_t m_OpaqueDrawCallCount = 0;
    uint32_t m_OpaqueBatchCount = 0;
    uint32_t m_OpaqueInstanceCount = 0;
    uint32_t m_TransparentDrawCallCount = 0;
    uint32_t m_TransparentBatchCount = 0;
    uint32_t m_TransparentInstanceCount = 0;
    std::vector<float> m_FrameTimesMs;
    Rhi::UniqueHandle<Rhi::BufferHandle> m_ScreenshotStagingBuffer;
    bool m_bScreenshotBufferReady = false;
};

int main(int argc, char** argv)
{
#ifdef _WIN32
    EnableAnsiColors();
#endif
    std::signal(SIGINT, HandleSIGINT);

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
    std::unique_ptr<SdlPlatform> pPlatform = nullptr;
    std::unique_ptr<Paths> pPaths = nullptr;
    std::unique_ptr<IJobSystem> pJobSystem = nullptr;
    std::unique_ptr<App> pApp = nullptr;

    try
    {
        pPlatform = std::make_unique<SdlPlatform>(WindowDesc{});

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

        pApp = std::make_unique<App>(*pPlatform, *pPaths, options, *pJobSystem, diagnostics);
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
