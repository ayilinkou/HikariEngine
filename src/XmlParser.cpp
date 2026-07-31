#include "XmlParser.h"
#include "Lights.h"
#include "Log.h"

#include <sstream>

#include "pugixml.hpp"

constexpr LogCategory LogXmlParser("XmlParser");

glm::vec3 XmlParser::ParseVec3(const std::string& str)
{
    std::istringstream iss(str);
    glm::vec3 v;
    iss >> v.x >> v.y >> v.z;
    return v;
}

SceneGraph XmlParser::LoadScene(const std::string& path)
{
	LogMsg(LogSeverity::Info, LogXmlParser, "Loading scene: {}", path.c_str());

    SceneGraph scene;

    pugi::xml_document doc;
    if (!doc.load_file(path.c_str()))
        throw std::runtime_error("Failed to load scene: " + path);

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
