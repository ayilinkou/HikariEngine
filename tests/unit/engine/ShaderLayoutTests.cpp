#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <rhi/RhiTypes.h>

#include "InstanceData.h"
#include "Vertex.h"
#include "shaders/ShaderTypes.h"

using Json = nlohmann::json;

namespace
{
/**
 * Where cmake/Shaders.cmake put the reflection Slang emitted beside each blob.
 * Injected rather than searched for, so a test run from anywhere finds it and a
 * missing directory is a failure rather than a silent skip.
 */
constexpr const char* kReflectionDir = HIKARI_SHADER_REFLECTION_DIR;

/** One field of a shared block, as the shader compiler laid it out. */
struct ReflectedField
{
    uint64_t Offset = 0u;
    uint64_t Size = 0u;
};

/** What C++ says about the same field, from the declaration the shader shares. */
struct ExpectedField
{
    const char* Name;
    size_t Offset;
    size_t Size;
};

std::vector<std::filesystem::path> ReflectionFiles()
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kReflectionDir))
    {
        if (entry.path().extension() == ".json")
            files.push_back(entry.path());
    }

    return files;
}

Json Load(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return Json::parse(file);
}

/**
 * Every field of the struct named `structName`, wherever it appears in this
 * reflection — inside a constant buffer, a push constant block, or nested in
 * another struct. Empty when the shader does not use the struct at all.
 */
void CollectStruct(const Json& node, const std::string& structName,
                   std::map<std::string, ReflectedField>& out)
{
    if (node.is_array())
    {
        for (const Json& element : node)
            CollectStruct(element, structName, out);

        return;
    }

    if (!node.is_object())
        return;

    const bool bIsTarget = node.value("kind", "") == "struct" &&
                           node.value("name", "") == structName && node.contains("fields");
    if (bIsTarget)
    {
        for (const Json& field : node.at("fields"))
        {
            const Json& binding = field.at("binding");
            if (binding.value("kind", "") != "uniform")
                continue;

            out[field.at("name").get<std::string>()] =
                ReflectedField{.Offset = binding.at("offset").get<uint64_t>(),
                               .Size = binding.at("size").get<uint64_t>()};
        }
    }

    for (const auto& [key, value] : node.items())
        CollectStruct(value, structName, out);
}

/**
 * Compares a shared block against what C++ makes of the same declaration, in
 * every reflection that mentions it — which is both targets, since the two
 * compiles emit one each.
 *
 * The find count is asserted, not assumed: a struct nothing reflects would make
 * every check below pass over an empty set, which reads exactly like a struct
 * that agrees.
 */
void CheckStructLayout(const std::string& structName, const std::vector<ExpectedField>& expected)
{
    size_t reflectionsChecked = 0u;

    for (const std::filesystem::path& path : ReflectionFiles())
    {
        std::map<std::string, ReflectedField> reflected;
        CollectStruct(Load(path), structName, reflected);
        if (reflected.empty())
            continue;

        ++reflectionsChecked;
        INFO("reflection: " << path.filename().string() << ", struct: " << structName);

        for (const ExpectedField& field : expected)
        {
            INFO("field: " << field.Name);
            const auto found = reflected.find(field.Name);
            REQUIRE(found != reflected.end());

            CHECK(found->second.Offset == field.Offset);
            CHECK(found->second.Size == field.Size);
        }

        // The other direction: a field the shader has and C++ does not would
        // otherwise pass unnoticed.
        CHECK(reflected.size() == expected.size());
    }

    INFO("struct: " << structName);
    CHECK(reflectionsChecked > 0u);
}
} // namespace

#define EXPECT_FIELD(Struct, Field)                                                                \
    ExpectedField                                                                                  \
    {                                                                                              \
        #Field, offsetof(Struct, Field), sizeof(Struct::Field)                                     \
    }

TEST_CASE("The shared constant blocks agree with C++ on every target", "[shaders][layout]")
{
    // Sharing the declaration removes the transcription error; it does not touch
    // the layout-rule divergence. SPIR-V follows std140/std430 and HLSL constant
    // buffers follow their own 16-byte packing, and the two need not agree — so a
    // struct that matches one can silently corrupt on the other (plan D30).
    SECTION("PointLightData")
    {
        CheckStructLayout("PointLightData", {EXPECT_FIELD(PointLightData, Color),
                                             EXPECT_FIELD(PointLightData, Intensity),
                                             EXPECT_FIELD(PointLightData, Pos),
                                             EXPECT_FIELD(PointLightData, Padding)});
    }

    SECTION("DirLightData")
    {
        CheckStructLayout("DirLightData",
                          {EXPECT_FIELD(DirLightData, Color), EXPECT_FIELD(DirLightData, Intensity),
                           EXPECT_FIELD(DirLightData, Dir), EXPECT_FIELD(DirLightData, Padding)});
    }

    SECTION("LightData")
    {
        CheckStructLayout("LightData",
                          {EXPECT_FIELD(LightData, PointLightCount),
                           EXPECT_FIELD(LightData, DirLightCount), EXPECT_FIELD(LightData, Padding),
                           EXPECT_FIELD(LightData, PointLights),
                           EXPECT_FIELD(LightData, DirLights)});
    }

    SECTION("CameraData")
    {
        CheckStructLayout("CameraData",
                          {EXPECT_FIELD(CameraData, View), EXPECT_FIELD(CameraData, Proj),
                           EXPECT_FIELD(CameraData, InvViewProj), EXPECT_FIELD(CameraData, Pos),
                           EXPECT_FIELD(CameraData, NearPlane), EXPECT_FIELD(CameraData, Padding),
                           EXPECT_FIELD(CameraData, FarPlane)});
    }

    SECTION("GlobalBuffer")
    {
        CheckStructLayout("GlobalBuffer",
                          {EXPECT_FIELD(GlobalBuffer, Lights), EXPECT_FIELD(GlobalBuffer, Camera),
                           EXPECT_FIELD(GlobalBuffer, SkyColor), EXPECT_FIELD(GlobalBuffer, Time)});
    }

    SECTION("MaterialPushConstant")
    {
        CheckStructLayout("MaterialPushConstant",
                          {EXPECT_FIELD(MaterialPushConstant, Albedo),
                           EXPECT_FIELD(MaterialPushConstant, Metallic),
                           EXPECT_FIELD(MaterialPushConstant, Roughness),
                           EXPECT_FIELD(MaterialPushConstant, AO),
                           EXPECT_FIELD(MaterialPushConstant, Opacity),
                           EXPECT_FIELD(MaterialPushConstant, bHasAlbedoTex),
                           EXPECT_FIELD(MaterialPushConstant, bHasNormalTex),
                           EXPECT_FIELD(MaterialPushConstant, bHasMetallicRoughnessTex),
                           EXPECT_FIELD(MaterialPushConstant, bTwoSided)});
    }

    SECTION("CloudPushConstants")
    {
        CheckStructLayout("CloudPushConstants",
                          {EXPECT_FIELD(CloudPushConstants, WindVelocity),
                           EXPECT_FIELD(CloudPushConstants, MinHeight),
                           EXPECT_FIELD(CloudPushConstants, MaxHeight),
                           EXPECT_FIELD(CloudPushConstants, Coverage),
                           EXPECT_FIELD(CloudPushConstants, Anisotropy),
                           EXPECT_FIELD(CloudPushConstants, BoundaryDisplacement),
                           EXPECT_FIELD(CloudPushConstants, ViewStepCount),
                           EXPECT_FIELD(CloudPushConstants, SunStepCount)});
    }

    SECTION("BakeConstants")
    {
        CheckStructLayout("BakeConstants", {EXPECT_FIELD(BakeConstants, Resolution),
                                            EXPECT_FIELD(BakeConstants, WorleyPointsPerCell)});
    }
}

TEST_CASE("Both targets lay the shared blocks out identically", "[shaders][layout]")
{
    // D30 measured them as agreeing everywhere today, which is what makes the
    // divergence latent rather than present. This is the case that notices the
    // day that stops being true, without needing to know which side moved.
    std::map<std::string, std::map<std::string, uint64_t>> byTarget;

    for (const std::filesystem::path& path : ReflectionFiles())
    {
        const std::string target =
            path.string().find(".dxil.") != std::string::npos ? "dxil" : "spv";

        std::map<std::string, ReflectedField> fields;
        CollectStruct(Load(path), "GlobalBuffer", fields);
        for (const auto& [name, field] : fields)
            byTarget[target][name] = field.Offset;
    }

    REQUIRE(byTarget.size() == 2u);
    CHECK(byTarget.at("spv") == byTarget.at("dxil"));
}

namespace
{
/**
 * A vertex input as the shader declared it: which location it binds to, and the
 * scalar type and element count that a Format has to agree with.
 */
struct ReflectedInput
{
    std::string ScalarType;
    uint32_t ElementCount = 0u;
};

/** Every varying input of every vertex entry point in a reflection, by location. */
void CollectVertexInputs(const Json& node, std::map<uint32_t, ReflectedInput>& out)
{
    if (node.is_array())
    {
        for (const Json& element : node)
            CollectVertexInputs(element, out);

        return;
    }

    if (!node.is_object())
        return;

    const bool bIsInput = node.contains("binding") && node.at("binding").is_object() &&
                          node.at("binding").value("kind", "") == "varyingInput" &&
                          node.at("binding").contains("index") && node.contains("type") &&
                          node.at("type").value("kind", "") == "vector";
    if (bIsInput)
    {
        const Json& type = node.at("type");
        out[node.at("binding").at("index").get<uint32_t>()] =
            ReflectedInput{.ScalarType = type.at("elementType").at("scalarType").get<std::string>(),
                           .ElementCount = type.at("elementCount").get<uint32_t>()};
    }

    for (const auto& [key, value] : node.items())
        CollectVertexInputs(value, out);
}

/** What a neutral Format means as a scalar type and a count. */
ReflectedInput Describe(Hikari::Rhi::Format format)
{
    switch (format)
    {
        case Hikari::Rhi::Format::RG32Float:
            return {"float32", 2u};
        case Hikari::Rhi::Format::RGB32Float:
            return {"float32", 3u};
        case Hikari::Rhi::Format::RGBA32Float:
            return {"float32", 4u};
        default:
            break;
    }

    // Named rather than defaulted, so an attribute using a format this test does
    // not describe fails loudly instead of comparing against nothing.
    FAIL("a vertex attribute uses a format this test cannot describe");
    return {};
}
} // namespace

TEST_CASE("Vertex input locations and formats agree with the shader", "[shaders][layout]")
{
    // VS_In carries semantics that C++ cannot express, so the two declarations
    // stay separate and this compares them instead (plan D32). The live hazard is
    // insertion: add a field to VS_In and every location after it shifts while
    // the C++ table keeps the old numbers — which validation cannot object to,
    // because the pipeline is still perfectly legal.
    //
    // Semantics are excluded deliberately: there is no C++ side to compare them
    // against, so asserting POSITION0 would only agree with a constant typed
    // beside it.
    std::vector<Hikari::Rhi::VertexAttribute> expected;
    for (const auto& attribute : Vertex::GetAttributeDescriptions())
        expected.push_back(attribute);

    for (const auto& attribute : InstanceData::GetAttributeDescriptions())
        expected.push_back(attribute);

    REQUIRE(expected.size() == 11u);

    // Named rather than found by counting inputs: a filter that skips a shader
    // whose input count is unexpected would excuse the very insertion this
    // exists to catch, reporting "checked fewer shaders" instead of "location 3
    // has the wrong format".
    const std::vector<std::string> surfaceVertexStages{
        "opaque.vert.spv.json", "opaque.vert.dxil.json", "weightedBlendedOIT.vert.spv.json",
        "weightedBlendedOIT.vert.dxil.json"};

    for (const std::string& stage : surfaceVertexStages)
    {
        INFO("reflection: " << stage);

        const std::filesystem::path path = std::filesystem::path(kReflectionDir) / stage;
        REQUIRE(std::filesystem::exists(path));

        std::map<uint32_t, ReflectedInput> inputs;
        CollectVertexInputs(Load(path), inputs);
        CHECK(inputs.size() == expected.size());

        for (const auto& attribute : expected)
        {
            INFO("location: " << attribute.Location);
            const auto found = inputs.find(attribute.Location);
            REQUIRE(found != inputs.end());

            const ReflectedInput described = Describe(attribute.AttributeFormat);
            CHECK(found->second.ScalarType == described.ScalarType);
            CHECK(found->second.ElementCount == described.ElementCount);
        }
    }
}

namespace
{
/** The shared header with its comments removed, so a check reads only code. */
std::string SharedHeaderCode()
{
    std::ifstream file(HIKARI_SHADER_TYPES_HEADER);
    REQUIRE(file.is_open());

    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

    std::string code;
    code.reserve(text.size());

    for (size_t i = 0u; i < text.size();)
    {
        if (text.compare(i, 2, "/*") == 0)
        {
            const size_t end = text.find("*/", i + 2u);
            i = end == std::string::npos ? text.size() : end + 2u;
        }
        else if (text.compare(i, 2, "//") == 0)
        {
            const size_t end = text.find('\n', i);
            i = end == std::string::npos ? text.size() : end;
        }
        else
        {
            code += text[i++];
        }
    }

    return code;
}

/** Whether `at` starts a whole word rather than part of a longer identifier. */
bool IsWholeWord(const std::string& text, size_t at, size_t length)
{
    const auto isIdentifier = [](char c)
    { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };

    if (at > 0u && isIdentifier(text[at - 1u]))
        return false;

    const size_t after = at + length;
    return after >= text.size() || !isIdentifier(text[after]);
}
} // namespace

TEST_CASE("The shared header never declares a plain bool", "[shaders][layout]")
{
    // A C++ bool is one byte and a shader's is four, so a shared block carrying
    // one disagrees about every offset after it. The layout test above would
    // notice — but only for a struct it already lists, and only as an offset
    // mismatch some way from the cause. This says what actually went wrong, at
    // the line it went wrong on.
    const std::string code = SharedHeaderCode();

    // The one legitimate occurrence: the Slang side of the bool32 alias, which
    // is what makes `bool` mean four bytes in a shader and available at all.
    const std::string alias = "typedef bool bool32;";
    const size_t aliasAt = code.find(alias);
    REQUIRE(aliasAt != std::string::npos);

    for (size_t at = code.find("bool"); at != std::string::npos; at = code.find("bool", at + 1u))
    {
        if (!IsWholeWord(code, at, 4u))
            continue;

        // Inside the alias declaration itself.
        if (at >= aliasAt && at < aliasAt + alias.size())
            continue;

        const size_t line = std::count(code.begin(), code.begin() + at, '\n') + 1u;
        INFO("ShaderTypes.h, around line " << line << " of the comment-stripped text");
        FAIL_CHECK("a plain bool in the shared header — use bool32, which is four bytes on "
                   "both sides");
    }
}

TEST_CASE("Every struct in the shared header has its layout checked", "[shaders][layout]")
{
    // Without this, adding a struct to the header is adding one nothing compares:
    // the case above checks a list, and a struct missing from that list passes by
    // not being looked at. The same reason the run report's fields are pinned.
    static const std::vector<std::string> checked{
        "PointLightData", "DirLightData",         "LightData",          "CameraData",
        "GlobalBuffer",   "MaterialPushConstant", "CloudPushConstants", "BakeConstants"};

    const std::string code = SharedHeaderCode();

    std::vector<std::string> declared;
    for (size_t at = code.find("struct "); at != std::string::npos;
         at = code.find("struct ", at + 1u))
    {
        if (!IsWholeWord(code, at, 6u))
            continue;

        const size_t nameAt = at + 7u;
        const size_t end = code.find_first_of(" \n\r\t{", nameAt);
        declared.push_back(code.substr(nameAt, end - nameAt));
    }

    REQUIRE_FALSE(declared.empty());

    for (const std::string& name : declared)
    {
        INFO("struct: " << name);
        CHECK(std::find(checked.begin(), checked.end(), name) != checked.end());
    }

    // And the other way, so a renamed struct does not leave a dead entry behind
    // that can never match.
    for (const std::string& name : checked)
    {
        INFO("checked struct: " << name);
        CHECK(std::find(declared.begin(), declared.end(), name) != declared.end());
    }
}
