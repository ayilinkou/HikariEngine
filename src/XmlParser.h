#pragma once

#include "Lights.h"

// TODO: move to separate class
struct SceneGraph
{
	std::vector<DirectionalLight> DirLights;
	std::vector<PointLight> PointLights;
};

class XmlParser
{
public:
	XmlParser() = delete;

	static glm::vec3 ParseVec3(const std::string& str);

	static SceneGraph LoadScene(const std::string& path);
};
