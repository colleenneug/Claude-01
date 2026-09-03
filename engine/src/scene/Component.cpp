#include "forge/scene/Component.hpp"

#include <algorithm>

#include "forge/core/Log.hpp"
#include "forge/scene/Actor.hpp"
#include "forge/scene/World.hpp"

namespace forge {

// -----------------------------------------------------------------
//  ActorComponent
// -----------------------------------------------------------------

World* ActorComponent::world() const { return owner_ ? owner_->world() : nullptr; }

void ActorComponent::onPropertyChanged(const std::string& n) {
    (void)n;
    onConstruct();
}

FORGE_CLASS_BEGIN(ActorComponent)
    FORGE_DISPLAY("Component")
    FORGE_CATEGORY("Component")
    FORGE_DESCRIBE("Behaviour attached to an actor.")
    FORGE_PROP(name).cat("Component")
    FORGE_PROP(tickEnabled).cat("Component").label("Tick Enabled")
FORGE_CLASS_END()
FORGE_REGISTER(ActorComponent)

// -----------------------------------------------------------------
//  SceneComponent
// -----------------------------------------------------------------

void SceneComponent::setLocalTransform(const Transform& t) {
    location = t.position;
    rotation = Rotator::fromQuat(t.rotation);
    scale = t.scale;
    markTransformDirty();
}

void SceneComponent::markTransformDirty() {
    if (worldDirty_) {
        // Already dirty means the subtree was marked with it; stopping
        // here is what keeps a deep hierarchy from being re-walked once
        // per component every frame.
        return;
    }
    worldDirty_ = true;
    for (SceneComponent* c : children_) c->markTransformDirty();
}

const Transform& SceneComponent::worldTransform() const {
    if (worldDirty_) {
        Transform local{location, rotation.toQuat(), scale};
        worldCache_ = parent_ ? parent_->worldTransform() * local : local;
        matrixCache_ = worldCache_.toMatrix();
        worldDirty_ = false;
    }
    return worldCache_;
}

const Mat4& SceneComponent::worldMatrix() const {
    worldTransform();
    return matrixCache_;
}

void SceneComponent::setWorldLocation(const Vec3& v) {
    if (!parent_) { setLocation(v); return; }
    setLocation(parent_->worldTransform().inverseTransformPoint(v));
}

void SceneComponent::setWorldRotation(const Quat& q) {
    if (!parent_) { setRotation(Rotator::fromQuat(q)); return; }
    setRotation(Rotator::fromQuat(parent_->worldTransform().rotation.inverse() * q));
}

void SceneComponent::setWorldTransform(const Transform& t) {
    setLocalTransform(parent_ ? parent_->worldTransform().inverse() * t : t);
}

void SceneComponent::attachTo(SceneComponent* newParent) {
    if (newParent == parent_) return;
    // Attaching to your own descendant would make a cycle, and the world
    // transform walk would never terminate.
    if (newParent && (newParent == this || newParent->isDescendantOf(this))) {
        FORGE_WARN("attachTo would create a cycle (%s -> %s); ignored",
                   name.c_str(), newParent->name.c_str());
        return;
    }
    detach();
    parent_ = newParent;
    if (parent_) parent_->children_.push_back(this);
    markTransformDirty();
}

void SceneComponent::detach() {
    if (!parent_) return;
    auto& siblings = parent_->children_;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    parent_ = nullptr;
    markTransformDirty();
}

bool SceneComponent::isDescendantOf(const SceneComponent* other) const {
    for (const SceneComponent* c = parent_; c; c = c->parent_)
        if (c == other) return true;
    return false;
}

bool SceneComponent::visibleInHierarchy() const {
    for (const SceneComponent* c = this; c; c = c->parent_)
        if (!c->visible) return false;
    return true;
}

Box SceneComponent::worldBounds() const {
    Box local = localBounds();
    if (!local.valid()) return {};
    return local.transformed(worldMatrix());
}

void SceneComponent::onPropertyChanged(const std::string& n) {
    markTransformDirty();
    Super::onPropertyChanged(n);
}

FORGE_CLASS_BEGIN(SceneComponent)
    FORGE_DISPLAY("Scene")
    FORGE_CATEGORY("Component")
    FORGE_DESCRIBE("A component with a place in the world.")
    FORGE_PROP(location).cat("Transform").tooltip("Position relative to the parent component.")
    FORGE_PROP(rotation).cat("Transform").tooltip("Degrees, applied yaw then pitch then roll.")
    FORGE_PROP(scale).cat("Transform")
    FORGE_PROP(visible).cat("Rendering")
FORGE_CLASS_END()
FORGE_REGISTER(SceneComponent)

} // namespace forge
