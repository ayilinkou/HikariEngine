# A host-only binary port: keep the complete runtime together, including DLLs,
# plugins and Linux SONAME symlinks. Picking just the CLI loses its image codecs.
if(VCPKG_CROSSCOMPILING)
    message(FATAL_ERROR "Compressonator is a host tool; declare it with host: true.")
endif()
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

if(VCPKG_TARGET_IS_WINDOWS)
    set(ARCHIVE_NAME "compressonatorcli-${VERSION}-win64.zip")
    set(ARCHIVE_HASH e99b43a3061ab5fe73bf9b820e888293ae0df394c8bd44a1beb110ccdfd6d5a94662f93f5fe7dada685596e4c39529adadec1ae0859fb8a42334f0ba6acd54bf)
    set(CLI_NAME compressonatorcli.exe)
elseif(VCPKG_TARGET_IS_LINUX)
    set(ARCHIVE_NAME "compressonatorcli-${VERSION}-Linux.tar.gz")
    set(ARCHIVE_HASH 46df6bce3158fcc4ce7b7d4f131ebd8fbfabb6fbfee78da8a0ad61056eaf8e3349b47a4128724505c7833b873b6ac53d22380040c94bfece9957ec997e014515)
    set(CLI_NAME compressonatorcli)
else()
    message(FATAL_ERROR "Compressonator supports only x64 Windows and Linux hosts.")
endif()

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/GPUOpen-Tools/compressonator/releases/download/V${VERSION}/${ARCHIVE_NAME}"
    FILENAME "${ARCHIVE_NAME}"
    SHA512 "${ARCHIVE_HASH}"
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

set(TOOL_DIR "${CURRENT_PACKAGES_DIR}/tools/${PORT}")
file(MAKE_DIRECTORY "${TOOL_DIR}")
if(VCPKG_TARGET_IS_WINDOWS)
    file(GLOB RUNTIME_FILES "${SOURCE_PATH}/*.dll" "${SOURCE_PATH}/*.spv")
    file(COPY "${SOURCE_PATH}/${CLI_NAME}" ${RUNTIME_FILES}
         "${SOURCE_PATH}/plugins" "${SOURCE_PATH}/qt.conf"
         DESTINATION "${TOOL_DIR}")
else()
    file(COPY "${SOURCE_PATH}/compressonatorcli-bin" "${SOURCE_PATH}/pkglibs"
         DESTINATION "${TOOL_DIR}")
    # Upstream's launcher has a blank line before its shebang, unquoted paths
    # and can hide a missing binary. exec preserves the child's failure status.
    file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/compressonatorcli"
         DESTINATION "${TOOL_DIR}"
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/license/license.txt")
configure_file("${CMAKE_CURRENT_LIST_DIR}/CompressonatorConfig.cmake.in"
               "${CURRENT_PACKAGES_DIR}/share/${PORT}/CompressonatorConfig.cmake" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/CompressonatorConfigVersion.cmake.in"
               "${CURRENT_PACKAGES_DIR}/share/${PORT}/CompressonatorConfigVersion.cmake" @ONLY)
