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
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

#include <core/Log.h>

#include <rhi/vulkan/DebugNames.h>

#include "vulkan/VulkanConversions.h"
#include "vulkan/VulkanUploadContext.h"

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

// Rejects the descriptions Vulkan would reject anyway, but with a message that
// names the caller's field rather than a VUID. Every one of these is a
// programming error rather than a runtime condition, so they throw.
void ValidateTextureDesc(const TextureDesc& desc)
{
    const auto fail = [&desc](std::string_view why)
    {
        throw std::runtime_error(
            std::format("Rhi::IDevice::CreateTexture('{}'): {}", desc.DebugName, why));
    };

    if (desc.Format == Rhi::Format::Undefined)
        fail("no format.");

    if (desc.Extent.Width == 0u || desc.Extent.Height == 0u || desc.Extent.Depth == 0u)
        fail("every extent must be at least 1.");

    if (desc.MipLevels == 0u || desc.ArrayLayers == 0u)
        fail("MipLevels and ArrayLayers must be at least 1.");

    // Depth is the third dimension of a 3D texture and the array is the layers;
    // Vulkan has no 3D array images, and mixing the two is the classic way to
    // describe a cubemap as six slices deep instead of six layers wide.
    if (desc.Dimension == TextureDimension::Texture3D && desc.ArrayLayers != 1u)
        fail("a 3D texture cannot have array layers.");

    if (desc.Dimension == TextureDimension::Texture2D && desc.Extent.Depth != 1u)
        fail("a 2D texture must have a depth of 1; use ArrayLayers for slices.");

    if (desc.bCubeCompatible &&
        (desc.Dimension != TextureDimension::Texture2D || desc.ArrayLayers % 6u != 0u))
        fail("a cube-compatible texture must be 2D with a multiple of 6 array layers.");
}
} // namespace

VulkanDevice::VulkanDevice(const DeviceDesc& desc)
    : m_OwnedDiagnostics(desc.pDiagnostics ? nullptr : std::make_unique<Diagnostics>()),
      m_pDiagnostics(desc.pDiagnostics ? desc.pDiagnostics : m_OwnedDiagnostics.get())
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

VulkanDevice::~VulkanDevice()
{
    // A resource still alive here was never destroyed. It is not a crash — the
    // pools free their payloads on the way out, and they are declared after the
    // allocator and the device so that happens in the right order — but it is a
    // leak for as long as the device ran, and the whole point of routing
    // resources through handles is that the count is knowable. Reported rather
    // than asserted so that a shutdown already unwinding from an error is not
    // made worse.
    const std::array<std::pair<const char*, uint32_t>, 4> live{
        std::pair{"buffer", m_Buffers.Size()},
        std::pair{"texture", m_Textures.Size()},
        std::pair{"texture view", m_TextureViews.Size()},
        std::pair{"sampler", m_Samplers.Size()},
    };

    bool bAnyLive = false;
    for (const auto& [kind, count] : live)
    {
        if (count == 0u)
            continue;

        bAnyLive = true;
        LogMsg(LogSeverity::Warning, LogRhi,
               "Device destroyed with {} {}(s) still alive — each is a resource whose owner "
               "never released it.",
               count, kind);
    }

    if (!bAnyLive)
    {
        LogMsg(LogSeverity::Info, LogRhi,
               "Device destroyed with 0 live buffers, textures, texture views and samplers.");
    }
}

void VulkanDevice::WaitIdle()
{
    m_Device.waitIdle();
}

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc& desc)
{
    if (desc.Size == 0u)
        throw std::runtime_error("Rhi::IDevice::CreateBuffer: a buffer must have a non-zero size.");

    const VmaMemoryParams memoryParams = ToVk(desc.Access);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(desc.Size);
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(ToVk(desc.Usage));
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryParams.Usage;
    allocInfo.flags = memoryParams.Flags;

    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocationInfo{};

    const vk::Result result = static_cast<vk::Result>(vmaCreateBuffer(
        m_Allocator, &bufferInfo, &allocInfo, &rawBuffer, &allocation, &allocationInfo));

    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error(std::format("Rhi::IDevice::CreateBuffer: VMA failed to allocate "
                                             "'{}' ({} bytes): {}.",
                                             desc.DebugName, desc.Size, vk::to_string(result)));
    }

    if (!desc.DebugName.empty())
    {
        SetVkDebugName(m_Device, vk::Buffer(rawBuffer), vk::ObjectType::eBuffer,
                       desc.DebugName.c_str());
        // Names the allocation as well as the buffer, because VMA's own leak
        // and budget dumps report allocations, not Vulkan objects.
        vmaSetAllocationName(m_Allocator, allocation, desc.DebugName.c_str());
    }

    return m_Buffers.Create(m_Allocator, vk::Buffer(rawBuffer), allocation, allocationInfo);
}

void VulkanDevice::Destroy(BufferHandle handle)
{
    if (m_Buffers.Release(handle))
        return;

    // Either a double destroy or a handle outliving what it named. Both are the
    // bug the generation counter exists to catch, so neither is silently
    // ignored — but neither is fatal either, since the slot is already free.
    ReportDiagnostic(DiagnosticSeverity::Error,
                     std::format("Rhi::IDevice::Destroy(BufferHandle): handle {:#010x} is stale or "
                                 "was never valid; it may have been destroyed already.",
                                 handle.Value));
}

void* VulkanDevice::GetMappedData(BufferHandle handle)
{
    const VulkanBuffer* pBuffer = m_Buffers.Get(handle);
    return pBuffer ? pBuffer->AllocationInfo.pMappedData : nullptr;
}

vk::Buffer VulkanDevice::GetBuffer(BufferHandle handle) const
{
    const VulkanBuffer* pBuffer = m_Buffers.Get(handle);
    return pBuffer ? pBuffer->Buffer : vk::Buffer{};
}

TextureHandle VulkanDevice::CreateTexture(const TextureDesc& desc)
{
    ValidateTextureDesc(desc);

    const vk::ImageCreateInfo imageInfo{
        .flags = desc.bCubeCompatible
                     ? vk::ImageCreateFlags{vk::ImageCreateFlagBits::eCubeCompatible}
                     : vk::ImageCreateFlags{},
        .imageType = ToVk(desc.Dimension),
        .format = ToVk(desc.Format),
        .extent = vk::Extent3D{desc.Extent.Width, desc.Extent.Height, desc.Extent.Depth},
        .mipLevels = desc.MipLevels,
        .arrayLayers = desc.ArrayLayers,
        .samples = ToVk(desc.Samples),
        .tiling = vk::ImageTiling::eOptimal,
        .usage = ToVk(desc.Usage),
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined};

    // Textures are always device-local: nothing here uploads by writing an
    // image's memory directly, it stages through a buffer and copies. That is
    // also the only portable path — D3D12 has no equivalent of a linear-tiled
    // host-visible image that a shader can sample.
    const VmaMemoryParams memoryParams = ToVk(MemoryAccess::GpuOnly);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryParams.Usage;
    allocInfo.flags = memoryParams.Flags;

    const VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
    VkImage rawImage = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    const vk::Result result = static_cast<vk::Result>(
        vmaCreateImage(m_Allocator, &cImageInfo, &allocInfo, &rawImage, &allocation, nullptr));

    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error(std::format("Rhi::IDevice::CreateTexture: VMA failed to allocate "
                                             "'{}' ({}x{}x{}): {}.",
                                             desc.DebugName, desc.Extent.Width, desc.Extent.Height,
                                             desc.Extent.Depth, vk::to_string(result)));
    }

    if (!desc.DebugName.empty())
    {
        SetVkDebugName(m_Device, vk::Image(rawImage), vk::ObjectType::eImage,
                       desc.DebugName.c_str());
        // Names the allocation as well as the image, because VMA's own leak and
        // budget dumps report allocations, not Vulkan objects.
        vmaSetAllocationName(m_Allocator, allocation, desc.DebugName.c_str());
    }

    return m_Textures.Create(m_Allocator, vk::Image(rawImage), allocation, desc);
}

TextureHandle VulkanDevice::RegisterExternalTexture(vk::Image image, const TextureDesc& desc)
{
    if (!image)
        throw std::runtime_error("Rhi::Vulkan::RegisterExternalTexture: null image.");

    ValidateTextureDesc(desc);

    if (!desc.DebugName.empty())
        SetVkDebugName(m_Device, image, vk::ObjectType::eImage, desc.DebugName.c_str());

    // No allocator and no allocation: VulkanTexture reads that as "not ours"
    // and frees nothing when the slot is released.
    return m_Textures.Create(VmaAllocator{}, image, VmaAllocation{}, desc);
}

void VulkanDevice::Destroy(TextureHandle handle)
{
    if (m_Textures.Release(handle))
        return;

    ReportDiagnostic(DiagnosticSeverity::Error,
                     std::format("Rhi::IDevice::Destroy(TextureHandle): handle {:#010x} is stale "
                                 "or was never valid; it may have been destroyed already.",
                                 handle.Value));
}

TextureViewHandle VulkanDevice::CreateTextureView(const TextureViewDesc& desc)
{
    const VulkanTexture* pTexture = m_Textures.Get(desc.Texture);
    if (!pTexture)
    {
        throw std::runtime_error(
            std::format("Rhi::IDevice::CreateTextureView: '{}' names texture handle {:#010x}, "
                        "which is stale or was never valid.",
                        desc.DebugName, desc.Texture.Value));
    }

    const Rhi::Format format =
        desc.Format == Rhi::Format::Undefined ? pTexture->Desc.Format : desc.Format;
    const TextureAspect aspect = Any(desc.Aspect) ? desc.Aspect : DefaultAspect(format);

    const vk::ImageViewCreateInfo createInfo{.image = pTexture->Image,
                                             .viewType = ToVk(desc.Dimension),
                                             .format = ToVk(format),
                                             .subresourceRange = {.aspectMask = ToVk(aspect),
                                                                  .baseMipLevel = desc.BaseMip,
                                                                  .levelCount = desc.MipCount,
                                                                  .baseArrayLayer = desc.BaseLayer,
                                                                  .layerCount = desc.LayerCount}};

    VulkanTextureView view{vk::raii::ImageView(m_Device, createInfo)};

    if (!desc.DebugName.empty())
    {
        SetVkDebugName(m_Device, *view.View, vk::ObjectType::eImageView, desc.DebugName.c_str());
    }

    return m_TextureViews.Create(std::move(view));
}

void VulkanDevice::Destroy(TextureViewHandle handle)
{
    if (m_TextureViews.Release(handle))
        return;

    ReportDiagnostic(
        DiagnosticSeverity::Error,
        std::format("Rhi::IDevice::Destroy(TextureViewHandle): handle {:#010x} is stale or "
                    "was never valid; it may have been destroyed already.",
                    handle.Value));
}

SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc& desc)
{
    // Only meaningful when anisotropy is enabled, and then it must lie within
    // the device's limit (VUID-VkSamplerCreateInfo-anisotropyEnable-01071). A
    // desc asking for 0 is asking for the best the device offers, which is what
    // spares every caller from plumbing the limit through to get it.
    float maxAnisotropy = desc.MaxAnisotropy;
    if (desc.bAnisotropyEnable)
    {
        const float limit = m_PhysicalDevice.getProperties().limits.maxSamplerAnisotropy;
        maxAnisotropy = desc.MaxAnisotropy <= 0.f ? limit : std::min(desc.MaxAnisotropy, limit);
    }

    const vk::SamplerCreateInfo createInfo{
        .magFilter = ToVk(desc.MagFilter),
        .minFilter = ToVk(desc.MinFilter),
        .mipmapMode = ToVk(desc.MipmapFilter),
        .addressModeU = ToVk(desc.AddressU),
        .addressModeV = ToVk(desc.AddressV),
        .addressModeW = ToVk(desc.AddressW),
        .mipLodBias = desc.MipLodBias,
        .anisotropyEnable = static_cast<vk::Bool32>(desc.bAnisotropyEnable),
        .maxAnisotropy = maxAnisotropy,
        .compareEnable = static_cast<vk::Bool32>(desc.bCompareEnable),
        .compareOp = ToVk(desc.Compare),
        .minLod = desc.MinLod,
        .maxLod = desc.MaxLod,
        .borderColor = ToVk(desc.Border),
        .unnormalizedCoordinates = vk::False};

    VulkanSampler sampler{vk::raii::Sampler(m_Device, createInfo)};

    if (!desc.DebugName.empty())
    {
        SetVkDebugName(m_Device, *sampler.Sampler, vk::ObjectType::eSampler,
                       desc.DebugName.c_str());
    }

    return m_Samplers.Create(std::move(sampler));
}

void VulkanDevice::Destroy(SamplerHandle handle)
{
    if (m_Samplers.Release(handle))
        return;

    ReportDiagnostic(DiagnosticSeverity::Error,
                     std::format("Rhi::IDevice::Destroy(SamplerHandle): handle {:#010x} is stale "
                                 "or was never valid; it may have been destroyed already.",
                                 handle.Value));
}

const TextureDesc* VulkanDevice::GetTextureDesc(TextureHandle handle) const
{
    const VulkanTexture* pTexture = m_Textures.Get(handle);
    return pTexture ? &pTexture->Desc : nullptr;
}

vk::Image VulkanDevice::GetImage(TextureHandle handle) const
{
    const VulkanTexture* pTexture = m_Textures.Get(handle);
    return pTexture ? pTexture->Image : vk::Image{};
}

vk::ImageView VulkanDevice::GetImageView(TextureViewHandle handle) const
{
    const VulkanTextureView* pView = m_TextureViews.Get(handle);
    return pView ? *pView->View : vk::ImageView{};
}

vk::Sampler VulkanDevice::GetSampler(SamplerHandle handle) const
{
    const VulkanSampler* pSampler = m_Samplers.Get(handle);
    return pSampler ? *pSampler->Sampler : vk::Sampler{};
}

std::unique_ptr<IUploadContext> VulkanDevice::CreateUploadContext(const UploadContextDesc& desc)
{
    return std::make_unique<VulkanUploadContext>(*this, desc);
}

void VulkanDevice::ReportStaleHandle(std::string_view what) const
{
    ReportDiagnostic(DiagnosticSeverity::Error, what);
}

void VulkanDevice::ReportDiagnostic(DiagnosticSeverity severity, std::string_view message) const
{
    m_pDiagnostics->Report(severity, message);
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanDevice::DebugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    const auto* device = static_cast<const VulkanDevice*>(pUserData);
    if (!device)
        return vk::False;

    // Filtered here as well as by the messenger's severity flags, so that a
    // message destined to be dropped does not pay for the std::format below.
    // The comparison is on the raw bit values, which the specification orders
    // verbose < info < warning < error, so this reads as a threshold.
    if (severity < ToVk(device->m_pDiagnostics->MinSeverity()))
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

    // Ask the driver only for what the threshold admits, so the filtering
    // happens before the callback rather than inside it. Verbose is never
    // requested: it collapses to Info on the neutral scale, and asking for it
    // would multiply the message volume for nothing a caller can distinguish.
    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    if (m_pDiagnostics->MinSeverity() <= DiagnosticSeverity::Warning)
        severityFlags |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
    if (m_pDiagnostics->MinSeverity() <= DiagnosticSeverity::Info)
        severityFlags |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;

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
