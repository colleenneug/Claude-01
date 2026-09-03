#include "forge/components/RenderComponents.hpp"

#include "forge/assets/AssetLibrary.hpp"
#include "forge/core/Log.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"

namespace forge {

// -----------------------------------------------------------------
//  PrimitiveComponent
// -----------------------------------------------------------------

Vec3 PrimitiveComponent::collisionExtent() const { return Vec3(0.5f) + collisionPadding; }

Collider* PrimitiveComponent::collider() const {
    World* w = world();
    if (!w) return nullptr;
    return w->physics().colliderFor(const_cast<PrimitiveComponent*>(this));
}

void PrimitiveComponent::onRegister() {
    World* w = world();
    if (!w) return;
    if ((CollisionResponse)collisionResponse == CollisionResponse::None) return;
    w->physics().addCollider(this);
    updateCollider();
}

void PrimitiveComponent::onUnregister() {
    if (World* w = world()) w->physics().removeCollider(this);
}

void PrimitiveComponent::onConstruct() { updateCollider(); }

void PrimitiveComponent::updateCollider() {
    World* w = world();
    if (!w) return;
    PhysicsScene& phys = w->physics();

    if ((CollisionResponse)collisionResponse == CollisionResponse::None) {
        // Turning collision off removes the collider outright rather than
        // leaving a disabled one sitting in the broadphase.
        if (phys.colliderFor(this)) phys.removeCollider(this);
        return;
    }

    Collider* c = phys.colliderFor(this);
    if (!c) c = phys.addCollider(this);
    if (!c) return;

    c->actor = owner();
    c->shape = (CollisionShape)(int)clampf((float)collisionShape, 0.0f, 2.0f);
    c->response = (CollisionResponse)(int)clampf((float)collisionResponse, 0.0f, 2.0f);
    c->isStatic = (Mobility)mobility == Mobility::Static;
    c->localExtent = collisionExtent();
    phys.refreshCollider(this);
}

FORGE_CLASS_BEGIN(PrimitiveComponent)
    FORGE_DISPLAY("Primitive")
    FORGE_CATEGORY("Component")
    FORGE_PROP(collisionShape).options({"Box", "Sphere", "Capsule"}).cat("Collision").label("Shape")
    FORGE_PROP(collisionResponse).options({"None", "Overlap", "Block"}).cat("Collision").label("Collision")
        .tooltip("Block stops things. Overlap fires events but lets them through.")
    FORGE_PROP(mobility).options({"Static", "Movable"}).cat("Collision")
        .tooltip("Static geometry goes in the broadphase grid and is much cheaper to test.")
    FORGE_PROP(castShadow).cat("Rendering")
    FORGE_PROP(collisionPadding).cat("Collision")
        .tooltip("Grows the collision shape without changing what is drawn.")
FORGE_CLASS_END()
FORGE_REGISTER(PrimitiveComponent)

// -----------------------------------------------------------------
//  StaticMeshComponent
// -----------------------------------------------------------------

Mesh* StaticMeshComponent::resolvedMesh() const {
    World* w = world();
    AssetLibrary* lib = w ? w->assets() : nullptr;
    return lib ? lib->meshOrDefault(mesh) : nullptr;
}

Material* StaticMeshComponent::resolvedMaterial() const {
    World* w = world();
    AssetLibrary* lib = w ? w->assets() : nullptr;
    return lib ? lib->materialOrDefault(material) : nullptr;
}

Box StaticMeshComponent::localBounds() const {
    Mesh* m = resolvedMesh();
    if (m && m->bounds.valid()) return m->bounds;
    return Box::fromCenterExtents(Vec3::Zero, Vec3(0.5f));
}

Vec3 StaticMeshComponent::collisionExtent() const {
    // The collision proxy follows the mesh, so swapping a cube for a
    // sphere changes what you bump into without a second edit.
    Box b = localBounds();
    Vec3 e = b.valid() ? b.extents() : Vec3(0.5f);
    return maxv(e + collisionPadding, Vec3(1e-3f));
}

void StaticMeshComponent::onConstruct() { updateCollider(); }

FORGE_CLASS_BEGIN(StaticMeshComponent)
    FORGE_DISPLAY("Static Mesh")
    FORGE_CATEGORY("Component")
    FORGE_PROP(mesh).asset(RefKind::Mesh).cat("Mesh")
    FORGE_PROP(material).asset(RefKind::Material).cat("Mesh")
    FORGE_PROP(tint).cat("Mesh").tooltip("Multiplied over the material's base colour.")
FORGE_CLASS_END()
FORGE_REGISTER(StaticMeshComponent)

// -----------------------------------------------------------------
//  CameraComponent
// -----------------------------------------------------------------

Mat4 CameraComponent::viewMatrix() const {
    const Transform& t = worldTransform();
    return Mat4::lookAt(t.position, t.position + t.rotation.forward(), t.rotation.up());
}

Mat4 CameraComponent::projectionMatrix(float aspect) const {
    if (orthographic) {
        const float hw = orthoWidth * 0.5f;
        const float hh = hw / std::max(aspect, 1e-4f);
        return Mat4::orthographic(-hw, hw, -hh, hh, nearClip, farClip);
    }
    return Mat4::perspective(fieldOfView, aspect, nearClip, farClip);
}

FORGE_CLASS_BEGIN(CameraComponent)
    FORGE_DISPLAY("Camera")
    FORGE_CATEGORY("Component")
    FORGE_PROP(fieldOfView).range(20.0f, 140.0f).cat("Camera").label("Field Of View")
    FORGE_PROP(nearClip).range(0.01f, 5.0f).cat("Camera")
    FORGE_PROP(farClip).range(10.0f, 5000.0f).cat("Camera")
    FORGE_PROP(orthographic).cat("Camera")
    FORGE_PROP(orthoWidth).range(1.0f, 200.0f).cat("Camera")
FORGE_CLASS_END()
FORGE_REGISTER(CameraComponent)

// -----------------------------------------------------------------
//  Lights
// -----------------------------------------------------------------

FORGE_CLASS_BEGIN(LightComponent)
    FORGE_DISPLAY("Light")
    FORGE_CATEGORY("Component")
    FORGE_PROP(color).cat("Light")
    FORGE_PROP(intensity).range(0.0f, 200.0f).cat("Light")
    FORGE_PROP(castShadow).cat("Light")
FORGE_CLASS_END()
FORGE_REGISTER(LightComponent)

Box PointLightComponent::localBounds() const {
    return Box::fromCenterExtents(Vec3::Zero, Vec3(radius));
}

FORGE_CLASS_BEGIN(PointLightComponent)
    FORGE_DISPLAY("Point Light")
    FORGE_PROP(radius).range(0.5f, 200.0f).cat("Light")
        .tooltip("Beyond this the light contributes nothing, so it can be culled.")
FORGE_CLASS_END()
FORGE_REGISTER(PointLightComponent)

FORGE_CLASS_BEGIN(SpotLightComponent)
    FORGE_DISPLAY("Spot Light")
    FORGE_PROP(innerConeDegrees).range(0.0f, 89.0f).cat("Light").label("Inner Cone")
    FORGE_PROP(outerConeDegrees).range(1.0f, 89.0f).cat("Light").label("Outer Cone")
FORGE_CLASS_END()
FORGE_REGISTER(SpotLightComponent)

// -----------------------------------------------------------------
//  SpringArmComponent
// -----------------------------------------------------------------

void SpringArmComponent::tick(float dt) {
    const Transform& t = worldTransform();
    Quat rot = t.rotation;
    if (inheritYawOnly) {
        // Ignoring pitch keeps the camera level while the pawn looks up
        // and down, which is what a shoulder camera usually wants.
        Rotator r = Rotator::fromQuat(rot);
        rot = Rotator{0.0f, r.yaw, 0.0f}.toQuat();
    }

    const Vec3 origin = t.position;
    const Vec3 back = rot * Vec3::Back;
    float desired = targetLength;

    if (collisionTest && world()) {
        QueryParams q;
        if (owner()) q.ignore.push_back(owner());
        HitResult h = world()->physics().raycast(origin, back, targetLength + probeRadius, q);
        // Stop short of the wall so the near plane does not end up inside it.
        if (h.hit) desired = std::max(0.0f, h.distance - probeRadius);
    }

    if (!initialised_) { currentLength_ = desired; initialised_ = true; }
    // Springing out is smoothed; springing in is immediate, because a
    // camera that eases into a wall clips through it on the way.
    currentLength_ = desired < currentLength_ ? desired : damp(currentLength_, desired, lagSpeed, dt);

    socket_ = origin + back * currentLength_ + rot * socketOffset;

    // The camera is a child, so writing the socket back is what actually
    // moves it.
    for (SceneComponent* child : children()) {
        if (auto* cam = dynamic_cast<CameraComponent*>(child)) {
            cam->setWorldLocation(socket_);
            cam->setWorldRotation(rot);
        }
    }
}

FORGE_CLASS_BEGIN(SpringArmComponent)
    FORGE_DISPLAY("Spring Arm")
    FORGE_CATEGORY("Component")
    FORGE_PROP(targetLength).range(0.0f, 40.0f).cat("Spring Arm")
    FORGE_PROP(socketOffset).cat("Spring Arm")
    FORGE_PROP(collisionTest).cat("Spring Arm")
        .tooltip("Pull the camera in when geometry would come between it and the target.")
    FORGE_PROP(probeRadius).range(0.05f, 2.0f).cat("Spring Arm")
    FORGE_PROP(lagSpeed).range(1.0f, 40.0f).cat("Spring Arm")
    FORGE_PROP(inheritYawOnly).cat("Spring Arm")
FORGE_CLASS_END()
FORGE_REGISTER(SpringArmComponent)

} // namespace forge
