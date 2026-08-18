#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/DeviceDesc.h>
#include <rhi/IDevice.h>
#include <rhi/vulkan/VulkanAllocator.h>

namespace Rhi::Vulkan
{
class VulkanDevice final : public IDevice
{
public:
    explicit VulkanDevice(const DeviceDesc& desc);

    const DeviceCaps& GetCaps() const override { return m_Caps; }
    void WaitIdle() override;

    // Everything below is reachable only through <rhi/vulkan/VulkanNative.h>,
    // which is the one sanctioned way for code outside this module to see a
    // Vulkan handle. These are non-const references because callers still create
    // Vulkan objects from them; that shrinks as resource creation moves in here.
    vk::raii::Instance& GetInstance() { return m_Instance; }
    vk::raii::PhysicalDevice& GetPhysicalDevice() { return m_PhysicalDevice; }
    vk::raii::Device& GetDevice() { return m_Device; }
    vk::raii::SurfaceKHR& GetSurface() { return m_Surface; }
    vk::raii::Queue& GetGraphicsQueue() { return m_GraphicsQueue; }
    uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
    VmaAllocator GetAllocator() const { return m_Allocator; }
    uint32_t GetApiVersion() const { return kApiVersion; }

private:
    void CreateInstance(const DeviceDesc& desc);
    void SetupDebugMessenger(const DeviceDesc& desc);
    void CreateSurface(const DeviceRequirements& requirements);
    void PickPhysicalDevice(const DeviceRequirements& requirements);
    void CreateLogicalDevice(const DeviceRequirements& requirements);

    bool IsPhysicalDeviceSuitable(const vk::raii::PhysicalDevice& device) const;

    // Returns ~0u when no family qualifies.
    uint32_t FindGraphicsQueueFamily(const vk::raii::PhysicalDevice& device,
                                     bool bRequirePresent) const;

    // Called from the driver's debug callback, on whichever thread the driver
    // happens to be on.
    void ReportDiagnostic(DiagnosticSeverity severity, std::string_view message) const;

    // Static so that it has C linkage-compatible calling convention while still
    // reaching the members above; the instance arrives via pUserData.
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL
    DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                  vk::DebugUtilsMessageTypeFlagsEXT type,
                  const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

    // 1.4 rather than the 1.3 that IsPhysicalDeviceSuitable requires: the
    // instance-level version is a ceiling on what the loader will expose, while
    // the device check is the actual hard requirement.
    static constexpr uint32_t kApiVersion = VK_API_VERSION_1_4;

    // Declaration order is destruction order reversed, and both matter here.
    // The allocator sits after the device so that it is destroyed first, and the
    // surface after the instance that has to outlive it.
    vk::raii::Context m_Context;
    vk::raii::Instance m_Instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;
    vk::raii::SurfaceKHR m_Surface = nullptr;
    vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
    vk::raii::Device m_Device = nullptr;
    VulkanAllocator m_Allocator{};
    vk::raii::Queue m_GraphicsQueue = nullptr;

    uint32_t m_GraphicsQueueFamily = ~0u;

    DeviceCaps m_Caps{};

    // Copied rather than referenced: the callback outlives the DeviceDesc the
    // caller passed in, which is routinely a temporary.
    std::function<void(DiagnosticSeverity, std::string_view)> m_OnDiagnosticMessage;
    DiagnosticSeverity m_MinDiagnosticSeverity = DiagnosticSeverity::Info;
};
} // namespace Rhi::Vulkan
