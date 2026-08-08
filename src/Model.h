#pragma once

#include "Drawable.h"
#include "SceneComponent.h"

class ModelData;
class Material;
class Mesh;

class Model : public SceneComponent
{
public:
    Model(const std::string& path);
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) = delete;
    Model& operator=(Model&&) = delete;

    std::vector<Drawable> GetDrawables() const;
    const std::string& GetPath() const { return m_Path; }

    static constexpr Transform GetDefaultTransform() { return Transform{}; }

private:
    ModelData* m_pModelData;
    std::string m_Path;
};
