#pragma once

#include "glm/glm.hpp"

#include "Component.h"
#include "Transform.h"

// SceneComponent is a Component class which contains a Transform. Components
// which do not need a Transform should inherit from LogicComponent.
class SceneComponent : public Component
{
public:
    SceneComponent() = default;
    virtual ~SceneComponent() = default;

    void SetOwningComponent(SceneComponent* pComp) { m_pOwningComp = pComp; }

    Component* GetOwningComponent() const { return m_pOwningComp; }
    Transform& GetTransform() { return m_Transform; }

    glm::mat4 GetAccumulatedTransform() const
    {
        // if this component is the root component
        if (m_pOwningComp == nullptr)
        {
            // This component is the root component. Its translation should
            // not be affected by scale and so will use ToWorldMatrix()
            return m_Transform.ToWorldMatrix();
        }

        // We want the transform to be affected by the parent's scale so
        // ToLocalMatrix() is used here.
        return m_pOwningComp->GetAccumulatedTransform() *
               m_Transform.ToLocalMatrix();
    }

protected:
    Transform m_Transform;
    SceneComponent* m_pOwningComp = nullptr;
};
