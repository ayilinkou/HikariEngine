#pragma once

#include "Lights.h"
#include "Entity.h"

struct SceneGraph
{
	std::vector<std::unique_ptr<DirectionalLight>> DirLights;
	std::vector<std::unique_ptr<PointLight>> PointLights;
	std::vector<std::unique_ptr<Entity>> Entities;
};

