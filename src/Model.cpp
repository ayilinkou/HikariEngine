#include "Model.h"

#include "ModelData.h"
#include "ModelManager.h"
#include "ResourceManager.h"

Model::Model(const std::string& path)
    : m_pModelData(ResourceManager::Get()->LoadModel(path))
{
    ModelManager::Get()->RegisterModel(this);
}

Model::~Model()
{
    ModelManager::Get()->UnregisterModel(this);
    ResourceManager::Get()->UnloadModel(m_pModelData->GetFilepath());
}

Drawable Model::GetDrawable() const
{
    return {.pModelData = m_pModelData,
            .pMat = m_pModelData->GetMaterial(),
            .Transform = GetAccumulatedTransform()};
}
