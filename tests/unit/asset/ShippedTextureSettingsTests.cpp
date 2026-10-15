#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <asset/TextureCooker.h>

using namespace Hikari::Asset;

TEST_CASE("Every shipped material texture has authored settings matching its semantic slot",
          "[asset][acquisition][content]")
{
    const std::filesystem::path root(HIKARI_SHIPPED_CONTENT);
    const TextureCooker cooker(root);
    size_t inspected = 0;
    for (const char* model : {"american_fullsize_73", "pbr_case", "m1887"})
    {
        const auto directory = root / "models" / model;
        std::ifstream file(directory / "scene.gltf");
        REQUIRE(file.is_open());
        const auto gltf = nlohmann::json::parse(file);
        for (const auto& material : gltf.at("materials"))
        {
            const auto check =
                [&](const nlohmann::json& fields, const char* key, TextureUsage usage)
            {
                if (!fields.contains(key))
                    return;
                const auto imageIndex = gltf.at("textures")
                                            .at(fields.at(key).at("index").get<size_t>())
                                            .at("source")
                                            .get<size_t>();
                const auto image =
                    directory / gltf.at("images").at(imageIndex).at("uri").get<std::string>();
                INFO(image.string());
                REQUIRE(std::filesystem::is_regular_file(image.string() + ".texture.json"));
                std::string error;
                std::vector<std::string> warnings;
                const auto prepared = cooker.Prepare({.Source = image}, error, warnings);
                INFO(error);
                REQUIRE(prepared);
                CHECK_FALSE(prepared->bWriteSettings);
                CHECK(prepared->Settings.Authored.Usage == usage);
                CHECK(prepared->Settings.ColorSpace == (usage == TextureUsage::Color
                                                            ? TextureColorSpace::Srgb
                                                            : TextureColorSpace::Linear));
                ++inspected;
            };
            const auto& pbr = material.at("pbrMetallicRoughness");
            check(pbr, "baseColorTexture", TextureUsage::Color);
            check(pbr, "metallicRoughnessTexture", TextureUsage::Packed);
            check(material, "normalTexture", TextureUsage::Normal);
        }
    }
    CHECK(inspected > 0);
    std::string error;
    std::vector<std::string> warnings;
    const auto sky =
        cooker.Prepare({.Source = "textures/skybox/sky.cubemap.json"}, error, warnings);
    INFO(error);
    REQUIRE(sky);
    REQUIRE(sky->Descriptor);
    CHECK(sky->Settings.Authored.Usage == TextureUsage::Color);
    CHECK(sky->Inputs[0].filename() == "right.jpg");
    CHECK(sky->Inputs[1].filename() == "left.jpg");
    CHECK(sky->Inputs[2].filename() == "top.jpg");
    CHECK(sky->Inputs[3].filename() == "bottom.jpg");
    CHECK(sky->Inputs[4].filename() == "back.jpg");
    CHECK(sky->Inputs[5].filename() == "front.jpg");
}
