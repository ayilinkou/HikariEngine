#include "XmlParser.h"
#include "Lights.h"
#include "Log.h"

#include <sstream>
#include <stdexcept>

#include "pugixml.hpp"

constexpr LogCategory LogXmlParser("XmlParser");

namespace XML
{
constexpr const char* Position = "position";
constexpr const char* Rotation = "rotation";
constexpr const char* Scale = "scale";
constexpr const char* Entity = "entity";
constexpr const char* Transform = "transform";
constexpr const char* Model = "model";
constexpr const char* Path = "path";
constexpr const char* Scene = "scene";
constexpr const char* Name = "name";
constexpr const char* Light = "light";
constexpr const char* Type = "type";
constexpr const char* Intensity = "intensity";
constexpr const char* Direction = "direction";
constexpr const char* Color = "color";
} // namespace XML

glm::vec3 XmlParser::ParseVec3(const std::string& str)
{
    std::istringstream iss(str);
    glm::vec3 v;
    iss >> v.x >> v.y >> v.z;
    return v;
}

Transform XmlParser::ParseTransform(const pugi::xml_node& node)
{
    auto posAtt = node.attribute(XML::Position);
    auto rotAtt = node.attribute(XML::Rotation);
    auto scaleAtt = node.attribute(XML::Scale);

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
    auto pathAtt = node.attribute(XML::Path);
    if (!pathAtt)
    {
        throw std::runtime_error(
            "Found model with no \"path\" value when parsing scene!");
    }

    const char* path = pathAtt.as_string();
    std::unique_ptr<Model> model = std::make_unique<Model>(path);

    if (auto transformNode = node.child(XML::Transform))
    {
        Transform transform = ParseTransform(transformNode);
        model->GetTransform() = transform;
    }

    return model;
};

NodeType XmlParser::TagToNodeType(std::string_view tag)
{
    if (tag == XML::Transform)
        return NodeType::Transform;
    if (tag == XML::Light)
        return NodeType::Light;
    if (tag == XML::Model)
        return NodeType::Model;
    return NodeType::Unknown;
}

std::unique_ptr<SceneGraph> XmlParser::LoadScene(const std::string& path)
{
    LogMsg(LogSeverity::Info, LogXmlParser, "Loading scene: {}", path.c_str());

	std::unique_ptr<SceneGraph> scene = std::make_unique<SceneGraph>();

    pugi::xml_document doc;
    if (!doc.load_file(path.c_str()))
        throw std::runtime_error("Failed to load scene: " + path);

    for (pugi::xml_node node : doc.child(XML::Scene).children(XML::Entity))
    {
        scene->Entities.push_back(std::make_unique<Entity>());
        Entity& entity = *scene->Entities.back();
        entity.SetName(node.attribute(XML::Name).as_string());

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

    for (pugi::xml_node node : doc.child(XML::Scene).children(XML::Light))
    {
        LightType type =
            static_cast<LightType>(node.attribute(XML::Type).as_int());

        switch (type)
        {
        case LightType::Directional:
        {
            scene->DirLights.push_back(std::make_unique<DirectionalLight>());
            DirectionalLight& dirLight = *scene->DirLights.back().get();
            if (auto dirAtt = node.attribute(XML::Direction))
            {
                glm::vec3 dir = ParseVec3(dirAtt.as_string());
                dirLight.SetDirection(dir);
            }
            if (auto intensityAtt = node.attribute(XML::Intensity))
            {
                float intensity = intensityAtt.as_float();
                dirLight.SetIntensity(intensity);
            }
            if (auto colorAtt = node.attribute(XML::Color))
            {
                glm::vec3 color = ParseVec3(colorAtt.as_string());
                dirLight.SetColor(color);
            }
            break;
        }
        case LightType::Point:
        {
            scene->PointLights.push_back(std::make_unique<PointLight>());
            PointLight& pointLight = *scene->PointLights.back().get();
            if (auto posAtt = node.attribute(XML::Position))
            {
                glm::vec3 pos = ParseVec3(posAtt.as_string());
                pointLight.SetPosition(pos);
            }
            if (auto intensityAtt = node.attribute(XML::Intensity))
            {
                float intensity = intensityAtt.as_float();
                pointLight.SetIntensity(intensity);
            }
            if (auto colorAtt = node.attribute(XML::Color))
            {
                glm::vec3 color = ParseVec3(colorAtt.as_string());
                pointLight.SetColor(color);
            }
            break;
        }
        default:
            throw std::runtime_error("Failed to load light type in scene " +
                                     path);
        }
    }

    return scene;
}

std::string XmlParser::Vec3ToString(glm::vec3 v)
{
    std::ostringstream oss;
    oss << v.x << " " << v.y << " " << v.z;
    return oss.str();
}

void XmlParser::WriteTransform(pugi::xml_node& parent, const Transform& t,
                               const Transform& defaultTransform)
{
    if (t == defaultTransform)
        return;

    pugi::xml_node transformNode = parent.append_child(XML::Transform);

    transformNode.append_attribute(XML::Position) =
        Vec3ToString(t.Position).c_str();
    transformNode.append_attribute(XML::Rotation) =
        Vec3ToString(t.Rotation).c_str();
    transformNode.append_attribute(XML::Scale) = Vec3ToString(t.Scale).c_str();
}

void XmlParser::SaveScene(const std::unique_ptr<SceneGraph>& sceneGraph, const std::string& path)
{
    LogMsg(LogSeverity::Info, LogXmlParser, "Saving scene: {}", path.c_str());

    pugi::xml_document doc;
    pugi::xml_node sceneNode = doc.append_child(XML::Scene);

    for (const std::unique_ptr<Entity>& entityPtr : sceneGraph->Entities)
    {
        const Entity& entity = *entityPtr.get();
        pugi::xml_node entityNode = sceneNode.append_child(XML::Entity);
        entityNode.append_attribute(XML::Name) = entity.GetName().c_str();

        WriteTransform(entityNode, entityPtr->GetTransform(),
                       entity.GetDefaultTransform());

        if (Model* pModel = entity.GetComponent<Model>())
        {
            pugi::xml_node modelNode = entityNode.append_child(XML::Model);
            modelNode.append_attribute(XML::Path) = pModel->GetPath().c_str();
            WriteTransform(modelNode, pModel->GetTransform(),
                           pModel->GetDefaultTransform());
        }
    }

    for (const std::unique_ptr<DirectionalLight>& dirLight :
         sceneGraph->DirLights)
    {
        pugi::xml_node lightNode = sceneNode.append_child(XML::Light);
        DirectionalLight::Data data = dirLight->GetData();
        lightNode.append_attribute(XML::Type) =
            static_cast<int>(LightType::Directional);
        lightNode.append_attribute(XML::Intensity) = data.Intensity;
        lightNode.append_attribute(XML::Direction) = Vec3ToString(data.Dir);
        lightNode.append_attribute(XML::Color) = Vec3ToString(data.Color);
    }

    for (const std::unique_ptr<PointLight>& pointLight : sceneGraph->PointLights)
    {
        pugi::xml_node lightNode = sceneNode.append_child(XML::Light);
        PointLight::Data data = pointLight->GetData();
        lightNode.append_attribute(XML::Type) =
            static_cast<int>(LightType::Point);
        lightNode.append_attribute(XML::Intensity) = data.Intensity;
        lightNode.append_attribute(XML::Position) = Vec3ToString(data.Pos);
        lightNode.append_attribute(XML::Color) = Vec3ToString(data.Color);
    }

    if (!doc.save_file(path.c_str(), "\t"))
        throw std::runtime_error("Failed to save scene: " + path);
}
