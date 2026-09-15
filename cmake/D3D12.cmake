# The D3D12 backend's build-side half: its packages, the Agility SDK version an
# executable opts into, and the function that deploys the runtime beside each
# executable that can create a D3D12 device. Windows only; everything here is a
# no-op elsewhere.
#
# Included after project(), because the SDK version is read from the tree vcpkg
# installed, which the toolchain names only once the project exists.

if(NOT WIN32)
  function(hikari_deploy_d3d12 target)
  endfunction()
  return()
endif()

find_package(directx-headers CONFIG REQUIRED)
find_package(directx12-agility CONFIG REQUIRED)
find_package(D3D12MemoryAllocator CONFIG REQUIRED)
find_package(directx-warp CONFIG REQUIRED)

# The SDK version an executable exports as D3D12SDKVersion is the Agility SDK
# release's minor number — 619 for 1.619.5 — and it has to name the
# D3D12Core.dll deployed beside the executable, or the runtime refuses to load
# that copy. Read from the port rather than typed here, so bumping the vcpkg
# baseline cannot leave the two disagreeing. The backend still checks the
# version the loaded D3D12Core.dll exports, because a copy left over from an
# older build would otherwise be believed.
set(agility_spdx
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/directx12-agility/vcpkg.spdx.json")
file(READ "${agility_spdx}" agility_spdx_json)
string(JSON agility_package_count LENGTH "${agility_spdx_json}" packages)
math(EXPR agility_last_package "${agility_package_count} - 1")

set(agility_version "")
foreach(index RANGE ${agility_last_package})
  string(JSON package_name GET "${agility_spdx_json}" packages ${index} name)
  if(package_name STREQUAL "directx12-agility")
    string(JSON agility_version GET "${agility_spdx_json}" packages ${index}
           versionInfo)
  endif()
endforeach()

if(NOT agility_version MATCHES "^1\\.([0-9]+)\\.")
  message(
    FATAL_ERROR
      "Could not read the Agility SDK's version from ${agility_spdx} (got '${agility_version}')"
  )
endif()

set(HIKARI_D3D12_SDK_VERSION ${CMAKE_MATCH_1})
message(
  STATUS
    "Agility SDK ${agility_version}: executables export D3D12SDKVersion ${HIKARI_D3D12_SDK_VERSION}"
)

# hikari_deploy_d3d12(<target>)
#
# Everything an executable needs before it can create a D3D12 device, and the
# one place that knows it: the two symbols that opt it into the Agility SDK,
# D3D12Core.dll and the SDK's debug layer in D3D12\ beside it, and NuGet WARP
# beside the executable itself.
#
# None of the three DLLs is an import of anything, which is why vcpkg's own DLL
# copying never picks them up: the runtime loads the first two from the path the
# executable exports, and DXGI loads WARP by name when a software adapter is
# enumerated — from beside the executable when a copy is there, and silently
# from System32 when it is anywhere else, D3D12\ included.
#
# The debug layer comes from the port's debug tree in every configuration, since
# the port ships it nowhere else; a Release build validates too whenever
# --validation on asks, and the layer is a hard requirement when it does.
function(hikari_deploy_d3d12 target)
  # Linked directly rather than inherited through the RHI: an OBJECT library's
  # files reach only the targets that name it, and a static library member that
  # nothing references is never extracted — so an export inherited any other
  # way would silently not be there, and the process would run on the in-box
  # runtime with no debug layer.
  target_link_libraries(${target} PRIVATE D3D12AgilityExports)

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Microsoft::DirectX12-Core>" "$<TARGET_FILE_DIR:${target}>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Microsoft::DirectX12-Layers>" "$<TARGET_FILE_DIR:${target}>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Microsoft::DirectX-WARP>" "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()
