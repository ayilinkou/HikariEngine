#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "SDL3/SDL.h"

#include "vulkan/vulkan_raii.hpp"

inline std::vector<char> ReadFile(const std::string filename)
{
    // std::ios::ate starts to read at end of file so that we can get the size
    // of the buffer
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Failed to open file!");

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
}

// Chooses an ideal swapchain format if available, if not picks the first
// one.
inline vk::SurfaceFormatKHR
ChooseSwapchainFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    if (formats.empty())
        throw std::runtime_error("No surface formats available!");

    const auto formatIt = std::ranges::find_if(
        formats,
        [](const auto& format)
        {
            return format.format == vk::Format::eB8G8R8A8Srgb &&
                   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });

    return formatIt != formats.end() ? *formatIt : formats[0];
}

// Chooses mailbox presentation mode if available. Falls back to FIFO.
inline vk::PresentModeKHR
ChoosePresentMode(const std::vector<vk::PresentModeKHR>& modes)
{
    if (modes.empty())
        throw std::runtime_error("No swapchain presentation modes available!");

    const auto modeIt =
        std::ranges::find_if(modes, [](const auto& mode)
                             { return mode == vk::PresentModeKHR::eMailbox; });
    return modeIt != modes.end() ? *modeIt : vk::PresentModeKHR::eFifo;
}

inline vk::Extent2D
ChooseSwapchainExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                      SDL_Window* window)
{
    // Some window managers allow resolutions which don't match the window. They
    // symbol this with max value of a uint32_t.
    if (capabilities.currentExtent.width !=
        std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    // This has to be used rather than the raw window width and height as high
    // DPI displays might not match screen coordinates and pixels.
    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                                 capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)};
}

// Tries to get at least 3 images.
inline uint32_t ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    uint32_t minCount = std::max(3u, capabilities.minImageCount);

    // maxImageCount == 0 indicates that there is no maximum
    if ((0 < capabilities.maxImageCount) &&
        (capabilities.maxImageCount < minCount))
        minCount = capabilities.maxImageCount;
    return minCount;
}

inline uint32_t FindMemoryType(vk::raii::PhysicalDevice physicalDevice,
                        uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
    vk::PhysicalDeviceMemoryProperties memProperties =
        physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) ==
                properties)
            return i;
    }
    throw std::runtime_error("Failed to find a suitable memory type!");
}

inline void CreateBuffer(vk::raii::Device& device,
                  vk::raii::PhysicalDevice& physicalDevice, vk::DeviceSize size,
                  vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer,
                  vk::raii::DeviceMemory& bufferMemory)
{
    vk::BufferCreateInfo bufferInfo{.size = size,
                                    .usage = usage,
                                    .sharingMode = vk::SharingMode::eExclusive};
    buffer = vk::raii::Buffer(device, bufferInfo);

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo = {
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(
            physicalDevice, memRequirements.memoryTypeBits, properties)};

    bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
    buffer.bindMemory(*bufferMemory, 0);
}

inline vk::raii::CommandBuffer
BeginSingleTimeCommand(vk::raii::Device& device,
                       vk::raii::CommandPool& commandPool)
{
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer =
        std::move(device.allocateCommandBuffers(allocInfo).front());
    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    commandBuffer.begin(beginInfo);
    return commandBuffer;
}

inline void EndSingleTimeCommand(vk::raii::CommandBuffer& commandBuffer,
                          vk::raii::Queue queue)
{
    commandBuffer.end();
    vk::SubmitInfo submitInfo{.commandBufferCount = 1,
                              .pCommandBuffers = &*commandBuffer};
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();
}
inline void CopyBuffer(vk::raii::Device& device, vk::raii::CommandPool& commandPool,
                vk::raii::Queue& transferQueue, vk::raii::Buffer& srcBuffer,
                vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
{
    vk::raii::CommandBuffer commandCopyBuffer =
        BeginSingleTimeCommand(device, commandPool);
    commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer,
                                 vk::BufferCopy{0, 0, size});
    EndSingleTimeCommand(commandCopyBuffer, transferQueue);
}
