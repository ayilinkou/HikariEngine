#pragma once

#include "Component.h"
#include "Transform.h"

class SceneComponent : public Component
{
public:
    SceneComponent() = default;
    virtual ~SceneComponent() = default;

    Transform& GetTransform() { return m_Transform; }

protected:
    Transform m_Transform;
};
