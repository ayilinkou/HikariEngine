#include "d3d12/D3D12OffscreenTarget.h"

#include <array>
#include <format>
#include <stdexcept>

#include <core/Log.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>

#include "d3d12/D3D12Device.h"

namespace Hikari::Rhi::D3D12
{
namespace
{
constexpr Core::LogCategory LogRhi("RHI");

/**
 * Everything an offscreen image is used for: rendered into, copied out of for a
 * screenshot or a readback, and sampled by a pass that composites over it.
 */
constexpr TextureUsage kOffscreenUsage =
    TextureUsage::ColorAttachment | TextureUsage::CopySrc | TextureUsage::Sampled;

/**
 * BGRA8Unorm first, as the Vulkan target and its swapchain ask for it first, so a
 * capture comes out in the same byte order whichever backend and target made it.
 */
Format ChooseFormat(const D3D12Device& device)
{
    constexpr std::array kPreferred{Format::BGRA8Unorm, Format::RGBA8Unorm};

    for (const Format format : kPreferred)
    {
        if (device.IsFormatSupported(format, kOffscreenUsage))
            return format;
    }

    throw std::runtime_error("Rhi::IDevice::CreatePresentTarget: this device supports neither "
                             "BGRA8Unorm nor RGBA8Unorm as an offscreen render target.");
}
} // namespace

D3D12OffscreenTarget::D3D12OffscreenTarget(D3D12Device& device, const PresentTargetDesc& desc)
    : m_Device(device), m_ImageCount(desc.FramesInFlight)
{
    if (desc.FramesInFlight == 0u)
        throw std::runtime_error("PresentTargetDesc::FramesInFlight must be at least 1.");

    m_Format = ChooseFormat(m_Device);
    Create(desc.Extent);
}

D3D12OffscreenTarget::~D3D12OffscreenTarget()
{
    Destroy();
}

void D3D12OffscreenTarget::Create(Core::Extent2D extent)
{
    m_Extent = extent;

    // Appended one at a time, so that a creation that throws part-way leaves Destroy
    // only images that exist.
    m_Images.reserve(m_ImageCount);
    for (uint32_t i = 0u; i < m_ImageCount; i++)
    {
        Image image{};
        image.Texture =
            m_Device.CreateTexture(TextureDesc{.Format = m_Format,
                                               .Extent = {m_Extent.Width, m_Extent.Height, 1u},
                                               .Usage = kOffscreenUsage,
                                               .DebugName = std::format("Offscreen Image_{}", i)});
        image.View = m_Device.CreateTextureView(TextureViewDesc{
            .Texture = image.Texture, .DebugName = std::format("Offscreen Image View_{}", i)});

        m_Images.push_back(image);
    }

    m_AcquireCount = 0u;

    Core::LogMsg(Core::LogSeverity::Info, LogRhi, "Offscreen target: {}x{}, {} images",
                 m_Extent.Width, m_Extent.Height, m_Images.size());
}

void D3D12OffscreenTarget::Destroy()
{
    for (const Image& image : m_Images)
    {
        m_Device.Destroy(image.View);
        m_Device.Destroy(image.Texture);
    }
    m_Images.clear();
}

AcquiredImage D3D12OffscreenTarget::Acquire()
{
    const uint32_t index = static_cast<uint32_t>(m_AcquireCount % m_Images.size());
    m_AcquireCount++;

    // No wait to arrange for the image's previous write: this image's next
    // submission runs on the same queue after it.
    Image& image = m_Images[index];
    image.State = PresentImageState::Acquired;

    AcquiredImage acquired{};
    acquired.Texture = image.Texture;
    acquired.View = image.View;
    acquired.Index = index;
    acquired.bNeedsRecreate = false;
    return acquired;
}

void D3D12OffscreenTarget::MarkSubmitted(uint32_t index)
{
    if (index >= m_Images.size())
        throw std::runtime_error(
            "Rhi::IDevice::Submit: the present image's index is out of range.");

    Image& image = m_Images[index];
    if (image.State != PresentImageState::Acquired)
    {
        throw std::logic_error("Rhi::IDevice::Submit: the present image was not acquired, or a "
                               "submission has already named it since its Acquire.");
    }

    image.State = PresentImageState::Submitted;
}

bool D3D12OffscreenTarget::Present(uint32_t index)
{
    if (index >= m_Images.size())
        throw std::runtime_error("IPresentTarget::Present: index out of range.");

    Image& image = m_Images[index];
    if (image.State != PresentImageState::Submitted)
    {
        throw std::logic_error("IPresentTarget::Present: no submission named this image since "
                               "its Acquire.");
    }

    image.State = PresentImageState::Idle;
    return true;
}

bool D3D12OffscreenTarget::Recreate(Core::Extent2D newExtent)
{
    // A zero extent cannot be created, and gets a minimised swapchain's answer:
    // nothing was touched, ask again.
    if (newExtent.Width == 0u || newExtent.Height == 0u)
        return false;

    // The images may still be read or written by work in flight.
    m_Device.WaitIdle();

    Destroy();
    Create(newExtent);
    return true;
}
} // namespace Hikari::Rhi::D3D12
