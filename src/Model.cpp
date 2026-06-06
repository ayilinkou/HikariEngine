#include "Model.h"

#include "ModelData.h"
#include "ResourceManager.h"
#include "ModelManager.h"

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
            .Transform =
                m_Transform
                    .ToWorldMatrix()}; // TODO: do I need world or local here?
}
