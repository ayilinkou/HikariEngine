#pragma once

#include <cstdint>
#include <memory>

#include "Component.h"
#include "SceneComponent.h"

class GameObject : public SceneComponent
{
public:
    GameObject() { m_ID = s_NextID++; }
    virtual ~GameObject() = default;

    // This moves a component into the components vector. Be sure to call with
    // std::move().
    void AddComponent(std::unique_ptr<Component> comp)
    {
        m_Components.push_back(std::move(comp));
    }

    // Gets the first instance of a Component of the templated type
    template <typename T> T* GetComponent()
    {
        for (std::unique_ptr<Component>& comp : m_Components)
        {
            T* ptr = dynamic_cast<T*>(comp.get());
            if (ptr)
                return ptr;
        }
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<Component>> m_Components;

    uint32_t m_ID;
    inline static uint32_t s_NextID = 0u;
};
