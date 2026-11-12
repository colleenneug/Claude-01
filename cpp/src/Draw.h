#pragma once
#include "Gl.h"
#include "Mesh.h"

// Material types the pbr fragment shader branches on. Everything shades
// procedurally from world position/normal (see shaders/pbr.frag) — there
// are no texture assets in this project.
enum class MaterialType : int { Armour = 0, Terrain = 1, Emissive = 2, Rock = 3, Planet = 4 };

struct DrawItem {
  const Mesh* mesh = nullptr;
  glm::mat4 model{1.0f};
  MaterialType material = MaterialType::Armour;
  glm::vec3 tint{0.6f, 0.63f, 0.68f};
  float metallic = 0.85f;
  float roughness = 0.25f;
  float wear = 1.0f;          // armour only: how chipped the paint is
  // Planet only: the second surface colour the continents mix toward, and
  // how far the polar caps reach (0 = none, 1 = iced over).
  glm::vec3 tint2{0.35f, 0.33f, 0.30f};
  float capExtent = 0.0f;
  glm::vec3 emissive{0.0f};
  float emissiveIntensity = 0.0f;
  float anisoStrength = 0.0f; // 0 = isotropic
  bool castShadow = true;
  // Surface detail — plates, chips, grain — is worked out from position.
  // For something bolted to the world that position is the world's, so two
  // walls that meet share one pattern across the join. For something that
  // moves it has to be the object's own, or the pattern stays where it is
  // and the object slides through it: a walking hostile whose scratches
  // crawl across its armour, a gun whose seams swim as you turn.
  bool objectSpace = false;
  // Emissive only: how much light this throws on what is near it, as a
  // multiple of what its size and brightness would give on their own
  // (Renderer::gatherLights). 0 = glows but lights nothing.
  float lightScale = 1.0f;
};
