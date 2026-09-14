#include "d3d12/D3D12DebugName.h"

#include "d3d12/AgilitySdk.h"

namespace Hikari::Rhi::D3D12
{

void SetDebugName(ID3D12Object& object, const std::string& name)
{
    if (name.empty())
        return;

    object.SetName(WideFromUtf8(name).c_str());

    // With the terminator, which d3dcommon.h's D3D_SET_OBJECT_NAME_A leaves out. Both
    // print correctly, but the layer's lifetime messages are the reader that treats a
    // name as a C string without checking, so the name stays terminated either way.
    object.SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size() + 1),
                          name.c_str());
}

} // namespace Hikari::Rhi::D3D12
