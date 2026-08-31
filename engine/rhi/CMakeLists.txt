engine_module(
  RHI
  SOURCES
  src/Diagnostics.cpp
  src/vulkan/VulkanConversions.cpp
  src/vulkan/QueueFamilies.cpp
  src/vulkan/OwnershipTransfer.cpp
  src/vulkan/VulkanDevice.cpp
  src/vulkan/SwapchainTarget.cpp
  src/vulkan/OffscreenTarget.cpp
  src/vulkan/VulkanNative.cpp
  src/vulkan/VulkanBuffer.cpp
  src/vulkan/VulkanTexture.cpp
  src/vulkan/VulkanCommandList.cpp
  src/vulkan/VulkanUploadContext.cpp
  src/vulkan/PipelineBuilder.cpp
  src/vulkan/VulkanPipelineCache.cpp
  src/vulkan/DescriptorAllocator.cpp
  src/vulkan/ComputePipelineBuilder.cpp
  # The single translation unit that instantiates VMA. It has to live in exactly
  # one target, and this is now the only one that owns Vulkan memory.
  src/vulkan/VMAImpl.cpp
  LINK_LIBRARIES
  Engine::Core
  Engine::Platform
  Vulkan::Vulkan
  GPUOpen::VulkanMemoryAllocator)

# Backend sources include each other as "vulkan/VulkanConversions.h" — relative
# to src/, not to the file doing the including — so the path reads the same from
# anywhere in the module. Deliberately PRIVATE: nothing outside the module may
# reach these headers, which is half of what keeps the boundary in place.
target_include_directories(RHI PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The validation layer comes from vcpkg rather than from a system-wide SDK, so
# the loader has to be pointed at it: VulkanDevice::CreateInstance puts
# HIKARI_VULKAN_LAYER_PATH on VK_ADD_LAYER_PATH before its first enumeration.
#
# The manifest is deliberately not used as vcpkg ships it. Its "library_path"
# names the layer by bare filename, which asks the loader to find it on the
# system's shared library search path — where it is not. An ordinary build gets
# away with that because the search reaches the loader's own $ORIGIN, which is
# the directory the layer sits in; a sanitizer build does not, because ASan
# interposes dlopen and the RUNPATH glibc then consults belongs to the sanitizer
# runtime rather than to the loader. Writing a copy whose library_path is
# absolute takes the search out of the picture on every configuration.
if(WIN32)
  set(layer_manifest
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/VkLayer_khronos_validation.json")
  set(layer_library_dir "bin")
  set(layer_library_name "VkLayer_khronos_validation.dll")
else()
  set(layer_manifest
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
  )
  set(layer_library_dir "lib")
  set(layer_library_name
      "${CMAKE_SHARED_LIBRARY_PREFIX}VkLayer_khronos_validation${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

# A textual replacement of the one value, rather than a JSON edit: CMake's
# string(JSON SET) reserialises the document and drops members it does not model,
# which silently cost the manifest its instance_extensions block — and with it the
# VK_EXT_debug_utils the messenger needs.
file(READ "${layer_manifest}" layer_manifest_json)
string(REPLACE "\"${layer_library_name}\""
       "\"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>${layer_library_dir}/${layer_library_name}\""
       layer_manifest_json "${layer_manifest_json}")
file(GENERATE
     OUTPUT "${CMAKE_BINARY_DIR}/vulkan_layers/$<CONFIG>/VkLayer_khronos_validation.json"
     CONTENT "${layer_manifest_json}")

target_compile_definitions(
  RHI PRIVATE HIKARI_VULKAN_LAYER_PATH="${CMAKE_BINARY_DIR}/vulkan_layers/$<CONFIG>")

# vulkan.hpp defines its structs differently depending on this macro (aggregates
# without constructors when set), so a translation unit that disagrees with its
# neighbours is an ODR violation waiting to be diagnosed as something else
# entirely. HikariEngine already sets it; PUBLIC here keeps every consumer of
# this module — the app, the tests, and anything Stage 6+ adds — in agreement.
target_compile_definitions(RHI PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)

# vulkan.hpp's raii Context otherwise builds its dispatcher from a loader it
# dlopens by bare filename, which need not be the loader this module is linked
# against: a bare-name dlopen resolves through the calling object's RUNPATH, and
# AddressSanitizer interposes dlopen, so the RUNPATH consulted becomes the
# sanitizer runtime's rather than the loader's. A sanitizer build then picked up
# whatever libvulkan.so.1 was installed system-wide and ran with two loaders at
# once. Turning the tool off selects the Context constructor that takes a
# vkGetInstanceProcAddr, which VulkanDevice hands the linked one.
#
# PUBLIC for the same ODR reason as the macros around it: it changes what
# vulkan_raii.hpp declares, so every translation unit that includes it has to
# agree.
target_compile_definitions(RHI PUBLIC VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0)

# vk::MetalSurfaceCreateInfoEXT / vk::EXTMetalSurfaceExtensionName and
# vk::KHRPortabilitySubsetExtensionName only exist in vulkan.hpp when these are
# defined, because VK_EXT_metal_surface and VK_KHR_portability_subset are both
# "beta"/provisional extensions. VulkanDevice.cpp (CreateInstance/CreateSurface)
# uses them directly for macOS surface creation, and every RHI header that
# reaches vulkan.hpp/vulkan_raii.hpp does so with no macro guard of its own — so
# this has to be set before any of them are included, for every translation
# unit in the module, not just VulkanDevice.cpp. PUBLIC for the same ODR reason
# as VULKAN_HPP_NO_STRUCT_CONSTRUCTORS above: the app also includes these
# headers and must agree with the module on what they expand to.
if(APPLE)
  target_compile_definitions(RHI PUBLIC VK_USE_PLATFORM_METAL_EXT
                                         VK_ENABLE_BETA_EXTENSIONS)
endif()

# SetVkDebugName (rhi/vulkan/DebugNames.h) compiles to nothing without this, and
# it is a template — so its body is instantiated in whichever translation unit
# calls it. Callers exist both inside this module and in the application, so
# PUBLIC is what keeps the two agreeing: without it the module's own sources
# would instantiate an empty body while the application instantiated a real one,
# which is an ODR violation whose symptom is debug names silently going missing
# from some objects and not others.
target_compile_definitions(RHI PUBLIC $<$<CONFIG:Debug>:DEBUG>)

if(MSVC)
  # The conversion tables rely on "switch with no default:" turning a forgotten
  # mapping into a build failure. That is -Wswitch on GCC/Clang, which -Wall
  # already enables, but MSVC's equivalent C4062 is a level-4 warning that /W3
  # leaves off — so without this the guarantee would hold on six of the nine CI
  # configurations and quietly not on the other three.
  target_compile_options(RHI PRIVATE /w14062)
endif()

# The stricter of this module's two header checks. engine_module() already
# created HeaderSelfContainment_RHI, which links the module and can therefore
# see Vulkan; this one compiles only the neutral headers and links only Core and
# Platform, so a neutral header that reaches for vulkan.hpp or vk_mem_alloc.h
# fails to compile.
#
# GLOB rather than GLOB_RECURSE is load-bearing: it is what excludes
# include/rhi/vulkan/, the transitional area that is allowed to expose Vulkan.
#
# This net has a known hole — a dependency also present on the default system
# include path is found regardless of what a target links, which on some
# distributions covers Vulkan. tests/scripts/rhi_boundary_check.sh exists
# because of that hole, not as a duplicate of this.
file(GLOB neutral_headers CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/*.h)

engine_header_self_containment(
  RHI_Neutral
  HEADERS
  ${neutral_headers}
  LINK_LIBRARIES
  Engine::Core
  Engine::Platform
  INCLUDE_DIRECTORIES
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
