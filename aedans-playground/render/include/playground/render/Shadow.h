#pragma once
// =============================================================================
//  playground/render/Shadow.h — where the sun's shadow map looks (no Vulkan).
//
//  A SHADOW MAP is a depth image rendered from the light: each texel holds
//  how far the light reaches along its ray before hitting something. A
//  pixel is in shadow when it is farther from the light than that.
//
//  The sun is far away, so its rays are parallel: the map is an
//  ORTHOGRAPHIC view along the light's direction. directionalShadowMatrix
//  frames it around the scene:
//    - a bounding SPHERE of the scene, not a tight box: its size does not
//      change as objects turn, so neither does the texel size;
//    - its centre is SNAPPED to whole texels across the map: as things
//      move, the map slides in whole-texel steps, and shadow edges do not
//      shimmer;
//    - depth runs across the sphere's diameter, REVERSE-Z like the main
//      camera: 1 nearest the light, 0 farthest.
// =============================================================================

#include "playground/core/Types.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace playground::render {

// An axis-aligned bounding box. Empty when min > max (nothing added yet).
struct Aabb {
  glm::vec3 min{1e30f};
  glm::vec3 max{-1e30f};

  [[nodiscard]] bool empty() const noexcept { return min.x > max.x; }
  [[nodiscard]] glm::vec3 centre() const noexcept { return 0.5f * (min + max); }
  [[nodiscard]] glm::vec3 size() const noexcept { return empty() ? glm::vec3(0.0f) : max - min; }
  void add(glm::vec3 point) noexcept;
  void add(const Aabb& other) noexcept;
  // The box around this box after `transform` (its eight corners).
  [[nodiscard]] Aabb transformed(const glm::mat4& transform) const noexcept;
};

struct ShadowProjection {
  glm::mat4 viewProjection{1.0f};  // world -> light clip space (x, y in [-1, 1], depth 1 near .. 0 far)
  f32 texelWorldSize = 0.0f;       // one shadow-map texel, in world units (for normal-offset bias)
};

// `lightDirection`: the way the light travels. `bounds`: what must be in
// the map (an empty box is treated as a unit sphere at the origin).
// `mapSize`: the map's width and height in texels.
[[nodiscard]] ShadowProjection directionalShadowMatrix(glm::vec3 lightDirection, const Aabb& bounds,
                                                       u32 mapSize) noexcept;

}  // namespace playground::render
