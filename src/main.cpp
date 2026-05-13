#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

#include "SDL3/SDL.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

#include "FrameData.h"
#include "Lights.h"
#include "Model.h"
#include "Utility.h"
#include "Vertex.h"
#include "Texture.h"

constexpr uint32_t WIDTH = 1920;
constexpr uint32_t HEIGHT = 1080;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;
const std::string MODEL_PATH = "models/sphere/scene.gltf";
const std::string TEXTURE_PATH = "models/viking_room/textures/viking_room.png";
constexpr uint32_t INSTANCES_PER_SIDE = 5;
constexpr uint32_t INSTANCE_COUNT = INSTANCES_PER_SIDE * INSTANCES_PER_SIDE;

std::atomic<bool> gbShouldClose = false;

void HandleSIGINT(int)
{
    gbShouldClose = true;
    std::cout << "\n";
}

// Each member must start at an offset that is a multiple of its base alignment.
// Eg. a float can start on offset 0, 4, 8 or 12.
// glm::vec3 is 12 bytes wide by default but is 16 byte aligned.
struct UniformBufferObject
{
    glm::mat4 Models[INSTANCE_COUNT];
    glm::mat4 View;
    glm::mat4 Proj;
    PointLight Light;
    glm::vec3 SphereColor;
    float Time;
};

std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool bEnableValidationLayers = false;
#else
constexpr bool bEnableValidationLayers = true;
#endif

vk::DebugUtilsMessageSeverityFlagBitsEXT validationSeverityThreshold =
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;

static VKAPI_ATTR vk::Bool32 VKAPI_CALL
DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
              vk::DebugUtilsMessageTypeFlagsEXT type,
              const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData)
{
    if (severity < validationSeverityThreshold)
        return vk::False;

    std::cerr << "Validation layer: type " << to_string(type)
              << " msg: " << pCallbackData->pMessage << std::endl;

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

    std::cout << "SDL video driver: " << SDL_GetCurrentVideoDriver() << "\n";

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
        SDL_DestroyWindow(pWindow);

    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

class App
{
public:
    App() {}
    App(SDL_Window* pWindow) : m_pWindow(pWindow) {}
    ~App() {}

    void Run()
    {
        Init();

        SDL_ShowWindow(m_pWindow);

        while (!gbShouldClose)
        {
            if (!m_bIsFocused)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);

                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                    gbShouldClose = true;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    RecreateSwapchainAndDepthStencil();
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    m_bIsFocused = true;
                    std::cout << "Focus gained.\n";
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    m_bIsFocused = false;
                    std::cout << "Focus lost.\n";
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE)
                        gbShouldClose = true;
                    break;
                }
            }

            DrawImGuiFrame();
            DrawFrame();
        }

        m_Device.waitIdle();
        Shutdown();
    }

private:
    void Init()
    {
        m_StartTime = std::chrono::high_resolution_clock::now();

        InitVulkan();
        InitImGui();
        std::cout << "Init() succeeded.\n";
    }

    void InitImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForVulkan(m_pWindow);

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = m_APIVersion;
        initInfo.Instance = *m_Instance;
        initInfo.PhysicalDevice = *m_PhysicalDevice;
        initInfo.Device = *m_Device;
        initInfo.QueueFamily = m_QueueIndex;
        initInfo.Queue = *m_GraphicsQueue;
        initInfo.DescriptorPool = VK_NULL_HANDLE;
        initInfo.DescriptorPoolSize = 1000;
        initInfo.MinImageCount = m_MinImageCount;
        initInfo.ImageCount = static_cast<uint32_t>(m_SwapImages.size());
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineCache = VK_NULL_HANDLE;
        initInfo.PipelineInfoMain = {.RenderPass = VK_NULL_HANDLE,
                                     .Subpass = 0,
                                     .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
                                     .ExtraDynamicStates = {},
                                     .PipelineRenderingCreateInfo =
                                         m_PipelineRenderingCreateInfo};
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = nullptr;
        ImGui_ImplVulkan_Init(&initInfo);
    }

    void InitVulkan()
    {
        CreateInstance();
        SetupDebugMessenger();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateSwapchain();
        CreateSwapchainImageViews();
        CreateDepthResources();
        CreateDescriptorSetLayout();
        CreateGraphicsPipeline();
        CreateCommandPool();
        CreateTextureImage();
        CreateTextureImageView();
        CreateTextureSampler();
        CreateCommandBuffers();
        LoadModel();
        CreateUniformBuffers();
        CreateDescriptorPool();
        CreateDescriptorSets();
        CreateSyncObjects();
    }

    void Shutdown() { ShutdownImGui(); }

    void ShutdownImGui()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
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
        auto fenceResult = m_Device.waitForFences(*frameData.m_DrawFence,
                                                  vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess)
            throw std::runtime_error("Failed to wait for fence!");

        auto [result, imageIndex] = m_Swapchain.acquireNextImage(
            UINT64_MAX, *frameData.m_PresentCompleteSemaphore, nullptr);

        if (result == vk::Result::eErrorOutOfDateKHR)
        {
            RecreateSwapchainAndDepthStencil();
            return;
        }
        else if (result != vk::Result::eSuccess &&
                 result != vk::Result::eSuboptimalKHR)
        {
            assert(result == vk::Result::eTimeout ||
                   result == vk::Result::eNotReady);
            throw std::runtime_error("Failed to acquire next swapchain image!");
        }

        m_Device.resetFences(*frameData.m_DrawFence);

        UpdateUniformBuffer(m_FrameIndex);
        RecordCommandBuffer(imageIndex);

        vk::PipelineStageFlags waitDestinationStageFlags(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        const vk::SubmitInfo submitInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*frameData.m_PresentCompleteSemaphore,
            .pWaitDstStageMask = &waitDestinationStageFlags,
            .commandBufferCount = 1,
            .pCommandBuffers = &*frameData.m_CommandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*m_RenderCompleteSemaphores[imageIndex]};

        m_GraphicsQueue.submit(submitInfo, *frameData.m_DrawFence);

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
            RecreateSwapchainAndDepthStencil();
        }
        else if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to present image!");
        }

        m_FrameIndex = (m_FrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void DrawImGuiFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Menu"))
        {
            ImGui::Text("Light");
            ImGui::DragFloat3("Position", &m_UBO.Light.Pos.x, 0.5f);
            ImGui::ColorEdit3("Color##Light", &m_UBO.Light.Color.r);
            ImGui::SliderFloat("Intensity", &m_UBO.Light.Intensity, 0.f, 10.f);

            ImGui::Dummy(ImVec2(2.f, 0.f));
            ImGui::Text("Spheres");
            ImGui::ColorEdit3("Color##Spheres", &m_UBO.SphereColor.r);
        }
        ImGui::End();

        ImGui::Render();
    }

    void CreateInstance()
    {
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

        auto extensionProperties =
            m_Context.enumerateInstanceExtensionProperties();

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

        vk::InstanceCreateInfo createInfo{
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = (uint32_t)requiredLayers.size(),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = (uint32_t)requiredExtensions.size(),
            .ppEnabledExtensionNames = requiredExtensions.data()};

        m_Instance = vk::raii::Instance(m_Context, createInfo);
    }

    void SetupDebugMessenger()
    {
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
            vk::KHRSwapchainExtensionName};
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
        VkSurfaceKHR rawSurface;
        if (!SDL_Vulkan_CreateSurface(m_pWindow, *m_Instance, nullptr,
                                      &rawSurface))
            throw SDLException("Failed to create Vulkan surface!");

        m_Surface = vk::raii::SurfaceKHR(m_Instance, rawSurface);
    }

    void PickPhysicalDevice()
    {
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
                           vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            featureChain = {
                {.features = {.samplerAnisotropy = true}},
                {.shaderDrawParameters = true},
                {.synchronization2 = true, .dynamicRendering = true},
                {.extendedDynamicState = true}};

        std::vector<const char*> requiredDeviceExtensions = {
            vk::KHRSwapchainExtensionName};

        vk::DeviceCreateInfo deviceCreateInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = (uint32_t)requiredDeviceExtensions.size(),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data()};

        m_Device = vk::raii::Device(m_PhysicalDevice, deviceCreateInfo);
        m_GraphicsQueue = vk::raii::Queue(m_Device, m_QueueIndex, 0);
    }

    void CreateSwapchain()
    {
        vk::SurfaceCapabilitiesKHR capabilities =
            m_PhysicalDevice.getSurfaceCapabilitiesKHR(*m_Surface);
        const std::vector<vk::SurfaceFormatKHR> formats =
            m_PhysicalDevice.getSurfaceFormatsKHR(*m_Surface);
        m_SwapchainSurfaceFormat = ChooseSwapchainFormat(formats);
        const std::vector<vk::PresentModeKHR> presentModes =
            m_PhysicalDevice.getSurfacePresentModesKHR(*m_Surface);
        m_SwapchainExtent = ChooseSwapchainExtent(capabilities, m_pWindow);

        std::cout << "Swapchain Extent: " << m_SwapchainExtent.width << "x"
                  << m_SwapchainExtent.height << "\n";

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
        m_SwapImages = m_Swapchain.getImages();

        std::cout << "Swapchain image count: " << m_SwapImages.size() << "\n";
    }

    [[nodiscard]] vk::raii::ImageView
    CreateImageView(const vk::Image& image, vk::Format format,
                    vk::ImageAspectFlags aspectFlags)
    {
        vk::ImageViewCreateInfo createInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = {aspectFlags, 0, 1, 0, 1}};
        return vk::raii::ImageView(m_Device, createInfo);
    }

    void CreateSwapchainImageViews()
    {
        assert(m_SwapImageViews.empty());
        for (const vk::Image& image : m_SwapImages)
        {
            m_SwapImageViews.push_back(
                CreateImageView(image, m_SwapchainSurfaceFormat.format,
                                vk::ImageAspectFlagBits::eColor));
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

    void CreateGraphicsPipeline()
    {
        vk::raii::ShaderModule shaderModule =
            CreateShaderModule(ReadFile("shaders/pbr.spv"));

        vk::PipelineShaderStageCreateInfo vertCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shaderModule,
            .pName = "vertMain"};

        vk::PipelineShaderStageCreateInfo fragCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shaderModule,
            .pName = "fragMain"};

        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = {
            vertCreateInfo, fragCreateInfo};

        auto bindingDesc = Vertex::GetBindingDescription();
        auto attributeDesc = Vertex::GetAttributeDescription();
        auto attributeCount = Vertex::GetAttributeCount();

        vk::PipelineVertexInputStateCreateInfo vertexInput{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDesc,
            .vertexAttributeDescriptionCount = attributeCount,
            .pVertexAttributeDescriptions = attributeDesc.data()};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
            .topology = vk::PrimitiveTopology::eTriangleList};

        vk::Viewport viewport{
            .x = 0.f,
            .y = 0.f,
            .width = static_cast<float>(m_SwapchainExtent.width),
            .height = static_cast<float>(m_SwapchainExtent.height),
            .minDepth = 0.f,
            .maxDepth = 1.f};
        vk::Rect2D scissor{.offset = vk::Offset2D{0, 0},
                           .extent = m_SwapchainExtent};
        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()};
        vk::PipelineViewportStateCreateInfo viewportState{
            .viewportCount = 1,
            .pViewports = &viewport,
            .scissorCount = 1,
            .pScissors = &scissor};

        // frontFace is counter-clockwise because we are flipping the Y in the
        // projection matrix
        vk::PipelineRasterizationStateCreateInfo rasterState{
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable = vk::False,
            .lineWidth = 1.f};

        vk::PipelineMultisampleStateCreateInfo multisampleState{
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = vk::False};

        vk::PipelineDepthStencilStateCreateInfo depthStencilState{
            .depthTestEnable = vk::True,
            .depthWriteEnable = vk::True,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False};

        vk::PipelineColorBlendAttachmentState attachmentState{
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR |
                              vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB |
                              vk::ColorComponentFlagBits::eA};
        vk::PipelineColorBlendStateCreateInfo blendState{
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &attachmentState};

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount = 1,
            .pSetLayouts = &*m_DescriptorSetLayout,
            .pushConstantRangeCount = 0};
        m_PipelineLayout =
            vk::raii::PipelineLayout(m_Device, pipelineLayoutInfo);

        m_PipelineRenderingCreateInfo = {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &m_SwapchainSurfaceFormat.format,
            .depthAttachmentFormat = m_DepthFormat};
        vk::StructureChain<vk::GraphicsPipelineCreateInfo,
                           vk::PipelineRenderingCreateInfo>
            pipelineCreateInfoChain = {
                {.stageCount = static_cast<uint32_t>(shaderStages.size()),
                 .pStages = shaderStages.data(),
                 .pVertexInputState = &vertexInput,
                 .pInputAssemblyState = &inputAssembly,
                 .pViewportState = &viewportState,
                 .pRasterizationState = &rasterState,
                 .pMultisampleState = &multisampleState,
                 .pDepthStencilState = &depthStencilState,
                 .pColorBlendState = &blendState,
                 .pDynamicState = &dynamicState,
                 .layout = m_PipelineLayout,
                 .renderPass = nullptr},
                m_PipelineRenderingCreateInfo};

        m_GraphicsPipeline = vk::raii::Pipeline(
            m_Device, nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    }

    void CreateCommandPool()
    {
        vk::CommandPoolCreateInfo createInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = m_QueueIndex};
        m_CommandPool = vk::raii::CommandPool(m_Device, createInfo);
    }

    void CreateCommandBuffers()
    {
        vk::CommandBufferAllocateInfo allocInfo{
            .commandPool = m_CommandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
        auto m_CommandBuffers = vk::raii::CommandBuffers(m_Device, allocInfo);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].m_CommandBuffer = std::move(m_CommandBuffers[i]);
        }
    }

    void RecordCommandBuffer(uint32_t imageIndex)
    {
        vk::raii::CommandBuffer& commandBuffer =
            m_Frames[m_FrameIndex].m_CommandBuffer;
        commandBuffer.reset();

        vk::CommandBufferBeginInfo beginInfo{};
        commandBuffer.begin(beginInfo);

        TransitionImageLayout(
            imageIndex, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        TransitionImageLayout(m_DepthImage, vk::ImageLayout::eUndefined,
                              vk::ImageLayout::eDepthAttachmentOptimal,
                              vk::ImageAspectFlagBits::eDepth);

        vk::ClearValue clearColor = vk::ClearColorValue(0.f, 0.f, 0.f, 1.f);
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.f, 0);
        vk::RenderingAttachmentInfo colorAttachmentInfo = {
            .imageView = m_SwapImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor};
        vk::RenderingAttachmentInfo depthAttachmentInfo = {
            .imageView = m_DepthImageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp =
                vk::AttachmentStoreOp::eDontCare, // might need to store later
            .clearValue = clearDepth};

        vk::RenderingInfo renderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = m_SwapchainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo};

        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   *m_GraphicsPipeline);
        commandBuffer.bindVertexBuffers(0, m_Model->GetVertexBuffer(), {0});
        commandBuffer.bindIndexBuffer(m_Model->GetIndexBuffer(), 0,
                                      vk::IndexType::eUint32);
        commandBuffer.setViewport(
            0, vk::Viewport(
                   0.f, 0.f, static_cast<float>(m_SwapchainExtent.width),
                   static_cast<float>(m_SwapchainExtent.height), 0.f, 1.f));
        commandBuffer.setScissor(
            0, vk::Rect2D(vk::Offset2D(0, 0), m_SwapchainExtent));
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics, m_PipelineLayout, 0,
            *m_DescriptorSets[m_FrameIndex], nullptr);

        commandBuffer.drawIndexed(
            static_cast<uint32_t>(m_Model->GetIndices().size()), INSTANCE_COUNT,
            0, 0, 0);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *commandBuffer);

        commandBuffer.endRendering();

        TransitionImageLayout(
            imageIndex, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe);

        commandBuffer.end();
    }

    void TransitionImageLayout(uint32_t imageIndex, vk::ImageLayout oldLayout,
                               vk::ImageLayout newLayout,
                               vk::AccessFlags2 srcAccessMask,
                               vk::AccessFlags2 dstAccessFlags,
                               vk::PipelineStageFlags2 srcStageMask,
                               vk::PipelineStageFlags2 dstStageMask)
    {
        vk::ImageMemoryBarrier2 barrier = {
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,
            .dstAccessMask = dstAccessFlags,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_SwapImages[imageIndex],
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};

        vk::DependencyInfo info = {.dependencyFlags = {},
                                   .imageMemoryBarrierCount = 1,
                                   .pImageMemoryBarriers = &barrier};
        m_Frames[m_FrameIndex].m_CommandBuffer.pipelineBarrier2(info);
    }

    void CreateSyncObjects()
    {
        for (size_t i = 0; i < m_SwapImages.size(); i++)
        {
            m_RenderCompleteSemaphores.emplace_back(
                vk::raii::Semaphore(m_Device, vk::SemaphoreCreateInfo()));
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].m_PresentCompleteSemaphore =
                vk::raii::Semaphore(m_Device, vk::SemaphoreCreateInfo());
            m_Frames[i].m_DrawFence = vk::raii::Fence(
                m_Device, {.flags = vk::FenceCreateFlagBits::eSignaled});
        }
    }

    void RecreateSwapchainAndDepthStencil()
    {
        std::cout << "Recreating swapchain and depth stencil...\n";

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

        m_DepthImageView = nullptr;
        m_DepthImage = nullptr;
        m_DepthImageMemory = nullptr;
        m_DepthFormat = vk::Format::eUndefined;

        m_Device.waitIdle();

        CreateSwapchain();
        CreateSwapchainImageViews();

        CreateDepthResources();

        // With dynamic rendering, this it the only change on the ImGui side
        // that has to be made.
        ImGui_ImplVulkan_SetMinImageCount(m_MinImageCount);
    }

    void CreateDescriptorSetLayout()
    {
        std::array bindings = {vk::DescriptorSetLayoutBinding(
                                   0, vk::DescriptorType::eUniformBuffer, 1,
                                   vk::ShaderStageFlagBits::eVertex |
                                       vk::ShaderStageFlagBits::eFragment,
                                   nullptr),
                               vk::DescriptorSetLayoutBinding(
                                   1, vk::DescriptorType::eCombinedImageSampler,
                                   1, vk::ShaderStageFlagBits::eFragment)};
        vk::DescriptorSetLayoutCreateInfo createInfo{
            .bindingCount = bindings.size(), .pBindings = bindings.data()};
        m_DescriptorSetLayout =
            vk::raii::DescriptorSetLayout(m_Device, createInfo);
    }

    void CreateUniformBuffers()
    {
        vk::DeviceSize size = sizeof(UniformBufferObject);
        if (size % 16 != 0)
            throw std::runtime_error(std::format(
                "Buffer must be 16 byte aligned! Size is {}", size));

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vk::raii::Buffer buffer({});
            vk::raii::DeviceMemory bufferMemory({});
            CreateBuffer(m_Device, m_PhysicalDevice, size,
                         vk::BufferUsageFlagBits::eUniformBuffer,
                         vk::MemoryPropertyFlagBits::eHostVisible |
                             vk::MemoryPropertyFlagBits::eHostCoherent,
                         buffer, bufferMemory);

            m_Frames[i].m_UniformBuffer = std::move(buffer);
            m_Frames[i].m_UniformBufferMemory = std::move(bufferMemory);
            // Mapping once like this for the application's whole lifetime is
            // called Persistent Mapping. Increases performance since mapping is
            // not free.
            m_Frames[i].m_UniformBufferMapping =
                m_Frames[i].m_UniformBufferMemory.mapMemory(0, size);
        }

        // In GLM, matrices are COLUMN MAJOR, and so must apply transformations
        // in reverse order.
        for (size_t i = 0; i < INSTANCE_COUNT; i++)
        {
            const float GRID_SIZE = 10.f;
            const float START = -GRID_SIZE / 2.f;
            const float INTERVAL = GRID_SIZE / (INSTANCES_PER_SIDE - 1);
            const float Z = -8.f;

            int x = i % INSTANCES_PER_SIDE;
            int y = i / INSTANCES_PER_SIDE;

            glm::vec3 pos;
            pos.x = START + x * INTERVAL;
            pos.y = START + y * INTERVAL;
            pos.z = Z;

            m_UBO.Models[i] = glm::mat4(1.f);
            m_UBO.Models[i] = glm::translate(m_UBO.Models[i], pos);
            m_UBO.Models[i] = glm::scale(m_UBO.Models[i], {1.f, 1.f, 1.f});
        }

        m_UBO.View =
            glm::lookAt(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -1.f),
                        glm::vec3(0.f, 1.f, 0.f));
        m_UBO.Proj =
            glm::perspective(glm::radians(90.f),
                             static_cast<float>(m_SwapchainExtent.width) /
                                 static_cast<float>(m_SwapchainExtent.height),
                             0.1f, 100.f);
        // GLM was designed for OpenGL, which has its Y coordinate in clip space
        // inverted. Compensate for this by scaling here.
        m_UBO.Proj[1][1] *= -1.f;

        m_UBO.Light.Pos = {10.f, 0.f, 0.f};
        m_UBO.Light.Intensity = 1000.f;
        m_UBO.Light.Color = {1.f, 1.f, 1.f};

        m_UBO.Time = 0.f;

        m_UBO.SphereColor = {1.f, 0.f, 0.f};
    }

    void UpdateUniformBuffer(uint32_t frameIndex)
    {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(
                         currentTime - m_StartTime)
                         .count();
        m_UBO.Time = time;
        memcpy(m_Frames[frameIndex].m_UniformBufferMapping, &m_UBO, sizeof(m_UBO));
    }

    void CreateDescriptorPool()
    {
        std::array poolSize = {
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                                   .descriptorCount = MAX_FRAMES_IN_FLIGHT},
            vk::DescriptorPoolSize{
                .type = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = MAX_FRAMES_IN_FLIGHT}};
        vk::DescriptorPoolCreateInfo createInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = MAX_FRAMES_IN_FLIGHT,
            .poolSizeCount = poolSize.size(),
            .pPoolSizes = poolSize.data()};

        m_DescriptorPool = vk::raii::DescriptorPool(m_Device, createInfo);
    }

    void CreateDescriptorSets()
    {
        m_DescriptorSets.clear();

        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                                     *m_DescriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool = *m_DescriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts = layouts.data()};

        m_DescriptorSets = m_Device.allocateDescriptorSets(allocInfo);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vk::DescriptorBufferInfo bufferInfo{
                .buffer = m_Frames[i].m_UniformBuffer,
                .offset = 0,
                .range = sizeof(UniformBufferObject)};
            vk::DescriptorImageInfo imageInfo{
                .sampler = m_TextureSampler,
                .imageView = m_Texture.GetImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

            std::array descriptorWrites = {
                vk::WriteDescriptorSet{.dstSet = m_DescriptorSets[i],
                                       .dstBinding = 0,
                                       .dstArrayElement = 0,
                                       .descriptorCount = 1,
                                       .descriptorType =
                                           vk::DescriptorType::eUniformBuffer,
                                       .pBufferInfo = &bufferInfo},
                vk::WriteDescriptorSet{
                    .dstSet = m_DescriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo}};

            m_Device.updateDescriptorSets(descriptorWrites, {});
        }
    }

    void CreateTextureImage()
    {
        int width, height, channels;
        stbi_uc* pixels = stbi_load(TEXTURE_PATH.c_str(), &width, &height,
                                    &channels, STBI_rgb_alpha);
        vk::DeviceSize imageSize = width * height * 4;

        if (!pixels)
            throw std::runtime_error(std::format("Failed to load texture: {}",
                                                 TEXTURE_PATH.c_str()));

        vk::raii::Buffer stagingBuffer({});
        vk::raii::DeviceMemory stagingMemory({});
        CreateBuffer(m_Device, m_PhysicalDevice, imageSize,
                     vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent,
                     stagingBuffer, stagingMemory);

        // Vulkan ensures that these CPU writes are visible to the GPU before
        // the command buffer starts executing.
        void* data = stagingMemory.mapMemory(0, imageSize);
        memcpy(data, pixels, imageSize);
        stagingMemory.unmapMemory();

        stbi_image_free(pixels);

        CreateImage(width, height, vk::Format::eR8G8B8A8Srgb,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eTransferDst |
                        vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal, m_Texture.GetImage(),
                    m_Texture.GetImageMemory());

        TransitionImageLayout(m_Texture.GetImage(), vk::ImageLayout::eUndefined,
                              vk::ImageLayout::eTransferDstOptimal,
                              vk::ImageAspectFlagBits::eColor);
        CopyBufferToImage(stagingBuffer, m_Texture.GetImage(),
                          static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height));
        TransitionImageLayout(m_Texture.GetImage(),
                              vk::ImageLayout::eTransferDstOptimal,
                              vk::ImageLayout::eShaderReadOnlyOptimal,
                              vk::ImageAspectFlagBits::eColor);
    }

    void CreateImage(uint32_t width, uint32_t height, vk::Format format,
                     vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                     vk::MemoryPropertyFlags properties, vk::raii::Image& image,
                     vk::raii::DeviceMemory& imageMemory)
    {
        vk::ImageCreateInfo createInfo{.imageType = vk::ImageType::e2D,
                                       .format = format,
                                       .extent = {width, height, 1},
                                       .mipLevels = 1,
                                       .arrayLayers = 1,
                                       .samples = vk::SampleCountFlagBits::e1,
                                       .tiling = tiling,
                                       .usage = usage,
                                       .sharingMode =
                                           vk::SharingMode::eExclusive};
        image = vk::raii::Image(m_Device, createInfo);

        vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(
                m_PhysicalDevice, memRequirements.memoryTypeBits, properties)};
        imageMemory = vk::raii::DeviceMemory(m_Device, allocInfo);
        image.bindMemory(imageMemory, 0);
    }

    void TransitionImageLayout(const vk::raii::Image& image,
                               vk::ImageLayout oldLayout,
                               vk::ImageLayout newLayout,
                               vk::ImageAspectFlags aspectFlags)
    {
        auto commandBuffer = BeginSingleTimeCommand(m_Device, m_CommandPool);
        vk::ImageMemoryBarrier2 barrier{
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {aspectFlags, 0, 1, 0, 1}};

        if (oldLayout == vk::ImageLayout::eUndefined &&
            newLayout == vk::ImageLayout::eTransferDstOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits2::eHostWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eHost;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        }
        else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
                 newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        }
        else if (newLayout == vk::ImageLayout::eDepthAttachmentOptimal &&
                 aspectFlags == vk::ImageAspectFlagBits::eDepth)
        {
            barrier.srcAccessMask =
                vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            barrier.dstAccessMask =
                vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

            barrier.srcStageMask =
                vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests;
            barrier.dstStageMask =
                vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests;
        }
        else
        {
            throw std::runtime_error("Unsupported layout transition!");
        }

        vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1,
                                          .pImageMemoryBarriers = &barrier};
        commandBuffer.pipelineBarrier2(dependencyInfo);
        EndSingleTimeCommand(commandBuffer, m_GraphicsQueue);
    }

    void CopyBufferToImage(const vk::raii::Buffer& buffer,
                           vk::raii::Image& image, uint32_t width,
                           uint32_t height)
    {
        auto commandBuffer = BeginSingleTimeCommand(m_Device, m_CommandPool);
        vk::BufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1}};
        commandBuffer.copyBufferToImage(
            buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
        EndSingleTimeCommand(commandBuffer, m_GraphicsQueue);
    }

    void CreateTextureImageView()
    {
		vk::raii::ImageView& imageView = m_Texture.GetImageView();
		imageView = CreateImageView(m_Texture.GetImage(), vk::Format::eR8G8B8A8Srgb,
                            vk::ImageAspectFlagBits::eColor);
    }

    void CreateTextureSampler()
    {
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
    }

    void CreateDepthResources()
    {
        m_DepthFormat = FindDepthFormat();
        CreateImage(m_SwapchainExtent.width, m_SwapchainExtent.height,
                    m_DepthFormat, vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment,
                    vk::MemoryPropertyFlagBits::eDeviceLocal, m_DepthImage,
                    m_DepthImageMemory);
        m_DepthImageView = CreateImageView(m_DepthImage, m_DepthFormat,
                                           vk::ImageAspectFlagBits::eDepth);
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

    void LoadModel()
    {
        m_Model = std::make_unique<Model>();
        m_Model->LoadModel(m_Device, m_PhysicalDevice, m_CommandPool,
                           m_GraphicsQueue, MODEL_PATH);
    }

private:
    vk::raii::Context m_Context;
    vk::raii::Instance m_Instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;
    vk::raii::SurfaceKHR m_Surface = nullptr;
    vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
    vk::raii::Device m_Device = nullptr;
    vk::raii::Queue m_GraphicsQueue = nullptr;
    vk::raii::SwapchainKHR m_Swapchain = nullptr;
    vk::raii::PipelineLayout m_PipelineLayout = nullptr;
    vk::raii::DescriptorSetLayout m_DescriptorSetLayout = nullptr;
    vk::raii::Pipeline m_GraphicsPipeline = nullptr;
    vk::raii::CommandPool m_CommandPool = nullptr;
    UniformBufferObject m_UBO = {};
	Texture m_Texture = {};
	vk::raii::Sampler m_TextureSampler = nullptr;
    vk::raii::DescriptorPool m_DescriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> m_DescriptorSets;
    vk::raii::Image m_DepthImage = nullptr;
    vk::raii::DeviceMemory m_DepthImageMemory = nullptr;
    vk::raii::ImageView m_DepthImageView = nullptr;
    vk::Format m_DepthFormat = vk::Format::eUndefined;
    vk::PipelineRenderingCreateInfo m_PipelineRenderingCreateInfo = {};

    vk::SurfaceFormatKHR m_SwapchainSurfaceFormat;
    vk::Extent2D m_SwapchainExtent;
    std::vector<vk::Image> m_SwapImages;
    std::vector<vk::raii::ImageView> m_SwapImageViews;
    uint32_t m_QueueIndex = ~0;
    uint32_t m_MinImageCount = 0;

    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_Frames;
    std::vector<vk::raii::Semaphore> m_RenderCompleteSemaphores;

    std::unique_ptr<Model> m_Model = nullptr;

    static constexpr uint32_t m_APIVersion = VK_API_VERSION_1_4;
    uint32_t m_FrameIndex = 0;
    SDL_Window* m_pWindow = nullptr;
    bool m_bIsFocused = true;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTime;
};

int main()
{
    std::signal(SIGINT, HandleSIGINT);

    SDL_Window* pWindow = nullptr;

    try
    {
        InitSDL();
        pWindow = CreateSDLWindow();

        App app(pWindow);
        app.Run();
    }
    catch (const SDLException& e)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL error: %s", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Error", e.what(),
                                 nullptr);
        return EXIT_FAILURE;
    }
    catch (const vk::SystemError& e)
    {
        std::cerr << "Vulkan error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    ShutdownSDL(pWindow);
    pWindow = nullptr;

    std::cout << "Exiting gracefully..." << std::endl;
    return EXIT_SUCCESS;
}
