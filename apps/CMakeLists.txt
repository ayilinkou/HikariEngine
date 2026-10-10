# Two binaries over one engine. They differ in the platform they build and the
# flags they accept; everything after that is Engine::RunApp.
foreach(app_name IN ITEMS HikariEditor HikariHeadless)
  if(app_name STREQUAL "HikariEditor")
    set(app_dir editor)
  else()
    set(app_dir headless)
  endif()

  add_executable(${app_name} ${app_dir}/main.cpp)

  # Both land beside the shaders, which are built once and read from beside
  # whichever binary is running.
  set_target_properties(${app_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                               ${HIKARI_EXE_DIR})

  target_link_libraries(
    ${app_name} PRIVATE Engine::Core Engine::Platform Engine::RHI Engine::Engine
                        Engine::Editor SanitizerShims)

  engine_set_warnings(${app_name})
  hikari_apply_build_options(${app_name})
  hikari_deploy_d3d12(${app_name})
  add_dependencies(${app_name} CompileShadersTarget)
endforeach()

# CLI parsing is shared with its tests; the asset service remains independent
# of Platform, and neither target links the engine, editor, RHI or shader build.
add_library(TextureCookerCli STATIC texture_cooker/TextureCookerCli.cpp)
target_include_directories(TextureCookerCli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/texture_cooker)
target_link_libraries(TextureCookerCli PUBLIC Engine::Asset Engine::Platform)
engine_set_warnings(TextureCookerCli)

add_executable(HikariTextureCooker texture_cooker/main.cpp)
target_link_libraries(HikariTextureCooker PRIVATE TextureCookerCli SanitizerShims)
set_target_properties(HikariTextureCooker PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${HIKARI_EXE_DIR})
target_compile_definitions(HikariTextureCooker PRIVATE
  HIKARI_COOKER_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content")
engine_set_warnings(HikariTextureCooker)
hikari_apply_build_options(HikariTextureCooker)
