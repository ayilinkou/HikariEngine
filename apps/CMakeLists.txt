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

set(HIKARI_TEXTURE_SOURCE_ROOT "${CMAKE_SOURCE_DIR}/content" CACHE PATH
    "Source root for the explicit cook-textures target")
set(HIKARI_TEXTURE_COOKED_ROOT "" CACHE PATH
    "Cooked root for cook-textures (empty selects the source root's sibling cooked/)")
set(HIKARI_TEXTURE_ENCODER_THREADS "8" CACHE STRING
    "CPU encoder thread limit for cook-textures (1..128)")
if(NOT HIKARI_TEXTURE_ENCODER_THREADS MATCHES "^[0-9]+$" OR
   HIKARI_TEXTURE_ENCODER_THREADS LESS 1 OR HIKARI_TEXTURE_ENCODER_THREADS GREATER 128)
  message(FATAL_ERROR "HIKARI_TEXTURE_ENCODER_THREADS must be an integer in [1, 128]")
endif()
set(texture_cooked_root_args)
if(NOT HIKARI_TEXTURE_COOKED_ROOT STREQUAL "")
  list(APPEND texture_cooked_root_args --cooked-root "${HIKARI_TEXTURE_COOKED_ROOT}")
endif()
add_custom_target(cook-textures
  COMMAND $<TARGET_FILE:HikariTextureCooker> cook-all
          --source-root "${HIKARI_TEXTURE_SOURCE_ROOT}"
          ${texture_cooked_root_args}
          --threads "${HIKARI_TEXTURE_ENCODER_THREADS}"
  DEPENDS HikariTextureCooker
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Cooking source textures with saved per-asset settings"
  VERBATIM)
