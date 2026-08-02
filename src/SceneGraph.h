#pragma once

#include "Lights.h"
#include "Entity.h"

struct SceneGraph
{
	std::vector<DirectionalLight*> DirLights;
	std::vector<PointLight*> PointLights;
	std::vector<std::unique_ptr<Entity>> Entities;
};

