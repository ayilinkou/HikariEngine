#include "ModelManager.h"

#include <stdexcept>

#include "ModelData.h"

void ModelManager::Init()
{
    if (s_Instance)
        throw std::runtime_error("Attempting to initialise ModelManager "
                                 "singleton when instance is not null!");

    s_Instance = new ModelManager();
}

void ModelManager::Shutdown() { delete s_Instance; }

void ModelManager::RegisterModel(Model* pModel) { m_Models.Pushback(pModel); }

void ModelManager::UnregisterModel(Model* pModel) { m_Models.Erase(pModel); }

void ModelManager::GenerateBatches()
{
    m_Drawables.clear();
    m_InstanceDatas.clear();
    m_Batches.clear();

    // get all drawables
    for (Model* pModel : m_Models)
    {
        m_Drawables.push_back(pModel->GetDrawable());
    }

    // sort by mesh and material
    std::sort(m_Drawables.begin(), m_Drawables.end());

    // when sorted
    // iterate and build batches and instance transforms
    size_t size = m_Drawables.size();
    for (size_t i = 0; i < size; i++)
    {
        ModelData* pModelData =
            m_Drawables[i].pModelData; // TODO: update to mesh after refactor

        MeshBatch batch;
        batch.pMaterial = m_Drawables[i].pMat;
        batch.FirstInstance = i;
        batch.IndexBuffer = pModelData->GetIndexBuffer();
        batch.VertexBuffer = pModelData->GetVertexBuffer();
        batch.IndexCount = pModelData->GetIndexCount();
        batch.FirstIndex = 0u; // TODO: update when added meshes

        while (i < size && pModelData == m_Drawables[i].pModelData &&
               m_Drawables[i].pMat == batch.pMaterial)
        {
            InstanceData data;
            data.ModelMatrix = glm::transpose(m_Drawables[i].Transform);

            // Normal matrix is the inverse transpose of the model matrix.
            // However GLM matrices are in column major and so we want to
            // transpose to row major. The two transposes cancel each other out.
            data.NormalMatrix = glm::inverse(m_Drawables[i].Transform);
            m_InstanceDatas.push_back(data);
            batch.InstanceCount++;
            i++;
        }

        m_Batches.push_back(batch);
    }
}
