#include "forge/scene/World.hpp"

#include <algorithm>

#include "forge/core/Log.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/physics/Physics.hpp"

namespace forge {

Vec3 WorldSettings::sunDirection() const {
    // Yaw and pitch describe where the sun sits; the light travels the
    // other way, which is the vector shading actually wants.
    const float yaw = radians(sunYaw), pitch = radians(sunPitch);
    Vec3 toSun{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    return -toSun.normalized();
}

FORGE_CLASS_BEGIN(WorldSettings)
    FORGE_DISPLAY("World Settings")
    FORGE_CATEGORY("World")
    FORGE_PROP(gameModeClass).classRef("GameMode").cat("Game").label("Game Mode")
    FORGE_PROP(backgroundColor).cat("Sky")
    FORGE_PROP(skyColor).cat("Sky")
    FORGE_PROP(groundColor).cat("Sky")
    FORGE_PROP(ambientIntensity).range(0.0f, 4.0f).cat("Sky")
    FORGE_PROP(sunColor).cat("Sun")
    FORGE_PROP(sunIntensity).range(0.0f, 12.0f).cat("Sun")
    FORGE_PROP(sunYaw).range(-180.0f, 180.0f).cat("Sun")
    FORGE_PROP(sunPitch).range(2.0f, 89.0f).cat("Sun")
    FORGE_PROP(sunShadows).cat("Sun")
    FORGE_PROP(timeOfDaySpeed).range(-30.0f, 30.0f).cat("Sun")
        .tooltip("Degrees of sun yaw per second while playing.")
    FORGE_PROP(fogEnabled).cat("Fog")
    FORGE_PROP(fogColor).cat("Fog")
    FORGE_PROP(fogDensity).range(0.0f, 0.1f, 0.001f).cat("Fog")
    FORGE_PROP(gravity).cat("Physics")
    FORGE_PROP(exposure).range(0.1f, 4.0f).cat("Post Process")
    FORGE_PROP(bloom).range(0.0f, 3.0f).cat("Post Process")
    FORGE_PROP(vignette).range(0.0f, 1.0f).cat("Post Process")
FORGE_CLASS_END()
FORGE_REGISTER(WorldSettings)

// -----------------------------------------------------------------
//  World
// -----------------------------------------------------------------

World::World(bool isEditorWorld) : editorWorld_(isEditorWorld) {
    physics_ = std::make_unique<PhysicsScene>(this);
    physics_->gravity = settings_.gravity;
}

World::~World() {
    // Tear down without running the deferred path: the world is going
    // away, so EndPlay must still fire but nothing may re-enter.
    if (playing_) endPlay();
    for (auto& a : actors_) {
        for (const auto& c : a->components_)
            if (c->registered_) { c->onUnregister(); c->registered_ = false; }
    }
    actors_.clear();
    byId_.clear();
}

Actor* World::spawn(const ClassInfo* cls, const std::string& name, const Vec3& location,
                    const Rotator& rotation) {
    if (!cls) { FORGE_ERROR("spawn: null class"); return nullptr; }
    if (!cls->isA("Actor")) { FORGE_ERROR("spawn: %s is not an Actor", cls->name().c_str()); return nullptr; }
    Object* raw = cls->construct();
    if (!raw) { FORGE_ERROR("spawn: %s is abstract", cls->name().c_str()); return nullptr; }

    std::unique_ptr<Actor> actor(static_cast<Actor*>(raw));
    actor->name = name.empty() ? makeUniqueName(cls->displayName()) : name;
    if (actor->root_) {
        actor->root_->setLocation(location);
        actor->root_->setRotation(rotation);
    }
    return adopt(std::move(actor));
}

Actor* World::adopt(std::unique_ptr<Actor> owned) {
    if (!owned) return nullptr;
    Actor* a = owned.get();
    a->world_ = this;
    a->id_ = nextActorId_++;
    a->ensureRoot();
    actors_.push_back(std::move(owned));
    byId_[a->id_] = a;
    registerActor(a);

    a->rerunConstruction();
    if (playing_) {
        for (const auto& c : a->components_)
            if (!c->begun_) { c->beginPlay(); c->begun_ = true; }
        a->beginPlay();
        a->begun_ = true;
    }
    onActorSpawned.broadcast(a);
    return a;
}

void World::registerActor(Actor* a) {
    for (const auto& c : a->components_) {
        if (c->registered_) continue;
        c->onRegister();
        c->registered_ = true;
    }
}

void World::destroyActor(Actor* a) {
    if (!a || a->pendingKill_) return;
    a->pendingKill_ = true;
    pendingDestroy_.push_back(a);
    // Outside of play, tear down at once: the outliner should update on
    // the same frame as the delete rather than a frame later.
    if (!playing_ && !ticking_) flushPendingDestroys();
}

void World::flushPendingDestroys() {
    while (!pendingDestroy_.empty()) {
        // Destroying an actor may destroy others (a script reacting to
        // EndPlay), so take one at a time rather than iterating a snapshot.
        Actor* a = pendingDestroy_.front();
        pendingDestroy_.erase(pendingDestroy_.begin());

        auto it = std::find_if(actors_.begin(), actors_.end(),
                               [&](const std::unique_ptr<Actor>& p) { return p.get() == a; });
        if (it == actors_.end()) continue;

        if (a->begun_) {
            for (const auto& c : a->components_)
                if (c->begun_) { c->endPlay(); c->begun_ = false; }
            a->endPlay();
            a->begun_ = false;
        }
        a->onDestroyed.broadcast(a);
        onActorDestroyed.broadcast(a);

        for (const auto& c : a->components_) {
            if (c->registered_) { c->onUnregister(); c->registered_ = false; }
        }
        if (gameMode_ == (GameMode*)a) gameMode_ = nullptr;
        if (playerController_ == (PlayerController*)a) playerController_ = nullptr;

        byId_.erase(a->id_);
        actors_.erase(it);
    }
}

Actor* World::findActor(uint32_t id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second;
}

Actor* World::findActorByName(const std::string& name) const {
    for (const auto& a : actors_)
        if (!a->isPendingKill() && a->name == name) return a.get();
    return nullptr;
}

std::vector<Actor*> World::actorsOfClass(const std::string& className) const {
    std::vector<Actor*> out;
    for (const auto& a : actors_)
        if (!a->isPendingKill() && a->getClass()->isA(className)) out.push_back(a.get());
    return out;
}

std::vector<Actor*> World::actorsWithTag(const std::string& tag) const {
    std::vector<Actor*> out;
    for (const auto& a : actors_)
        if (!a->isPendingKill() && a->hasTag(tag)) out.push_back(a.get());
    return out;
}

std::string World::makeUniqueName(const std::string& base) const {
    bool clash = false;
    for (const auto& a : actors_) if (a->name == base) { clash = true; break; }
    if (!clash) return base;
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = base + std::to_string(i);
        bool taken = false;
        for (const auto& a : actors_) if (a->name == candidate) { taken = true; break; }
        if (!taken) return candidate;
    }
    return base;
}

// ---- timers ----

TimerHandle World::setTimer(float delay, bool loop, std::function<void()> fn) {
    Timer t;
    t.id = nextTimerId_++;
    t.interval = std::max(1e-4f, delay);
    t.remaining = t.interval;
    t.loop = loop;
    t.dead = false;
    t.fn = std::move(fn);
    timers_.push_back(std::move(t));
    return TimerHandle{timers_.back().id};
}

void World::clearTimer(TimerHandle h) {
    for (Timer& t : timers_) if (t.id == h.id) t.dead = true;
}

// ---- play ----

void World::beginPlay() {
    if (playing_) return;
    playing_ = true;
    time_ = 0.0f;
    frame_ = 0;
    physics_->gravity = settings_.gravity;

    // The GameMode comes first: it spawns the controller and the pawn,
    // and gameplay actors expect both to exist by the time BeginPlay runs.
    const ClassInfo* gmClass = ClassRegistry::get().find(settings_.gameModeClass);
    if (!gmClass || !gmClass->isA("GameMode")) {
        if (!settings_.gameModeClass.empty() && settings_.gameModeClass != "GameMode")
            FORGE_WARN("unknown game mode '%s'; falling back to GameMode", settings_.gameModeClass.c_str());
        gmClass = GameMode::staticClass();
    }
    gameMode_ = static_cast<GameMode*>(spawn(gmClass, "GameMode"));
    if (gameMode_) gameMode_->startPlay();

    // Snapshot the list: an actor's BeginPlay is allowed to spawn more,
    // and those are begun as they are adopted rather than here.
    std::vector<Actor*> initial;
    initial.reserve(actors_.size());
    for (const auto& a : actors_) initial.push_back(a.get());
    for (Actor* a : initial) {
        if (a->begun_ || a->pendingKill_) continue;
        for (const auto& c : a->components_)
            if (!c->begun_) { c->beginPlay(); c->begun_ = true; }
        a->beginPlay();
        a->begun_ = true;
    }
    onPlayBegan.broadcast();
    FORGE_LOG("Play started: %zu actors", actors_.size());
}

void World::endPlay() {
    if (!playing_) return;
    playing_ = false;
    for (const auto& a : actors_) {
        if (!a->begun_) continue;
        for (const auto& c : a->components_)
            if (c->begun_) { c->endPlay(); c->begun_ = false; }
        a->endPlay();
        a->begun_ = false;
    }
    timers_.clear();
    onPlayEnded.broadcast();
}

CameraComponent* World::viewCamera() const {
    return playerController_ ? playerController_->viewCamera() : nullptr;
}

void World::tick(float dt) {
    if (paused_) dt = 0.0f;
    dt *= timeDilation;
    delta_ = dt;
    time_ += dt;
    ++frame_;

    if (!playing_) {
        // Editor idle: components that animate for preview still tick so
        // the viewport is not a still frame, but no gameplay runs.
        if (editorWorld_) {
            for (const auto& a : actors_)
                for (const auto& c : a->components_) c->editorTick(dt);
        }
        return;
    }
    if (dt <= 0.0f) return;

    ticking_ = true;

    if (settings_.timeOfDaySpeed != 0.0f)
        settings_.sunYaw = Rotator::normalizeAngle(settings_.sunYaw + settings_.timeOfDaySpeed * dt);

    // Timers before actors, so a timer that spawns something gives it a
    // tick this frame rather than next.
    for (size_t i = 0; i < timers_.size(); ++i) {
        Timer& t = timers_[i];
        if (t.dead) continue;
        t.remaining -= dt;
        if (t.remaining > 0.0f) continue;
        if (t.loop) t.remaining += t.interval;
        else t.dead = true;
        auto fn = t.fn;
        if (fn) fn();
    }
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const Timer& t) { return t.dead; }),
                  timers_.end());

    // The controller runs before pawns so this frame's input moves the
    // pawn this frame, not next.
    if (playerController_ && !playerController_->isPendingKill())
        playerController_->tickController(dt);

    // Snapshot: an actor may spawn or destroy during its own tick.
    std::vector<Actor*> ticking;
    ticking.reserve(actors_.size());
    for (const auto& a : actors_) ticking.push_back(a.get());
    for (Actor* a : ticking) {
        if (a->pendingKill_) continue;
        for (const auto& c : a->components_)
            if (c->tickEnabled && !a->pendingKill_) c->tick(dt);
        if (a->tickEnabled && !a->pendingKill_) a->tick(dt);
    }

    physics_->step(dt);

    ticking_ = false;
    flushPendingDestroys();
}

} // namespace forge
