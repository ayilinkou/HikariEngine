#pragma once

#include "Transform.h"
#include "SceneGraph.h"
#include "Model.h"

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

	static SceneGraph LoadScene(const std::string& path);

private:
	static NodeType TagToNodeType(std::string_view tag);
};
