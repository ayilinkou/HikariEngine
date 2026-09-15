#pragma once

#include <cstdint>

#include <engine/IUiBackend.h>

namespace Hikari::Editor
{

/**
 * ImGui over D3D12 and SDL3: VulkanUiBackend's sibling, and everything that names
 * D3D12 in the UI path.
 *
 * ImGui's DX12 backend binds no descriptor heap of its own. It records into a list
 * that already has the device's heaps bound, and keeps its texture descriptors in the
 * device's resource heap, allocated one at a time through Rhi::D3D12's accessors.
 */
class D3D12UiBackend final : public Engine::IUiBackend
{
public:
    ~D3D12UiBackend() override;

    void Init(const Engine::UiBackendDesc& desc) override;
    void Shutdown() override;
    void NewFrame() override;
    void Render(Rhi::ICommandList& commandList) override;
    void OnTargetRecreated(uint32_t imageCount, Rhi::Format targetFormat) override;
    void ProcessPlatformEvent(const void* pEvent) override;

private:
    bool m_bInitialised = false;

    /** False for a run with no window, which has no platform half to drive. */
    bool m_bHasPlatformBackend = false;

    /** What ImGui's pipeline state was created to render into. */
    Rhi::Format m_TargetFormat = Rhi::Format::Undefined;
};

} // namespace Hikari::Editor
