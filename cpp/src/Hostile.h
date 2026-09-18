#pragma once
#include "Gl.h"
#include "Draw.h"
#include "Content.h"
#include "Level.h"
#include <vector>

// A hostile's procedural rig — pelvis, chest, head with a lit visor,
// pauldrons, two-segment arms and legs — built once from a single shared
// unit box and unit sphere, and reused by every hostile regardless of its
// EnemyType's height/radius: each part's real size and position is baked
// into its own draw-time model matrix instead of into the mesh, the same
// technique Level.cpp uses for walls and crates. That means arbitrarily
// many hostiles of arbitrarily different sizes cost one shared geometry
// set, not one set per instance.
// Every hostile is assembled out of these four unit primitives, scaled and
// placed per part. A rig is a few dozen parts, so per-type meshes would mean
// a few dozen vertex buffers per archetype for shapes a scale matrix already
// gives us; sharing four means a marauder and a colossus cost the same
// upload, and the parts batch together in the draw list.
//
// The taper is the one shape a scale cannot fake: a limb that is thicker at
// the shoulder than the elbow reads as a limb, and a straight tube reads as
// a pipe. One taper ratio serves every limb on every archetype.
class HostileGeometry {
public:
  static void ensure();     // builds the shared meshes on first use
  static void destroyShared();
  static const Mesh& unitBox();
  static const Mesh& unitSphere();
  static const Mesh& unitCylinder();   // straight, unit diameter and height
  static const Mesh& unitTaper();      // 0.72 at the top, 1.0 at the bottom
};

enum class HostileState { Idle, Chase, Attack, Dying, Gone };

// One spawned enemy: the data an AI step needs, plus enough state to pose
// and render it. `type` is a pointer into Content's loaded table — content
// is the source of truth, this just tracks one instance's runtime state.
struct Hostile {
  const EnemyType* type = nullptr;
  glm::vec3 pos{0.0f}, vel{0.0f};
  float yaw = 0.0f;
  float hp = 1.0f, maxHp = 1.0f;
  HostileState state = HostileState::Idle;
  float cooldown = 0.0f;
  float deathT = 0.0f;
  float bob = 0.0f;
  float hitFlash = 0.0f;
  bool isBoss = false;
  float bossScale = 1.0f;
  // Direct pursuit alone deadlocks perfectly against an obstacle centred on
  // the straight line to the player — Level::resolve pushes it back to the
  // exact same boundary point every frame with nowhere to slide. stuckT
  // tracks how long a Chase/Attack step has failed to make real progress;
  // once it crosses a threshold, a fixed perpendicular bias (avoidSide) is
  // blended into the steering direction until it's making progress again.
  float stuckT = 0.0f;
  float avoidSide = 1.0f;

  void spawn(const EnemyType* t, glm::vec3 at, bool boss = false, float hpMult = 1.0f);

  // Returns true exactly on the frame this attack lands, so the caller
  // (Game) applies `type->damage` to the player once per real hit rather
  // than re-deriving it from state.
  bool update(float dt, const glm::vec3& playerPos, const Level& level);

  // Returns true if this hit killed it (caller awards xp exactly once).
  bool takeDamage(float amount);

  bool alive() const { return state != HostileState::Gone; }
  bool blocksShots() const { return state != HostileState::Gone && state != HostileState::Dying; }
  glm::vec3 headCentre() const { return pos + glm::vec3(0, type->height * 0.93f, 0); }
  glm::vec3 bodyCentre() const { return pos + glm::vec3(0, type->height * 0.55f, 0); }

  void collect(std::vector<DrawItem>& out) const;

private:
  // Two bodies, because a thing with legs and a thing that hovers share
  // nothing but their materials. Both are ported from the browser build's
  // rigs in src/js/fps/hostiles.js.
  void collectWalker(std::vector<DrawItem>& out, const DrawItem& base,
                     const glm::mat4& root, float h, float r, float swing,
                     float stride) const;
  void collectFlyer(std::vector<DrawItem>& out, const DrawItem& base,
                    const glm::mat4& root, float h, float r, float speedFrac) const;
};
