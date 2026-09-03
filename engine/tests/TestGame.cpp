// ============================================================
//  Plays the sample game.
//
//  An engine is only an engine if you can make something with it, so
//  this drives the shipped game the way a player would: it walks the
//  pawn onto the orbs, checks the rules react, pushes it off the edge
//  to check it respawns, and finishes the course.
// ============================================================
#include "Test.hpp"

#include "../samples/skyforge/Skyforge.hpp"

#include "forge/assets/AssetLibrary.hpp"
#include "forge/core/Log.hpp"
#include "forge/scene/Level.hpp"
#include "forge/scene/World.hpp"
#include "forge/script/ScriptGraph.hpp"

using namespace forge;

namespace {

struct Game {
    AssetLibrary assets;
    World world{false};

    Game() {
        registerCoreNodes();
        assets.createStarterContent();
        world.setAssets(&assets);
        skyforge::buildLevel(world, assets);
    }

    void run(float seconds, float step = 1.0f / 60.0f) {
        for (float t = 0.0f; t < seconds; t += step) world.tick(step);
    }

    skyforge::SkyforgeGameMode* rules() {
        return dynamic_cast<skyforge::SkyforgeGameMode*>(world.gameMode());
    }
    Pawn* player() {
        return world.playerController() ? world.playerController()->pawn() : nullptr;
    }
    // Puts the player on top of an actor, which stands in for walking
    // there without needing to solve the platforming.
    void teleportTo(const Vec3& p) {
        Pawn* pawn = player();
        if (!pawn) return;
        pawn->setLocation(p);
        if (auto* move = pawn->findComponentOfClass<CharacterMovementComponent>())
            move->velocity = Vec3::Zero;
    }
};

} // namespace

TEST(sample_level_is_built_correctly) {
    Game g;
    CHECK(g.world.levelName() == "Skyforge");
    CHECK(g.world.settings().gameModeClass == "SkyforgeGameMode");
    CHECK(g.world.actorsOfClass("Orb").size() == 6);
    CHECK(!g.world.actorsOfClass("PlayerStart").empty());
    CHECK(!g.world.actorsWithTag("goal").empty());
    CHECK(!g.world.actorsOfClass("MovingPlatform").empty());
    // The script the beacon uses must actually be in the library, or the
    // component would warn and do nothing.
    CHECK(g.assets.script("BeaconPulse") != nullptr);
    CHECK(g.assets.script("BeaconPulse")->validate().empty());
}

TEST(sample_game_starts_with_a_player_on_the_ground) {
    Game g;
    g.world.beginPlay();
    CHECK(g.rules() != nullptr);
    CHECK(g.player() != nullptr);
    CHECK(g.rules()->orbsRemaining() == 6);
    CHECK(!g.rules()->goalOpen());
    CHECK(!g.rules()->finished());

    // Settle, and confirm the player is standing rather than falling.
    g.run(1.5f);
    auto* move = g.player()->findComponentOfClass<CharacterMovementComponent>();
    CHECK(move != nullptr);
    CHECK(move->isGrounded());
    CHECK(g.player()->location().y > 0.0f);
}

TEST(walking_into_an_orb_collects_it) {
    Game g;
    g.world.beginPlay();
    g.run(0.5f);
    CHECK(g.rules()->orbsRemaining() == 6);

    Actor* orb = g.world.findActorByName("Orb1");
    CHECK(orb != nullptr);
    const Vec3 where = orb->location();
    g.teleportTo(where);
    g.run(0.3f);

    // The orb is gone and the rules noticed.
    CHECK(g.world.findActorByName("Orb1") == nullptr);
    CHECK(g.rules()->orbsRemaining() == 5);
    CHECK(!g.rules()->goalOpen());
}

TEST(the_goal_only_opens_once_every_orb_is_taken) {
    Game g;
    g.world.beginPlay();
    g.run(0.5f);

    Actor* goal = g.world.actorsWithTag("goal").front();
    // Standing in the goal early must not finish the course.
    g.teleportTo(goal->location());
    g.run(0.5f);
    CHECK(!g.rules()->finished());

    for (int i = 1; i <= 6; ++i) {
        Actor* orb = g.world.findActorByName("Orb" + std::to_string(i));
        if (!orb) continue;
        g.teleportTo(orb->location());
        g.run(0.2f);
    }
    CHECK(g.rules()->orbsRemaining() == 0);
    CHECK(g.rules()->goalOpen());
    CHECK(g.world.actorsOfClass("Orb").empty());

    g.teleportTo(goal->location());
    g.run(0.5f);
    CHECK(g.rules()->finished());
}

TEST(falling_off_the_course_respawns_the_player) {
    Game g;
    g.world.beginPlay();
    g.run(0.5f);
    CHECK(g.rules()->respawnCount() == 0);

    g.teleportTo({40.0f, 3.0f, 0.0f});   // beside the start, over nothing
    g.run(3.0f);

    CHECK(g.rules()->respawnCount() >= 1);
    // Back near the start, and not still falling.
    Actor* start = g.world.actorsOfClass("PlayerStart").front();
    CHECK(distance(g.player()->location().flat(), start->location().flat()) < 6.0f);
    CHECK(g.player()->location().y > g.rules()->killHeight);
}

TEST(the_moving_platform_carries_and_returns) {
    Game g;
    g.world.beginPlay();
    Actor* bridge = g.world.actorsOfClass("MovingPlatform").front();
    const Vec3 origin = bridge->location();

    g.run(1.0f);
    const Vec3 moved = bridge->location();
    CHECK(distance(origin, moved) > 0.2f);

    // One full cycle at 0.18 round trips per second is about 5.6s; it
    // must come back rather than drifting away for ever.
    g.run(5.0f);
    CHECK(distance(bridge->location(), origin) < distance(moved, origin) + 12.0f);
    float furthest = 0.0f;
    for (int i = 0; i < 400; ++i) {
        g.world.tick(1.0f / 60.0f);
        furthest = std::max(furthest, distance(bridge->location(), origin));
    }
    // It never travels further than it was told to.
    CHECK(furthest <= 9.0f + 0.01f);
}

TEST(the_sample_level_survives_a_save_and_load) {
    Game g;
    const Json doc = LevelSerializer::saveWorld(g.world);

    AssetLibrary assets2;
    assets2.createStarterContent();
    // The script has to come across too, or the beacon loses its
    // behaviour on reload.
    assets2.loadJson(g.assets.toJson());

    World loaded(false);
    loaded.setAssets(&assets2);
    std::string err;
    CHECK(LevelSerializer::loadWorld(loaded, Json::parse(doc.dump(), &err)));
    CHECK(err.empty());

    CHECK(loaded.levelName() == "Skyforge");
    CHECK(loaded.actorsOfClass("Orb").size() == 6);
    CHECK(loaded.settings().gameModeClass == "SkyforgeGameMode");

    // And the reloaded level is still playable and still winnable.
    loaded.beginPlay();
    auto* rules = dynamic_cast<skyforge::SkyforgeGameMode*>(loaded.gameMode());
    CHECK(rules != nullptr);
    for (int i = 0; i < 30; ++i) loaded.tick(1.0f / 60.0f);
    CHECK(loaded.playerController() != nullptr);
    CHECK(loaded.playerController()->pawn() != nullptr);
    CHECK(rules->orbsRemaining() == 6);
}

int main() {
    Log::get().setEchoToConsole(false);
    return forge_test::runAll("game");
}
