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
  void build(float arenaSize, glm::vec3 floorTint = glm::vec3(0.31f, 0.26f, 0.21f));
  void destroy();

  void collect(std::vector<DrawItem>& out) const;

  // Resolves a cylinder (feet at `pos`, given radius/height) against every
  // collider, pushing it out along whichever axis overlaps least. Returns
  // true if the ground directly beneath is solid (so the caller can zero
  // vertical velocity) — this project's floor is one giant collider, so in
  // practice this is really "is pos.y at/below the floor's top".
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
  std::vector<Collider> colliders_;
  float half_ = 40.0f;
  float floorTop_ = 0.0f;
  float wallHeight_ = 6.0f;
  glm::vec3 floorTint_{0.31f, 0.26f, 0.21f};
};
