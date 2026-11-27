#pragma once
// =============================================================================
//  playground/engine/Camera.h — where the view is, and how the world is
//  projected onto the screen.
//
//  WORLD COORDINATES: x right, y up, z towards the viewer. The 2D game lives
//  on the plane z = 0 (plus small z offsets for sprite layers).
//
//  TWO PROJECTIONS
//    Orthographic  Things keep their size at any distance: the 2D view.
//                  `orthoHalfExtent` is the world area (around the target)
//                  that must be visible; it is fitted to the window's shape
//                  without stretching (render::fitView), with extra world
//                  showing on the longer axis.
//    Perspective   Distant things look smaller: a 3D view. `fovY` is the
//                  vertical field of view.
//    Both look from `position` towards `target` (glm::lookAt).
//
//  REVERSE-Z
//    The depth buffer stores 1 at the near plane and 0 at the far plane
//    (the reverse of OpenGL's habit). Floating-point numbers are densest
//    near 0, and perspective squeezes far depths together, so storing far
//    away near 0 spreads the precision evenly instead of wasting it right
//    in front of the camera. Both projections here are converted to it, and
//    the renderer clears depth to 0 and keeps fragments with GREATER depth.
//
//  SCREEN COORDINATES: pixels from the window's top-left, y down, as the
//  mouse reports them. screenToWorld() turns a cursor position into the
//  point on the gameplay plane under it, for aiming and clicking; it works
//  in both projections because it casts a ray rather than assuming one.
// =============================================================================

#include "playground/core/Types.h"

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace playground::engine {

enum class Projection : u8 { Orthographic, Perspective };

// A line from the camera through a screen point, into the world.
struct Ray {
  glm::vec3 origin{0.0f};
  glm::vec3 direction{0.0f, 0.0f, -1.0f};  // not normalised: origin + direction reaches the far plane
};

struct Camera {
  Projection projection = Projection::Orthographic;

  glm::vec3 position{0.0f, 0.0f, 1000.0f};  // the eye
  glm::vec3 target{0.0f, 0.0f, 0.0f};       // what it looks at
  glm::vec3 up{0.0f, 1.0f, 0.0f};           // which way is up on screen

  glm::vec2 orthoHalfExtent{800.0f, 450.0f};  // orthographic: world area that must be visible
  f32 fovY = glm::radians(60.0f);             // perspective: vertical field of view, radians
  f32 nearPlane = 1.0f;                       // distance from the eye; nothing nearer is drawn
  f32 farPlane = 10000.0f;                    // nothing farther is drawn

  // World -> camera space.
  [[nodiscard]] glm::mat4 view() const;
  // Camera -> clip space, reverse-Z, depth 0..1, y up (the renderer flips
  // the viewport so +y is up on screen). `viewportSize` in pixels.
  [[nodiscard]] glm::mat4 projectionMatrix(glm::vec2 viewportSize) const;
  // projectionMatrix * view: what the shader multiplies positions by.
  [[nodiscard]] glm::mat4 viewProjection(glm::vec2 viewportSize) const;

  // The ray through a screen point (pixels, y down), from the near plane to
  // the far plane.
  [[nodiscard]] Ray screenRay(glm::vec2 screen, glm::vec2 viewportSize) const;

  // The point on the plane z = planeZ under a screen point. Empty if the ray
  // never meets the plane in front of the camera (looking along or away
  // from it).
  [[nodiscard]] std::optional<glm::vec3> screenToWorld(glm::vec2 screen, glm::vec2 viewportSize,
                                                       f32 planeZ = 0.0f) const;

  // Where a world point appears on screen, in pixels (y down). Empty if it
  // is behind the camera.
  [[nodiscard]] std::optional<glm::vec2> worldToScreen(glm::vec3 world, glm::vec2 viewportSize) const;
};

}  // namespace playground::engine
