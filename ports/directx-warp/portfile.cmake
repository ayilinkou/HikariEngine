# The repository's own port, because vcpkg has none for WARP. It follows
# directx12-agility's portfile nearly line for line: both download a Microsoft
# NuGet package by SHA-512 and install the DLL inside it, so every binary
# dependency arrives through the one channel and cache the rest of the build
# already uses.
#
# Why the NuGet build rather than the copy Windows ships: the in-box WARP has no
# enhanced barriers and no unrestricted buffer-texture copy pitch, and the D3D12
# backend refuses an adapter without the latter.

set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_download_distfile(ARCHIVE
    URLS "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.WARP/${VERSION}"
    FILENAME "Microsoft.Direct3D.WARP.${VERSION}.zip"
    SHA512 b050e7bfd66588e5f256b1c4b882abc45fba8e1b05e3c83d5012c2763a83bef2efb60651b6363ade0cfb7dd0281939867290b89356714caa99dd08ce35682259
)

vcpkg_extract_source_archive(
    PACKAGE_PATH
    ARCHIVE ${ARCHIVE}
    NO_REMOVE_ONE_LEVEL
)

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(REDIST_ARCH arm64)
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(REDIST_ARCH win32)
else()
    set(REDIST_ARCH x64)
endif()

# The same DLL in both trees, since there is no debug build of it. The PDB is
# left out: at three times the DLL's size it would be cached twice per triplet
# for a symbol file nobody steps into.
file(COPY "${PACKAGE_PATH}/build/native/bin/${REDIST_ARCH}/d3d10warp.dll"
        DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
file(COPY "${PACKAGE_PATH}/build/native/bin/${REDIST_ARCH}/d3d10warp.dll"
        DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${PACKAGE_PATH}/LICENSE.TXT")

message(STATUS "BY USING THE SOFTWARE, YOU ACCEPT THESE TERMS: https://www.nuget.org/packages/Microsoft.Direct3D.WARP/${VERSION}/License")

configure_file("${CMAKE_CURRENT_LIST_DIR}/directx-warp-config.cmake.in" "${CURRENT_PACKAGES_DIR}/share/${PORT}/${PORT}-config.cmake" @ONLY)
