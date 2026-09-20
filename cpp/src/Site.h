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
  };

  std::vector<Level::Part> parts;
  glm::vec3 playerSpawn{0.0f};
  float playerYaw = 0.0f;         // degrees, camera convention: 0 looks down +X
  float floorY = 0.0f;
  std::vector<Trigger> triggers;
  std::vector<Spawn> hostiles;
  std::vector<WeaponDrop> drops;
  std::vector<Objective> objectives;
};

// Builds the named site. Returns false for an unknown name, so a mission
// with a typo in its `layout` fails to load rather than dropping the player
// into an empty world.
bool buildSite(const std::string& name, Site& out);
