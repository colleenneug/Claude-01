#include "playground/render/Shadow.h"

#include "playground/render/FrameData.h"  // reverseZ()

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

namespace playground::render {

void Aabb::add(glm::vec3 point) noexcept {
  min = glm::min(min, point);
  max = glm::max(max, point);
}

void Aabb::add(const Aabb& other) noexcept {
  if (other.empty()) return;
  add(other.min);
  add(other.max);
}

Aabb Aabb::transformed(const glm::mat4& transform) const noexcept {
  Aabb out;
  if (empty()) return out;
  for (int corner = 0; corner < 8; ++corner) {
    const glm::vec3 p{(corner & 1) ? max.x : min.x, (corner & 2) ? max.y : min.y, (corner & 4) ? max.z : min.z};
    out.add(glm::vec3(transform * glm::vec4(p, 1.0f)));
  }
  return out;
}

ShadowProjection directionalShadowMatrix(glm::vec3 lightDirection, const Aabb& bounds, u32 mapSize) noexcept {
  const f32 length = glm::length(lightDirection);
  const glm::vec3 direction = length > 1e-6f ? lightDirection / length : glm::vec3(0.0f, -1.0f, 0.0f);
  const glm::vec3 centre = bounds.empty() ? glm::vec3(0.0f) : bounds.centre();
  const f32 radius = std::max(bounds.empty() ? 1.0f : 0.5f * glm::length(bounds.size()), 0.01f) * 1.02f;  // + a margin

  // Rotation only, about the world origin: looking along the light. Any
  // "up" not parallel to it will do.
  const glm::vec3 up = std::fabs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f), direction, up);

  // The sphere's centre in light space, snapped across the map to whole
  // texels. Depth is not snapped: it does not cause shimmering.
  const u32 size = std::max(mapSize, 1u);
  const f32 texel = 2.0f * radius / static_cast<f32>(size);
  const glm::vec3 c = glm::vec3(view * glm::vec4(centre, 1.0f));
  const f32 x = std::floor(c.x / texel) * texel;
  const f32 y = std::floor(c.y / texel) * texel;
  // Right-handed: the light looks down -z, so distances ahead are -z.
  const f32 nearDistance = -c.z - radius;
  const f32 farDistance = -c.z + radius;
  const glm::mat4 projection =
      reverseZ() * glm::orthoRH_ZO(x - radius, x + radius, y - radius, y + radius, nearDistance, farDistance);
  return {.viewProjection = projection * view, .texelWorldSize = texel};
}

}  // namespace playground::render
