#include "d3d12/D3D12SwapchainTarget.h"

#include <algorithm>
#include <format>
#include <stdexcept>

#include <SDL3/SDL.h>

#include <core/Log.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>

#include "d3d12/D3D12Conversions.h"
#include "d3d12/D3D12Device.h"

namespace Hikari::Rhi::D3D12
{
namespace
{
constexpr Core::LogCategory LogRhi("RHI");

using Microsoft::WRL::ComPtr;

/**
 * What a back buffer is used for: rendered into, and copied out of for a capture. The
 * swapchain's buffers are always render targets, and D3D12 lets a back buffer be a
 * copy source.
 */
constexpr TextureUsage kBackBufferUsage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;

std::string HexResult(HRESULT hr)
{
    return std::format("0x{:08X}", static_cast<uint32_t>(hr));
}
} // namespace

D3D12SwapchainTarget::D3D12SwapchainTarget(D3D12Device& device, void* pWindow,
                                           const PresentTargetDesc& desc)
    : m_Device(device)
{
    if (desc.FramesInFlight == 0u)
        throw std::runtime_error("PresentTargetDesc::FramesInFlight must be at least 1.");

    // Asked of SDL, as the Vulkan backend asks SDL for its surface: the platform hands
    // the RHI its SDL window whatever the backend.
    const HWND hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(static_cast<SDL_Window*>(pWindow)),
                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr)
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreatePresentTarget: the window has no HWND ({}).", SDL_GetError()));
    }

    // Mailbox, the Vulkan target's first preference, which DXGI's flip model always
    // offers; Immediate would need tearing support asked of the factory, and only a
    // request for a particular mode would reach it.
    IDXGIFactory4& factory = m_Device.GetFactory();
    m_PresentMode = PresentMode::Mailbox;
    m_SwapChainFlags = 0u;

    // Two buffers at least, which the flip model requires, and one per frame in flight
    // beyond that, as the offscreen target makes one per frame in flight.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = desc.Extent.Width;
    swapChainDesc.Height = desc.Extent.Height;
    swapChainDesc.Format = ToDxgi(m_Format);
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = std::max(2u, desc.FramesInFlight);
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapChainDesc.Flags = m_SwapChainFlags;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = factory.CreateSwapChainForHwnd(&m_Device.GetDirectQueue(), hwnd, &swapChainDesc,
                                                nullptr, nullptr, &swapChain1);
    m_Device.DrainDebugMessages();
    if (FAILED(hr) || FAILED(swapChain1.As(&m_SwapChain)))
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreatePresentTarget: creating the DXGI swapchain failed ({}).",
            HexResult(hr)));
    }

    // The platform owns switching between windowed and fullscreen; DXGI's own Alt+Enter
    // would change the window behind its back.
    factory.MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // A zero extent asks DXGI for the window's client size, so the buffers say what
    // was made.
    DXGI_SWAP_CHAIN_DESC1 created{};
    m_SwapChain->GetDesc1(&created);
    m_Extent = Core::Extent2D{created.Width, created.Height};

    CreateImages();
}

D3D12SwapchainTarget::~D3D12SwapchainTarget()
{
    DestroyImages();
}

void D3D12SwapchainTarget::CreateImages()
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    m_SwapChain->GetDesc1(&swapChainDesc);

    m_Images.reserve(swapChainDesc.BufferCount);
    for (UINT i = 0u; i < swapChainDesc.BufferCount; ++i)
    {
        ComPtr<ID3D12Resource> buffer;
        const HRESULT hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&buffer));
        if (FAILED(hr))
        {
            throw std::runtime_error(std::format(
                "Getting back buffer {} of the DXGI swapchain failed ({}).", i, HexResult(hr)));
        }

        // Registered rather than created: the buffers belong to the swapchain, and a
        // handle is how the rest of the RHI names one.
        Image image{};
        image.Texture = m_Device.RegisterExternalTexture(
            std::move(buffer), TextureDesc{.Format = m_Format,
                                           .Extent = {m_Extent.Width, m_Extent.Height, 1u},
                                           .Usage = kBackBufferUsage,
                                           .DebugName = std::format("Swapchain Image_{}", i)});
        image.View = m_Device.CreateTextureView(TextureViewDesc{
            .Texture = image.Texture, .DebugName = std::format("Swapchain Image View_{}", i)});

        m_Images.push_back(image);
    }

    Core::LogMsg(Core::LogSeverity::Info, LogRhi, "Swapchain: {}x{}, {} images, sync interval {}{}",
                 m_Extent.Width, m_Extent.Height, m_Images.size(),
                 m_PresentMode == PresentMode::Fifo ? 1 : 0,
                 m_PresentMode == PresentMode::Immediate ? ", tearing" : "");
}

void D3D12SwapchainTarget::DestroyImages()
{
    // Every reference to a back buffer has to go before ResizeBuffers, which refuses a
    // swapchain whose buffers are still held; destroying the texture releases its one.
    for (const Image& image : m_Images)
    {
        m_Device.Destroy(image.View);
        m_Device.Destroy(image.Texture);
    }
    m_Images.clear();
}

AcquiredImage D3D12SwapchainTarget::Acquire()
{
    // The flip model rotates the buffers itself: the one to write is whichever it
    // names now, and presenting advances it.
    const uint32_t index = m_SwapChain->GetCurrentBackBufferIndex();
    if (index >= m_Images.size())
        throw std::runtime_error("The DXGI swapchain named a back buffer it does not have.");

    Image& image = m_Images[index];
    image.State = PresentImageState::Acquired;

    return AcquiredImage{
        .Texture = image.Texture, .View = image.View, .Index = index, .bNeedsRecreate = false};
}

void D3D12SwapchainTarget::MarkSubmitted(uint32_t index)
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

bool D3D12SwapchainTarget::Present(uint32_t index)
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

    const UINT syncInterval = m_PresentMode == PresentMode::Fifo ? 1u : 0u;
    const UINT flags = m_PresentMode == PresentMode::Immediate ? DXGI_PRESENT_ALLOW_TEARING : 0u;

    const HRESULT hr = m_SwapChain->Present(syncInterval, flags);
    m_Device.DrainDebugMessages();

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        throw std::runtime_error(std::format(
            "Presenting failed because the D3D12 device was removed ({}; reason {}).",
            HexResult(hr), HexResult(m_Device.GetNativeDevice().GetDeviceRemovedReason())));
    }

    if (FAILED(hr))
        throw std::runtime_error(std::format("Presenting failed ({}).", HexResult(hr)));

    // DXGI_STATUS_OCCLUDED is a success: the window cannot be seen, and nothing about
    // the swapchain needs rebuilding for that. A resize is not reported here at all —
    // the flip model stretches a mismatched buffer — so the platform's resize event is
    // what recreates the target.
    return true;
}

bool D3D12SwapchainTarget::Recreate(Core::Extent2D newExtent)
{
    // A window with no area cannot back buffers; nothing is touched and the caller
    // asks again, as a minimised window makes a Vulkan swapchain answer.
    if (newExtent.Width == 0u || newExtent.Height == 0u)
        return false;

    // The buffers may still be written or presented by work in flight.
    m_Device.WaitIdle();

    DestroyImages();

    // Zero keeps the buffer count and UNKNOWN the format.
    const HRESULT hr = m_SwapChain->ResizeBuffers(0u, newExtent.Width, newExtent.Height,
                                                  DXGI_FORMAT_UNKNOWN, m_SwapChainFlags);
    m_Device.DrainDebugMessages();
    if (FAILED(hr))
    {
        throw std::runtime_error(std::format("Resizing the DXGI swapchain to {}x{} failed ({}).",
                                             newExtent.Width, newExtent.Height, HexResult(hr)));
    }

    m_Extent = newExtent;
    CreateImages();
    return true;
}
} // namespace Hikari::Rhi::D3D12
