#pragma once
#include "Gl.h"
#include "Mesh.h"
#include <vector>

// Material types the pbr fragment shader branches on. Everything shades
// procedurally from world position/normal (see shaders/pbr.frag) — there
// are no texture assets in this project, matching the browser build's
// "generate every surface at load time" approach.
enum class MaterialType : int { Armour = 0, Terrain = 1, Emissive = 2, Rock = 3 };

struct DrawItem {
  const Mesh* mesh = nullptr;
  glm::mat4 model{1.0f};
  MaterialType material = MaterialType::Armour;
  glm::vec3 tint{0.6f, 0.63f, 0.68f};
  float metallic = 0.85f;
  float roughness = 0.25f;
  float wear = 1.0f;          // armour only: how chipped the paint is
  glm::vec3 emissive{0.0f};
  float emissiveIntensity = 0.0f;
  float anisoStrength = 0.0f; // 0 = isotropic
  bool castShadow = true;
};

// A small procedurally-posed armoured figure — the C++ analogue of the
// browser build's fps/hostiles.js walker rig: pelvis, chest, a head with a
// lit visor, pauldrons and two-segment arms/legs with real elbows and
// knees, assembled from a joint hierarchy rather than skinned. It exists
// to give the PBR armour material and the bloom pass something with a
// silhouette to demonstrate on, not to reproduce the full enemy roster.
class Sentinel {
public:
  void build();
  void destroy();
  // Appends this frame's posed parts to `out`. originYaw points it at the
  // camera; phase drives the walk cycle.
  void pose(const glm::vec3& origin, float yaw, float phase, float speed, std::vector<DrawItem>& out) const;

private:
  Mesh partBox_[8];  // torso, head, pauldron, upperArm, foreArm, thigh, shin, foot — shared geometry
  Mesh sphereSmall_;
};

class Scene {
public:
  void build();
  void destroy();

  // Fills `out` with every opaque item to draw this frame (ground, props,
  // the sentinel at its current pose).
  void collect(float time, std::vector<DrawItem>& out) const;

  glm::vec3 sunDirection{0.0f};  // direction the light *travels*, world space
  glm::vec3 sunColour{1.0f, 0.94f, 0.82f};
  float sunIntensityLux = 4.0f;

  // Dust motes: uploaded once as a point cloud wrapped around the camera by
  // the vertex shader (see shaders/motes.vert), exactly like atmos.js.
  GLuint moteVao() const { return moteVao_; }
  int moteCount() const { return moteCount_; }
  float moteBoxSize() const { return moteBox_; }

private:
  Mesh ground_;
  Mesh crate_, pillar_, rock_;
  Sentinel sentinel_;
  std::vector<glm::mat4> crates_, pillars_, rocks_;

  GLuint moteVao_ = 0, moteVbo_ = 0;
  int moteCount_ = 3000;
  float moteBox_ = 30.0f;
};
