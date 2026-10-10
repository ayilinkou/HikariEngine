cmake_minimum_required(VERSION 4.0)

# execute_process passes argument lists without a shell. Both installation and
# scratch paths may contain spaces; successful exit alone is not codec coverage.
function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
                  OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Texture tool probe failed (${result}): ${ARGV}\n${output}\n${error}")
  endif()
endfunction()

execute_process(COMMAND "${CLI}" -version RESULT_VARIABLE result
                OUTPUT_VARIABLE version ERROR_VARIABLE error TIMEOUT 30)
if(NOT result STREQUAL "0" OR NOT version MATCHES "version 4\\.5\\.52")
  message(FATAL_ERROR "Pinned Compressonator failed its version check: ${version}\n${error}")
endif()

# No display or Vulkan ICD is available to these child processes. Windows GPU
# plugins are packaged by upstream but CPU encoding/decode must not load them.
unset(ENV{DISPLAY})
unset(ENV{WAYLAND_DISPLAY})
set(ENV{VK_DRIVER_FILES} "${WORK}/no-graphics-driver.json")
file(MAKE_DIRECTORY "${WORK}")
# A previous run's output must never make a no-output encoder success pass.
file(GLOB old_outputs "${WORK}/*.dds")
if(old_outputs)
  file(REMOVE ${old_outputs})
endif()
run("${PROBE}" generate "${WORK}")

foreach(extent IN ITEMS 1x1 2x2 4x4 7x5 8x4)
  string(REPLACE "x" ";" dimensions "${extent}")
  list(GET dimensions 0 width)
  list(GET dimensions 1 height)
  foreach(format IN ITEMS BC1 BC2 BC3 BC4 BC4_S BC5 BC5_S BC7)
    set(encoded "${WORK}/${extent}-${format}.dds")
    set(decoded "${WORK}/${extent}-${format}-decoded.dds")
    run("${CLI}" -fd "${format}" -EncodeWith CPU -NumThreads 2 -nomipmap
        "${WORK}/${extent}.png" "${encoded}")
    run("${PROBE}" encoded "${encoded}" "${format}" "${width}" "${height}" 1)
    # Linux's PNG writer fails on decoded images; uncompressed DDS works on
    # both hosts and preserves signed components for missing-mip filtering.
    run("${CLI}" -fd ARGB_8888 -nomipmap "${encoded}" "${decoded}")
    run("${PROBE}" decoded "${decoded}" "${format}" "${width}" "${height}")
  endforeach()
endforeach()

# Exercise JPEG decoding without making the test runner depend on another image
# library. The committed synthetic fixture has the same constant RGB values.
run("${CLI}" -fd BC7 -EncodeWith CPU -NumThreads 2 -nomipmap
    "${JPEG}" "${WORK}/jpeg.dds")
run("${PROBE}" encoded "${WORK}/jpeg.dds" BC7 4 4 1)
run("${CLI}" -fd ARGB_8888 -nomipmap "${WORK}/jpeg.dds" "${WORK}/jpeg-decoded.dds")
run("${PROBE}" decoded "${WORK}/jpeg-decoded.dds" BC7 4 4)

foreach(format IN ITEMS BC1 BC2 BC3 BC4 BC4_S BC5 BC5_S BC7)
  run("${CLI}" -fd "${format}" -EncodeWith CPU -NumThreads 2 -miplevels 3
      "${WORK}/4x4.png" "${WORK}/${format}-mips.dds")
  run("${PROBE}" encoded "${WORK}/${format}-mips.dds" "${format}" 4 4 3)
endforeach()

run("${CLI}" -fd ARGB_8888 -miplevels 3 "${WORK}/bw.png" "${WORK}/bw-mips.dds")
run("${PROBE}" filter "${WORK}/bw-mips.dds" bw)
run("${CLI}" -fd ARGB_8888 -miplevels 3 -FilterGamma 2.2
    "${WORK}/bw.png" "${WORK}/gamma-mips.dds")
run("${PROBE}" filter "${WORK}/gamma-mips.dds" gamma)
run("${CLI}" -fd ARGB_8888 -miplevels 3 "${WORK}/normal.png" "${WORK}/normal-mips.dds")
run("${PROBE}" filter "${WORK}/normal-mips.dds" normal)

run("${CLI}" -fd BC1 -EncodeWith CPU -NumThreads 2 -AlphaThreshold 128 -nomipmap
    "${WORK}/mask.png" "${WORK}/mask.dds")
run("${CLI}" -fd ARGB_8888 -nomipmap "${WORK}/mask.dds" "${WORK}/mask-decoded.dds")
run("${PROBE}" alpha "${WORK}/mask-decoded.dds" binary)
run("${CLI}" -fd BC7 -EncodeWith CPU -NumThreads 2 -nomipmap
    "${WORK}/mask.png" "${WORK}/rgba.dds")
run("${CLI}" -fd ARGB_8888 -nomipmap "${WORK}/rgba.dds" "${WORK}/rgba-decoded.dds")
run("${PROBE}" alpha "${WORK}/rgba-decoded.dds" varying)

message(STATUS "Compressonator CPU formats, signed decode, dimensions, mips and alpha; libktx block round trips passed")
