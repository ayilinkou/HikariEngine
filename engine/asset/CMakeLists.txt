# The asset layer. Small for now: the cache, and the image encoding that used to
# sit in the application. The registry that owns them both still names types
# that live in the Engine module, and follows them here when Stage 8 splits it.
engine_module(Asset SOURCES src/ImageReader.cpp src/ImageWriter.cpp
              src/TextureSettings.cpp src/SettingsJson.cpp src/CubemapDescriptor.cpp
              src/TextureCookPaths.cpp src/CookFileUtil.cpp src/TextureCooker.cpp
              src/CookHash.cpp src/CookLock.cpp src/CookRecord.cpp src/CookPublication.cpp
              LINK_LIBRARIES Engine::Core)
target_link_libraries(Asset PRIVATE nlohmann_json::nlohmann_json KTX::ktx)

# stb has no CMake package version; identify the actual decoder/PNG writer
# headers instead of guessing their version or relying on timestamps.
file(SHA256 "${Stb_INCLUDE_DIR}/stb_image.h" stb_image_identity)
file(SHA256 "${Stb_INCLUDE_DIR}/stb_image_write.h" stb_write_identity)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${Stb_INCLUDE_DIR}/stb_image.h" "${Stb_INCLUDE_DIR}/stb_image_write.h")
target_compile_definitions(Asset PRIVATE
  HIKARI_COOK_COMPRESSONATOR_VERSION="${Compressonator_VERSION}"
  HIKARI_COOK_KTX_VERSION="${Ktx_VERSION}"
  HIKARI_COOK_STB_IDENTITY="${stb_image_identity}:${stb_write_identity}")

# SYSTEM, so that stb's own warnings are not this module's problem: both
# implementations are compiled here and neither builds clean under -Wextra.
target_include_directories(Asset SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
