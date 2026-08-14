#include "ModelManager.h"

#include "ModelData.h"
#include <stdexcept>

void ModelManager::Init()
{
    if (s_Instance)
        throw std::runtime_error("Attempting to initialise ModelManager "
                                 "singleton when instance is not null!");

    s_Instance = new ModelManager();
}

void ModelManager::Shutdown()
{
    delete s_Instance;
    s_Instance = nullptr;
}

void ModelManager::RegisterModel(Model* pModel)
{
    m_Models.Pushback(pModel);
}

void ModelManager::UnregisterModel(Model* pModel)
{
    m_Models.Erase(pModel);
}

void ModelManager::GenerateBatches()
{
    m_Drawables.clear();
    m_InstanceDatas.clear();
    m_OpaqueBatches.clear();
    m_TransparentBatches.clear();

    // get all drawables
    for (Model* pModel : m_Models)
    {
        const std::vector<Drawable> drawables = pModel->GetDrawables();
        m_Drawables.insert(m_Drawables.end(), drawables.begin(), drawables.end());
    }

    // sort by mesh and material
    std::sort(m_Drawables.begin(), m_Drawables.end());

    // when sorted
    // iterate and build batches and instance transforms
    size_t size = m_Drawables.size();
    uint32_t i = 0u;
    while (i < size)
    {
        Mesh* pMesh = m_Drawables[i].pMesh;
        ModelData* pModelData = pMesh->GetModelData();

        MeshBatch batch;
        batch.pMaterial = m_Drawables[i].pMat;
        batch.FirstInstance = i;
        batch.IndexBuffer = pModelData->GetIndexBuffer();
        batch.VertexBuffer = pModelData->GetVertexBuffer();
        batch.IndexCount = pMesh->GetIndexCount();
        batch.FirstIndex = pMesh->GetIndexOffset();

        while (i < size && pMesh == m_Drawables[i].pMesh && m_Drawables[i].pMat == batch.pMaterial)
        {
            InstanceData data;
            // float4x4 constructor already implicitely transposes, no don't
            // need to explicitely transpose here before sending to GPU
            data.ModelMatrix = m_Drawables[i].Transform;
            data.NormalMatrix = glm::transpose(glm::inverse(m_Drawables[i].Transform));
            m_InstanceDatas.push_back(data);
            batch.InstanceCount++;
            i++;
        }

        BlendMode blendMode = batch.pMaterial->GetBlendMode();
        if (blendMode == BlendMode::Opaque)
            m_OpaqueBatches.push_back(batch);
        else if (blendMode == BlendMode::Transparent)
            m_TransparentBatches.push_back(batch);
        else
            throw std::runtime_error(std::format("BlendMode of type {} is not supported!",
                                                 static_cast<uint8_t>(blendMode)));
    }
}
