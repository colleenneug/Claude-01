#pragma once

#include "Level.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct MissionDef;

// A planet's open world (a mission with `mode = world`): one big place, not
// an arena or a route. A flat landing ground in the middle, named regions
// around it — mesas you climb by their ramps, canyons, boulder fields, ruined
// streets, craters, spires, dunes — and cliffs all the way round the edge.
//
// Built from a seed taken from the mission id, never the clock: every
// player's game has to build exactly the same world, or the Cybers the host
// runs would be walking through walls that only exist for somebody else.
struct WorldRegion {
  std::string name;     // shown when you walk into it
  std::string style;
  glm::vec3 centre{0.0f};
  float radius = 40.0f;
};

struct World {
  std::vector<Level::Part> parts;
  std::vector<WorldRegion> regions;     // [0] is the landing ground in the middle
  std::vector<glm::vec3> landingZones;  // where you come down, and get back up
  float half = 260.0f;                  // the cliffs stand at +-half
};

// Region styles: mesas, canyon, boulders, ruins, crater, spires, dunes,
// fissures. A mission names its regions with `region <style> <NAME>`; one
// that names none gets a set chosen for its biome.
void buildWorld(const MissionDef& mission, World& out);
