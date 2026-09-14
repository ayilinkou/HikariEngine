#pragma once

#include <string>

#include <directx/d3d12.h>

namespace Hikari::Rhi::D3D12
{
/**
 * Names a D3D12 object in both of the encodings the debug layer reads, and does nothing
 * for an empty name.
 *
 * SetName alone is not enough. It stores UTF-16 under WKPDID_D3DDebugObjectNameW, which
 * the layer's error messages print in full — but its object lifetime messages print the
 * UTF-8 name under WKPDID_D3DDebugObjectName instead, and given only the UTF-16 one they
 * print its first eight bytes followed by whatever memory comes next (Agility SDK
 * 1.619.5, on the RX 580 and on WARP). SetName's documentation allows the UTF-8 GUID
 * directly. Set both, and every message names the object.
 *
 * Except the objects the layer creates for itself, which nothing here can reach. With
 * GPU-based validation on, it keeps a patched copy of every pipeline state and root
 * signature, and those print as "D3D12 De" and then garbage: the layer names them
 * itself and falls into its own truncation. They pair one-to-one with the engine's
 * pipelines and layouts, and are gone when GPU-based validation is off.
 */
void SetDebugName(ID3D12Object& object, const std::string& name);
} // namespace Hikari::Rhi::D3D12
