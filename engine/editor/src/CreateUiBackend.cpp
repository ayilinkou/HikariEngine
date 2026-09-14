#include <editor/CreateUiBackend.h>

#include <format>
#include <stdexcept>

#include "VulkanUiBackend.h"

#ifdef HIKARI_EDITOR_D3D12
#include "D3D12UiBackend.h"
#endif

namespace Hikari::Editor
{

std::unique_ptr<Engine::IUiBackend> CreateUiBackend(Rhi::Backend backend)
{
    switch (backend)
    {
        case Rhi::Backend::Vulkan:
            return std::make_unique<VulkanUiBackend>();

        case Rhi::Backend::D3D12:
#ifdef HIKARI_EDITOR_D3D12
            return std::make_unique<D3D12UiBackend>();
#else
            // A build without it lists no D3D12 backend, so --backend refuses it first.
            break;
#endif
    }

    throw std::runtime_error(std::format("No UI backend for backend: {}", Rhi::ToString(backend)));
}

} // namespace Hikari::Editor
