#include "forge/scene/Actor.hpp"

#include <algorithm>

#include "forge/core/Log.hpp"
#include "forge/physics/Physics.hpp"
#include "forge/scene/World.hpp"

namespace forge {

Actor::Actor() = default;

Actor::~Actor() = default;

ActorComponent* Actor::addComponentByClass(const ClassInfo* cls, const std::string& n,
                                           SceneComponent* attachTo) {
    if (!cls) { FORGE_ERROR("addComponent: null class"); return nullptr; }
    if (!cls->isA("ActorComponent")) {
        FORGE_ERROR("addComponent: %s is not a component", cls->name().c_str());
        return nullptr;
    }
    Object* raw = cls->construct();
    if (!raw) { FORGE_ERROR("addComponent: %s is abstract", cls->name().c_str()); return nullptr; }

    auto* comp = static_cast<ActorComponent*>(raw);
    comp->owner_ = this;
    comp->name = n.empty() ? cls->displayName() : n;
    components_.emplace_back(comp);

    if (auto* sc = dynamic_cast<SceneComponent*>(comp)) {
        // The first scene component to arrive becomes the root; anything
        // later attaches under it unless told otherwise.
        if (!root_) root_ = sc;
        else sc->attachTo(attachTo ? attachTo : root_);
    }

    if (world_) {
        comp->onRegister();
        comp->registered_ = true;
        comp->onConstruct();
        if (begun_) { comp->beginPlay(); comp->begun_ = true; }
    }
    return comp;
}

void Actor::removeComponent(ActorComponent* c) {
    auto it = std::find_if(components_.begin(), components_.end(),
                           [&](const std::unique_ptr<ActorComponent>& p) { return p.get() == c; });
    if (it == components_.end()) return;

    if (c->begun_) { c->endPlay(); c->begun_ = false; }
    if (c->registered_) { c->onUnregister(); c->registered_ = false; }
    if (auto* sc = dynamic_cast<SceneComponent*>(c)) {
        // Re-home the children rather than orphaning them, so removing a
        // middle component does not silently drop a subtree.
        for (SceneComponent* child : std::vector<SceneComponent*>(sc->children()))
            child->attachTo(sc->parent());
        sc->detach();
        if (root_ == sc) root_ = nullptr;
    }
    instanceComponents_.erase(std::remove(instanceComponents_.begin(), instanceComponents_.end(), c),
                              instanceComponents_.end());
    components_.erase(it);

    if (!root_) {
        for (const auto& p : components_)
            if (auto* sc = dynamic_cast<SceneComponent*>(p.get())) { root_ = sc; break; }
    }
}

SceneComponent* Actor::ensureRoot() {
    if (root_) return root_;
    // Cannot be done in the constructor: it runs before the subclass
    // body, so it would claim the root slot from a class that was about
    // to build its own.
    return addComponent<SceneComponent>("Root");
}

ActorComponent* Actor::findComponent(const std::string& n) const {
    for (const auto& c : components_)
        if (c->name == n) return c.get();
    for (const auto& c : components_)
        if (c->className() == n) return c.get();
    return nullptr;
}

// ---- transform ----

Vec3 Actor::location() const { return root_ ? root_->location : Vec3::Zero; }
Rotator Actor::rotation() const { return root_ ? root_->rotation : Rotator{}; }
Vec3 Actor::scale3D() const { return root_ ? root_->scale : Vec3::One; }
void Actor::setLocation(const Vec3& v) { if (root_) root_->setLocation(v); }
void Actor::setRotation(const Rotator& r) { if (root_) root_->setRotation(r); }
void Actor::setScale(const Vec3& s) { if (root_) root_->setScale(s); }
void Actor::addOffset(const Vec3& v) { if (root_) root_->addOffset(v); }
void Actor::addRotation(const Rotator& r) { if (root_) root_->addRotation(r); }
Transform Actor::transform() const { return root_ ? root_->worldTransform() : Transform{}; }
void Actor::setTransform(const Transform& t) { if (root_) root_->setWorldTransform(t); }
Vec3 Actor::forward() const { return root_ ? root_->forward() : Vec3::Forward; }
Vec3 Actor::right() const { return root_ ? root_->right() : Vec3::Right; }
Vec3 Actor::up() const { return root_ ? root_->up() : Vec3::Up; }

float Actor::distanceTo(const Actor* other) const {
    if (!other) return 0.0f;
    return distance(location(), other->location());
}

Box Actor::worldBounds() const {
    Box out;
    for (const auto& c : components_)
        if (auto* sc = dynamic_cast<SceneComponent*>(c.get())) out.expand(sc->worldBounds());
    if (!out.valid()) {
        // A component-less actor still needs a pickable extent in the
        // editor, so fall back to a small box at its origin.
        Vec3 p = location();
        out = Box::fromCenterExtents(p, Vec3(0.25f));
    }
    return out;
}

void Actor::rerunConstruction() {
    for (const auto& c : components_) c->onConstruct();
    onConstruct();
}

void Actor::destroy() {
    if (pendingKill_) return;
    if (world_) world_->destroyActor(this);
    else pendingKill_ = true;
}

void Actor::onPropertyChanged(const std::string& n) {
    (void)n;
    onConstruct();
}

float Actor::takeDamage(float amount, Actor* instigator) {
    if (pendingKill_ || amount <= 0.0f) return 0.0f;
    onDamaged.broadcast(amount, instigator);
    return amount;
}

bool Actor::hasTag(const std::string& tag) const {
    if (tag.empty() || tags.empty()) return false;
    size_t pos = 0;
    while (pos < tags.size()) {
        size_t end = tags.find_first_of(", ", pos);
        if (end == std::string::npos) end = tags.size();
        if (end > pos && tags.compare(pos, end - pos, tag) == 0) return true;
        pos = end + 1;
    }
    return false;
}

void Actor::addTag(const std::string& tag) {
    if (hasTag(tag)) return;
    if (!tags.empty()) tags += ",";
    tags += tag;
}

FORGE_CLASS_BEGIN(Actor)
    FORGE_DISPLAY("Actor")
    FORGE_CATEGORY("Basic")
    FORGE_ICON("A")
    FORGE_DESCRIBE("An empty actor. Add components to give it substance.")
    FORGE_PROP(name).cat("Actor")
    FORGE_PROP(tags).cat("Actor").tooltip("Comma-separated tags that scripts can test.")
    FORGE_PROP(tickEnabled).cat("Actor").label("Tick Enabled")
    FORGE_PROP(hiddenInGame).cat("Rendering").label("Hidden In Game")
FORGE_CLASS_END()
FORGE_REGISTER(Actor)

} // namespace forge
