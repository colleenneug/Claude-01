// ============================================================
//  Actor — anything that can be placed in a level.
//
//  An actor is a name, a transform (through its root component) and a
//  tree of components. Its lifecycle is the one Unreal taught everyone
//  to expect:
//
//    onConstruct   whenever a property changes, in the editor as well
//                  as at spawn — the construction script, and why
//                  dragging a slider rebuilds the actor live
//    beginPlay     once, when the level starts
//    tick          every frame while playing
//    endPlay       once, on destroy or level end
//
//  Subclasses build their component tree in the constructor. A level
//  file then only stores what differs from that, so changing a class
//  updates every instance already placed.
// ============================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "forge/core/Delegate.hpp"
#include "forge/scene/Component.hpp"

namespace forge {

class World;
struct HitResult;

class Actor : public Object {
    FORGE_OBJECT(Actor, Object)
public:
    Actor();
    ~Actor() override;

    // ---- identity ----
    uint32_t id() const { return id_; }
    const std::string& actorName() const { return name; }
    void setActorName(std::string n) { name = std::move(n); }
    World* world() const { return world_; }
    bool isPendingKill() const { return pendingKill_; }
    bool hasBegunPlay() const { return begun_; }

    // ---- components ----
    // Components are owned by the actor. The returned pointer stays valid
    // until the component or the actor is destroyed.
    template <typename T>
    T* addComponent(const std::string& name, SceneComponent* attachTo = nullptr);
    ActorComponent* addComponentByClass(const ClassInfo* cls, const std::string& name,
                                        SceneComponent* attachTo = nullptr);
    void removeComponent(ActorComponent* c);

    SceneComponent* root() const { return root_; }
    void setRoot(SceneComponent* c) { root_ = c; }
    // Gives the actor a plain root if its class did not build one, so
    // every actor has a transform the editor can move.
    SceneComponent* ensureRoot();
    const std::vector<std::unique_ptr<ActorComponent>>& components() const { return components_; }

    ActorComponent* findComponent(const std::string& name) const;
    template <typename T> T* findComponentOfClass() const;
    template <typename T> std::vector<T*> findComponentsOfClass() const;

    // ---- transform, forwarded to the root ----
    Vec3 location() const;
    Rotator rotation() const;
    Vec3 scale3D() const;
    void setLocation(const Vec3& v);
    void setRotation(const Rotator& r);
    void setScale(const Vec3& s);
    void addOffset(const Vec3& v);
    void addRotation(const Rotator& r);
    Transform transform() const;
    void setTransform(const Transform& t);
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
    float distanceTo(const Actor* other) const;
    Box worldBounds() const;

    // ---- lifecycle ----
    virtual void onConstruct() {}
    virtual void beginPlay() {}
    virtual void tick(float dt) { (void)dt; }
    virtual void endPlay() {}
    // Re-runs construction on this actor and every component.
    void rerunConstruction();

    void destroy();
    void onPropertyChanged(const std::string& n) override;

    // ---- damage, which the engine owns so hazards, weapons and scripts
    //      all report it the same way ----
    virtual float takeDamage(float amount, Actor* instigator);

    // ---- tags ----
    bool hasTag(const std::string& tag) const;
    void addTag(const std::string& tag);

    // ---- events ----
    Delegate<Actor*> onBeginOverlap;
    Delegate<Actor*> onEndOverlap;
    Delegate<const HitResult&> onHit;
    Delegate<float, Actor*> onDamaged;
    Delegate<Actor*> onDestroyed;

    // Reflected properties.
    std::string name = "Actor";
    std::string tags;
    bool tickEnabled = true;
    bool hiddenInGame = false;

private:
    friend class World;
    friend class LevelSerializer;

    uint32_t id_ = 0;
    World* world_ = nullptr;
    SceneComponent* root_ = nullptr;
    std::vector<std::unique_ptr<ActorComponent>> components_;
    bool pendingKill_ = false;
    bool begun_ = false;
    // Components a designer added to this instance in the editor, kept
    // apart from the ones the class builds so the serialiser can tell
    // "added here" from "came with the class".
    std::vector<ActorComponent*> instanceComponents_;
};

template <typename T>
T* Actor::addComponent(const std::string& n, SceneComponent* attachTo) {
    return static_cast<T*>(addComponentByClass(T::staticClass(), n, attachTo));
}

template <typename T>
T* Actor::findComponentOfClass() const {
    for (const auto& c : components_)
        if (auto* t = dynamic_cast<T*>(c.get())) return t;
    return nullptr;
}

template <typename T>
std::vector<T*> Actor::findComponentsOfClass() const {
    std::vector<T*> out;
    for (const auto& c : components_)
        if (auto* t = dynamic_cast<T*>(c.get())) out.push_back(t);
    return out;
}

} // namespace forge
