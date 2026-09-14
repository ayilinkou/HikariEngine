#include "D3D12UiBackend.h"

#include <format>
#include <stdexcept>

#include <SDL3/SDL.h>

#include <rhi/d3d12/D3D12Native.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_sdl3.h"

#include <core/Log.h>

namespace Hikari::Editor
{

namespace
{
constexpr Core::LogCategory LogUi("UI Backend");

/** The device the callbacks below allocate from, carried in ImGui's user data. */
Rhi::IDevice& DeviceOf(const ImGui_ImplDX12_InitInfo* pInfo)
{
    return *static_cast<Rhi::IDevice*>(pInfo->UserData);
}
} // namespace

D3D12UiBackend::~D3D12UiBackend()
{
    Shutdown();
}

void D3D12UiBackend::Init(const Engine::UiBackendDesc& desc)
{
    Core::LogMsg(Core::LogSeverity::Info, LogUi, "Init()");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    const Rhi::D3D12::NativeDevice native = Rhi::D3D12::GetNative(*desc.pDevice);

    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device = native.Device;
    initInfo.CommandQueue = native.GraphicsQueue;

    // Sizes ImGui's ring of vertex and index buffers, which it rewrites through a
    // mapping each frame, so it must be at least the frames in flight: a shorter ring
    // would be rewritten while an earlier frame was still reading it.
    initInfo.NumFramesInFlight = static_cast<int>(desc.RingSize);
    initInfo.RTVFormat = Rhi::D3D12::GetNativeFormat(desc.TargetFormat);
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.UserData = desc.pDevice;

    // The device's own resource heap rather than one for ImGui: at most one resource
    // heap is bound to a list at a time, and the list ImGui records into already has
    // the device's bound.
    initInfo.SrvDescriptorHeap = native.ResourceHeap;
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* pInfo,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpu)
    {
        const Rhi::D3D12::NativeDescriptor descriptor =
            Rhi::D3D12::AllocateResourceDescriptor(DeviceOf(pInfo));
        *pOutCpu = descriptor.Cpu;
        *pOutGpu = descriptor.Gpu;
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* pInfo, D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    { Rhi::D3D12::FreeResourceDescriptor(DeviceOf(pInfo), gpu); };

    // As for Vulkan, the platform half is the only one that needs a window, and a
    // windowless caller sets io.DisplaySize and io.DeltaTime itself.
    m_bHasPlatformBackend = desc.pNativeWindowHandle != nullptr;
    if (m_bHasPlatformBackend)
        ImGui_ImplSDL3_InitForD3D(static_cast<SDL_Window*>(desc.pNativeWindowHandle));

    ImGui_ImplDX12_Init(&initInfo);
    m_bInitialised = true;
    m_TargetFormat = desc.TargetFormat;

    // Created here rather than left to the first NewFrame, which creates them lazily
    // and reports a failure only through an assertion a release build compiles out.
    if (!ImGui_ImplDX12_CreateDeviceObjects())
        throw std::runtime_error("ImGui's DX12 backend could not create its pipeline state.");
}

void D3D12UiBackend::Shutdown()
{
    if (!m_bInitialised)
        return;

    Core::LogMsg(Core::LogSeverity::Info, LogUi, "Shutdown()");

    ImGui_ImplDX12_Shutdown();
    if (m_bHasPlatformBackend)
        ImGui_ImplSDL3_Shutdown();

    ImGui::DestroyContext();
    m_bInitialised = false;
}

void D3D12UiBackend::NewFrame()
{
    ImGui_ImplDX12_NewFrame();
    if (m_bHasPlatformBackend)
        ImGui_ImplSDL3_NewFrame();
}

void D3D12UiBackend::Render(Rhi::ICommandList& commandList)
{
    // Null when nothing built a UI frame, as VulkanUiBackend explains.
    ImDrawData* pDrawData = ImGui::GetDrawData();
    if (pDrawData == nullptr)
        return;

    ImGui_ImplDX12_RenderDrawData(pDrawData, Rhi::D3D12::GetNative(commandList));
}

/**
 * ImGui's DX12 backend takes the target format once, at Init, and has nothing to be
 * told about the image count: its ring is sized by the frames in flight. So only a
 * change of format matters, and neither D3D12 target changes its format on a
 * recreate — the swapchain resizes its buffers in the format it was made with — so one
 * that did is refused rather than drawn into with a pipeline state made for another.
 */
void D3D12UiBackend::OnTargetRecreated(uint32_t, Rhi::Format targetFormat)
{
    if (targetFormat != m_TargetFormat)
    {
        throw std::logic_error(
            "D3D12UiBackend::OnTargetRecreated: the target's format changed on a recreate, "
            "which no D3D12 target does; ImGui's pipeline state was made for the old one.");
    }
}

void D3D12UiBackend::ProcessPlatformEvent(const void* pEvent)
{
    if (m_bHasPlatformBackend)
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(pEvent));
}

} // namespace Hikari::Editor
