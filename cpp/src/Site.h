#pragma once
#include "Gl.h"
#include "Level.h"
#include <string>
#include <vector>

// A hand-built place: rooms with doors between them, which is not something
// a scatter of cover on a walled field can express, and the opening of this
// game is rooms.
//
// A site is pure data — geometry, where things stand, where the volumes are
// that fire a scene or tick an objective. Game reads it and does the rest,
// exactly as it does for a procedural arena, so nothing downstream has to
// know which kind of level it is playing in.
struct Site {
  // A named box. Walking into it fires the cutscene of the same name (if the
  // mission has one) and completes the objective waiting on it.
  struct Trigger {
    std::string id;
    glm::vec3 min{0.0f}, max{0.0f};
  };

  struct Spawn {
    std::string enemyId;
    glm::vec3 pos{0.0f};
    // Hostiles inside are asleep until the trigger of this name fires, so
    // the building does not empty itself into the corridor behind you while
    // you are still looking for a weapon. Empty means awake from the start.
    std::string wakeOn;
    // Which encounter it belongs to, for an objective that ends when that
    // encounter is down rather than when everything in the site is.
    std::string group;
  };

  // A weapon lying where somebody left it. Walking over it picks it up.
  struct WeaponDrop {
    std::string weaponId;
    glm::vec3 pos{0.0f};
    std::string note;     // "SIDEARM RECOVERED" — shown for a moment on pickup
  };

  // What you are doing right now, in order. Each one ends when its trigger
  // fires — or, for `needsClear`, when everything awake is down. They change
  // quietly: no banner, no pause, because a wall of AREA COMPLETE every ten
  // metres is what turns a place into a corridor of checkpoints.
  struct Objective {
    std::string text;      // "FIND SOMETHING TO FIGHT WITH"
    std::string hint;      // "W A S D TO MOVE"
    std::string trigger;   // the box that ends it, or empty
    bool needsClear = false;
    // With needsClear: only this group has to be down (Spawn::group). Empty
    // means everything.
    std::string clearGroup;
    // Ends when the mission's boss has been spawned and killed.
    bool needsBoss = false;

    // Timed: a clock that only runs while you are inside the zone. HOLD is
    // a wide ring you keep against reinforcements; UPLINK is a point you
    // stand at while a bar fills.
    enum class Timed { None, Hold, Uplink };
    Timed timed = Timed::None;
    float seconds = 0.0f;
    glm::vec3 zone{0.0f};
    float zoneRadius = 0.0f;
    // Collect: every core in this group picked up (Site::cores).
    std::string collectGroup;
    // While a timed objective runs, more of these keep coming, from the
    // edges of this box, into this group.
    std::vector<std::string> reinforce;
    glm::vec3 areaMin{0.0f}, areaMax{0.0f};
    std::string reinforceGroup;
  };

  // Something to pick up for a collect objective: a data core, a black box.
  struct Core {
    glm::vec3 pos{0.0f};
    std::string group;
  };

  // An expedition's areas, in route order: where each one is, and the
  // doorways in and out of it. The waypoint walks you through the doors
  // instead of pointing straight through a wall at the next room.
  struct Area {
    std::string name;
    glm::vec3 min{0.0f}, max{0.0f};
    glm::vec3 entry{0.0f}, exit{0.0f};
    bool hasEntry = false, hasExit = false;
  };

  std::vector<Level::Part> parts;
  glm::vec3 playerSpawn{0.0f};
  float playerYaw = 0.0f;         // degrees, camera convention: 0 looks down +X
  float floorY = 0.0f;
  std::vector<Trigger> triggers;
  std::vector<Spawn> hostiles;
  std::vector<WeaponDrop> drops;
  std::vector<Objective> objectives;
  std::vector<Area> areas;
  // Resupply set down along a route: rounds and a medkit at the way into
  // each area. A route a few hundred metres long is not survivable on what
  // you walked in with, and a resupply that only drops off kills runs dry
  // in exactly the fight that needed it.
  std::vector<glm::vec3> ammoCaches, medkits;
  std::vector<Core> cores;
  // Lit fixtures for timed objectives — the uplink's beacon, the ring you
  // hold — drawn but not solid. Kept apart from `parts` so they can be lit
  // up while the objective is live and dimmed once it is done.
  struct Marker { glm::vec3 min{0.0f}, max{0.0f}; glm::vec3 colour{1.0f}; size_t objective = 0; };
  std::vector<Marker> markers;

  // An expedition carries the mission's own waves, placed in its areas, so
  // Game does not spawn them again on a ring round the origin.
  bool consumesWaves = false;
  // Where the boss appears, and the trigger it waits on: you walk into its
  // arena and then it comes, rather than it standing in an empty room at
  // the far end of the map from the moment the last guard falls.
  bool hasBossSpawn = false;
  glm::vec3 bossSpawn{0.0f};
  std::string bossWaitsOn;
};

// Builds the named site. Returns false for an unknown name, so a mission
// with a typo in its `layout` fails to load rather than dropping the player
// into an empty world.
bool buildSite(const std::string& name, Site& out);

// A campaign mission's expedition: the long way through a place, area by area.
// Built from the mission's own data — its waves become the encounters, its
// size sets the scale, its boss the last room — so every campaign mission is
// one without a hand-built map each. See the definition for the shape.
struct MissionDef;
void buildExpedition(const MissionDef& mission, Site& out);
