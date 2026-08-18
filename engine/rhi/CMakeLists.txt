engine_module(
  RHI
  SOURCES
  src/vulkan/VulkanConversions.cpp
  src/vulkan/AllocatedBuffer.cpp
  src/vulkan/AllocatedImage.cpp
  src/vulkan/Texture.cpp
  src/vulkan/Cubemap.cpp
  src/vulkan/PipelineBuilder.cpp
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

# vulkan.hpp defines its structs differently depending on this macro (aggregates
# without constructors when set), so a translation unit that disagrees with its
# neighbours is an ODR violation waiting to be diagnosed as something else
# entirely. VulkanApp already sets it; PUBLIC here keeps every consumer of this
# module — the app, the tests, and anything Stage 6+ adds — in agreement.
target_compile_definitions(RHI PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)

# SetVkDebugName (rhi/vulkan/DebugNames.h) compiles to nothing without this, and
# it is a template — so its body is instantiated in whichever translation unit
# calls it. Before R3 every caller was in VulkanApp, which defines DEBUG itself,
# so they all agreed. Now that callers exist on both sides of the module
# boundary, PUBLIC is what keeps them agreeing: without it the module's own
# sources would instantiate an empty body while the application instantiated a
# real one, which is an ODR violation whose symptom is debug names silently
# going missing from some objects and not others.
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
