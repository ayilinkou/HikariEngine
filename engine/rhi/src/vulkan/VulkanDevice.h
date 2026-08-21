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
#include <rhi/vulkan/VulkanAllocator.h>

#include "vulkan/QueueFamilies.h"
#include "vulkan/VulkanBuffer.h"

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

    // The buffer behind `handle`, or a null vk::Buffer if it is stale. Backs
    // Rhi::Vulkan::GetBuffer() in <rhi/vulkan/VulkanNative.h>.
    vk::Buffer GetBuffer(BufferHandle handle) const;

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

    QueueFamilies m_QueueFamilies;

    DeviceCaps m_Caps{};
};
} // namespace Rhi::Vulkan
