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
  void build(float arenaSize);
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

private:
  Mesh floorMesh_, wallMesh_, crateMesh_;
  std::vector<glm::mat4> walls_, crates_;
  std::vector<Collider> colliders_;
  float half_ = 40.0f;
  float floorTop_ = 0.0f;
  float wallHeight_ = 6.0f;
};
