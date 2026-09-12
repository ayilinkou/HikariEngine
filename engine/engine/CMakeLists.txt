# The engine, and — for now — everything it is made of. The renderer, the scene
# and the asset types moved here whole at step 46 rather than into Render and
# Scene modules of their own: those boundaries are Stage 8's to draw
# deliberately. They live under src/, so nothing outside this module can include
# them; apps see <engine/IEngine.h> and the run description types beside it.
engine_module(
  Engine
  SOURCES
  src/Engine.cpp
  src/RunApp.cpp
  src/ParseEngineOptions.cpp
  src/RunReportJson.cpp
  src/AssetRegistry.cpp
  src/Camera.cpp
  src/CloudSystem.cpp
  src/Cubemap.cpp
  src/CubemapLoader.cpp
  src/Entity.cpp
  src/Material.cpp
  src/MaterialFactory.cpp
  src/Mesh.cpp
  src/Model.cpp
  src/ModelData.cpp
  src/ModelLoader.cpp
  src/ModelManager.cpp
  src/Node.cpp
  src/PBRMaterial.cpp
  src/SceneComponent.cpp
  src/TextureLoader.cpp
  src/Texture.cpp
  src/Transform.cpp
  src/XmlParser.cpp
  LINK_LIBRARIES
  Engine::Core
  Engine::Platform
  Engine::RHI
  Engine::Asset
  SDL3::SDL3
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator
  glm::glm-header-only
  assimp::assimp
  imgui::imgui
  pugixml::pugixml
  ImGuiFileDialog)

# Private, and deliberately: these are the headers apps must not reach.
target_include_directories(Engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                          ${Stb_INCLUDE_DIR})

# One spelling per architecture, whatever the platform calls it. Lower case,
# because these are identifiers rather than prose: the run report's "os" is a
# proper noun and its "arch" is not.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" hikari_processor)
if(hikari_processor MATCHES "^(amd64|x86_64|x64)$")
  set(HIKARI_ARCH_NAME "x86_64")
elseif(hikari_processor MATCHES "^(aarch64|arm64)$")
  set(HIKARI_ARCH_NAME "arm64")
else()
  # Reported as it came rather than guessed at: an unknown name in a report is
  # readable, and a wrong one is worse than an unfamiliar one.
  set(HIKARI_ARCH_NAME "${hikari_processor}")
endif()

target_precompile_headers(Engine PRIVATE src/pch.h)

hikari_apply_build_options(Engine)

target_compile_definitions(
  Engine
  PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
         GLM_FORCE_DEPTH_ZERO_TO_ONE
         GLM_ENABLE_EXPERIMENTAL
         $<$<CONFIG:Debug>:DEBUG>
  PRIVATE
    # Named in the run report, because frame times from a debug build and a
    # release one are not comparable and nothing else in the file says which
    # produced them. Derived rather than passed in by the preset: $<CONFIG> is
    # correct under multi-config generators, where there is no single
    # CMAKE_BUILD_TYPE, and for a tree configured without a preset at all.
    HIKARI_BUILD_CONFIG="$<LOWER_CASE:$<CONFIG>>$<$<BOOL:${ENABLE_SANITIZERS}>:+asan>"
    # The machine a report describes, as the build's target rather than a
    # runtime query. CMAKE_SYSTEM_NAME is already spelled "Linux" / "Windows";
    # the architecture is normalised above, because CMAKE_SYSTEM_PROCESSOR says
    # "AMD64" on Windows and "x86_64" on Linux for the same hardware, and a
    # report that names the same machine two ways is a report nobody can diff.
    HIKARI_OS="${CMAKE_SYSTEM_NAME}"
    HIKARI_ARCH="${HIKARI_ARCH_NAME}")
