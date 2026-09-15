#pragma once

#include <cstdint>

#include <engine/IUiBackend.h>

namespace Hikari::Editor
{

/**
 * ImGui over Vulkan and SDL3.
 *
 * Everything that names Vulkan in the UI path lives here. The engine holds the
 * interface, CreateUiBackend builds this for a Vulkan run, and D3D12UiBackend is
 * the same thing for the other backend.
 */
class VulkanUiBackend final : public Engine::IUiBackend
{
public:
    ~VulkanUiBackend() override;

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
};

} // namespace Hikari::Editor
