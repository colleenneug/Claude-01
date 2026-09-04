#pragma once
#include "Gl.h"
#include "Mesh.h"

// Material types the pbr fragment shader branches on. Everything shades
// procedurally from world position/normal (see shaders/pbr.frag) — there
// are no texture assets in this project.
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
