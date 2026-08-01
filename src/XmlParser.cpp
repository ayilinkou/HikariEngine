#include "XmlParser.h"
#include "Log.h"

#include <sstream>
#include <stdexcept>

#include "pugixml.hpp"

constexpr LogCategory LogXmlParser("XmlParser");

glm::vec3 XmlParser::ParseVec3(const std::string& str)
{
    std::istringstream iss(str);
    glm::vec3 v;
    iss >> v.x >> v.y >> v.z;
    return v;
}

Transform XmlParser::ParseTransform(const pugi::xml_node& node)
{
    auto posAtt = node.attribute("position");
    auto rotAtt = node.attribute("rotation");
    auto scaleAtt = node.attribute("scale");

    if (!posAtt || !rotAtt || !scaleAtt)
    {
        throw std::runtime_error(
            "Found transform without all position, rotation and scale values "
            "when parsing scene!");
    }

    Transform t;
    std::istringstream iss;
    iss = std::istringstream(posAtt.as_string());
    iss >> t.Position.x >> t.Position.y >> t.Position.z;
    iss = std::istringstream(rotAtt.as_string());
    iss >> t.Rotation.x >> t.Rotation.y >> t.Rotation.z;
    iss = std::istringstream(scaleAtt.as_string());
    iss >> t.Scale.x >> t.Scale.y >> t.Scale.z;
    return t;
}

std::unique_ptr<Model> XmlParser::ParseModel(const pugi::xml_node& node)
{
    auto pathAtt = node.attribute("path");
    if (!pathAtt)
    {
        throw std::runtime_error(
            "Found model with no \"path\" value when parsing scene!");
    }

    const char* path = pathAtt.as_string();
    std::unique_ptr<Model> model = std::make_unique<Model>(path);

    if (auto transformNode = node.child("transform"))
    {
        Transform transform = ParseTransform(transformNode);
        model->GetTransform() = transform;
    }

    return model;
};

NodeType XmlParser::TagToNodeType(std::string_view tag)
{
    if (tag == "transform")
        return NodeType::Transform;
    if (tag == "light")
        return NodeType::Light;
    if (tag == "model")
        return NodeType::Model;
    return NodeType::Unknown;
}

SceneGraph XmlParser::LoadScene(const std::string& path)
{
    LogMsg(LogSeverity::Info, LogXmlParser, "Loading scene: {}", path.c_str());

    SceneGraph scene;

    pugi::xml_document doc;
    if (!doc.load_file(path.c_str()))
        throw std::runtime_error("Failed to load scene: " + path);

    for (pugi::xml_node node : doc.child("scene").children("entity"))
    {
        scene.Entities.push_back(std::make_unique<Entity>());
        Entity& entity = *scene.Entities.back();
        entity.SetName(node.attribute("name").as_string());

        for (pugi::xml_node comp : node.children())
        {
            switch (TagToNodeType(comp.name()))
            {
            case NodeType::Transform:
            {
                Transform transform = ParseTransform(comp);
                entity.GetTransform() = transform;
                continue;
            }
            case NodeType::Light:
            {
                // TODO: move lights into here
                continue;
            }
            case NodeType::Model:
            {
                std::unique_ptr<Model> model = ParseModel(comp);
                entity.AddComponent(std::move(model));
                continue;
            }
            default:
                throw std::runtime_error("Unable to parse xml node " +
                                         std::string(comp.name()));
            }
        }
    }

    for (pugi::xml_node node : doc.child("scene").children("light"))
    {
        LightType type =
            static_cast<LightType>(node.attribute("type").as_int());

        switch (type)
        {
        case LightType::Directional:
        {
            DirectionalLight dirLight{};
            if (auto dirAtt = node.attribute("direction"))
            {
                glm::vec3 dir = ParseVec3(dirAtt.as_string());
                dirLight.SetDirection(dir);
            }
            if (auto intensityAtt = node.attribute("intensity"))
            {
                float intensity = intensityAtt.as_float();
                dirLight.SetIntensity(intensity);
            }
            scene.DirLights.push_back(std::move(dirLight));
            break;
        }
        case LightType::Point:
        {
            PointLight pointLight{};
            if (auto posAtt = node.attribute("position"))
            {
                glm::vec3 pos = ParseVec3(posAtt.as_string());
                pointLight.SetPosition(pos);
            }
            if (auto intensityAtt = node.attribute("intensity"))
            {
                float intensity = intensityAtt.as_float();
                pointLight.SetIntensity(intensity);
            }
            scene.PointLights.push_back(std::move(pointLight));
            break;
        }
        default:
            throw std::runtime_error("Failed to load light type in scene " +
                                     path);
        }
    }

    return scene;
}
