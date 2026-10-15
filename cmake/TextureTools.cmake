# Find the host package explicitly; target and host triplets need not agree.
# No renderer target links the encoder, and libktx's graphics upload features
# are not requested. The cooker will upload through the neutral RHI instead.
find_package(Ktx CONFIG REQUIRED)
find_package(Compressonator 4.5.52 EXACT CONFIG REQUIRED
             PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/share/compressonator"
             NO_DEFAULT_PATH)
message(STATUS "Texture tools: libktx from the vcpkg baseline, Compressonator ${Compressonator_VERSION} (${Compressonator_EXECUTABLE})")
