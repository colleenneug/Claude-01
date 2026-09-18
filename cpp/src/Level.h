#pragma once
#include "Gl.h"
#include "Mesh.h"
#include "Draw.h"
#include <vector>

// An axis-aligned box collider. The level is small enough (a walled arena,
// a few dozen crates) that a flat vector and an O(n) sweep every query is
// fine — the browser build's spatial hash exists for a much bigger world
// than this vertical slice needs yet.
struct Collider {
  glm::vec3 min, max;
};

// A walled arena: floor, perimeter walls, and scattered cover. Owns both
// the render geometry (layered terrain floor, armoured walls and crates —
// the same materials the renderer was built to show off) and the collision
// data physics resolves against.
class Level {
public:
  // floorTint is the mission's own ground colour (MissionDef::floorColour):
  // a sand shelf and the floor of a dead ark are not the same place, and a
  // single hardcoded dust brown made every sector look like the first one.
  void build(float arenaSize, glm::vec3 floorTint = glm::vec3(0.31f, 0.26f, 0.21f),
             float coverDensity = 1.0f);

  // One box of a hand-built level. The station (Station.cpp) is a list of
  // these: a station is a set of rooms, and rooms are easier to write as the
  // space they occupy than as a centre plus a size.
  struct Part {
    glm::vec3 min{0.0f}, max{0.0f};
    glm::vec3 tint{0.5f};
    float metallic = 0.3f, roughness = 0.6f, wear = 0.8f;
    // Emissive parts are the lights and the signage: they read as sources
    // rather than as paint, and they are never solid.
    bool emissive = false;
    float emissiveIntensity = 2.0f;
    // Trim, railings' tops, ceiling panels: seen but not collided with.
    bool solid = true;
    bool castShadow = true;
  };

  // Build from an explicit list of boxes instead of the procedural arena.
  // Nothing is generated and nothing is clamped to a perimeter — a station
  // is not a walled field, and clamping to one would pin you inside the
  // bounding box of the whole structure.
  void buildFromParts(const std::vector<Part>& parts, float floorY);
  void destroy();

  void collect(std::vector<DrawItem>& out) const;

  // Resolves a cylinder (feet at `pos`, given radius/height) against every
  // collider and returns whether it is standing on something.
  //
  // Three things happen, in order, and the order is the whole design:
  //   1. Find what holds it up — the highest box top under its footprint that
  //      is no more than a step above its feet. That is what makes a stair a
  //      stair rather than a wall, and what lets a deck plate seven metres up
  //      be a floor to whoever is on it and a ceiling to whoever is under it.
  //   2. Push out of anything that actually blocks: a box whose top is more
  //      than a step above the feet AND whose bottom is below the head. A
  //      deck overhead fails the second test and you walk under it; a stair
  //      step fails the first and you walk up it.
  //   3. Settle onto the support.
  static constexpr float kStepHeight = 0.45f;
  bool resolve(glm::vec3& pos, float radius, float height) const;

  float floorY() const { return floorTop_; }
  float arenaHalf() const { return half_; }

  // A spawn point inside the arena at roughly `radius` from the centre, on
  // a deterministic ring so waves spread out rather than stacking.
  glm::vec3 spawnPoint(int index, int total, float radius) const;

  const std::vector<Collider>& colliders() const { return colliders_; }

  // Is something solid at this point, and if so which way does its nearest
  // face point? Used by movement that has to know about a wall it is not
  // standing on. The normal is the outward one of whichever face the point
  // is closest to, which for the axis-aligned boxes this level is made of is
  // exact rather than approximate.
  bool wallAt(const glm::vec3& point, glm::vec3& normalOut) const;

  // Is the straight line from `from` to `to` clear of cover? Sampled rather
  // than solved, which is all the callers need: it answers "can this shot
  // get there", not "exactly where does it stop" — Weapon::fire owns that.
  bool lineOfSight(const glm::vec3& from, const glm::vec3& to) const;

private:
  // Cover comes in three shapes because they do three different jobs: a
  // block you can vault or hide behind, a barricade you crouch behind and
  // shoot over, and a pillar that takes a sightline away entirely. An arena
  // of nothing but waist-high crates plays the same at eighty metres as at
  // two hundred — every fight is still everyone shooting everyone.
  enum class CoverKind { Block, Barricade, Pillar };

  struct Prop {
    glm::mat4 model{1.0f};
    CoverKind kind = CoverKind::Block;
  };

  Mesh floorMesh_, boxMesh_;
  std::vector<glm::mat4> walls_;
  std::vector<Prop> props_;
  std::vector<Part> parts_;          // set only when built from parts
  std::vector<Collider> colliders_;
  bool fromParts_ = false;
  float half_ = 40.0f;
  float floorTop_ = 0.0f;
  float wallHeight_ = 6.0f;
  glm::vec3 floorTint_{0.31f, 0.26f, 0.21f};
};
