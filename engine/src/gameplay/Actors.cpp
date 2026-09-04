#include "forge/gameplay/Actors.hpp"

#include "forge/scene/World.hpp"

namespace forge {

// -----------------------------------------------------------------
//  StaticMeshActor
// -----------------------------------------------------------------

StaticMeshActor::StaticMeshActor() {
    name = "Static Mesh";
    mesh_ = addComponent<StaticMeshComponent>("Mesh");
    setRoot(mesh_);
    tickEnabled = false;
}

void StaticMeshActor::onConstruct() {
    if (!mesh_) return;
    mesh_->mesh = mesh;
    mesh_->material = material;
    mesh_->collisionResponse = collision;
    mesh_->mobility = movable ? (int)Mobility::Movable : (int)Mobility::Static;
    // The collision proxy follows the mesh, so a sphere mesh gets a
    // sphere collider without a second decision.
    mesh_->collisionShape = (mesh == "Sphere") ? (int)CollisionShape::Sphere
                          : (mesh == "Capsule") ? (int)CollisionShape::Capsule
                          : (int)CollisionShape::Box;
    mesh_->updateCollider();
}

FORGE_CLASS_BEGIN(StaticMeshActor)
    FORGE_DISPLAY("Static Mesh")
    FORGE_CATEGORY("Basic")
    FORGE_ICON("M")
    FORGE_DESCRIBE("A piece of level geometry.")
    FORGE_PROP(mesh).asset(RefKind::Mesh).cat("Mesh")
    FORGE_PROP(material).asset(RefKind::Material).cat("Mesh")
    FORGE_PROP(collision).options({"None", "Overlap", "Block"}).cat("Collision")
    FORGE_PROP(movable).cat("Collision")
        .tooltip("Movable geometry can be repositioned while playing, at some cost.")
FORGE_CLASS_END()
FORGE_REGISTER(StaticMeshActor)

// -----------------------------------------------------------------
//  Lights
// -----------------------------------------------------------------

PointLightActor::PointLightActor() {
    name = "Point Light";
    light_ = addComponent<PointLightComponent>("Light");
    setRoot(light_);
    tickEnabled = false;
}

void PointLightActor::onConstruct() {
    if (!light_) return;
    light_->color = lightColor;
    light_->intensity = intensity;
    light_->radius = radius;
}

FORGE_CLASS_BEGIN(PointLightActor)
    FORGE_DISPLAY("Point Light")
    FORGE_CATEGORY("Lighting")
    FORGE_ICON("L")
    FORGE_DESCRIBE("An omnidirectional light.")
    FORGE_PROP(lightColor).cat("Light").label("Color")
    FORGE_PROP(intensity).range(0.0f, 200.0f).cat("Light")
    FORGE_PROP(radius).range(0.5f, 200.0f).cat("Light")
FORGE_CLASS_END()
FORGE_REGISTER(PointLightActor)

SpotLightActor::SpotLightActor() {
    name = "Spot Light";
    light_ = addComponent<SpotLightComponent>("Light");
    setRoot(light_);
    // Pointing straight down is the useful default for a placed spot.
    light_->setRotation({-90.0f, 0.0f, 0.0f});
    tickEnabled = false;
}

void SpotLightActor::onConstruct() {
    if (!light_) return;
    light_->color = lightColor;
    light_->intensity = intensity;
    light_->radius = radius;
    light_->innerConeDegrees = std::min(innerCone, outerCone);
    light_->outerConeDegrees = std::max(innerCone, outerCone);
}

FORGE_CLASS_BEGIN(SpotLightActor)
    FORGE_DISPLAY("Spot Light")
    FORGE_CATEGORY("Lighting")
    FORGE_ICON("L")
    FORGE_DESCRIBE("A cone of light.")
    FORGE_PROP(lightColor).cat("Light").label("Color")
    FORGE_PROP(intensity).range(0.0f, 400.0f).cat("Light")
    FORGE_PROP(radius).range(0.5f, 200.0f).cat("Light")
    FORGE_PROP(innerCone).range(0.0f, 89.0f).cat("Light")
    FORGE_PROP(outerCone).range(1.0f, 89.0f).cat("Light")
FORGE_CLASS_END()
FORGE_REGISTER(SpotLightActor)

// -----------------------------------------------------------------
//  TriggerVolume
// -----------------------------------------------------------------

TriggerVolume::TriggerVolume() {
    name = "Trigger";
    volume_ = addComponent<StaticMeshComponent>("Volume");
    volume_->mesh = "Cube";
    volume_->material = "GlowCyan";
    volume_->collisionResponse = (int)CollisionResponse::Overlap;
    volume_->castShadow = false;
    setRoot(volume_);
    hiddenInGame = true;
    tickEnabled = false;
}

void TriggerVolume::onConstruct() {
    if (!volume_) return;
    volume_->collisionShape = shape;
    volume_->mesh = shape == (int)CollisionShape::Sphere ? "Sphere"
                  : shape == (int)CollisionShape::Capsule ? "Capsule" : "Cube";
    // The mesh is a unit shape, so the extent is expressed as scale and
    // the collider picks it up from the transform.
    volume_->setScale(maxv(extent, Vec3(0.01f)) * 2.0f);
    hiddenInGame = !showInGame;
    volume_->updateCollider();
}

FORGE_CLASS_BEGIN(TriggerVolume)
    FORGE_DISPLAY("Trigger Volume")
    FORGE_CATEGORY("Gameplay")
    FORGE_ICON("T")
    FORGE_DESCRIBE("Reports what enters and leaves it. Invisible while playing.")
    FORGE_PROP(extent).cat("Trigger").tooltip("Half size of the volume.")
    FORGE_PROP(shape).options({"Box", "Sphere", "Capsule"}).cat("Trigger")
    FORGE_PROP(showInGame).cat("Trigger")
FORGE_CLASS_END()
FORGE_REGISTER(TriggerVolume)

// -----------------------------------------------------------------
//  CameraActor
// -----------------------------------------------------------------

CameraActor::CameraActor() {
    name = "Camera";
    camera_ = addComponent<CameraComponent>("Camera");
    setRoot(camera_);
    tickEnabled = false;
}

FORGE_CLASS_BEGIN(CameraActor)
    FORGE_DISPLAY("Camera")
    FORGE_CATEGORY("Gameplay")
    FORGE_ICON("C")
    FORGE_DESCRIBE("A placed camera, for fixed views and cutscenes.")
FORGE_CLASS_END()
FORGE_REGISTER(CameraActor)

// -----------------------------------------------------------------
//  RotatingActor
// -----------------------------------------------------------------

RotatingActor::RotatingActor() {
    name = "Rotator";
    mesh_ = addComponent<StaticMeshComponent>("Mesh");
    mesh_->mesh = "Torus";
    mesh_->material = "GlowAmber";
    mesh_->collisionResponse = (int)CollisionResponse::Overlap;
    mesh_->mobility = (int)Mobility::Movable;
    setRoot(mesh_);
    spin_ = addComponent<RotatingMovementComponent>("Spin");
}

void RotatingActor::onConstruct() {
    if (mesh_) {
        mesh_->mesh = mesh;
        mesh_->material = material;
        mesh_->collisionResponse = collision;
        // Anything that moves must be movable, or its collider would stay
        // where the level file put it.
        mesh_->mobility = (int)Mobility::Movable;
        mesh_->updateCollider();
    }
    if (spin_) {
        spin_->rotationRate = rotationRate;
        spin_->bobAmplitude = bobAmplitude;
        spin_->bobSpeed = bobSpeed;
    }
}

FORGE_CLASS_BEGIN(RotatingActor)
    FORGE_DISPLAY("Rotating Actor")
    FORGE_CATEGORY("Gameplay")
    FORGE_ICON("R")
    FORGE_DESCRIBE("A mesh that spins and bobs. Pickups, fans, platforms.")
    FORGE_PROP(mesh).asset(RefKind::Mesh).cat("Mesh")
    FORGE_PROP(material).asset(RefKind::Material).cat("Mesh")
    FORGE_PROP(rotationRate).cat("Motion").tooltip("Degrees per second.")
    FORGE_PROP(bobAmplitude).range(0.0f, 10.0f).cat("Motion")
    FORGE_PROP(bobSpeed).range(0.0f, 10.0f).cat("Motion")
    FORGE_PROP(collision).options({"None", "Overlap", "Block"}).cat("Collision")
FORGE_CLASS_END()
FORGE_REGISTER(RotatingActor)

} // namespace forge
