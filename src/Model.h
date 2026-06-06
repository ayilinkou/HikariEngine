#pragma once

#include <string>

#include "SceneComponent.h"

class ModelData;
class Material;

struct Drawable
{
    ModelData* pModelData = nullptr; // TODO: replace with Mesh once refactored
    Material* pMat = nullptr;
    glm::mat4 Transform = glm::mat4(1.f);

    bool operator<(const Drawable& other) const
    {
        if (pModelData != other.pModelData)
            return pModelData < other.pModelData;
        return pMat < other.pMat;
    }
};

class Model : public SceneComponent
{
public:
    Model(const std::string& path);
    ~Model();

    Drawable GetDrawable() const;

private:
    ModelData* m_pModelData;
};
