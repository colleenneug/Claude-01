// ============================================================
//  World — the container a level lives in.
//
//  It owns the actors, the physics scene they collide in, and the
//  clock. The editor keeps two: the one you are editing, and a
//  throwaway built from a snapshot of it when you press Play. That is
//  what makes Play-In-Editor non-destructive — the play world is
//  discarded on Stop, and the edit world was never touched.
// ============================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "forge/core/Delegate.hpp"
#include "forge/scene/Actor.hpp"

namespace forge {

class PhysicsScene;
class GameMode;
class PlayerController;
class CameraComponent;
class AssetLibrary;

// Level-wide settings: sky, fog, sun, gravity, and which GameMode runs.
// Reflected like anything else, so the details panel draws it with no
// special-case code.
class WorldSettings : public Object {
    FORGE_OBJECT(WorldSettings, Object)
public:
    std::string gameModeClass = "GameMode";
    Color backgroundColor = Color::fromHex("#8fb3d9");
    Color skyColor = Color::fromHex("#b9d4f2");
    Color groundColor = Color::fromHex("#4a4238");
    float ambientIntensity = 0.75f;
    Color sunColor = Color::fromHex("#fff2dd");
    float sunIntensity = 2.6f;
    float sunYaw = 40.0f;
    float sunPitch = 48.0f;
    bool sunShadows = true;
    float timeOfDaySpeed = 0.0f;
    bool fogEnabled = true;
    Color fogColor = Color::fromHex("#9dbcd8");
    float fogDensity = 0.006f;
    Vec3 gravity{0.0f, -22.0f, 0.0f};
    float exposure = 1.0f;
    float bloom = 0.4f;
    float vignette = 0.3f;

    Vec3 sunDirection() const;
};

struct TimerHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

class World {
public:
    explicit World(bool isEditorWorld = false);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ---- actors ----
    Actor* spawn(const ClassInfo* cls, const std::string& name = {},
                 const Vec3& location = Vec3::Zero, const Rotator& rotation = Rotator{});
    template <typename T>
    T* spawn(const std::string& name = {}, const Vec3& location = Vec3::Zero,
             const Rotator& rotation = Rotator{}) {
        return static_cast<T*>(spawn(T::staticClass(), name, location, rotation));
    }
    // Adopt an actor built outside the world (the level loader does this
    // so it can set properties before BeginPlay runs).
    Actor* adopt(std::unique_ptr<Actor> actor);

    void destroyActor(Actor* a);
    // Actors are destroyed at a frame boundary, so a script may safely
    // destroy something it is iterating over.
    void flushPendingDestroys();

    const std::vector<std::unique_ptr<Actor>>& actors() const { return actors_; }
    Actor* findActor(uint32_t id) const;
    Actor* findActorByName(const std::string& name) const;
    std::vector<Actor*> actorsOfClass(const std::string& className) const;
    std::vector<Actor*> actorsWithTag(const std::string& tag) const;
    template <typename T> std::vector<T*> actorsOfType() const {
        std::vector<T*> out;
        for (const auto& a : actors_)
            if (auto* t = dynamic_cast<T*>(a.get())) if (!t->isPendingKill()) out.push_back(t);
        return out;
    }
    template <typename T> T* firstActorOfType() const {
        for (const auto& a : actors_)
            if (auto* t = dynamic_cast<T*>(a.get())) if (!t->isPendingKill()) return t;
        return nullptr;
    }
    // A unique display name, so two dropped cubes are "Cube" and "Cube1"
    // rather than two things the outliner cannot tell apart.
    std::string makeUniqueName(const std::string& base) const;

    // ---- timers ----
    TimerHandle setTimer(float delay, bool loop, std::function<void()> fn);
    void clearTimer(TimerHandle h);

    // ---- play ----
    void beginPlay();
    void endPlay();
    bool isPlaying() const { return playing_; }
    bool isEditorWorld() const { return editorWorld_; }
    void setPaused(bool p) { paused_ = p; }
    bool isPaused() const { return paused_; }

    void tick(float dt);

    // ---- accessors ----
    PhysicsScene& physics() { return *physics_; }
    const PhysicsScene& physics() const { return *physics_; }
    WorldSettings& settings() { return settings_; }
    const WorldSettings& settings() const { return settings_; }
    AssetLibrary* assets() const { return assets_; }
    void setAssets(AssetLibrary* a) { assets_ = a; }

    GameMode* gameMode() const { return gameMode_; }
    PlayerController* playerController() const { return playerController_; }
    void setPlayerController(PlayerController* pc) { playerController_ = pc; }
    // The camera the viewport renders from during play.
    CameraComponent* viewCamera() const;

    float timeSeconds() const { return time_; }
    float deltaSeconds() const { return delta_; }
    uint64_t frameNumber() const { return frame_; }
    float timeDilation = 1.0f;

    const std::string& levelName() const { return levelName_; }
    void setLevelName(std::string n) { levelName_ = std::move(n); }

    Delegate<Actor*> onActorSpawned;
    Delegate<Actor*> onActorDestroyed;
    Delegate<> onPlayBegan;
    Delegate<> onPlayEnded;

private:
    void registerActor(Actor* a);

    std::vector<std::unique_ptr<Actor>> actors_;
    std::unordered_map<uint32_t, Actor*> byId_;
    std::vector<Actor*> pendingDestroy_;
    std::unique_ptr<PhysicsScene> physics_;
    WorldSettings settings_;
    AssetLibrary* assets_ = nullptr;

    GameMode* gameMode_ = nullptr;
    PlayerController* playerController_ = nullptr;

    struct Timer {
        uint32_t id;
        float remaining;
        float interval;
        bool loop;
        bool dead;
        std::function<void()> fn;
    };
    std::vector<Timer> timers_;
    uint32_t nextTimerId_ = 1;
    uint32_t nextActorId_ = 1;

    std::string levelName_ = "Untitled";
    float time_ = 0.0f;
    float delta_ = 0.0f;
    uint64_t frame_ = 0;
    bool playing_ = false;
    bool paused_ = false;
    bool editorWorld_ = false;
    bool ticking_ = false;
};

} // namespace forge
