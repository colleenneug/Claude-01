// ============================================================
//  Components that put something in the world you can see or hit.
//
//  PrimitiveComponent is the base for anything with extent: it owns the
//  collision settings and registers a collider with the physics scene.
//  StaticMeshComponent draws geometry. The light and camera components
//  are what the renderer gathers each frame.
// ============================================================
#pragma once

#include "forge/physics/Physics.hpp"
#include "forge/scene/Component.hpp"

namespace forge {

class Mesh;
class Material;

class PrimitiveComponent : public SceneComponent {
    FORGE_OBJECT(PrimitiveComponent, SceneComponent)
public:
    // Reflected as ints with name tables, which is what gives the details
    // panel a dropdown without a bespoke widget per enum.
    int collisionShape = (int)CollisionShape::Box;
    int collisionResponse = (int)CollisionResponse::Block;
    int mobility = (int)Mobility::Static;
    bool castShadow = true;
    Vec3 collisionPadding{0, 0, 0};

    void onRegister() override;
    void onUnregister() override;
    void onConstruct() override;

    // Pushes the current transform and settings into the physics scene.
    // Movable primitives are refreshed by the solver each step; static
    // ones only when something changes.
    void updateCollider();
    Collider* collider() const;

    // Half extents in local space, before scale, which the collider
    // turns into world extents.
    virtual Vec3 collisionExtent() const;
};

class StaticMeshComponent : public PrimitiveComponent {
    FORGE_OBJECT(StaticMeshComponent, PrimitiveComponent)
public:
    std::string mesh = "Cube";
    std::string material = "Default";
    Color tint{1, 1, 1, 1};

    Box localBounds() const override;
    Vec3 collisionExtent() const override;
    void onConstruct() override;

    // Resolved through the world's asset library; never null once the
    // component is registered in a world that has one.
    Mesh* resolvedMesh() const;
    Material* resolvedMaterial() const;
};

class CameraComponent : public SceneComponent {
    FORGE_OBJECT(CameraComponent, SceneComponent)
public:
    float fieldOfView = 70.0f;
    float nearClip = 0.1f;
    float farClip = 500.0f;
    bool orthographic = false;
    float orthoWidth = 20.0f;

    Mat4 viewMatrix() const;
    Mat4 projectionMatrix(float aspect) const;
};

class LightComponent : public SceneComponent {
    FORGE_OBJECT(LightComponent, SceneComponent)
public:
    Color color{1, 1, 1, 1};
    float intensity = 10.0f;
    bool castShadow = false;
};

class PointLightComponent : public LightComponent {
    FORGE_OBJECT(PointLightComponent, LightComponent)
public:
    float radius = 12.0f;
    Box localBounds() const override;
};

class SpotLightComponent : public PointLightComponent {
    FORGE_OBJECT(SpotLightComponent, PointLightComponent)
public:
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 35.0f;
};

// Holds a camera at a distance behind its owner and pulls it in when
// something would come between them. The standard third-person rig.
class SpringArmComponent : public SceneComponent {
    FORGE_OBJECT(SpringArmComponent, SceneComponent)
public:
    float targetLength = 6.0f;
    Vec3 socketOffset{0, 0, 0};
    bool collisionTest = true;
    float probeRadius = 0.3f;
    float lagSpeed = 12.0f;
    bool inheritYawOnly = false;

    void tick(float dt) override;
    void onRegister() override { tickEnabled = true; }
    Vec3 socketLocation() const { return socket_; }

private:
    Vec3 socket_{0, 0, 0};
    float currentLength_ = 0.0f;
    bool initialised_ = false;
};

} // namespace forge
