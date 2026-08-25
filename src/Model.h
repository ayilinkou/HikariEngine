#pragma once

#include <memory>
#include <string>
#include <vector>

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

private:
    std::shared_ptr<ModelData> m_ModelData = nullptr;
    std::string m_Path;
};
