#pragma once

#include <fstream>

#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "Barrier.h"
#include "Texture.h"

template <typename T>
inline void SetVkDebugName([[maybe_unused]] vk::raii::Device& device, [[maybe_unused]] T handle,
                           [[maybe_unused]] vk::ObjectType objectType,
                           [[maybe_unused]] const char* name)
{
#ifdef DEBUG
    // convert vk:: C++ types into C types
    // eg. vk::Image -> VkImage
    using CType = decltype(static_cast<typename T::CType>(handle));

    vk::DebugUtilsObjectNameInfoEXT nameInfo{
        .objectType = objectType,
        .objectHandle = reinterpret_cast<uint64_t>(static_cast<CType>(handle)),
        .pObjectName = name};
    device.setDebugUtilsObjectNameEXT(nameInfo);
#endif
}

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
inline vk::SurfaceFormatKHR ChooseSwapchainFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    if (formats.empty())
        throw std::runtime_error("No surface formats available!");

    const auto formatIt =
        std::ranges::find_if(formats,
                             [](const auto& format)
                             {
                                 return format.format == vk::Format::eB8G8R8A8Unorm &&
                                        format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                             });

    return formatIt != formats.end() ? *formatIt : formats[0];
}

// Chooses mailbox presentation mode if available. Falls back to FIFO.
inline vk::PresentModeKHR ChoosePresentMode(const std::vector<vk::PresentModeKHR>& modes)
{
    if (modes.empty())
        throw std::runtime_error("No swapchain presentation modes available!");

    const auto modeIt = std::ranges::find_if(modes, [](const auto& mode)
                                             { return mode == vk::PresentModeKHR::eMailbox; });
    return modeIt != modes.end() ? *modeIt : vk::PresentModeKHR::eFifo;
}

inline vk::Extent2D ChooseSwapchainExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                          SDL_Window* window)
{
    // Some window managers allow resolutions which don't match the window. They
    // symbol this with max value of a uint32_t.
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
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
    if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < minCount))
        minCount = capabilities.maxImageCount;
    return minCount;
}

// Shouldn't need to use this anymore since I am now using VMA.
inline uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter,
                               vk::MemoryPropertyFlags properties)
{
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("Failed to find a suitable memory type!");
}

[[nodiscard]] inline AllocatedBuffer CreateBuffer(VmaAllocator allocator, vk::DeviceSize size,
                                                  vk::BufferUsageFlags bufferUsage,
                                                  VmaMemoryUsage memoryUsage,
                                                  VmaAllocationCreateFlags allocFlags = 0)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(size);
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(bufferUsage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocFlags;

    VkBuffer rawBuffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;

    vk::Result result = static_cast<vk::Result>(vmaCreateBuffer(
        allocator, &bufferInfo, &allocInfo, &rawBuffer, &allocation, &allocationInfo));

    if (result != vk::Result::eSuccess)
        throw std::runtime_error("Failed to create buffer via VMA!");

    return AllocatedBuffer(allocator, vk::Buffer(rawBuffer), allocation, allocationInfo);
}

[[nodiscard]] inline vk::raii::CommandBuffer BeginSingleTimeCommand(vk::raii::Device& device,
                                                                    vk::CommandPool commandPool)
{
    vk::CommandBufferAllocateInfo allocInfo{.commandPool = commandPool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer =
        std::move(device.allocateCommandBuffers(allocInfo).front());
    SetVkDebugName(device, *commandBuffer, vk::ObjectType::eCommandBuffer,
                   "Single Use Command Buffer");
    vk::CommandBufferBeginInfo beginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    commandBuffer.begin(beginInfo);
    return commandBuffer;
}

inline void EndSingleTimeCommand(vk::CommandBuffer commandBuffer, vk::Queue queue)
{
    commandBuffer.end();
    vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();
}

inline void CopyBuffer(vk::raii::Device& device, vk::raii::CommandPool& commandPool,
                       vk::raii::Queue& transferQueue, vk::Buffer srcBuffer, vk::Buffer dstBuffer,
                       vk::DeviceSize size)
{
    vk::raii::CommandBuffer commandCopyBuffer = BeginSingleTimeCommand(device, commandPool);
    commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, vk::BufferCopy{0, 0, size});
    EndSingleTimeCommand(commandCopyBuffer, transferQueue);
}

[[nodiscard]] inline vk::raii::ImageView
CreateImageView(vk::raii::Device& device, const vk::Image& image, vk::ImageViewType imageViewType,
                vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t layerCount)
{
    vk::ImageViewCreateInfo createInfo{.image = image,
                                       .viewType = imageViewType,
                                       .format = format,
                                       .subresourceRange = {.aspectMask = aspectFlags,
                                                            .baseMipLevel = 0u,
                                                            .levelCount = 1u,
                                                            .baseArrayLayer = 0u,
                                                            .layerCount = layerCount}};
    return vk::raii::ImageView(device, createInfo);
}

[[nodiscard]] inline AllocatedImage CreateImage(VmaAllocator allocator,
                                                vk::ImageCreateInfo imageInfo)
{
    VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage rawImage;
    VmaAllocation allocation;

    vk::Result result = static_cast<vk::Result>(
        vmaCreateImage(allocator, &cImageInfo, &allocInfo, &rawImage, &allocation, nullptr));

    if (result != vk::Result::eSuccess)
        throw std::runtime_error("Failed to create image via VMA!");

    return AllocatedImage(allocator, vk::Image(rawImage), allocation);
}

// TODO: collect barriers and group them into a single pipelineBarrier2 call
inline void RecordImageBarrier(vk::raii::CommandBuffer& cmd, vk::Image image,
                               const ImageBarrierDesc& desc)
{
    const vk::ImageSubresourceRange range{.aspectMask = desc.aspect,
                                          .baseMipLevel = desc.baseMip,
                                          .levelCount = desc.mipCount,
                                          .baseArrayLayer = desc.baseLayer,
                                          .layerCount = desc.layerCount};

    const vk::ImageMemoryBarrier2 barrier{.srcStageMask = desc.srcStage,
                                          .srcAccessMask = desc.srcAccess,
                                          .dstStageMask = desc.dstStage,
                                          .dstAccessMask = desc.dstAccess,
                                          .oldLayout = desc.oldLayout,
                                          .newLayout = desc.newLayout,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .image = image,
                                          .subresourceRange = range};

    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &barrier};
    cmd.pipelineBarrier2(dependencyInfo);
}

inline void RecordImageBarrier(vk::raii::CommandBuffer& cmd, const AllocatedImage& image,
                               const ImageBarrierDesc& desc)
{
    RecordImageBarrier(cmd, image.Image, desc);
}

inline void CopyBufferToImage(vk::raii::CommandBuffer& cmd, vk::Buffer buffer, vk::Image image,
                              uint32_t width, uint32_t height, uint32_t layerCount = 1u)
{
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, layerCount},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}};
    cmd.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
}

[[nodiscard]] inline AllocatedBuffer
CreateStagedBuffer(VmaAllocator allocator, vk::raii::Device& device,
                   vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
                   vk::DeviceSize bufferSize, vk::BufferUsageFlags usage, void* pData)
{
    AllocatedBuffer stagingBuffer = CreateBuffer(
        allocator, bufferSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    memcpy(stagingBuffer.AllocationInfo.pMappedData, pData, static_cast<size_t>(bufferSize));

    AllocatedBuffer gpuBuffer =
        CreateBuffer(allocator, bufferSize, usage | vk::BufferUsageFlagBits::eTransferDst,
                     VMA_MEMORY_USAGE_AUTO);

    CopyBuffer(device, commandPool, transferQueue, vk::Buffer(stagingBuffer.Buffer),
               vk::Buffer(gpuBuffer.Buffer), bufferSize);

    return gpuBuffer;
}

[[nodiscard]] inline Texture CreateRenderTexture(VmaAllocator allocator, vk::raii::Device& device,
                                                 uint32_t width, uint32_t height, vk::Format format,
                                                 vk::ImageUsageFlags usage,
                                                 vk::ImageAspectFlags aspect,
                                                 const std::string& name)
{
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D{width, height, 1};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.format = format;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    AllocatedImage image = CreateImage(allocator, imageInfo);
    vk::raii::ImageView imageView =
        CreateImageView(device, image.Image, vk::ImageViewType::e2D, format, aspect, 1u);

    SetVkDebugName(device, image.Image, vk::ObjectType::eImage, name.c_str());
    SetVkDebugName(device, *imageView, vk::ObjectType::eImageView, (name + " View").c_str());
    vmaSetAllocationName(allocator, image.Allocation, (name + " allocation").c_str());
    Texture tex(std::move(image), std::move(imageView), name.c_str());
    return tex;
}

inline void EnsureParentDirectoryExists(std::string_view path)
{
    std::filesystem::path p(path);
    if (p.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec)
        {
            throw std::runtime_error(std::format("Failed to create directory {}: {}",
                                                 p.parent_path().string(), ec.message()));
        }
    }
}

inline std::string EnsureExtension(const std::string& path, const std::string& ext)
{
    std::filesystem::path p(path);
    if (p.extension() != ext)
        p.replace_extension(ext);
    return p.string();
}
