#pragma once

#include <string>

#include "SceneComponent.h"
#include "Drawable.h"

class ModelData;
class Material;
class Mesh;

class Model : public SceneComponent
{
public:
    Model(const std::string& path);
    ~Model();

	std::vector<Drawable> GetDrawables() const;

private:
    ModelData* m_pModelData;
};
