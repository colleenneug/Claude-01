// ============================================================
//  The actors the Place Actors palette offers.
//
//  Each is a thin composition of components with a sensible default —
//  which is the point of an actor class: not new behaviour, but a
//  useful starting arrangement you then edit in the details panel.
// ============================================================
#pragma once

#include "forge/components/MovementComponents.hpp"
#include "forge/components/RenderComponents.hpp"
#include "forge/scene/Actor.hpp"

namespace forge {

class ScriptComponent;

// A piece of level geometry: one mesh, one material, blocking collision.
class StaticMeshActor : public Actor {
    FORGE_OBJECT(StaticMeshActor, Actor)
public:
    StaticMeshActor();
    StaticMeshComponent* meshComponent() const { return mesh_; }
    void onConstruct() override;

    std::string mesh = "Cube";
    std::string material = "Default";
    int collision = (int)CollisionResponse::Block;
    bool movable = false;

private:
    StaticMeshComponent* mesh_ = nullptr;
};

class PointLightActor : public Actor {
    FORGE_OBJECT(PointLightActor, Actor)
public:
    PointLightActor();
    void onConstruct() override;

    Color lightColor{1.0f, 0.95f, 0.85f, 1.0f};
    float intensity = 25.0f;
    float radius = 14.0f;

private:
    PointLightComponent* light_ = nullptr;
};

class SpotLightActor : public Actor {
    FORGE_OBJECT(SpotLightActor, Actor)
public:
    SpotLightActor();
    void onConstruct() override;

    Color lightColor{1.0f, 0.95f, 0.85f, 1.0f};
    float intensity = 40.0f;
    float radius = 25.0f;
    float innerCone = 18.0f;
    float outerCone = 32.0f;

private:
    SpotLightComponent* light_ = nullptr;
};

// An invisible volume that reports what enters and leaves it. The way
// most level logic gets started.
class TriggerVolume : public Actor {
    FORGE_OBJECT(TriggerVolume, Actor)
public:
    TriggerVolume();
    void onConstruct() override;

    Vec3 extent{2, 2, 2};
    int shape = (int)CollisionShape::Box;
    bool showInGame = false;

private:
    StaticMeshComponent* volume_ = nullptr;
};

// A camera you can place, for cutscenes and fixed views.
class CameraActor : public Actor {
    FORGE_OBJECT(CameraActor, Actor)
public:
    CameraActor();
    CameraComponent* cameraComponent() const { return camera_; }

private:
    CameraComponent* camera_ = nullptr;
};

// A mesh that spins or bobs. A pickup, a fan, a moving platform.
class RotatingActor : public Actor {
    FORGE_OBJECT(RotatingActor, Actor)
public:
    RotatingActor();
    void onConstruct() override;

    std::string mesh = "Torus";
    std::string material = "GlowAmber";
    Rotator rotationRate{0, 120, 0};
    float bobAmplitude = 0.25f;
    float bobSpeed = 0.6f;
    int collision = (int)CollisionResponse::Overlap;

private:
    StaticMeshComponent* mesh_ = nullptr;
    RotatingMovementComponent* spin_ = nullptr;
};

} // namespace forge
