#include "Model.h"

#include "ModelData.h"
#include "ModelManager.h"
#include "ResourceManager.h"

Model::Model(const std::string& path)
    : m_pModelData(ResourceManager::Get()->LoadModel(path)), m_Path(path)
{
    ModelManager::Get()->RegisterModel(this);
}

Model::~Model()
{
    ModelManager::Get()->UnregisterModel(this);
    ResourceManager::Get()->UnloadModel(m_pModelData->GetFilepath());
}

std::vector<Drawable> Model::GetDrawables() const
{
    std::vector<Drawable> drawables = m_pModelData->GetDrawables();
    // TODO: not ideal, can maybe move into a GPU buffer and handle in the
    // shader
    for (Drawable& d : drawables)
    {
        d.Transform = GetAccumulatedTransform() * d.Transform;
    }
    return drawables;
}
