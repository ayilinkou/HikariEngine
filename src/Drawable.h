#pragma once

#include "glm/glm.hpp"

class Mesh;
class Material;

struct Drawable
{
    Mesh* pMesh = nullptr;
    Material* pMat = nullptr;
    glm::mat4 Transform = glm::mat4(1.f);

    bool operator<(const Drawable& other) const
    {
        if (pMesh != other.pMesh)
            return pMesh < other.pMesh;
        return pMat < other.pMat;
    }
};
