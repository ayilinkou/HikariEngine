#pragma once

#include "InstanceData.h"
#include "Model.h"
#include "SwapbackArray.h"

class ModelManager
{
private:
    ModelManager() {}

public:
    static void Init();
    static void Shutdown();

    static ModelManager* Get() { return s_Instance; }

    void RegisterModel(Model* pModel);
    void UnregisterModel(Model* pModel);

    void GenerateBatches();
    const std::vector<MeshBatch>& GetOpaqueBatches() const { return m_OpaqueBatches; }
    const std::vector<MeshBatch>& GetTransparentBatches() const { return m_TransparentBatches; }
    const std::vector<InstanceData>& GetInstanceDatas() const { return m_InstanceDatas; }

private:
    SwapbackArray<Model*> m_Models;

    std::vector<InstanceData> m_InstanceDatas;
    std::vector<MeshBatch> m_OpaqueBatches;
    std::vector<MeshBatch> m_TransparentBatches;
    std::vector<Drawable> m_Drawables;

    inline static ModelManager* s_Instance = nullptr;
};
