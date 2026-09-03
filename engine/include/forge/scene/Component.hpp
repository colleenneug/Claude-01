// ============================================================
//  Components.
//
//  ActorComponent is behaviour with no place in the world: movement
//  rules, health, a timer. SceneComponent adds a transform and can be
//  attached to another, which is how an actor gets a hierarchy — a
//  turret ring under a hull, a muzzle under a barrel, a camera on a
//  spring arm behind a character.
//
//  World transforms are computed lazily. A component marks itself and
//  its subtree dirty when its local transform changes, and the world
//  matrix is rebuilt only when something asks for it. Moving a root
//  with fifty children therefore costs one flag write, not fifty
//  matrix multiplies.
// ============================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "forge/core/Object.hpp"
#include "forge/math/Math.hpp"

namespace forge {

class Actor;
class World;
class Collider;

class ActorComponent : public Object {
    FORGE_OBJECT(ActorComponent, Object)
public:
    ActorComponent() = default;
    ~ActorComponent() override = default;

    const std::string& componentName() const { return name; }
    void setComponentName(std::string n) { name = std::move(n); }

    Actor* owner() const { return owner_; }
    World* world() const;

    // Lifecycle. onRegister runs in the editor too, so what the viewport
    // shows is what play will show.
    virtual void onRegister() {}
    virtual void onUnregister() {}
    virtual void beginPlay() {}
    virtual void tick(float dt) { (void)dt; }
    virtual void endPlay() {}
    // Runs whenever a property changes, in the editor as well as at
    // spawn: the construction script.
    virtual void onConstruct() {}
    // Editor-only preview motion, so the viewport is not a still frame.
    virtual void editorTick(float dt) { (void)dt; }

    void onPropertyChanged(const std::string& n) override;

    std::string name;
    bool tickEnabled = false;

private:
    // The world drives registration and BeginPlay across every actor, so
    // it needs the same access the owning actor has.
    friend class Actor;
    friend class World;
    Actor* owner_ = nullptr;
    bool registered_ = false;
    bool begun_ = false;
};

class SceneComponent : public ActorComponent {
    FORGE_OBJECT(SceneComponent, ActorComponent)
public:
    // ---- local transform (reflected; what the details panel edits) ----
    Vec3 location{0, 0, 0};
    Rotator rotation{0, 0, 0};
    Vec3 scale{1, 1, 1};
    bool visible = true;

    void setLocation(const Vec3& v) { location = v; markTransformDirty(); }
    void setRotation(const Rotator& r) { rotation = r; markTransformDirty(); }
    void setScale(const Vec3& s) { scale = s; markTransformDirty(); }
    void setScale(float s) { setScale(Vec3(s)); }
    void addOffset(const Vec3& v) { location += v; markTransformDirty(); }
    void addRotation(const Rotator& r) { rotation = (rotation + r).normalized(); markTransformDirty(); }

    Transform localTransform() const { return {location, rotation.toQuat(), scale}; }
    void setLocalTransform(const Transform& t);

    // ---- world transform ----
    const Transform& worldTransform() const;
    const Mat4& worldMatrix() const;
    Vec3 worldLocation() const { return worldTransform().position; }
    Quat worldRotation() const { return worldTransform().rotation; }
    Rotator worldRotator() const { return Rotator::fromQuat(worldTransform().rotation); }
    Vec3 worldScale() const { return worldTransform().scale; }

    void setWorldLocation(const Vec3& v);
    void setWorldRotation(const Quat& q);
    void setWorldTransform(const Transform& t);

    Vec3 forward() const { return worldRotation().forward(); }
    Vec3 right() const { return worldRotation().right(); }
    Vec3 up() const { return worldRotation().up(); }

    // ---- attachment ----
    void attachTo(SceneComponent* parent);
    void detach();
    SceneComponent* parent() const { return parent_; }
    const std::vector<SceneComponent*>& children() const { return children_; }
    bool isDescendantOf(const SceneComponent* other) const;

    // Visible taking parents into account: hiding a root hides the tree.
    bool visibleInHierarchy() const;

    void markTransformDirty();
    void onPropertyChanged(const std::string& n) override;

    // Local-space bounds, which subclasses that draw or collide override.
    virtual Box localBounds() const { return Box{Vec3::Zero, Vec3::Zero}; }
    Box worldBounds() const;

private:
    SceneComponent* parent_ = nullptr;
    std::vector<SceneComponent*> children_;
    mutable Transform worldCache_;
    mutable Mat4 matrixCache_;
    mutable bool worldDirty_ = true;
};

} // namespace forge
