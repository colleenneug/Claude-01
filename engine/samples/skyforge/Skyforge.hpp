// ============================================================
//  Skyforge -- a small, complete game built with Forge.
//
//  Its purpose is to be evidence: an engine is only an engine if you
//  can make something with it. The game is a course of floating
//  platforms with orbs to collect; take them all and the goal opens.
//
//  The gameplay classes live here rather than in main.cpp so the game
//  can be played by an application and driven by a test.
// ============================================================
#pragma once

#include "forge/components/MovementComponents.hpp"
#include "forge/components/RenderComponents.hpp"
#include "forge/gameplay/Actors.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/scene/Actor.hpp"

namespace forge { class AssetLibrary; class World; class ScriptGraph; }

namespace skyforge {

using namespace forge;

// A collectible. A gameplay class rather than a script, because it
// carries state the rules care about and wants its own properties.
class Orb : public Actor {
    FORGE_OBJECT(Orb, Actor)
public:
    Orb();
    void beginPlay() override;
    bool collected() const { return collected_; }

    float value = 1.0f;

private:
    bool collected_ = false;
};

// A platform that travels between where it is placed and that plus
// Travel, easing at each end.
class MovingPlatform : public Actor {
    FORGE_OBJECT(MovingPlatform, Actor)
public:
    MovingPlatform();
    void beginPlay() override;
    void tick(float dt) override;

    Vec3 travel{6, 0, 0};
    float speed = 0.25f;

private:
    Vec3 origin_{0, 0, 0};
    float phase_ = 0.0f;
};

// The rules: count the orbs, open the goal, respawn anyone who falls.
class SkyforgeGameMode : public GameMode {
    FORGE_OBJECT(SkyforgeGameMode, GameMode)
public:
    SkyforgeGameMode();
    void beginPlay() override;
    void tick(float dt) override;

    bool finished() const { return finished_; }
    bool goalOpen() const { return goalOpen_; }
    int orbsRemaining() const { return lastRemaining_; }
    int respawnCount() const { return respawns_; }

    float killHeight = -12.0f;

private:
    int totalOrbs_ = 0;
    int lastRemaining_ = -1;
    int respawns_ = 0;
    bool goalOpen_ = false;
    bool finished_ = false;
};

// Fills a world with the course and registers the script it uses.
void buildLevel(World& world, AssetLibrary& assets);

} // namespace skyforge
