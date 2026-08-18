#include "vulkan/VulkanDevice.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <core/Log.h>

#include <rhi/vulkan/DebugNames.h>

#include "vulkan/VulkanConversions.h"

namespace Rhi::Vulkan
{
namespace
{
constexpr LogCategory LogRhi("RHI");

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

// "family 1 (dedicated)", "family 0", or "none", for the startup log.
std::string DescribeFamily(const QueueFamilies& families, QueueType role)
{
    const uint32_t index = families.Get(role);
    if (index == QueueFamilies::kInvalid)
        return "none";

    return std::format("family {}{}", index, families.IsDedicated(role) ? " (dedicated)" : "");
}
} // namespace

VulkanDevice::VulkanDevice(const DeviceDesc& desc)
    : m_OnDiagnosticMessage(desc.OnDiagnosticMessage),
      m_MinDiagnosticSeverity(desc.MinDiagnosticSeverity)
{
    CreateInstance(desc);
    SetupDebugMessenger(desc);
    CreateSurface(desc.Requirements);
    PickPhysicalDevice(desc.Requirements);
    FindQueueFamilies(desc.Requirements);
    CreateLogicalDevice();

    m_Allocator = VulkanAllocator(m_Instance, m_PhysicalDevice, m_Device, kApiVersion);

    // Vulkan's clip space has Y pointing down relative to what GLM produces.
    m_Caps.bFlipClipSpaceY = true;
    m_Caps.bPresentSupported = desc.Requirements.bPresent;
    m_Caps.bHasDedicatedComputeQueue = m_QueueFamilies.IsDedicated(QueueType::Compute);
    m_Caps.bHasDedicatedCopyQueue = m_QueueFamilies.IsDedicated(QueueType::Copy);
}

void VulkanDevice::WaitIdle()
{
    m_Device.waitIdle();
}

void VulkanDevice::ReportDiagnostic(DiagnosticSeverity severity, std::string_view message) const
{
    if (m_OnDiagnosticMessage)
        m_OnDiagnosticMessage(severity, message);
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanDevice::DebugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    const auto* device = static_cast<const VulkanDevice*>(pUserData);
    if (!device)
        return vk::False;

    // The comparison is on the raw bit values, which the specification orders
    // verbose < info < warning < error, so this reads as a threshold.
    if (severity < ToVk(device->m_MinDiagnosticSeverity))
        return vk::False;

    device->ReportDiagnostic(FromVk(severity), std::format("Type: {}. Msg: {}", vk::to_string(type),
                                                           pCallbackData->pMessage));

    // False tells the driver to carry on. Returning true is reserved for layer
    // development and aborts the call that triggered the message.
    return vk::False;
}

void VulkanDevice::CreateInstance(const DeviceDesc& desc)
{
    LogMsg(LogSeverity::Info, LogRhi, "CreateInstance()");

    const vk::ApplicationInfo appInfo{.pApplicationName = desc.ApplicationName.c_str(),
                                      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                      .pEngineName = "No Engine",
                                      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                      .apiVersion = kApiVersion};

    // extensions
    uint32_t countInstanceExtensions = 0;
    const char* const* instanceExtensions =
        SDL_Vulkan_GetInstanceExtensions(&countInstanceExtensions);

    if (countInstanceExtensions == 0)
        throw std::runtime_error("No available instance extensions found!");

    std::vector<const char*> requiredExtensions(countInstanceExtensions);
    memcpy(requiredExtensions.data(), instanceExtensions,
           countInstanceExtensions * sizeof(const char*));
    requiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);

#if defined(__APPLE__)
    // MoltenVK is a portability driver. On macOS the Vulkan loader requires the
    // app to explicitly opt into enumerating portability drivers, otherwise
    // vkCreateInstance fails with VK_ERROR_INCOMPATIBLE_DRIVER.
    requiredExtensions.push_back(vk::KHRPortabilityEnumerationExtensionName);
    // Required so we can build the swapchain surface via VK_EXT_metal_surface
    // (SDL_Vulkan_CreateSurface is unreliable on macOS).
    requiredExtensions.push_back(vk::EXTMetalSurfaceExtensionName);
#endif

    auto extensionProperties = m_Context.enumerateInstanceExtensionProperties();

    // VK_EXT_layer_settings is implemented by layers (e.g.
    // VK_LAYER_KHRONOS_validation), not by the loader, so it never appears in
    // vkEnumerateInstanceExtensionProperties(nullptr). It takes effect purely
    // through the VkLayerSettingsCreateInfoEXT pNext chain below; the gate for
    // attaching it is whether the validation layer is enabled, not whether the
    // extension happens to be enumerated.
    auto unsupportedExtensionIt = std::ranges::find_if(
        requiredExtensions,
        [&extensionProperties](auto const& requiredExtension)
        {
            return std::ranges::none_of(
                extensionProperties, [requiredExtension](auto const& extensionProperty)
                { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
        });

    if (unsupportedExtensionIt != requiredExtensions.end())
        throw std::runtime_error("Required extension not supported: " +
                                 std::string(*unsupportedExtensionIt));

    // layers
    std::vector<const char*> requiredLayers;
    if (desc.bEnableValidation)
        requiredLayers.push_back(kValidationLayerName);

    auto layerProperties = m_Context.enumerateInstanceLayerProperties();

    auto unsupportedLayerIt = std::ranges::find_if(
        requiredLayers,
        [&layerProperties](auto const& requiredLayer)
        {
            return std::ranges::none_of(
                layerProperties, [requiredLayer](auto const& layerProperty)
                { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
        });

    if (unsupportedLayerIt != requiredLayers.end())
        throw std::runtime_error("Required layer not supported: " +
                                 std::string(*unsupportedLayerIt));

    const vk::Bool32 bSyncValEnabled = VK_TRUE;
    const vk::Bool32 bBestPracticesValEnabled = VK_TRUE;

    std::array<vk::LayerSettingEXT, 2> settings = {
        vk::LayerSettingEXT{.pLayerName = kValidationLayerName,
                            .pSettingName = "validate_sync",
                            .type = vk::LayerSettingTypeEXT::eBool32,
                            .valueCount = 1,
                            .pValues = &bSyncValEnabled},
        vk::LayerSettingEXT{.pLayerName = kValidationLayerName,
                            .pSettingName = "validate_best_practices",
                            .type = vk::LayerSettingTypeEXT::eBool32,
                            .valueCount = 1,
                            .pValues = &bBestPracticesValEnabled},
    };

    vk::LayerSettingsCreateInfoEXT layerSettingsInfo{
        .settingCount = static_cast<uint32_t>(settings.size()), .pSettings = settings.data()};

    vk::InstanceCreateInfo createInfo{.pNext =
                                          desc.bEnableValidation ? &layerSettingsInfo : nullptr,
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

void VulkanDevice::SetupDebugMessenger(const DeviceDesc& desc)
{
    LogMsg(LogSeverity::Info, LogRhi, "SetupDebugMessenger()");

    if (!desc.bEnableValidation)
        return;

    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo);
    vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);

    vk::DebugUtilsMessengerCreateInfoEXT createInfo{.messageSeverity = severityFlags,
                                                    .messageType = messageTypeFlags,
                                                    .pfnUserCallback = &VulkanDevice::DebugCallback,
                                                    .pUserData = this};

    m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(createInfo);
}

void VulkanDevice::CreateSurface(const DeviceRequirements& requirements)
{
    LogMsg(LogSeverity::Info, LogRhi, "CreateSurface()");

    if (!requirements.bPresent)
        return;

    if (!requirements.NativeWindowHandle)
        throw std::runtime_error("A present-capable device needs a native window handle!");

    auto* pWindow = static_cast<SDL_Window*>(requirements.NativeWindowHandle);

#if defined(__APPLE__)
    // SDL_Vulkan_CreateSurface can crash on macOS because SDL resolves the
    // surface-creation function pointer through its own Vulkan loader, which may
    // differ from the one this app linked (and the instance belongs to). Create
    // the Metal surface directly via our Vulkan loader instead.
    SDL_MetalView metalView = SDL_Metal_CreateView(pWindow);
    if (!metalView)
        throw std::runtime_error("Failed to create Metal view!");

    void* metalLayer = SDL_Metal_GetLayer(metalView);
    vk::MetalSurfaceCreateInfoEXT createInfo{.pLayer = metalLayer};

    m_Surface = m_Instance.createMetalSurfaceEXT(createInfo);
#else
    VkSurfaceKHR rawSurface;
    if (!SDL_Vulkan_CreateSurface(pWindow, *m_Instance, nullptr, &rawSurface))
        throw std::runtime_error("Failed to create Vulkan surface!");

    m_Surface = vk::raii::SurfaceKHR(m_Instance, rawSurface);
#endif
}

bool VulkanDevice::IsPhysicalDeviceSuitable(const vk::raii::PhysicalDevice& device) const
{
    auto properties = device.getProperties();

    bool bSupportsVulkan13 = properties.apiVersion >= vk::ApiVersion13;

    auto queueFamilies = device.getQueueFamilyProperties();
    bool bSupportsGraphicsQ =
        std::ranges::any_of(queueFamilies, [](const auto& qfp)
                            { return FamilySupports(qfp.queueFlags, QueueType::Graphics); });

    std::vector<const char*> requiredExtensions = {vk::KHRSwapchainExtensionName,
                                                   vk::EXTDescriptorIndexingExtensionName};
    auto availableExtensions = device.enumerateDeviceExtensionProperties();
    bool bSupportsAllExtensions = std::ranges::all_of(
        requiredExtensions,
        [&availableExtensions](const auto& requiredExtension)
        {
            return std::ranges::any_of(
                availableExtensions, [requiredExtension](const auto& availableExtension)
                { return strcmp(availableExtension.extensionName, requiredExtension) == 0; });
        });

    auto features =
        device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
                            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    bool bSupportsAllFeatures =
        features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
        features.get<vk::PhysicalDeviceFeatures2>().features.independentBlend &&
        features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
        features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

    if (bSupportsVulkan13 && bSupportsGraphicsQ && bSupportsAllExtensions && bSupportsAllFeatures)
        return true;
    return false;
}

void VulkanDevice::PickPhysicalDevice(const DeviceRequirements& requirements)
{
    LogMsg(LogSeverity::Info, LogRhi, "PickPhysicalDevice()");

    (void)requirements;

    auto devices = m_Instance.enumeratePhysicalDevices();
    const auto deviceIt = std::ranges::find_if(devices, [&](const auto& device)
                                               { return IsPhysicalDeviceSuitable(device); });

    if (deviceIt == devices.end())
        throw std::runtime_error("Failed to find a suitable GPU!");

    m_PhysicalDevice = *deviceIt;
}

void VulkanDevice::FindQueueFamilies(const DeviceRequirements& requirements)
{
    LogMsg(LogSeverity::Info, LogRhi, "FindQueueFamilies()");

    const std::vector<vk::QueueFamilyProperties> families =
        m_PhysicalDevice.getQueueFamilyProperties();

    // Presentation is a property of a (family, surface) pair rather than a queue
    // capability, so it has to be queried — and only where there is a surface to
    // query against, since a null one is not a valid argument.
    const PresentSupportFn presentSupported = [this](uint32_t index)
    { return static_cast<bool>(m_PhysicalDevice.getSurfaceSupportKHR(index, m_Surface)); };

    for (uint32_t index = 0; index < families.size(); index++)
    {
        const char* presentNote = "";
        if (*m_Surface)
            presentNote = presentSupported(index) ? ", present" : ", no present";

        LogMsg(LogSeverity::Info, LogRhi, "Family {}: {} queue(s), {}{}", index,
               families[index].queueCount, vk::to_string(families[index].queueFlags), presentNote);
    }

    m_QueueFamilies = SelectQueueFamilies(families, presentSupported, requirements.bPresent);

    if (m_QueueFamilies.Graphics == QueueFamilies::kInvalid)
        throw std::runtime_error(requirements.bPresent
                                     ? "Could not find a queue for graphics and presenting!"
                                     : "Could not find a queue for graphics!");

    LogMsg(LogSeverity::Info, LogRhi, "Graphics: {}. Compute: {}. Copy: {}.",
           DescribeFamily(m_QueueFamilies, QueueType::Graphics),
           DescribeFamily(m_QueueFamilies, QueueType::Compute),
           DescribeFamily(m_QueueFamilies, QueueType::Copy));
}

void VulkanDevice::CreateLogicalDevice()
{
    LogMsg(LogSeverity::Info, LogRhi, "CreateLogicalDevice()");

    // Only the graphics family gets a queue. A compute or copy family that is
    // never submitted to would be a queue the driver has to schedule for
    // nothing, so each one is created by the step that starts using it.
    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo queueCreateInfo{.queueFamilyIndex = m_QueueFamilies.Graphics,
                                              .queueCount = 1,
                                              .pQueuePriorities = &queuePriority};

    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                       vk::PhysicalDeviceDescriptorIndexingFeaturesEXT>
        featureChain = {{.features = {.independentBlend = true, .samplerAnisotropy = true}},
                        {.shaderDrawParameters = true},
                        {.synchronization2 = true, .dynamicRendering = true},
                        {.extendedDynamicState = true},
                        {.descriptorBindingPartiallyBound = true}};

    std::vector<const char*> requiredDeviceExtensions = {vk::KHRSwapchainExtensionName};

#if defined(__APPLE__)
    // MoltenVK exposes VK_KHR_portability_subset and requires it to be enabled
    // on the logical device; otherwise device creation has undefined behaviour
    // and can crash.
    requiredDeviceExtensions.push_back(vk::KHRPortabilitySubsetExtensionName);
#endif

    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = (uint32_t)requiredDeviceExtensions.size(),
        .ppEnabledExtensionNames = requiredDeviceExtensions.data()};

    m_Device = vk::raii::Device(m_PhysicalDevice, deviceCreateInfo);
    SetVkDebugName(m_Device, *m_Device, vk::ObjectType::eDevice, "Device");
    m_GraphicsQueue = vk::raii::Queue(m_Device, m_QueueFamilies.Graphics, 0);
    SetVkDebugName(m_Device, *m_GraphicsQueue, vk::ObjectType::eQueue, "Graphics Queue");

    // Setting debug names for objects which were created before the device was
    // created.
    SetVkDebugName(m_Device, *m_Instance, vk::ObjectType::eInstance, "Instance");
    SetVkDebugName(m_Device, *m_PhysicalDevice, vk::ObjectType::ePhysicalDevice, "Physical Device");
    if (*m_Surface)
        SetVkDebugName(m_Device, *m_Surface, vk::ObjectType::eSurfaceKHR, "Surface");
}

} // namespace Rhi::Vulkan

namespace Rhi
{
std::unique_ptr<IDevice> CreateDevice(const DeviceDesc& desc)
{
    return std::make_unique<Vulkan::VulkanDevice>(desc);
}
} // namespace Rhi
