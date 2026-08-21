#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "vulkan/vulkan_raii.hpp"

#include <core/HandlePool.h>

#include <rhi/BufferDesc.h>
#include <rhi/DeviceDesc.h>
#include <rhi/Diagnostics.h>
#include <rhi/Handles.h>
#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>
#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>
#include <rhi/vulkan/VulkanAllocator.h>

#include "vulkan/QueueFamilies.h"
#include "vulkan/VulkanBuffer.h"
#include "vulkan/VulkanSampler.h"
#include "vulkan/VulkanTexture.h"
#include "vulkan/VulkanTextureView.h"

namespace Rhi::Vulkan
{
class VulkanDevice final : public IDevice
{
public:
    explicit VulkanDevice(const DeviceDesc& desc);
    ~VulkanDevice() override;

    const DeviceCaps& GetCaps() const override { return m_Caps; }
    Diagnostics& GetDiagnostics() override { return *m_pDiagnostics; }
    void WaitIdle() override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void Destroy(BufferHandle handle) override;
    void* GetMappedData(BufferHandle handle) override;
    uint32_t GetLiveBufferCount() const override { return m_Buffers.Size(); }

    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void Destroy(TextureHandle handle) override;
    TextureViewHandle CreateTextureView(const TextureViewDesc& desc) override;
    void Destroy(TextureViewHandle handle) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void Destroy(SamplerHandle handle) override;
    const TextureDesc* GetTextureDesc(TextureHandle handle) const override;
    uint32_t GetLiveTextureCount() const override { return m_Textures.Size(); }
    uint32_t GetLiveTextureViewCount() const override { return m_TextureViews.Size(); }
    uint32_t GetLiveSamplerCount() const override { return m_Samplers.Size(); }

    // Gives an image the device did not allocate a pool slot, so that barriers,
    // views and copies can name it by handle like any other texture. Destroying
    // the returned handle releases the slot and does not touch the image.
    //
    // Exists for the swapchain, whose images belong to the presentation engine.
    // It retires when Stage 6's IPresentTarget owns the swapchain and hands out
    // the handle itself; until then the swapchain lives in the renderer and this
    // is what lets the rest of the RHI treat its images normally.
    TextureHandle RegisterExternalTexture(vk::Image image, const TextureDesc& desc);

    // The Vulkan objects behind a handle, or a null object if it is stale. These
    // back the accessors in <rhi/vulkan/VulkanNative.h>, and are also what
    // VulkanCommandList resolves handles through.
    vk::Buffer GetBuffer(BufferHandle handle) const;
    vk::Image GetImage(TextureHandle handle) const;
    vk::ImageView GetImageView(TextureViewHandle handle) const;
    vk::Sampler GetSampler(SamplerHandle handle) const;

    // Reports a handle that resolved to nothing, from the places that cannot
    // throw over it — recording a barrier or a copy, where the caller's own
    // command list is already half-built.
    void ReportStaleHandle(std::string_view what) const;

    // Everything below is reachable only through <rhi/vulkan/VulkanNative.h>,
    // which is the one sanctioned way for code outside this module to see a
    // Vulkan handle. These are non-const references because callers still create
    // Vulkan objects from them; that shrinks as resource creation moves in here.
    vk::raii::Instance& GetInstance() { return m_Instance; }
    vk::raii::PhysicalDevice& GetPhysicalDevice() { return m_PhysicalDevice; }
    vk::raii::Device& GetDevice() { return m_Device; }
    vk::raii::SurfaceKHR& GetSurface() { return m_Surface; }
    vk::raii::Queue& GetGraphicsQueue() { return m_GraphicsQueue; }
    VmaAllocator GetAllocator() const { return m_Allocator; }
    uint32_t GetApiVersion() const { return kApiVersion; }

    // The queue family serving `role`, or QueueFamilies::kInvalid when the
    // device has none. Only the graphics family is backed by a created queue —
    // compute and copy work is still submitted there, and the other families
    // are known but idle.
    uint32_t GetQueueFamily(QueueType role) const { return m_QueueFamilies.Get(role); }

private:
    void CreateInstance(const DeviceDesc& desc);
    void SetupDebugMessenger(const DeviceDesc& desc);
    void CreateSurface(const DeviceRequirements& requirements);
    void PickPhysicalDevice(const DeviceRequirements& requirements);
    void FindQueueFamilies(const DeviceRequirements& requirements);
    void CreateLogicalDevice();

    bool IsPhysicalDeviceSuitable(const vk::raii::PhysicalDevice& device) const;

    // Called from the driver's debug callback, on whichever thread the driver
    // happens to be on.
    void ReportDiagnostic(DiagnosticSeverity severity, std::string_view message) const;

    // Static so that it has C linkage-compatible calling convention while still
    // reaching the members above; the instance arrives via pUserData.
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

    // 1.4 rather than the 1.3 that IsPhysicalDeviceSuitable requires: the
    // instance-level version is a ceiling on what the loader will expose, while
    // the device check is the actual hard requirement.
    static constexpr uint32_t kApiVersion = VK_API_VERSION_1_4;

    // Declaration order is destruction order reversed, and both matter here.
    // The allocator sits after the device so that it is destroyed first, and the
    // surface after the instance that has to outlive it.
    //
    // Diagnostics comes first of all, because m_DebugMessenger below is
    // destroyed second-to-last and the driver reports validation messages
    // raised while the allocator and the logical device are being torn down.
    // Anything the callback touches has to still be alive at that point.
    std::unique_ptr<Diagnostics> m_OwnedDiagnostics;

    // Either the caller's or m_OwnedDiagnostics; never null after construction.
    Diagnostics* m_pDiagnostics = nullptr;

    vk::raii::Context m_Context;
    vk::raii::Instance m_Instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;
    vk::raii::SurfaceKHR m_Surface = nullptr;
    vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
    vk::raii::Device m_Device = nullptr;
    VulkanAllocator m_Allocator{};
    vk::raii::Queue m_GraphicsQueue = nullptr;

    // After the allocator, so that every buffer is destroyed before the
    // allocator that owns their memory. Releasing a slot frees its VulkanBuffer,
    // so this is also what makes an un-destroyed buffer merely a leak reported
    // at shutdown rather than a crash during it.
    HandlePool<VulkanBuffer, BufferTag> m_Buffers;
    HandlePool<VulkanTexture, TextureTag> m_Textures;

    // After m_Textures so that views are destroyed before the images they were
    // made from: a VkImageView outliving its VkImage is undefined behaviour
    // rather than something the driver diagnoses.
    HandlePool<VulkanTextureView, TextureViewTag> m_TextureViews;
    HandlePool<VulkanSampler, SamplerTag> m_Samplers;

    QueueFamilies m_QueueFamilies;

    DeviceCaps m_Caps{};
};
} // namespace Rhi::Vulkan
