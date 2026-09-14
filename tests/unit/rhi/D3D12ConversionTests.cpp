#include <catch2/catch_test_macros.hpp>

#include <rhi/SamplerDesc.h>

#include "d3d12/D3D12Conversions.h"

/**
 * CPU-only, like the Vulkan tables' tests, and for the same reason: a mapping the
 * debug layer does not check renders plausibly and wrongly rather than failing.
 */
using namespace Hikari::Rhi;
using namespace Hikari::Rhi::D3D12;

TEST_CASE("A D3D12 sampler's anisotropy is always within the documented 1 to 16",
          "[RhiConversions][d3d12]")
{
    // The debug layer accepts a 0, which D3D12_SAMPLER_DESC documents as invalid,
    // so nothing but this test notices a conversion that passes one through.
    SECTION("asking for the device's best gets 16, which every D3D12 device supports")
    {
        const D3D12_SAMPLER_DESC sampler = ToD3D12Sampler(SamplerDesc{.bAnisotropyEnable = true});

        CHECK(sampler.Filter == D3D12_FILTER_ANISOTROPIC);
        CHECK(sampler.MaxAnisotropy == D3D12_REQ_MAXANISOTROPY);
    }

    SECTION("a request within range is kept, and one past 16 is clamped")
    {
        CHECK(ToD3D12Sampler(SamplerDesc{.bAnisotropyEnable = true, .MaxAnisotropy = 4.f})
                  .MaxAnisotropy == 4u);
        CHECK(ToD3D12Sampler(SamplerDesc{.bAnisotropyEnable = true, .MaxAnisotropy = 64.f})
                  .MaxAnisotropy == D3D12_REQ_MAXANISOTROPY);
    }

    SECTION("without anisotropy the value is the valid 1 rather than the desc's 0")
    {
        CHECK(ToD3D12Sampler(SamplerDesc{}).MaxAnisotropy == 1u);
    }
}
