#pragma once

#include <concepts>

#include "LogicComponent.h"
#include "SceneComponent.h"

class Entity
{
public:
    Entity();
    virtual ~Entity() = default;
    Entity(Entity&&) = default;
    Entity& operator=(Entity&&) = default;
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    void AddComponent(std::unique_ptr<SceneComponent> comp);
    void AddComponent(std::unique_ptr<LogicComponent> comp);

    // Gets the first instance of a Component of the templated type.
    template <std::derived_from<SceneComponent> T> T* GetComponent() const
    {
        for (const std::unique_ptr<SceneComponent>& comp : m_SceneComponents)
        {
            T* ptr = dynamic_cast<T*>(comp.get());
            if (ptr)
                return ptr;
        }
        return nullptr;
    }

    // Gets the first instance of a Component of the templated type.
    template <std::derived_from<LogicComponent> T> T* GetComponent() const
    {
        for (const std::unique_ptr<LogicComponent>& comp : m_LogicComponents)
        {
            T* ptr = dynamic_cast<T*>(comp.get());
            if (ptr)
                return ptr;
        }
        return nullptr;
    }

    SceneComponent* GetRootComponent() { return &m_RootComponent; }
    Transform& GetTransform() { return m_RootComponent.GetTransform(); }
    const std::string& GetName() const { return m_Name; }
    static constexpr Transform GetDefaultTransform() { return Transform{}; }

    void SetName(const std::string& newName) { m_Name = newName; }

private:
    SceneComponent m_RootComponent{};
    std::vector<std::unique_ptr<SceneComponent>> m_SceneComponents;
    std::vector<std::unique_ptr<LogicComponent>> m_LogicComponents;

    std::string m_Name;
    uint32_t m_ID;
    inline static uint32_t s_NextID = 0u;
};
