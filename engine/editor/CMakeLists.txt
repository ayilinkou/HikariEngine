# The UI stack. Above Engine rather than below it: an app asks CreateUiBackend for
# the backend matching its device and hands it to the engine, which calls it
# through IUiBackend and never names ImGui's graphics integration itself.
engine_module(
  Editor
  SOURCES
  src/CreateUiBackend.cpp
  src/VulkanUiBackend.cpp
  LINK_LIBRARIES
  Engine::Core
  Engine::Platform
  Engine::RHI
  Engine::Engine
  SDL3::SDL3
  Vulkan::Vulkan
  imgui::imgui)

# ImGui over D3D12, where the RHI builds that backend. HIKARI_EDITOR_D3D12 is what
# CreateUiBackend keys its D3D12 case on, defined with the source and nowhere else,
# as the RHI's own dispatcher does.
if(WIN32)
  target_sources(Editor PRIVATE src/D3D12UiBackend.cpp)
  set_source_files_properties(src/D3D12UiBackend.cpp
                              PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN;NOMINMAX")
  target_compile_definitions(Editor PRIVATE HIKARI_EDITOR_D3D12)
  target_link_libraries(Editor PRIVATE Microsoft::DirectX-Headers d3d12.lib dxgi.lib)
endif()
