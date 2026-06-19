#pragma once

#include <fstream>
#include <string>
#include <vector>

template <typename T>
inline void SetVkDebugName(vk::raii::Device& device, T handle,
                           vk::ObjectType objectType, const char* name)
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
inline uint32_t
ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    uint32_t minCount = std::max(3u, capabilities.minImageCount);

    // maxImageCount == 0 indicates that there is no maximum
    if ((0 < capabilities.maxImageCount) &&
        (capabilities.maxImageCount < minCount))
        minCount = capabilities.maxImageCount;
    return minCount;
}

inline uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice,
                               uint32_t typeFilter,
                               vk::MemoryPropertyFlags properties)
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
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::DeviceSize size, vk::BufferUsageFlags usage,
                         vk::MemoryPropertyFlags properties,
                         vk::raii::Buffer& buffer,
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
BeginSingleTimeCommand(vk::raii::Device& device, vk::CommandPool commandPool)
{
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer =
        std::move(device.allocateCommandBuffers(allocInfo).front());
    SetVkDebugName(device, *commandBuffer, vk::ObjectType::eCommandBuffer,
                   "Single Use Command Buffer");
    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    commandBuffer.begin(beginInfo);
    return commandBuffer;
}

inline void EndSingleTimeCommand(vk::CommandBuffer commandBuffer,
                                 vk::Queue queue)
{
    commandBuffer.end();
    vk::SubmitInfo submitInfo{.commandBufferCount = 1,
                              .pCommandBuffers = &commandBuffer};
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();
}

inline void CopyBuffer(vk::raii::Device& device,
                       vk::raii::CommandPool& commandPool,
                       vk::raii::Queue& transferQueue,
                       vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer,
                       vk::DeviceSize size)
{
    vk::raii::CommandBuffer commandCopyBuffer =
        BeginSingleTimeCommand(device, commandPool);
    commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer,
                                 vk::BufferCopy{0, 0, size});
    EndSingleTimeCommand(commandCopyBuffer, transferQueue);
}

[[nodiscard]] inline vk::raii::ImageView
CreateImageView(vk::raii::Device& device, const vk::Image& image,
                vk::Format format, vk::ImageAspectFlags aspectFlags)
{
    vk::ImageViewCreateInfo createInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {aspectFlags, 0, 1, 0, 1}};
    return vk::raii::ImageView(device, createInfo);
}

inline void CreateImage(vk::raii::Device& device,
                        vk::raii::PhysicalDevice& physicalDevice,
                        uint32_t width, uint32_t height, vk::Format format,
                        vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                        vk::MemoryPropertyFlags properties,
                        vk::raii::Image& image,
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
                                   .sharingMode = vk::SharingMode::eExclusive};
    image = vk::raii::Image(device, createInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(
            physicalDevice, memRequirements.memoryTypeBits, properties)};
    imageMemory = vk::raii::DeviceMemory(device, allocInfo);
    image.bindMemory(imageMemory, 0);
}

inline void TransitionImageLayout(vk::raii::CommandBuffer& cmd,
                                  const vk::raii::Image& image,
                                  vk::ImageLayout oldLayout,
                                  vk::ImageLayout newLayout,
                                  vk::ImageAspectFlags aspectFlags)
{
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
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eHost;
        barrier.srcAccessMask = vk::AccessFlagBits2::eHostWrite;

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
    }
    else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
             newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    }
    else if (newLayout == vk::ImageLayout::eDepthAttachmentOptimal &&
             aspectFlags == vk::ImageAspectFlagBits::eDepth)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                               vk::PipelineStageFlagBits2::eLateFragmentTests;
        barrier.srcAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                               vk::PipelineStageFlagBits2::eLateFragmentTests;
        barrier.dstAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    }
    else if (oldLayout == vk::ImageLayout::eDepthAttachmentOptimal &&
             newLayout == vk::ImageLayout::eDepthReadOnlyOptimal)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                               vk::PipelineStageFlagBits2::eLateFragmentTests;
        barrier.srcAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                               vk::PipelineStageFlagBits2::eLateFragmentTests;
        barrier.dstAccessMask =
            vk::AccessFlagBits2::eDepthStencilAttachmentRead;
    }
    else if (oldLayout == vk::ImageLayout::eUndefined &&
             newLayout == vk::ImageLayout::eColorAttachmentOptimal)
    {
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
        barrier.srcAccessMask = vk::AccessFlagBits2::eNone;

        barrier.dstStageMask =
            vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal &&
             newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcStageMask =
            vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;

        barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    }
    else
    {
        throw std::runtime_error("Unsupported layout transition!");
    }

    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &barrier};
    cmd.pipelineBarrier2(dependencyInfo);
}

inline void CopyBufferToImage(vk::raii::CommandBuffer& cmd,
                              const vk::raii::Buffer& buffer,
                              vk::raii::Image& image, uint32_t width,
                              uint32_t height)
{
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}};
    cmd.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal,
                          {region});
}

inline void CreateVertexBuffer(vk::raii::Device& device,
                               vk::raii::PhysicalDevice& physicalDevice,
                               vk::raii::CommandPool& commandPool,
                               vk::raii::Queue& transferQueue,
                               size_t elementSize, size_t vertexCount,
                               void* pData, vk::raii::Buffer& vertexBuffer,
                               vk::raii::DeviceMemory& bufferMemory)
{
    vk::DeviceSize bufferSize = elementSize * vertexCount;

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostCoherent |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, pData, static_cast<size_t>(bufferSize));
    stagingBufferMemory.unmapMemory();

    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eVertexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, vertexBuffer,
                 bufferMemory);

    CopyBuffer(device, commandPool, transferQueue, stagingBuffer, vertexBuffer,
               bufferSize);
}

inline void CreateIndexBuffer(vk::raii::Device& device,
                              vk::raii::PhysicalDevice& physicalDevice,
                              vk::raii::CommandPool& commandPool,
                              vk::raii::Queue& transferQueue,
                              size_t elementSize, size_t indexCount,
                              void* pData, vk::raii::Buffer& indexBuffer,
                              vk::raii::DeviceMemory& bufferMemory)
{
    vk::DeviceSize bufferSize = elementSize * indexCount;

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostCoherent |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, pData, static_cast<size_t>(bufferSize));
    stagingBufferMemory.unmapMemory();

    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eIndexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, indexBuffer,
                 bufferMemory);

    CopyBuffer(device, commandPool, transferQueue, stagingBuffer, indexBuffer,
               bufferSize);
}
