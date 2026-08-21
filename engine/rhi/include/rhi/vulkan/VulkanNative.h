#pragma once

#include <cstdint>
#include <memory>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/Handles.h>
#include <rhi/ICommandList.h>
#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>

// The one sanctioned way to get a Vulkan handle out of an IDevice.
//
// It exists because ImGui's Vulkan backend needs raw instance, physical device,
// device, queue family index and queue handles, and wrapping ImGui to avoid that
// is not worth doing. Anything that reaches in here is by definition
// backend-specific and will not compile against a second backend — which is the
// point: the leak is *listed*, in one file, rather than spread through the
// renderer where nobody can count it.
//
// The RAII accessors below are a wider hole than the ImGui one, and a temporary
// one. They exist because the renderer still creates Vulkan objects directly —
// swapchains, pipelines, descriptor sets — and cannot do that from raw C
// handles. Every resource type that moves behind IDevice removes callers from
// this list, and the last of them removes the accessors.
namespace Rhi::Vulkan
{
// Raw handles, for C APIs such as ImGui that take them by value.
struct NativeDevice
{
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    uint32_t GraphicsQueueFamily = ~0u;
    uint32_t ApiVersion = 0;
};

NativeDevice GetNative(IDevice& device);

// Transitional accessors for code that still builds Vulkan objects itself.
// Each returns a reference into the device, so it stays valid for as long as the
// device does and must not outlive it.
//
// There is deliberately no instance accessor here: nothing outside this module
// needs one now that surface creation lives inside it, and the raw handle is
// already in NativeDevice for ImGui's benefit.
vk::raii::PhysicalDevice& GetPhysicalDevice(IDevice& device);
vk::raii::Device& GetDevice(IDevice& device);
vk::raii::SurfaceKHR& GetSurface(IDevice& device);
vk::raii::Queue& GetGraphicsQueue(IDevice& device);
uint32_t GetGraphicsQueueFamily(IDevice& device);

// The buffer a handle names, or a null vk::Buffer if the handle is stale.
//
// Wider than it looks, and the narrowest thing that works today: the renderer
// still records draws and writes descriptor sets itself, and vkCmdBindVertexBuffers,
// vkCmdBindIndexBuffer, vkCmdCopyBufferToImage and VkDescriptorBufferInfo all
// take a VkBuffer. Every one of those call sites moves behind ICommandList or a
// neutral descriptor model later; this goes away with the last of them.
vk::Buffer GetBuffer(IDevice& device, BufferHandle handle);

// The view and sampler a handle names, or a null object if the handle is stale.
// Same shape as GetBuffer and there for the same reason: descriptor writes
// (VkDescriptorImageInfo) and dynamic rendering attachments
// (vk::RenderingAttachmentInfo) both take raw Vulkan objects, and both still
// happen in the renderer — descriptor writes until bindless, attachments until
// Stage 8's frame graph. They go away with those call sites.
//
// There is deliberately no GetImage: nothing outside this module names an image
// any more. Barriers and copies take a TextureHandle through ICommandList, and
// that is the whole of what a VkImage was reached for.
vk::ImageView GetImageView(IDevice& device, TextureViewHandle handle);
vk::Sampler GetSampler(IDevice& device, SamplerHandle handle);

// Adopts a VkImage the device did not allocate, so that barriers, views and
// copies can name it by handle. Destroying the returned handle frees nothing.
//
// This exists for the swapchain and nothing else. Its images belong to the
// presentation engine, and the swapchain itself stays in the renderer until
// Stage 6's IPresentTarget takes it — at which point IPresentTarget hands out
// the handle and this goes away.
TextureHandle RegisterExternalTexture(IDevice& device, vk::Image image, const TextureDesc& desc);

// A neutral command list recording into a VkCommandBuffer the caller owns and
// submits. Records only barriers and copies; draws stay on the raw buffer until
// Stage 8 (plan D7, D8).
//
// Returns an owning pointer because the concrete type is module-private. It
// holds no Vulkan resource of its own, so destroying it records and frees
// nothing — the command buffer's lifetime is entirely the caller's.
[[nodiscard]] std::unique_ptr<ICommandList> WrapCommandList(IDevice& device, vk::CommandBuffer cmd);

// vk::Format for a neutral one and back.
//
// Needed because format selection and pipeline creation are still Vulkan-shaped
// in the renderer: the depth format is chosen by querying
// VkFormatProperties, and PipelineBuilder takes vk::Format for the dynamic-
// rendering attachment formats (D8). Both move behind the neutral API when
// pipeline creation does. Throws on a vk::Format with no neutral equivalent —
// Rhi::Format is curated, not a mirror (D11).
vk::Format GetNativeFormat(Format format);
Format FromNativeFormat(vk::Format format);
} // namespace Rhi::Vulkan
