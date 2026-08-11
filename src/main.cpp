#include "AllocatedImage.h"
#include "Barrier.h"
#include "Camera.h"
#include "CloudSystem.h"
#include "Common.h"
#include "Cubemap.h"
#include "Entity.h"
#include "FrameData.h"
#include "InstanceData.h"
#include "Lights.h"
#include "Log.h"
#include "MaterialFactory.h"
#include "Model.h"
#include "ModelManager.h"
#include "PBRMaterial.h"
#include "PipelineBuilder.h"
#include "ResourceManager.h"
#include "ThreadPool.h"
#include "Timer.h"
#include "Utility.h"
#include "Vertex.h"
#include "VulkanAllocator.h"
#include "XmlParser.h"

#include "ImGuiFileDialog.h"
#include "vulkan/vulkan.hpp"

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

#define MULTITHREADED_COMMAND_RECORDING 1

constexpr uint32_t WIDTH = 1920u;
constexpr uint32_t HEIGHT = 1080u;
constexpr uint32_t MAX_INSTANCE_COUNT = 1024u;
constexpr int NUM_FRAMES_IN_FLIGHT = 2;
constexpr glm::vec3 SKY_COLOR = {0.4f, 0.8f, 1.f};

constexpr LogCategory LogValidationLayer("Validation Layer");
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

std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool bEnableValidationLayers = false;
#else
constexpr bool bEnableValidationLayers = true;
#endif

vk::DebugUtilsMessageSeverityFlagBitsEXT validationSeverityThreshold =
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;

static VKAPI_ATTR vk::Bool32 VKAPI_CALL
DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
              vk::DebugUtilsMessageTypeFlagsEXT type,
              const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData)
{
    if (severity < validationSeverityThreshold)
        return vk::False;

    LogSeverity logSeverity;
    switch (severity)
    {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
        logSeverity = LogSeverity::Info;
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
        logSeverity = LogSeverity::Warning;
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
        logSeverity = LogSeverity::Error;
        break;
    }

    LogMsg(logSeverity, LogValidationLayer, "Type: {}. Msg: {}",
           to_string(type).c_str(), pCallbackData->pMessage);

    if (pUserData)
    {
        // TODO: use this when needed
    }
    return vk::False;
}

class SDLException : public std::runtime_error
{
public:
    SDLException(const std::string& message)
        : std::runtime_error(std::format("{} {}", message, SDL_GetError()))
    {
    }
};

void InitSDL()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw SDLException("Failed to initialise SDL!");

    LogMsg(LogSeverity::Info, LogSDL, "SDL video driver: {}",
           SDL_GetCurrentVideoDriver());

    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw SDLException("Failed to load Vulkan library!");
}

SDL_Window* CreateSDLWindow()
{
    // hidden to hide the window while initialisation is taking place
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                            SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS;
    SDL_Window* window = SDL_CreateWindow("Vulkan App", WIDTH, HEIGHT, flags);
    if (window == nullptr)
        throw SDLException("Failed to create window!");

    SDL_SetWindowFullscreen(window, false);
    return window;
}

void ShutdownSDL(SDL_Window* pWindow)
{
    if (pWindow)
    {
        SDL_WarpMouseInWindow(pWindow, 0.f, 0.f);
        SDL_SetWindowRelativeMouseMode(pWindow, false);
        SDL_DestroyWindow(pWindow);
    }

    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

class App
{
public:
    App() {}
    App(SDL_Window* pWindow) : m_pWindow(pWindow) {}
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

        SDL_ShowWindow(m_pWindow);

        while (!g_bShouldClose)
        {
            if (!m_bIsFocused)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto now = std::chrono::high_resolution_clock::now();
            m_DeltaTime =
                std::chrono::duration<float, std::chrono::seconds::period>(
                    now - m_LastTime)
                    .count();
            m_RunTime =
                std::chrono::duration<float, std::chrono::seconds::period>(
                    now - m_StartTime)
                    .count();
            m_LastTime = now;

            const float smoothing = 0.9f;
            float currentFrameTime = m_DeltaTime * 1000.f;
            m_DisplayFrameTime = (m_DisplayFrameTime * smoothing) +
                                 (currentFrameTime * (1.f - smoothing));

            float currentFPS = 1.f / m_DeltaTime;
            m_DisplayFPS =
                (m_DisplayFPS * smoothing) + (currentFPS * (1.f - smoothing));

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
            DrawFrame();
        }

        m_Device.waitIdle();
        Shutdown();
    }

private:
    void Init()
    {
        LogMsg(LogSeverity::Info, LogMain, "Init()");

        m_StartTime = std::chrono::high_resolution_clock::now();
        m_LastTime = m_StartTime;

        InitVulkan();
        InitImGui();

        ThreadPool::Init();

        m_SceneGraph = std::make_unique<SceneGraph>();

        // TODO: read from scene
        /*CubemapCreateInfo createInfo{};
        createInfo.Name = "Skybox";
        createInfo.Format = vk::Format::eR8G8B8A8Srgb;

        const std::string skyboxRoot = "textures/skybox/";
        createInfo.RightPath = skyboxRoot + "right.jpg";
        createInfo.LeftPath = skyboxRoot + "left.jpg";
        createInfo.TopPath = skyboxRoot + "top.jpg";
        createInfo.BottomPath = skyboxRoot + "bottom.jpg";
        createInfo.FrontPath = skyboxRoot + "front.jpg";
        createInfo.BackPath = skyboxRoot + "back.jpg";

        m_pSkybox = ResourceManager::Get()->LoadCubemap(createInfo);*/

        m_Camera = std::make_unique<Camera>();
        m_Camera->GetTransform().Position += glm::vec3(0.f, 0.f, 10.f);

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

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = m_APIVersion;
        initInfo.Instance = *m_Instance;
        initInfo.PhysicalDevice = *m_PhysicalDevice;
        initInfo.Device = *m_Device;
        initInfo.QueueFamily = m_QueueIndex;
        initInfo.Queue = *m_GraphicsQueue;
        initInfo.DescriptorPool = VK_NULL_HANDLE;
        initInfo.DescriptorPoolSize =
            IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
        initInfo.MinImageCount = m_MinImageCount;
        initInfo.ImageCount = static_cast<uint32_t>(m_SwapImages.size());
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineCache = VK_NULL_HANDLE;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo =
            pipelineRenderingInfo;
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = nullptr;

        ImGui_ImplSDL3_InitForVulkan(m_pWindow);
        ImGui_ImplVulkan_Init(&initInfo);
    }

    void InitVulkan()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "InitVulkan()");

        CreateInstance();
        SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();

        m_VmaAllocator = VulkanAllocator(m_Instance, m_PhysicalDevice, m_Device,
                                         m_APIVersion);

        CreateSwapchain();
        CreateSwapchainImageViews();
        CreateDepthResources();
        CreateDescriptorSetLayouts();
        CreateCommandPools();
        CreateTextureSampler();

        ResourceManager::Init(m_Device, m_PhysicalDevice, m_GenericCommandPool,
                              m_GraphicsQueue, m_VmaAllocator);
        MaterialFactory::Init(m_Device, m_TextureSampler);

        CreatePipelines();
        CreateCommandBuffers();
        CreateGlobalBuffers();
        CreateInstanceBuffers();
        CreateRenderTargets();
        CreateDescriptorPool();

        // TODO: read from scene
        CloudSystemCreateInfo cloudCreateInfo{
            .Device = m_Device,
            .PhysicalDevice = m_PhysicalDevice,
            .GlobalSetLayout = m_GlobalBufferSetLayout,
            .DepthSetLayout = m_DepthSetLayout,
            .CommandPool = m_GenericCommandPool,
            .ComputeQueue = m_GraphicsQueue, // TODO: find and store a dedicated
                                             // compute queue
            .SwapchainWidth = m_SwapchainExtent.width,
            .SwapchainHeight = m_SwapchainExtent.height,
            .FramesInFlight = NUM_FRAMES_IN_FLIGHT,
            .Allocator = m_VmaAllocator};
        m_CloudSystem = std::make_unique<CloudSystem>(cloudCreateInfo);

        CreateDescriptorSets();
        CreateSyncObjects();
        CreateQuadBuffers();
    }

    void Shutdown()
    {
        LogMsg(LogSeverity::Info, LogMain, "Shutdown()");

        if (m_pSkybox)
        {
            ResourceManager::Get()->UnloadCubemap(m_pSkybox->GetCreateInfo());
            m_pSkybox = nullptr;
        }

        m_SceneGraph.reset();
        ThreadPool::Shutdown();
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
        if (!m_bCursorVisible)
            m_Camera->Rotate(x, y);
    }

    void ShowCursor()
    {
        SDL_WarpMouseInWindow(
            m_pWindow, static_cast<float>(m_SwapchainExtent.width / 2.f),
            static_cast<float>(m_SwapchainExtent.height / 2.f));
        SDL_SetWindowRelativeMouseMode(m_pWindow, false);
        m_bCursorVisible = true;
    }

    void HideCursor()
    {
        SDL_SetWindowRelativeMouseMode(m_pWindow, true);
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
        }
    }

    // Checking the state of the keys every frame, bypassing OS key repeat
    // delay.
    void HandleMovement()
    {
        if (m_bCursorVisible)
            return;

        glm::vec3 camOffset = {0.f, 0.f, 0.f};
        const bool* state = SDL_GetKeyboardState(nullptr);
        if (state[SDL_SCANCODE_A])
        {
            camOffset += -m_Camera->GetRightVector() *
                         m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_D])
        {
            camOffset += m_Camera->GetRightVector() * m_Camera->GetMoveSpeed() *
                         m_DeltaTime;
        }
        if (state[SDL_SCANCODE_W])
        {
            camOffset += m_Camera->GetForwardVector() *
                         m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_S])
        {
            camOffset += -m_Camera->GetForwardVector() *
                         m_Camera->GetMoveSpeed() * m_DeltaTime;
        }
        if (state[SDL_SCANCODE_Q])
        {
            camOffset += glm::vec3(0.f, -1.f, 0.f) * m_Camera->GetMoveSpeed() *
                         m_DeltaTime;
        }
        if (state[SDL_SCANCODE_E])
        {
            camOffset += glm::vec3(0.f, 1.f, 0.f) * m_Camera->GetMoveSpeed() *
                         m_DeltaTime;
        }

        if ((std::fabs(camOffset.x) + std::fabs(camOffset.y) +
             std::fabs(camOffset.z)) > 0.f)
            m_Camera->GetTransform().Position += camOffset;
    }

    void DrawFrame()
    {
        // Semaphores coordinate GPU to GPU synchronisation, for example
        // ordering work between queues. They get reset automatically after the
        // waiting operation begins.
        //
        // Fences coordinate CPU to GPU synchronisation, for times when
        // the CPU needs to know that the GPU has finished a task. Must be
        // explicitely reset by the host.

        FrameData& frameData = m_Frames[m_FrameIndex];
        auto fenceResult =
            m_Device.waitForFences(*frameData.DrawFence, vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess)
            throw std::runtime_error("Failed to wait for fence!");

        auto [result, imageIndex] = m_Swapchain.acquireNextImage(
            UINT64_MAX, *frameData.PresentCompleteSemaphore, nullptr);

        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            RecreateSwapchainAndRenderImages();
            return;
        }
        else if (result != vk::Result::eSuccess &&
                 result != vk::Result::eSuboptimalKHR)
        {
            assert(result == vk::Result::eTimeout ||
                   result == vk::Result::eNotReady);
            throw std::runtime_error("Failed to acquire next swapchain image!");
        }

        m_Device.resetFences(*frameData.DrawFence);

        UpdateGlobalBuffer(m_FrameIndex);
        UpdateInstanceBuffer(m_FrameIndex);

        {
            // Timer recordTimer("Command buffer recording");
#if MULTITHREADED_COMMAND_RECORDING
            ThreadPool* threadPool = ThreadPool::Get();
            std::future<void> opaqueFuture =
                threadPool->Submit([&] { RecordOpaqueCommandBuffer(); });
            std::future<void> transparentFuture =
                threadPool->Submit([&] { RecordTransparentCommandBuffer(); });

            // these command buffers are very small and so likely faster to
            // record on main thread
            RecordSwapImageToDrawLayout(imageIndex);
            RecordCloudsCommandBuffer();
            RecordCompositeCommandBuffer(imageIndex);
            RecordImGui(imageIndex);
            RecordSwapImageToPresentLayout(imageIndex);

            opaqueFuture.get();
            transparentFuture.get();
#else
            RecordSwapImageToDrawLayout(imageIndex);
            RecordOpaqueCommandBuffer();
            RecordTransparentCommandBuffer();
            RecordCloudsCommandBuffer();
            RecordCompositeCommandBuffer(imageIndex);
            RecordImGui(imageIndex);
            RecordSwapImageToPresentLayout(imageIndex);
#endif
        }

        // TODO: even when ImGui is not showing, it's being submitted
        std::array<vk::CommandBuffer, 7> commandBuffers = {
            frameData.DrawLayoutCommandBuffer,   frameData.OpaqueCommandBuffer,
            frameData.TransparentCommandBuffer,  frameData.CloudCommandBuffer,
            frameData.CompositeCommandBuffer,    frameData.ImGuiCommandBuffer,
            frameData.PresentLayoutCommandBuffer};
        vk::PipelineStageFlags waitDestinationStageFlags(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &*frameData.PresentCompleteSemaphore,
            .pWaitDstStageMask = &waitDestinationStageFlags,
            .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
            .pCommandBuffers = commandBuffers.data(),
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &*m_RenderCompleteSemaphores[imageIndex]};
        m_GraphicsQueue.submit(submitInfo, *frameData.DrawFence);

        const vk::PresentInfoKHR presentInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*m_RenderCompleteSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &*m_Swapchain,
            .pImageIndices = &imageIndex};

        result = m_GraphicsQueue.presentKHR(presentInfo);
        if ((result == vk::Result::eSuboptimalKHR) ||
            (result == vk::Result::eErrorOutOfDateKHR))
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
                ImGui::DragFloat3("Position", &pPointLight->GetPosition().x,
                                  0.5f);
                ImGui::ColorEdit3("Color##PointLight",
                                  &pPointLight->GetColor().r);
                ImGui::SliderFloat("Intensity##PointLight",
                                   &pPointLight->GetIntensity(), 0.f, 1000.f);
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
                ImGui::SliderFloat("Intensity##DirLight",
                                   &pDirLight->GetIntensity(), 0.f, 10.f);

                ImGui::PopID();
            }

            ImGui::Dummy(ImVec2(0.f, 20.f));

            ImVec2 minFileDialogSize = ImVec2(600, 400);
            if (ImGui::Button("Load Scene"))
            {
                IGFD::FileDialogConfig config;
                config.path = "scenes/";
                ImGuiFileDialog::Instance()->OpenDialog(
                    "LoadSceneDlg", "Choose Scene to Load", ".map", config);
            }

            if (ImGuiFileDialog::Instance()->Display(
                    "LoadSceneDlg", ImGuiWindowFlags_NoCollapse,
                    minFileDialogSize))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    std::string path =
                        ImGuiFileDialog::Instance()->GetFilePathName();

                    m_Device.waitIdle();

                    // Loading the new scene before unloading current scene.
                    // Speeds up load times by not unloading resources which are
                    // used in both scenes. This does mean that you have to
                    // temporarily store both scenes in memory until the load if
                    // finished though. Can look into this in the future if it
                    // becomes a problem.
                    std::unique_ptr<SceneGraph> tempSceneGraph =
                        XmlParser::LoadScene(path);
                    if (tempSceneGraph.get())
                    {
                        m_SceneGraph.reset();
                        m_SceneGraph = std::move(tempSceneGraph);
                    }
                }
                ImGuiFileDialog::Instance()->Close();
            }

            if (ImGui::Button("Save Scene"))
            {
                IGFD::FileDialogConfig config;
                config.path = "scenes/";
                config.fileName = "new_scene.map";
                ImGuiFileDialog::Instance()->OpenDialog(
                    "SaveSceneDlg", "Save Scene As", ".map", config);
            }

            if (ImGuiFileDialog::Instance()->Display(
                    "SaveSceneDlg", ImGuiWindowFlags_NoCollapse,
                    minFileDialogSize))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    std::string path =
                        ImGuiFileDialog::Instance()->GetFilePathName();

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

    void CreateInstance()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateInstance()");

        constexpr vk::ApplicationInfo appInfo{
            .pApplicationName = "Vulkan App",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = m_APIVersion};

        // extensions
        Uint32 countInstanceExtensions;
        const char* const* instanceExtensions =
            SDL_Vulkan_GetInstanceExtensions(&countInstanceExtensions);

        if (countInstanceExtensions == 0)
            throw std::runtime_error("No available instance extensions found!");

        std::vector<const char*> requiredExtensions(countInstanceExtensions);
        memcpy(requiredExtensions.data(), instanceExtensions,
               countInstanceExtensions * sizeof(const char*));
        requiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);

#if defined(__APPLE__)
        // MoltenVK is a portability driver. On macOS the Vulkan loader
        // requires the app to explicitly opt into enumerating portability
        // drivers, otherwise vkCreateInstance fails with
        // VK_ERROR_INCOMPATIBLE_DRIVER.
        requiredExtensions.push_back(
            vk::KHRPortabilityEnumerationExtensionName);
        // Required so we can build the swapchain surface via
        // VK_EXT_metal_surface (SDL_Vulkan_CreateSurface is unreliable on
        // macOS).
        requiredExtensions.push_back(vk::EXTMetalSurfaceExtensionName);
#endif

        auto extensionProperties =
            m_Context.enumerateInstanceExtensionProperties();

        // VK_EXT_layer_settings is implemented by layers (e.g.
        // VK_LAYER_KHRONOS_validation), not by the loader, so it never appears
        // in vkEnumerateInstanceExtensionProperties(nullptr). It takes effect
        // purely through the VkLayerSettingsCreateInfoEXT pNext chain below; the
        // gate for attaching it is whether the validation layer is enabled, not
        // whether the extension happens to be enumerated.
        auto unsupportedExtensionIt = std::ranges::find_if(
            requiredExtensions,
            [&extensionProperties](auto const& requiredExtension)
            {
                return std::ranges::none_of(
                    extensionProperties,
                    [requiredExtension](auto const& extensionProperty)
                    {
                        return strcmp(extensionProperty.extensionName,
                                      requiredExtension) == 0;
                    });
            });

        if (unsupportedExtensionIt != requiredExtensions.end())
            throw std::runtime_error("Required extension not supported: " +
                                     std::string(*unsupportedExtensionIt));

        // layers
        std::vector<const char*> requiredLayers;
        if (bEnableValidationLayers)
        {
            requiredLayers.assign(validationLayers.begin(),
                                  validationLayers.end());
        }

        auto layerProperties = m_Context.enumerateInstanceLayerProperties();

        auto unsupportedLayerIt = std::ranges::find_if(
            requiredLayers,
            [&layerProperties](auto const& requiredLayer)
            {
                return std::ranges::none_of(
                    layerProperties,
                    [requiredLayer](auto const& layerProperty)
                    {
                        return strcmp(layerProperty.layerName, requiredLayer) ==
                               0;
                    });
            });

        if (unsupportedLayerIt != requiredLayers.end())
            throw std::runtime_error("Required layer not supported: " +
                                     std::string(*unsupportedLayerIt));

        const vk::Bool32 bSyncValEnabled = VK_TRUE;
        const vk::Bool32 bBestPracticesValEnabled = VK_TRUE;

        std::array<vk::LayerSettingEXT, 2> settings = {
            vk::LayerSettingEXT{.pLayerName = "VK_LAYER_KHRONOS_validation",
                                .pSettingName = "validate_sync",
                                .type = vk::LayerSettingTypeEXT::eBool32,
                                .valueCount = 1,
                                .pValues = &bSyncValEnabled},
            vk::LayerSettingEXT{.pLayerName = "VK_LAYER_KHRONOS_validation",
                                .pSettingName = "validate_best_practices",
                                .type = vk::LayerSettingTypeEXT::eBool32,
                                .valueCount = 1,
                                .pValues = &bBestPracticesValEnabled},
        };

        vk::LayerSettingsCreateInfoEXT layerSettingsInfo{
            .settingCount = static_cast<uint32_t>(settings.size()),
            .pSettings = settings.data()};

        vk::InstanceCreateInfo createInfo{
            .pNext = bEnableValidationLayers ? &layerSettingsInfo : nullptr,
#if defined(__APPLE__)
            .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#endif
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = (uint32_t)requiredLayers.size(),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = (uint32_t)requiredExtensions.size(),
            .ppEnabledExtensionNames = requiredExtensions.data()};

        m_Instance = vk::raii::Instance(m_Context, createInfo);
    }

    void SetupDebugMessenger()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "SetupDebugMessenger()");

        if (!bEnableValidationLayers)
            return;

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo);
        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);

        vk::DebugUtilsMessengerCreateInfoEXT createInfo{
            .messageSeverity = severityFlags,
            .messageType = messageTypeFlags,
            .pfnUserCallback = &DebugCallback};

        m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(createInfo);
    }

    bool IsPhysicalDeviceSuitable(const vk::raii::PhysicalDevice& device)
    {
        auto properties = device.getProperties();

        bool bSupportsVulkan13 = properties.apiVersion >= vk::ApiVersion13;

        auto queueFamilies = device.getQueueFamilyProperties();
        bool bSupportsGraphicsQ = std::ranges::any_of(
            queueFamilies, [](const auto& qfp)
            { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

        std::vector<const char*> requiredExtensions = {
            vk::KHRSwapchainExtensionName,
            vk::EXTDescriptorIndexingExtensionName};
        auto availableExtensions = device.enumerateDeviceExtensionProperties();
        bool bSupportsAllExtensions = std::ranges::all_of(
            requiredExtensions,
            [&availableExtensions](const auto& requiredExtension)
            {
                return std::ranges::any_of(
                    availableExtensions,
                    [requiredExtension](const auto& availableExtension)
                    {
                        return strcmp(availableExtension.extensionName,
                                      requiredExtension) == 0;
                    });
            });

        auto features = device.getFeatures2<
            vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        bool bSupportsAllFeatures =
            features.get<vk::PhysicalDeviceFeatures2>()
                .features.samplerAnisotropy &&
            features.get<vk::PhysicalDeviceFeatures2>()
                .features.independentBlend &&
            features.get<vk::PhysicalDeviceVulkan13Features>()
                .dynamicRendering &&
            features.get<vk::PhysicalDeviceVulkan13Features>()
                .synchronization2 &&
            features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
                .extendedDynamicState;

        if (bSupportsVulkan13 && bSupportsGraphicsQ && bSupportsAllExtensions &&
            bSupportsAllFeatures)
            return true;
        return false;
    }

    void CreateSurface()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateSurface()");

#if defined(__APPLE__)
        // SDL_Vulkan_CreateSurface can crash on macOS because SDL resolves the
        // surface-creation function pointer through its own Vulkan loader,
        // which may differ from the one this app linked (and the instance
        // belongs to). Create the Metal surface directly via our Vulkan loader
        // instead.
        SDL_MetalView metalView = SDL_Metal_CreateView(m_pWindow);
        if (!metalView)
            throw SDLException("Failed to create Metal view!");

        void* metalLayer = SDL_Metal_GetLayer(metalView);
        vk::MetalSurfaceCreateInfoEXT createInfo{.pLayer = metalLayer};

        m_Surface = m_Instance.createMetalSurfaceEXT(createInfo);
#else
        VkSurfaceKHR rawSurface;
        if (!SDL_Vulkan_CreateSurface(m_pWindow, *m_Instance, nullptr,
                                      &rawSurface))
            throw SDLException("Failed to create Vulkan surface!");

        m_Surface = vk::raii::SurfaceKHR(m_Instance, rawSurface);
#endif
    }

    void PickPhysicalDevice()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "PickPhysicalDevice()");

        auto devices = m_Instance.enumeratePhysicalDevices();
        const auto deviceIt =
            std::ranges::find_if(devices, [&](const auto& device)
                                 { return IsPhysicalDeviceSuitable(device); });

        if (deviceIt == devices.end())
            throw std::runtime_error("Failed to find a suitable GPU!");

        m_PhysicalDevice = *deviceIt;
    }

    void CreateLogicalDevice()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateLogicalDevice()");

        std::vector<vk::QueueFamilyProperties> qfProperties =
            m_PhysicalDevice.getQueueFamilyProperties();

        for (size_t qfpIndex = 0; qfpIndex < qfProperties.size(); qfpIndex++)
        {
            if ((qfProperties[qfpIndex].queueFlags &
                 vk::QueueFlagBits::eGraphics) !=
                    static_cast<vk::QueueFlags>(0) &&
                m_PhysicalDevice.getSurfaceSupportKHR(
                    static_cast<uint32_t>(qfpIndex), m_Surface))

            {
                m_QueueIndex = static_cast<uint32_t>(qfpIndex);
                break;
            }
        }

        if (m_QueueIndex == std::numeric_limits<uint32_t>::max())
            throw std::runtime_error(
                "Could not find a queue for graphics and presenting!");

        float queuePriority = 0.5f;
        vk::DeviceQueueCreateInfo queueCreateInfo{
            .queueFamilyIndex = m_QueueIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority};

        vk::StructureChain<vk::PhysicalDeviceFeatures2,
                           vk::PhysicalDeviceVulkan11Features,
                           vk::PhysicalDeviceVulkan13Features,
                           vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                           vk::PhysicalDeviceDescriptorIndexingFeaturesEXT>
            featureChain = {
                {.features = {.independentBlend = true,
                              .samplerAnisotropy = true}},
                {.shaderDrawParameters = true},
                {.synchronization2 = true, .dynamicRendering = true},
                {.extendedDynamicState = true},
                {.descriptorBindingPartiallyBound = true}};

        std::vector<const char*> requiredDeviceExtensions = {
            vk::KHRSwapchainExtensionName};

#if defined(__APPLE__)
        // MoltenVK exposes VK_KHR_portability_subset and requires it to be
        // enabled on the logical device; otherwise device creation has
        // undefined behaviour and can crash.
        requiredDeviceExtensions.push_back(
            vk::KHRPortabilitySubsetExtensionName);
#endif

        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = (uint32_t)requiredDeviceExtensions.size(),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data()};

        m_Device = vk::raii::Device(m_PhysicalDevice, deviceCreateInfo);
        SetVkDebugName(m_Device, *m_Device, vk::ObjectType::eDevice, "Device");
        m_GraphicsQueue = vk::raii::Queue(m_Device, m_QueueIndex, 0);
        SetVkDebugName(m_Device, *m_GraphicsQueue, vk::ObjectType::eQueue,
                       "Graphics Queue");

        // Setting debug names for objects which were created before the device
        // was created.
        SetVkDebugName(m_Device, *m_Instance, vk::ObjectType::eInstance,
                       "Instance");
        SetVkDebugName(m_Device, *m_PhysicalDevice,
                       vk::ObjectType::ePhysicalDevice, "Physical Device");
        SetVkDebugName(m_Device, *m_Surface, vk::ObjectType::eSurfaceKHR,
                       "Surface");
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
        m_SwapchainExtent = ChooseSwapchainExtent(capabilities, m_pWindow);

        LogMsg(LogSeverity::Info, LogRenderer, "Swapchain Extent: {}x{}",
               m_SwapchainExtent.width, m_SwapchainExtent.height);

        m_MinImageCount = ChooseSwapMinImageCount(capabilities);
        vk::SwapchainCreateInfoKHR createInfo{
            .surface = *m_Surface,
            .minImageCount = m_MinImageCount,
            .imageFormat = m_SwapchainSurfaceFormat.format,
            .imageColorSpace = m_SwapchainSurfaceFormat.colorSpace,
            .imageExtent = m_SwapchainExtent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = ChoosePresentMode(presentModes),
            .clipped = true,
            .oldSwapchain = nullptr};

        m_Swapchain = vk::raii::SwapchainKHR(m_Device, createInfo);
        SetVkDebugName(m_Device, *m_Swapchain, vk::ObjectType::eSwapchainKHR,
                       "Swapchain");

        m_SwapImages = m_Swapchain.getImages();
        for (size_t i = 0; i < m_SwapImages.size(); i++)
        {
            SetVkDebugName(m_Device, m_SwapImages[i], vk::ObjectType::eImage,
                           std::format("Swapchain Image_{}", i).c_str());
        }

        LogMsg(LogSeverity::Info, LogRenderer, "Swapchain image count: {}",
               m_SwapImages.size());
    }

    void CreateSwapchainImageViews()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateSwapchainImageViews()");

        assert(m_SwapImageViews.empty());
        for (size_t i = 0; i < m_SwapImages.size(); i++)
        {
            m_SwapImageViews.push_back(CreateImageView(
                m_Device, m_SwapImages[i], vk::ImageViewType::e2D,
                m_SwapchainSurfaceFormat.format,
                vk::ImageAspectFlagBits::eColor, 1u));
            SetVkDebugName(m_Device, *m_SwapImageViews.back(),
                           vk::ObjectType::eImageView,
                           std::format("Swapchain Image View_{}", i).c_str());
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

        std::array<vk::VertexInputBindingDescription, 2> bindingDescs = {
            vertexBindingDesc, instanceBindingDesc};
        std::array<vk::VertexInputAttributeDescription,
                   Vertex::AttributeCount + InstanceData::AttributeCount>
            attributeDescs;
        std::ranges::copy(vertexAttributeDesc, attributeDescs.begin());
        std::ranges::copy(instanceAttributeDesc,
                          attributeDescs.begin() + Vertex::AttributeCount);

        vk::PipelineColorBlendAttachmentState attachmentState{
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR |
                              vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB |
                              vk::ColorComponentFlagBits::eA};

        std::array setLayouts{*m_GlobalBufferSetLayout,
                              MaterialFactory::Get()->GetDescriptorSetLayout()};

        vk::PushConstantRange pushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .size = sizeof(PBRMaterial::MaterialData)};

        auto [opaqueLayout, opaquePipeline] =
            PipelineBuilder(m_Device)
                .Shaders("shaders/opaque.spv")
                .VertexInput(bindingDescs, attributeDescs)
                .Depth(true, true, vk::CompareOp::eLess)
                .ColorAttachments(std::array{m_OpaqueImageFormat},
                                  std::array{attachmentState})
                .DepthAttachment(m_DepthFormat)
                .Cull(vk::CullModeFlagBits::eNone, true)
                .Layout(setLayouts, std::array{pushConstantRange})
                .DebugName("Opaque")
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

        std::array<vk::VertexInputBindingDescription, 2> bindingDescs = {
            vertexBindingDesc, instanceBindingDesc};
        std::array<vk::VertexInputAttributeDescription,
                   Vertex::AttributeCount + InstanceData::AttributeCount>
            attributeDescs;
        std::ranges::copy(vertexAttributeDesc, attributeDescs.begin());
        std::ranges::copy(instanceAttributeDesc,
                          attributeDescs.begin() + Vertex::AttributeCount);

        std::array<vk::Format, 2> attachmentFormats = {m_AccumImageFormat,
                                                       m_RevealageImageFormat};

        std::array<vk::PipelineColorBlendAttachmentState, 2> attachmentStates{
            {{.blendEnable = vk::True,
              .srcColorBlendFactor = vk::BlendFactor::eOne,
              .dstColorBlendFactor = vk::BlendFactor::eOne,
              .colorBlendOp = vk::BlendOp::eAdd,
              .srcAlphaBlendFactor = vk::BlendFactor::eOne,
              .dstAlphaBlendFactor = vk::BlendFactor::eOne,
              .alphaBlendOp = vk::BlendOp::eAdd,
              .colorWriteMask = vk::ColorComponentFlagBits::eR |
                                vk::ColorComponentFlagBits::eG |
                                vk::ColorComponentFlagBits::eB |
                                vk::ColorComponentFlagBits::eA},
             {.blendEnable = vk::True,
              .srcColorBlendFactor = vk::BlendFactor::eZero,
              .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcColor,
              .colorWriteMask = vk::ColorComponentFlagBits::eR}}};

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            m_GlobalBufferSetLayout,
            MaterialFactory::Get()->GetDescriptorSetLayout()};

        vk::PushConstantRange pushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
            .size = sizeof(PBRMaterial::MaterialData)};

        auto [transparentLayout, transparentPipeline] =
            PipelineBuilder(m_Device)
                .Shaders("shaders/weightedBlendedOIT.spv")
                .VertexInput(bindingDescs, attributeDescs)
                .Depth(true, false, vk::CompareOp::eLess)
                .ColorAttachments(attachmentFormats, attachmentStates)
                .DepthAttachment(m_DepthFormat)
                .Cull(vk::CullModeFlagBits::eNone)
                .Layout(setLayouts, std::array{pushConstantRange})
                .DebugName("Transparent")
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
            .colorWriteMask = vk::ColorComponentFlagBits::eR |
                              vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB |
                              vk::ColorComponentFlagBits::eA};

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            m_GlobalBufferSetLayout, m_CompositeSetLayout};

        auto [compositeLayout, compositePipeline] =
            PipelineBuilder(m_Device)
                .Shaders("shaders/composite.spv")
                .VertexInput(bindingDescs, attributeDescs)
                .Depth(false, false, vk::CompareOp::eLess)
                .ColorAttachments(std::array{m_SwapchainSurfaceFormat.format},
                                  std::array{attachmentState})
                .DepthAttachment(vk::Format::eUndefined)
                .Cull(vk::CullModeFlagBits::eNone)
                .Layout(setLayouts, {})
                .DebugName("Composite")
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

        vk::CommandPoolCreateInfo createInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = m_QueueIndex};
        m_GenericCommandPool = vk::raii::CommandPool(m_Device, createInfo);
        SetVkDebugName(m_Device, *m_GenericCommandPool,
                       vk::ObjectType::eCommandPool, "Generic Command Pool");

        createInfo =
            vk::CommandPoolCreateInfo{.queueFamilyIndex = m_QueueIndex};
        for (size_t i = 0u; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            FrameData& frame = m_Frames[i];

            frame.DrawLayoutCommandPool =
                vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(
                m_Device, *frame.DrawLayoutCommandPool,
                vk::ObjectType::eCommandPool,
                std::format("Draw Layout Command Pool Frame {}", i).c_str());

            frame.OpaqueCommandPool =
                vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(
                m_Device, *frame.OpaqueCommandPool,
                vk::ObjectType::eCommandPool,
                std::format("Opaque Command Pool Frame {}", i).c_str());

            frame.CloudCommandPool =
                vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(
                m_Device, *frame.CloudCommandPool, vk::ObjectType::eCommandPool,
                std::format("Cloud Command Pool Frame {}", i).c_str());

            frame.TransparentCommandPool =
                vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(
                m_Device, *frame.TransparentCommandPool,
                vk::ObjectType::eCommandPool,
                std::format("Transparent Command Pool Frame {}", i).c_str());

            frame.CompositeCommandPool =
                vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(
                m_Device, *frame.CompositeCommandPool,
                vk::ObjectType::eCommandPool,
                std::format("Composite Command Pool Frame {}", i).c_str());

            frame.ImGuiCommandPool =
                vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(
                m_Device, *frame.ImGuiCommandPool, vk::ObjectType::eCommandPool,
                std::format("ImGui Command Pool Frame {}", i).c_str());

            frame.PresentLayoutCommandPool =
                vk::raii::CommandPool(m_Device, createInfo);
            SetVkDebugName(
                m_Device, *frame.PresentLayoutCommandPool,
                vk::ObjectType::eCommandPool,
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

            allocInfo = vk::CommandBufferAllocateInfo{
                .commandPool = frame.DrawLayoutCommandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1u};
            cmd = std::move(
                vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.DrawLayoutCommandBuffer = std::move(cmd);
            SetVkDebugName(
                m_Device, *frame.DrawLayoutCommandBuffer,
                vk::ObjectType::eCommandBuffer,
                std::format("Draw Layout Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{
                .commandPool = frame.OpaqueCommandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1u};
            cmd = std::move(
                vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.OpaqueCommandBuffer = std::move(cmd);
            SetVkDebugName(
                m_Device, *frame.OpaqueCommandBuffer,
                vk::ObjectType::eCommandBuffer,
                std::format("Opaque Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{
                .commandPool = frame.CloudCommandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1u};
            cmd = std::move(
                vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.CloudCommandBuffer = std::move(cmd);
            SetVkDebugName(
                m_Device, *frame.CloudCommandBuffer,
                vk::ObjectType::eCommandBuffer,
                std::format("Cloud Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{
                .commandPool = frame.TransparentCommandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1u};
            cmd = std::move(
                vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.TransparentCommandBuffer = std::move(cmd);
            SetVkDebugName(
                m_Device, *frame.TransparentCommandBuffer,
                vk::ObjectType::eCommandBuffer,
                std::format("Transparent Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{
                .commandPool = frame.CompositeCommandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1u};
            cmd = std::move(
                vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.CompositeCommandBuffer = std::move(cmd);
            SetVkDebugName(
                m_Device, *frame.CompositeCommandBuffer,
                vk::ObjectType::eCommandBuffer,
                std::format("Composite Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{
                .commandPool = frame.ImGuiCommandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1u};
            cmd = std::move(
                vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.ImGuiCommandBuffer = std::move(cmd);
            SetVkDebugName(
                m_Device, *frame.ImGuiCommandBuffer,
                vk::ObjectType::eCommandBuffer,
                std::format("ImGui Command Buffer Frame {}", i).c_str());

            allocInfo = vk::CommandBufferAllocateInfo{
                .commandPool = frame.PresentLayoutCommandPool,
                .level = vk::CommandBufferLevel::ePrimary,
                .commandBufferCount = 1u};
            cmd = std::move(
                vk::raii::CommandBuffers(m_Device, allocInfo).front());

            frame.PresentLayoutCommandBuffer = std::move(cmd);
            SetVkDebugName(
                m_Device, *frame.PresentLayoutCommandBuffer,
                vk::ObjectType::eCommandBuffer,
                std::format("Present Layout Command Buffer Frame {}", i)
                    .c_str());
        }
    }

    void RecordOpaqueCommandBuffer()
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.OpaqueCommandPool.reset();
        vk::raii::CommandBuffer& cmd = frame.OpaqueCommandBuffer;

        vk::CommandBufferBeginInfo beginInfo{};
        cmd.begin(beginInfo);

        RecordImageBarrier(cmd, frame.DepthTexture.GetImage(),
                           Barriers::UndefinedToDepthAttachment());
        RecordImageBarrier(cmd, frame.OpaqueTexture.GetImage(),
                           Barriers::UndefinedToColorAttachment());

        vk::ClearValue clearColor =
            vk::ClearColorValue(SKY_COLOR.r, SKY_COLOR.g, SKY_COLOR.b, 1.f);
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.f, 0);
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = frame.OpaqueTexture.GetImageView(),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor};
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = frame.DepthTexture.GetImageView(),
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

        cmd.setViewport(
            0, vk::Viewport(
                   0.f, 0.f, static_cast<float>(m_SwapchainExtent.width),
                   static_cast<float>(m_SwapchainExtent.height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapchainExtent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               m_OpaquePipelineLayout, 0,
                               *frame.GlobalBufferDescriptorSet, nullptr);

        cmd.bindVertexBuffers(1, frame.InstanceBuffer.Buffer, {0});

        vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;

        // per mesh batch
        const std::vector<MeshBatch>& batches =
            ModelManager::Get()->GetOpaqueBatches();
        for (const MeshBatch& batch : batches)
        {
            vk::CullModeFlags requiredCullMode =
                batch.pMaterial->IsTwoSided() ? vk::CullModeFlagBits::eNone
                                              : vk::CullModeFlagBits::eBack;

            if (requiredCullMode != cullMode)
            {
                cmd.setCullMode(requiredCullMode);
                cullMode = requiredCullMode;
            }

            cmd.bindVertexBuffers(0, batch.VertexBuffer, {0});
            cmd.bindIndexBuffer(batch.IndexBuffer, 0, vk::IndexType::eUint32);
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, m_OpaquePipelineLayout, 1,
                batch.pMaterial->GetDescriptorSet(), nullptr);
            cmd.pushConstants<PBRMaterial::MaterialData>(
                m_OpaquePipelineLayout, vk::ShaderStageFlagBits::eFragment, 0u,
                *static_cast<PBRMaterial::MaterialData*>(
                    batch.pMaterial->GetPushConstantData()));
            cmd.drawIndexed(batch.IndexCount, batch.InstanceCount,
                            batch.FirstIndex, 0, batch.FirstInstance);
        }

        cmd.endRendering();

        cmd.end();
    }

    void RecordCloudsCommandBuffer()
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.CloudCommandPool.reset();

        m_CloudSystem->RecordDispatch(frame.CloudCommandBuffer, m_FrameIndex,
                                      frame.GlobalBufferDescriptorSet,
                                      frame.DepthBufferDescriptorSet);
    }

    void RecordTransparentCommandBuffer()
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.TransparentCommandPool.reset();
        vk::raii::CommandBuffer& cmd = frame.TransparentCommandBuffer;

        vk::CommandBufferBeginInfo beginInfo{};
        cmd.begin(beginInfo);

        RecordImageBarrier(cmd, frame.AccumTexture.GetImage(),
                           Barriers::UndefinedToColorAttachment());
        RecordImageBarrier(cmd, frame.RevealageTexture.GetImage(),
                           Barriers::UndefinedToColorAttachment());
        RecordImageBarrier(cmd, frame.DepthTexture.GetImage(),
                           Barriers::DepthAttachmentToShaderRead());

        vk::ClearValue accumClearColor =
            vk::ClearColorValue(0.f, 0.f, 0.f, 0.f);
        vk::ClearValue revealageClearColor =
            vk::ClearColorValue(1.f, 0.f, 0.f, 0.f);
        std::array<vk::RenderingAttachmentInfo, 2> colorAttachmentInfos = {
            {{.imageView = frame.AccumTexture.GetImageView(),
              .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
              .loadOp = vk::AttachmentLoadOp::eClear,
              .storeOp = vk::AttachmentStoreOp::eStore,
              .clearValue = accumClearColor},
             {.imageView = frame.RevealageTexture.GetImageView(),
              .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
              .loadOp = vk::AttachmentLoadOp::eClear,
              .storeOp = vk::AttachmentStoreOp::eStore,
              .clearValue = revealageClearColor}}};
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = frame.DepthTexture.GetImageView(),
            .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eNone};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = m_SwapchainExtent},
            .layerCount = 1,
            .colorAttachmentCount =
                static_cast<uint32_t>(colorAttachmentInfos.size()),
            .pColorAttachments = colorAttachmentInfos.data(),
            .pDepthAttachment = &depthAttachmentInfo};

        cmd.beginRendering(renderingInfo);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         *m_TransparentPipeline);

        cmd.setViewport(
            0, vk::Viewport(
                   0.f, 0.f, static_cast<float>(m_SwapchainExtent.width),
                   static_cast<float>(m_SwapchainExtent.height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapchainExtent));
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               m_TransparentPipelineLayout, 0,
                               *frame.GlobalBufferDescriptorSet, nullptr);

        cmd.bindVertexBuffers(1, frame.InstanceBuffer.Buffer, {0});

        // per mesh batch
        const std::vector<MeshBatch>& batches =
            ModelManager::Get()->GetTransparentBatches();
        for (const MeshBatch& batch : batches)
        {
            cmd.bindVertexBuffers(0, batch.VertexBuffer, {0});
            cmd.bindIndexBuffer(batch.IndexBuffer, 0, vk::IndexType::eUint32);
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, m_TransparentPipelineLayout,
                1, batch.pMaterial->GetDescriptorSet(), nullptr);
            cmd.pushConstants<PBRMaterial::MaterialData>(
                m_TransparentPipelineLayout, vk::ShaderStageFlagBits::eFragment,
                0u,
                *static_cast<PBRMaterial::MaterialData*>(
                    batch.pMaterial->GetPushConstantData()));
            cmd.drawIndexed(batch.IndexCount, batch.InstanceCount,
                            batch.FirstIndex, 0, batch.FirstInstance);
        }

        cmd.endRendering();

        cmd.end();
    }

    void RecordCompositeCommandBuffer(uint32_t imageIndex)
    {
        FrameData& frame = m_Frames[m_FrameIndex];
        frame.CompositeCommandPool.reset();
        vk::raii::CommandBuffer& cmd = frame.CompositeCommandBuffer;

        vk::CommandBufferBeginInfo beginInfo{};
        cmd.begin(beginInfo);

        RecordImageBarrier(cmd, frame.OpaqueTexture.GetImage(),
                           Barriers::ColorAttachmentToShaderRead());
        RecordImageBarrier(cmd, frame.AccumTexture.GetImage(),
                           Barriers::ColorAttachmentToShaderRead());
        RecordImageBarrier(cmd, frame.RevealageTexture.GetImage(),
                           Barriers::ColorAttachmentToShaderRead());

        vk::ClearValue clearColor = vk::ClearColorValue(0.f, 0.f, 0.f, 1.f);
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = m_SwapImageViews[imageIndex],
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
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         *m_CompositePipeline);

        cmd.setViewport(
            0, vk::Viewport(
                   0.f, 0.f, static_cast<float>(m_SwapchainExtent.width),
                   static_cast<float>(m_SwapchainExtent.height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapchainExtent));

        std::array descriptorSets = {*frame.GlobalBufferDescriptorSet,
                                     *frame.CompositeDescriptorSet};
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               m_CompositePipelineLayout, 0u, descriptorSets,
                               nullptr);

        cmd.bindVertexBuffers(0u, m_QuadVertexBuffer.Buffer, {0});
        cmd.bindIndexBuffer(m_QuadIndexBuffer.Buffer, 0u,
                            vk::IndexType::eUint32);

        constexpr uint32_t QUAD_INDEX_COUNT = 6u;
        cmd.drawIndexed(QUAD_INDEX_COUNT, 1u, 0u, 0, 0u);

        cmd.endRendering();

        cmd.end();
    }

    void RecordImGui(uint32_t imageIndex)
    {
        m_Frames[m_FrameIndex].ImGuiCommandPool.reset();
        vk::raii::CommandBuffer& cmd =
            m_Frames[m_FrameIndex].ImGuiCommandBuffer;
        vk::CommandBufferBeginInfo beginInfo{};
        cmd.begin(beginInfo);

        constexpr ImageBarrierDesc barrierDesc{
            .srcStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccess = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStage = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccess = vk::AccessFlagBits2::eColorAttachmentRead,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .aspect = vk::ImageAspectFlagBits::eColor};
        RecordImageBarrier(cmd, m_SwapImages[imageIndex], barrierDesc);

        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = m_SwapImageViews[imageIndex],
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
        cmd.end();
    }

    void RecordSwapImageToDrawLayout(uint32_t imageIndex)
    {
        m_Frames[m_FrameIndex].DrawLayoutCommandPool.reset();
        vk::raii::CommandBuffer& cmd =
            m_Frames[m_FrameIndex].DrawLayoutCommandBuffer;
        vk::CommandBufferBeginInfo beginInfo{};
        cmd.begin(beginInfo);
        RecordImageBarrier(cmd, m_SwapImages[imageIndex],
                           Barriers::AcquiredSwapchainToColorAttachment());
        cmd.end();
    }

    void RecordSwapImageToPresentLayout(uint32_t imageIndex)
    {
        m_Frames[m_FrameIndex].PresentLayoutCommandPool.reset();
        vk::raii::CommandBuffer& cmd =
            m_Frames[m_FrameIndex].PresentLayoutCommandBuffer;
        vk::CommandBufferBeginInfo beginInfo{};
        cmd.begin(beginInfo);
        RecordImageBarrier(cmd, m_SwapImages[imageIndex],
                           Barriers::ColorAttachmentToPresent());
        cmd.end();
    }

    void CreateSyncObjects()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateSyncObjects()");

        for (size_t i = 0; i < m_SwapImages.size(); i++)
        {
            m_RenderCompleteSemaphores.emplace_back(
                vk::raii::Semaphore(m_Device, vk::SemaphoreCreateInfo()));
            SetVkDebugName(
                m_Device, *m_RenderCompleteSemaphores.back(),
                vk::ObjectType::eSemaphore,
                std::format("Render Complete Semaphore_{}", i).c_str());
        }

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].PresentCompleteSemaphore =
                vk::raii::Semaphore(m_Device, vk::SemaphoreCreateInfo());
            SetVkDebugName(
                m_Device, *m_Frames[i].PresentCompleteSemaphore,
                vk::ObjectType::eSemaphore,
                std::format("Present Complete Semaphore_{}", i).c_str());

            m_Frames[i].DrawFence = vk::raii::Fence(
                m_Device, {.flags = vk::FenceCreateFlagBits::eSignaled});
            SetVkDebugName(m_Device, *m_Frames[i].DrawFence,
                           vk::ObjectType::eFence,
                           std::format("Draw Fence_{}", i).c_str());
        }
    }

    void RecreateSwapchainAndRenderImages()
    {
        LogMsg(LogSeverity::Info, LogRenderer,
               "Recreating swapchain and render images...");

        int width, height;
        SDL_GetWindowSizeInPixels(m_pWindow, &width, &height);
        while (width == 0 || height == 0)
        {
            SDL_GetWindowSizeInPixels(m_pWindow, &width, &height);
            SDL_Event event;
            SDL_WaitEvent(&event);
        }

        m_Device.waitIdle();

        m_SwapImageViews.clear();
        m_Swapchain = nullptr;

        m_DepthFormat = vk::Format::eUndefined;

        m_Device.waitIdle();

        CreateSwapchain();
        CreateSwapchainImageViews();

        CreateDepthResources();
        CreateRenderTargets();

        m_CloudSystem->Resize(m_SwapchainExtent.width,
                              m_SwapchainExtent.height);
        UpdateDepthDescriptorSets();
        UpdateCompositeDescriptorSet();

        m_Camera->SetProjection(
            m_Camera->GetFOV(),
            static_cast<float>(m_SwapchainExtent.width) /
                static_cast<float>(m_SwapchainExtent.height),
            m_Camera->GetNearPlane(), m_Camera->GetFarPlane());

        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{
            .colorAttachmentCount = 1u,
            .pColorAttachmentFormats = &m_SwapchainSurfaceFormat.format};
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
            vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment |
                vk::ShaderStageFlagBits::eCompute,
            nullptr)};
        vk::DescriptorSetLayoutCreateInfo frameCreateInfo{
            .bindingCount = static_cast<uint32_t>(frameBindings.size()),
            .pBindings = frameBindings.data()};
        m_GlobalBufferSetLayout =
            vk::raii::DescriptorSetLayout(m_Device, frameCreateInfo);
        SetVkDebugName(m_Device, *m_GlobalBufferSetLayout,
                       vk::ObjectType::eDescriptorSetLayout,
                       "Frame Uniform Buffer Descriptor Set Layout");

        std::array compositeBindings = {
            vk::DescriptorSetLayoutBinding(
                0u, vk::DescriptorType::eSampledImage, 1u,
                vk::ShaderStageFlagBits::eFragment, nullptr),
            vk::DescriptorSetLayoutBinding(
                1u, vk::DescriptorType::eSampledImage, 1u,
                vk::ShaderStageFlagBits::eFragment, nullptr),
            vk::DescriptorSetLayoutBinding(
                2u, vk::DescriptorType::eSampledImage, 1u,
                vk::ShaderStageFlagBits::eFragment, nullptr),
            vk::DescriptorSetLayoutBinding(
                3u, vk::DescriptorType::eCombinedImageSampler, 1u,
                vk::ShaderStageFlagBits::eFragment, nullptr)};
        vk::DescriptorSetLayoutCreateInfo compositeCreateInfo{
            .bindingCount = static_cast<uint32_t>(compositeBindings.size()),
            .pBindings = compositeBindings.data()};
        m_CompositeSetLayout =
            vk::raii::DescriptorSetLayout(m_Device, compositeCreateInfo);
        SetVkDebugName(m_Device, *m_CompositeSetLayout,
                       vk::ObjectType::eDescriptorSetLayout,
                       "Composite Descriptor Set Layout");

        std::array depthBindings = {vk::DescriptorSetLayoutBinding(
            0u, vk::DescriptorType::eSampledImage, 1u,
            vk::ShaderStageFlagBits::eFragment |
                vk::ShaderStageFlagBits::eCompute,
            nullptr)};
        vk::DescriptorSetLayoutCreateInfo depthCreateInfo{
            .bindingCount = static_cast<uint32_t>(depthBindings.size()),
            .pBindings = depthBindings.data()};
        m_DepthSetLayout =
            vk::raii::DescriptorSetLayout(m_Device, depthCreateInfo);
        SetVkDebugName(m_Device, *m_DepthSetLayout,
                       vk::ObjectType::eDescriptorSetLayout,
                       "Depth Descriptor Set Layout");
    }

    void CreateGlobalBuffers()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateGlobalBuffers()");

        vk::DeviceSize size = sizeof(GlobalBuffer);
        if (size % 16 != 0)
            throw std::runtime_error(std::format(
                "Buffer must be 16 byte aligned! Size is {}", size));

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            AllocatedBuffer globalBuffer = CreateBuffer(
                m_VmaAllocator, size, vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            SetVkDebugName(m_Device, globalBuffer.Buffer,
                           vk::ObjectType::eBuffer,
                           std::format("Global Buffer Frame {}", i).c_str());
            vmaSetAllocationName(
                m_VmaAllocator, globalBuffer.Allocation,
                std::format("Global Buffer Memory Frame {}", i).c_str());

            m_Frames[i].GlobalBuffer = std::move(globalBuffer);
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
        proj[1][1] *= -1.f;
        m_GlobalBuffer.CamData.Proj = glm::transpose(proj);
        m_GlobalBuffer.CamData.NearPlane = m_Camera->GetNearPlane();
        m_GlobalBuffer.CamData.FarPlane = m_Camera->GetFarPlane();

        m_GlobalBuffer.CamData.InvViewProj =
            glm::inverse(glm::transpose(m_GlobalBuffer.CamData.Proj) * view);

        uint32_t& pointLightCount = m_GlobalBuffer.Lights.PointLightCount;
        for (pointLightCount = 0u;
             pointLightCount <
             std::min(static_cast<uint32_t>(m_SceneGraph->PointLights.size()),
                      static_cast<uint32_t>(MAX_POINT_LIGHTS));
             pointLightCount++)
        {
            m_GlobalBuffer.Lights.PointLights[pointLightCount] =
                m_SceneGraph->PointLights[pointLightCount]->GetData();
        }

        uint32_t& dirLightCount = m_GlobalBuffer.Lights.DirLightCount;
        for (dirLightCount = 0u;
             dirLightCount <
             std::min(static_cast<uint32_t>(m_SceneGraph->DirLights.size()),
                      static_cast<uint32_t>(MAX_DIR_LIGHTS));
             dirLightCount++)
        {
            m_GlobalBuffer.Lights.DirLights[dirLightCount] =
                m_SceneGraph->DirLights[dirLightCount]->GetData();
        }

        memcpy(m_Frames[frameIndex].GlobalBuffer.AllocationInfo.pMappedData,
               &m_GlobalBuffer, sizeof(m_GlobalBuffer));
    }

    void CreateDescriptorPool()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateDescriptorPool()");

        std::array framePoolSize = {
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                                   .descriptorCount = NUM_FRAMES_IN_FLIGHT}};
        vk::DescriptorPoolCreateInfo frameCreateInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = NUM_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(framePoolSize.size()),
            .pPoolSizes = framePoolSize.data()};

        m_FrameDescriptorPool =
            vk::raii::DescriptorPool(m_Device, frameCreateInfo);
        SetVkDebugName(m_Device, *m_FrameDescriptorPool,
                       vk::ObjectType::eDescriptorPool,
                       "Frame Descriptor Pool");

        std::array compositePoolSize = {
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage,
                                   .descriptorCount = NUM_FRAMES_IN_FLIGHT * 3},
            vk::DescriptorPoolSize{
                .type = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = NUM_FRAMES_IN_FLIGHT * 1}};
        vk::DescriptorPoolCreateInfo compCreateInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = NUM_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(compositePoolSize.size()),
            .pPoolSizes = compositePoolSize.data()};

        m_CompositeDescriptorPool =
            vk::raii::DescriptorPool(m_Device, compCreateInfo);
        SetVkDebugName(m_Device, *m_CompositeDescriptorPool,
                       vk::ObjectType::eDescriptorPool,
                       "Composite Descriptor Pool");

        std::array genericPoolSize = {
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage,
                                   .descriptorCount = NUM_FRAMES_IN_FLIGHT}};
        vk::DescriptorPoolCreateInfo genericCreateInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = NUM_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(genericPoolSize.size()),
            .pPoolSizes = genericPoolSize.data()};

        m_GenericDescriptorPool =
            vk::raii::DescriptorPool(m_Device, genericCreateInfo);
        SetVkDebugName(m_Device, *m_GenericDescriptorPool,
                       vk::ObjectType::eDescriptorPool,
                       "Generic Descriptor Pool");
    }

    void CreateDescriptorSets()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateDescriptorSets()");

        std::vector<vk::DescriptorSetLayout> globalBufferLayouts(
            NUM_FRAMES_IN_FLIGHT, *m_GlobalBufferSetLayout);
        vk::DescriptorSetAllocateInfo globalBufferAllocInfo{
            .descriptorPool = *m_FrameDescriptorPool,
            .descriptorSetCount =
                static_cast<uint32_t>(globalBufferLayouts.size()),
            .pSetLayouts = globalBufferLayouts.data()};
        std::vector<vk::raii::DescriptorSet> uniformDescriptorSets =
            m_Device.allocateDescriptorSets(globalBufferAllocInfo);

        std::vector<vk::DescriptorSetLayout> compSetLayouts(
            NUM_FRAMES_IN_FLIGHT, *m_CompositeSetLayout);
        vk::DescriptorSetAllocateInfo compAllocInfo{
            .descriptorPool = m_CompositeDescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(compSetLayouts.size()),
            .pSetLayouts = compSetLayouts.data()};
        std::vector<vk::raii::DescriptorSet> compositeDescriptorSets =
            m_Device.allocateDescriptorSets(compAllocInfo);

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            FrameData& frame = m_Frames[i];

            frame.GlobalBufferDescriptorSet =
                std::move(uniformDescriptorSets[i]);
            SetVkDebugName(
                m_Device, *frame.GlobalBufferDescriptorSet,
                vk::ObjectType::eDescriptorSet,
                std::format("Main Descriptor Set Frame {}", i).c_str());

            vk::DescriptorBufferInfo bufferInfo{.buffer =
                                                    frame.GlobalBuffer.Buffer,
                                                .offset = 0,
                                                .range = sizeof(GlobalBuffer)};

            std::array globalDescriptorWrites = {vk::WriteDescriptorSet{
                .dstSet = frame.GlobalBufferDescriptorSet,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &bufferInfo}};

            m_Device.updateDescriptorSets(globalDescriptorWrites, {});

            frame.CompositeDescriptorSet =
                std::move(compositeDescriptorSets[i]);
            SetVkDebugName(
                m_Device, *frame.CompositeDescriptorSet,
                vk::ObjectType::eDescriptorSet,
                std::format("Composite Descriptor Set Frame {}", i).c_str());
        }

        UpdateCompositeDescriptorSet();

        std::vector<vk::DescriptorSetLayout> depthBufferSetLayouts(
            1, *m_DepthSetLayout);
        vk::DescriptorSetAllocateInfo depthAllocInfo{
            .descriptorPool = m_GenericDescriptorPool,
            .descriptorSetCount =
                static_cast<uint32_t>(depthBufferSetLayouts.size()),
            .pSetLayouts = depthBufferSetLayouts.data()};

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].DepthBufferDescriptorSet = std::move(
                m_Device.allocateDescriptorSets(depthAllocInfo).front());
            SetVkDebugName(m_Device, *m_Frames[i].DepthBufferDescriptorSet,
                           vk::ObjectType::eDescriptorSet,
                           "Depth Buffer Descriptor Set");
        }

        UpdateDepthDescriptorSets();
    }

    void CreateTextureSampler()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateTextureSampler()");

        float maxAnisotropy =
            m_PhysicalDevice.getProperties().limits.maxSamplerAnisotropy;
        vk::SamplerCreateInfo createInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .mipLodBias = 0.f,
            .anisotropyEnable = vk::True,
            .maxAnisotropy = maxAnisotropy,
            .compareEnable = vk::False,
            .compareOp = vk::CompareOp::eAlways,
            .minLod = 0.f,
            .maxLod = 0.f,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False};
        m_TextureSampler = vk::raii::Sampler(m_Device, createInfo);
        SetVkDebugName(m_Device, *m_TextureSampler, vk::ObjectType::eSampler,
                       "Texture Sampler");
    }

    void CreateDepthResources()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateDepthResources()");

        m_DepthFormat = FindDepthFormat();
        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].DepthTexture = CreateRenderTexture(
                m_VmaAllocator, m_Device, m_SwapchainExtent.width,
                m_SwapchainExtent.height, m_DepthFormat,
                vk::ImageUsageFlagBits::eDepthStencilAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::ImageAspectFlagBits::eDepth,
                std::format("Frame_{} Depth Image", i).c_str());
        }
    }

    vk::Format FindSupportedFormat(const std::vector<vk::Format>& candidates,
                                   vk::ImageTiling tiling,
                                   vk::FormatFeatureFlags features)
    {
        for (const vk::Format format : candidates)
        {
            vk::FormatProperties properties =
                m_PhysicalDevice.getFormatProperties(format);

            if (tiling == vk::ImageTiling::eLinear &&
                (properties.linearTilingFeatures & features) == features)
                return format;
            if (tiling == vk::ImageTiling::eOptimal &&
                (properties.optimalTilingFeatures & features) == features)
                return format;
        }
        throw std::runtime_error("Failed to find a supported format!");
    }

    vk::Format FindDepthFormat()
    {
        return FindSupportedFormat(
            {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
             vk::Format::eD24UnormS8Uint, vk::Format::eD16UnormS8Uint},
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    }

    bool HasStencilComponent(vk::Format format)
    {
        return format == vk::Format::eD32SfloatS8Uint ||
               format == vk::Format::eD24UnormS8Uint ||
               format == vk::Format::eD16UnormS8Uint;
    }

    void CreateInstanceBuffers()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateInstanceBuffers()");

        // TODO: allocating memory 3 times, can probably allocate once and
        // store offsets Can do the same with uniform buffer.
        vk::DeviceSize size = sizeof(InstanceData) * MAX_INSTANCE_COUNT;
        for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            AllocatedBuffer buffer = CreateBuffer(
                m_VmaAllocator, size, vk::BufferUsageFlagBits::eVertexBuffer,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            SetVkDebugName(m_Device, buffer.Buffer, vk::ObjectType::eBuffer,
                           std::format("Instance Buffer Frame {}", i).c_str());
            vmaSetAllocationName(
                m_VmaAllocator, buffer.Allocation,
                std::format("Instance Buffer Memory Frame {}", i).c_str());

            m_Frames[i].InstanceBuffer = std::move(buffer);
        }
    }

    void UpdateInstanceBuffer(uint32_t frameIndex)
    {
        const std::vector<InstanceData>& instanceDatas =
            ModelManager::Get()->GetInstanceDatas();

        if (instanceDatas.size() > MAX_INSTANCE_COUNT)
            throw std::runtime_error("Max instance count exceeded!");

        memcpy(m_Frames[frameIndex].InstanceBuffer.AllocationInfo.pMappedData,
               instanceDatas.data(),
               sizeof(InstanceData) * instanceDatas.size());
    }

    void CreateRenderTargets()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateRenderTargets()");
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment |
                                    vk::ImageUsageFlagBits::eSampled;
        vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            Texture opaqueTex = CreateRenderTexture(
                m_VmaAllocator, m_Device, m_SwapchainExtent.width,
                m_SwapchainExtent.height, m_OpaqueImageFormat, usage, aspect,
                std::format("Frame_{} Opaque Image", i).c_str());
            m_Frames[i].OpaqueTexture = std::move(opaqueTex);

            Texture accumTex = CreateRenderTexture(
                m_VmaAllocator, m_Device, m_SwapchainExtent.width,
                m_SwapchainExtent.height, m_AccumImageFormat, usage, aspect,
                std::format("Frame_{} Accum Image", i).c_str());
            m_Frames[i].AccumTexture = std::move(accumTex);

            Texture revealageTex = CreateRenderTexture(
                m_VmaAllocator, m_Device, m_SwapchainExtent.width,
                m_SwapchainExtent.height, m_RevealageImageFormat, usage, aspect,
                std::format("Frame_{} Revealage Image", i).c_str());
            m_Frames[i].RevealageTexture = std::move(revealageTex);
        }
    }

    void CreateQuadBuffers()
    {
        LogMsg(LogSeverity::Info, LogRenderer, "CreateQuadBuffers()");

        std::array<QuadVertex, 4> vertices = {
            {{.Pos = {-1.f, -1.f}, .TexCoord{0.f, 0.f}},
             {.Pos = {-1.f, 1.f}, .TexCoord{0.f, 1.f}},
             {.Pos = {1.f, 1.f}, .TexCoord{1.f, 1.f}},
             {.Pos = {1.f, -1.f}, .TexCoord{1.f, 0.f}}}};

        assert(vertices.size() == 4);

        m_QuadVertexBuffer = CreateStagedBuffer(
            m_VmaAllocator, m_Device, m_GenericCommandPool, m_GraphicsQueue,
            sizeof(vertices[0]) * vertices.size(),
            vk::BufferUsageFlagBits::eVertexBuffer, vertices.data());

        std::array<uint32_t, 6> indices = {0, 1, 2, 0, 2, 3};

        m_QuadIndexBuffer = CreateStagedBuffer(
            m_VmaAllocator, m_Device, m_GenericCommandPool, m_GraphicsQueue,
            sizeof(indices[0]) * indices.size(),
            vk::BufferUsageFlagBits::eIndexBuffer, indices.data());
    }

    void UpdateCompositeDescriptorSet()
    {
        LogMsg(LogSeverity::Info, LogRenderer,
               "UpdateCompositeDescriptorSet()");

        for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            FrameData& frame = m_Frames[i];
            vk::DescriptorImageInfo opaqueImageInfo{
                .imageView = frame.OpaqueTexture.GetImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo accumImageInfo{
                .imageView = frame.AccumTexture.GetImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo revealageImageInfo{
                .imageView = frame.RevealageTexture.GetImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo cloudsImageInfo{
                .sampler = m_TextureSampler,
                .imageView =
                    m_CloudSystem->GetImageView(static_cast<uint8_t>(i)),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

            std::array compDescriptorWrites = {
                vk::WriteDescriptorSet{.dstSet = frame.CompositeDescriptorSet,
                                       .dstBinding = 0u,
                                       .dstArrayElement = 0u,
                                       .descriptorCount = 1u,
                                       .descriptorType =
                                           vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &opaqueImageInfo},
                vk::WriteDescriptorSet{.dstSet = frame.CompositeDescriptorSet,
                                       .dstBinding = 1u,
                                       .dstArrayElement = 0u,
                                       .descriptorCount = 1u,
                                       .descriptorType =
                                           vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &accumImageInfo},
                vk::WriteDescriptorSet{.dstSet = frame.CompositeDescriptorSet,
                                       .dstBinding = 2u,
                                       .dstArrayElement = 0u,
                                       .descriptorCount = 1u,
                                       .descriptorType =
                                           vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &revealageImageInfo},
                vk::WriteDescriptorSet{
                    .dstSet = frame.CompositeDescriptorSet,
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
                .imageView = m_Frames[i].DepthTexture.GetImageView(),
                .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal};

            std::array depthDescriptorWrites = {vk::WriteDescriptorSet{
                .dstSet = m_Frames[i].DepthBufferDescriptorSet,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &imageInfo}};

            m_Device.updateDescriptorSets(depthDescriptorWrites, {});
        }
    }

private:
    vk::raii::Context m_Context;
    vk::raii::Instance m_Instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;
    vk::raii::SurfaceKHR m_Surface = nullptr;
    vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
    vk::raii::Device m_Device = nullptr;

    // must be destroyed before the device and after all resources that had
    // memory allocated using this allocator
    VulkanAllocator m_VmaAllocator{};

    vk::raii::Queue m_GraphicsQueue = nullptr;
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
    vk::raii::Sampler m_TextureSampler = nullptr;
    vk::raii::DescriptorPool m_FrameDescriptorPool = nullptr;
    vk::raii::DescriptorPool m_CompositeDescriptorPool = nullptr;
    vk::raii::DescriptorPool m_GenericDescriptorPool = nullptr;
    vk::Format m_DepthFormat = vk::Format::eUndefined;
    static constexpr vk::Format m_OpaqueImageFormat =
        vk::Format::eR16G16B16A16Sfloat;
    static constexpr vk::Format m_AccumImageFormat =
        vk::Format::eR16G16B16A16Sfloat;
    static constexpr vk::Format m_RevealageImageFormat = vk::Format::eR8Unorm;
    AllocatedBuffer m_QuadVertexBuffer;
    AllocatedBuffer m_QuadIndexBuffer;

    vk::SurfaceFormatKHR m_SwapchainSurfaceFormat;
    vk::Extent2D m_SwapchainExtent;
    std::vector<vk::Image> m_SwapImages;
    std::vector<vk::raii::ImageView> m_SwapImageViews;
    uint32_t m_QueueIndex = ~0;
    uint32_t m_MinImageCount = 0;

    std::array<FrameData, NUM_FRAMES_IN_FLIGHT> m_Frames;
    std::vector<vk::raii::Semaphore> m_RenderCompleteSemaphores;

    std::unique_ptr<SceneGraph> m_SceneGraph = nullptr;

    std::unique_ptr<Camera> m_Camera = nullptr;
    Cubemap* m_pSkybox = nullptr;
    std::unique_ptr<CloudSystem> m_CloudSystem = nullptr;

    static constexpr uint32_t m_APIVersion = VK_API_VERSION_1_4;
    uint32_t m_FrameIndex = 0;
    SDL_Window* m_pWindow = nullptr;
    bool m_bIsFocused = true;
    bool m_bCursorVisible = true;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_LastTime;
    float m_RunTime = 0.f;
    float m_DeltaTime = 0.f;
    float m_DisplayFrameTime = 0.f;
    float m_DisplayFPS = 0.f;
    bool m_bShutdown = false;
};

int main()
{
#ifdef _WIN32
    EnableAnsiColors();
#endif
    std::signal(SIGINT, HandleSIGINT);

    Log::g_MinSeverity = LogSeverity::Info;

    // will be destroyed in reverse order of declaration
    std::unique_ptr<SDL_Window, decltype(&ShutdownSDL)> pWindow(nullptr,
                                                                &ShutdownSDL);
    std::unique_ptr<App> pApp = nullptr;

    try
    {
        InitSDL();
        pWindow.reset(CreateSDLWindow());

        pApp = std::make_unique<App>(pWindow.get());
        pApp->Run();
    }
    catch (const SDLException& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL error: %s", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Error", e.what(),
                                 nullptr);
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
    pWindow.reset();

    LogMsg(LogSeverity::Info, LogMain, "Exiting gracefully...");
    return EXIT_SUCCESS;
}
