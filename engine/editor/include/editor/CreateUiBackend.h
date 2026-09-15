#pragma once

#include <memory>

#include <rhi/Backend.h>

#include <engine/IUiBackend.h>

namespace Hikari::Editor
{

/**
 * The UI backend for a run on `backend`: ImGui's integration with that graphics API
 * and with SDL3.
 *
 * A factory rather than a class an app names, because the backend is chosen at run
 * time and the choice belongs beside the one that creates the device. Throws for a
 * backend this build does not contain, as Rhi::CreateDevice does.
 */
[[nodiscard]] std::unique_ptr<Engine::IUiBackend> CreateUiBackend(Rhi::Backend backend);

} // namespace Hikari::Editor
