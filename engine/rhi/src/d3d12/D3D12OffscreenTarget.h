#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <core/Extent2D.h>
#include <rhi/Handles.h>
#include <rhi/IPresentTarget.h>
#include <rhi/RhiTypes.h>

#include "d3d12/D3D12PresentTarget.h"

namespace Hikari::Rhi::D3D12
{
class D3D12Device;

/**
 * IPresentTarget over N textures this target owns, for a device with no window: the
 * D3D12 counterpart of the Vulkan backend's offscreen target, and the same shape.
 *
 * It creates its images through the neutral device calls alone. The format, the
 * extent and the count are decided here as that target decides them, so that a
 * headless capture comes out the same under either backend.
 */
class D3D12OffscreenTarget final : public D3D12PresentTarget
{
public:
    /** Throws if the device can back neither format an offscreen image can be created in. */
    D3D12OffscreenTarget(D3D12Device& device, const PresentTargetDesc& desc);
    ~D3D12OffscreenTarget() override;

    D3D12OffscreenTarget(const D3D12OffscreenTarget&) = delete;
    D3D12OffscreenTarget& operator=(const D3D12OffscreenTarget&) = delete;

    Format GetFormat() const override { return m_Format; }
    Core::Extent2D GetExtent() const override { return m_Extent; }
    uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_Images.size()); }

    /** Nothing: an offscreen target writes into images and never presents. */
    std::optional<PresentMode> GetPresentMode() const override { return std::nullopt; }

    /** None, for the reasons IPresentTarget gives: nothing presents these images. */
    TextureLayout GetRequiredFinalLayout() const override { return TextureLayout::Undefined; }

    [[nodiscard]] AcquiredImage Acquire() override;
    bool Present(uint32_t index) override;
    [[nodiscard]] bool Recreate(Core::Extent2D newExtent) override;

    bool BelongsTo(const D3D12Device& device) const override { return &m_Device == &device; }
    void MarkSubmitted(uint32_t index) override;

private:
    struct Image
    {
        TextureHandle Texture;
        TextureViewHandle View;
        PresentImageState State = PresentImageState::Idle;
    };

    void Create(Core::Extent2D extent);
    void Destroy();

    D3D12Device& m_Device;

    Core::Extent2D m_Extent{};
    Format m_Format = Format::Undefined;

    /** One image per frame in flight, as the Vulkan target decides it. */
    uint32_t m_ImageCount = 0u;

    std::vector<Image> m_Images;

    /** Which image the next Acquire hands out, before the modulo. */
    uint64_t m_AcquireCount = 0u;
};
} // namespace Hikari::Rhi::D3D12
