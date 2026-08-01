#pragma once

#include "Model.h"
#include "SceneGraph.h"
#include "Transform.h"

namespace pugi
{
class xml_node;
}

enum class NodeType : uint8_t
{
    Transform,
    Light,
    Model,

    Unknown
};

class XmlParser
{
public:
    XmlParser() = delete;

    static glm::vec3 ParseVec3(const std::string& str);
    static Transform ParseTransform(const pugi::xml_node& node);
    static std::unique_ptr<Model> ParseModel(const pugi::xml_node& node);

    static std::unique_ptr<SceneGraph> LoadScene(const std::string& path);

    static std::string Vec3ToString(glm::vec3 v);
    static void WriteTransform(pugi::xml_node& parent, const Transform& t,
                               const Transform& defaultTransform);
    static void WriteModel(pugi::xml_node& parent, const Model& model,
                           const Model& defaultModel);

    static void SaveScene(const std::unique_ptr<SceneGraph>& sceneGraph,
                          const std::string& path);

private:
    static NodeType TagToNodeType(std::string_view tag);
};
