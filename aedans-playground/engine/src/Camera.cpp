#include "playground/engine/Camera.h"

#include "playground/render/FrameData.h"

#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

namespace playground::engine {
namespace {

glm::vec2 safeViewport(glm::vec2 viewportSize) noexcept {
  return {viewportSize.x > 0.0f ? viewportSize.x : 1.0f, viewportSize.y > 0.0f ? viewportSize.y : 1.0f};
}

}  // namespace

glm::mat4 Camera::view() const { return glm::lookAtRH(position, target, up); }

glm::mat4 Camera::projectionMatrix(glm::vec2 viewportSize) const {
  const glm::vec2 viewport = safeViewport(viewportSize);
  glm::mat4 clipFromView(1.0f);
  if (projection == Projection::Orthographic) {
    // Fit the required area to the window's shape without stretching.
    const glm::vec2 half = render::fitView(0.5f * viewport, orthoHalfExtent);
    clipFromView = glm::orthoRH_ZO(-half.x, half.x, -half.y, half.y, nearPlane, farPlane);
  } else {
    clipFromView = glm::perspectiveRH_ZO(fovY, viewport.x / viewport.y, nearPlane, farPlane);
  }
  return render::reverseZ() * clipFromView;
}

glm::mat4 Camera::viewProjection(glm::vec2 viewportSize) const { return projectionMatrix(viewportSize) * view(); }

Ray Camera::screenRay(glm::vec2 screen, glm::vec2 viewportSize) const {
  const glm::vec2 viewport = safeViewport(viewportSize);
  // Pixels (y down) -> normalised device coordinates (-1..1, y up).
  const float ndcX = 2.0f * screen.x / viewport.x - 1.0f;
  const float ndcY = 1.0f - 2.0f * screen.y / viewport.y;

  // Undo the projection at the near plane (depth 1, reverse-Z) and at the
  // far plane (depth 0): two points on the line through that pixel.
  const glm::mat4 inverse = glm::inverse(viewProjection(viewport));
  const glm::vec4 nearClip = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  const glm::vec4 farClip = inverse * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
  const glm::vec3 nearPoint = glm::vec3(nearClip) / nearClip.w;
  const glm::vec3 farPoint = glm::vec3(farClip) / farClip.w;
  return {.origin = nearPoint, .direction = farPoint - nearPoint};
}

std::optional<glm::vec3> Camera::screenToWorld(glm::vec2 screen, glm::vec2 viewportSize, f32 planeZ) const {
  const Ray ray = screenRay(screen, viewportSize);
  // Where along the ray does z reach planeZ? origin.z + t * direction.z = planeZ.
  if (std::fabs(ray.direction.z) < 1e-6f) return std::nullopt;  // looking along the plane
  const float t = (planeZ - ray.origin.z) / ray.direction.z;
  if (t < 0.0f) return std::nullopt;  // the plane is behind the camera
  return ray.origin + t * ray.direction;
}

std::optional<glm::vec2> Camera::worldToScreen(glm::vec3 world, glm::vec2 viewportSize) const {
  const glm::vec2 viewport = safeViewport(viewportSize);
  const glm::vec4 clip = viewProjection(viewport) * glm::vec4(world, 1.0f);
  if (clip.w <= 0.0f) return std::nullopt;  // behind the eye
  const glm::vec2 ndc = glm::vec2(clip) / clip.w;
  return glm::vec2{(ndc.x + 1.0f) * 0.5f * viewport.x, (1.0f - ndc.y) * 0.5f * viewport.y};
}

}  // namespace playground::engine
