# Proves that DXC validated the DXIL it just emitted.
#
# Run as `cmake -DDXIL_FILE=<path> -P CheckDxilSignature.cmake`, immediately
# after each DXIL compile — the same placement spirv-val has on the SPIR-V side,
# and for the same reason: a check that runs on its own schedule is a check that
# can silently stop covering something.
#
# It is a signature check rather than a second validator, and that is the whole
# design. DXC validates and signs every compile it makes, so there is nothing
# left for an external validator to find — its release notes for 1.8.2505 say
# "the compiler will now always use the internal validator instead of searching
# for an external DXIL.dll". What can still go wrong is validation being switched
# *off*, and that is visible in the container: an unvalidated container carries a
# zeroed hash where a signed one carries a digest. Measured rather than assumed —
# the same shader compiled with and without `-Xdxc -Vd` differs in exactly those
# sixteen bytes and in nothing else, not even its length.
#
# dxv was rejected as the gate: vcpkg's port does not install it, and run from
# the release archive it reported "Validation succeeded" on the deliberately
# unsigned control.
#
# The layout is DxilContainerHeader from DirectXShaderCompiler's
# include/dxc/DxilContainer/DxilContainer.h, which is packed to 1:
#
#   offset 0   uint32_t HeaderFourCC      DFCC_Container, 'D','X','B','C'
#   offset 4   uint8_t  Hash[16]          zeroed when validation was skipped
#   offset 20  uint16_t VersionMajor      1
#   offset 22  uint16_t VersionMinor      0
#   offset 24  uint32_t ContainerSizeInBytes
#   offset 28  uint32_t PartCount

if(NOT DEFINED DXIL_FILE)
  message(FATAL_ERROR "CheckDxilSignature: DXIL_FILE is required.")
endif()

if(NOT EXISTS "${DXIL_FILE}")
  message(FATAL_ERROR "CheckDxilSignature: ${DXIL_FILE} does not exist.")
endif()

# Hex rather than bytes because CMake has no byte type, and lowercase because
# that is what file(READ ... HEX) produces.
file(READ "${DXIL_FILE}" header HEX LIMIT 20)

string(LENGTH "${header}" header_length)
if(header_length LESS 40)
  message(FATAL_ERROR "CheckDxilSignature: ${DXIL_FILE} is too short to be a DXIL container.")
endif()

string(SUBSTRING "${header}" 0 8 four_cc)
string(SUBSTRING "${header}" 8 32 container_hash)

# 'D','X','B','C' little-endian in the file, which is the same byte order read
# as characters.
if(NOT four_cc STREQUAL "44584243")
  message(
    FATAL_ERROR
      "CheckDxilSignature: ${DXIL_FILE} is not a DXIL container — expected the "
      "bytes DXBC, found 0x${four_cc}.")
endif()

if(container_hash STREQUAL "00000000000000000000000000000000")
  message(
    FATAL_ERROR
      "CheckDxilSignature: ${DXIL_FILE} carries a zeroed container hash, which "
      "means it was never validated or signed. Validation was switched off — "
      "look for -Vd on the compile.")
endif()

# DxilContainer.h's PreviewByPassHash: sixteen bytes of 0x02, written when a
# preview shader model bypasses validation.
if(container_hash STREQUAL "02020202020202020202020202020202")
  message(
    FATAL_ERROR
      "CheckDxilSignature: ${DXIL_FILE} carries the preview-bypass hash, which "
      "means validation was skipped for a preview shader model.")
endif()
