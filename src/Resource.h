#pragma once

#include <cstdint>

class Resource
{
    friend class ResourceManager;

public:
    Resource(void* pData) : m_pData(pData) { AddRef(); }

    uint32_t AddRef() { return ++m_RefCount; }
    uint32_t RemoveRef() { return --m_RefCount; }

    void* GetDataPtr() const { return m_pData; }

private:
    uint32_t m_RefCount = 0;
    void* m_pData = nullptr;
};
