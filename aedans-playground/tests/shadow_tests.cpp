// Tests for the sun's shadow-map projection (render/Shadow.h): framing,
// depth direction, texel snapping, awkward light directions. No GPU.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/render/Shadow.h"

#include <cmath>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

using namespace playground;
using render::Aabb;
using render::directionalShadowMatrix;

namespace {

glm::vec3 project(const glm::mat4& m, glm::vec3 p) {
  const glm::vec4 clip = m * glm::vec4(p, 1.0f);
  return glm::vec3(clip) / clip.w;
}

Aabb box(glm::vec3 min, glm::vec3 max) {
  Aabb b;
  b.add(min);
  b.add(max);
  return b;
}

}  // namespace

TEST_CASE("Aabb: grows with points and boxes; a transformed box holds the transformed corners") {
  Aabb b;
  CHECK(b.empty());
  b.add({1.0f, 2.0f, 3.0f});
  b.add({-1.0f, 0.0f, 5.0f});
  CHECK(b.min == glm::vec3(-1.0f, 0.0f, 3.0f));
  CHECK(b.max == glm::vec3(1.0f, 2.0f, 5.0f));
  const Aabb moved = b.transformed(glm::translate(glm::mat4(1.0f), {10.0f, 0.0f, 0.0f}));
  CHECK(moved.min.x == doctest::Approx(9.0f));
  const Aabb turned = box({-1, -1, -1}, {1, 1, 1}).transformed(
      glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), {0.0f, 1.0f, 0.0f}));
  CHECK(turned.max.x == doctest::Approx(std::sqrt(2.0f)));  // a cube turned 45 degrees is wider
  CHECK(Aabb{}.transformed(glm::mat4(2.0f)).empty());
}

TEST_CASE("every corner of the scene lands inside the shadow map, depth in [0, 1]") {
  const Aabb scene = box({-3.0f, 0.0f, -2.0f}, {4.0f, 2.5f, 3.0f});
  for (const glm::vec3 direction : {glm::vec3(-0.45f, -1.0f, -0.55f), glm::vec3(1.0f, -0.2f, 0.0f),
                                    glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.3f, 1.0f, 0.1f)}) {
    const auto shadow = directionalShadowMatrix(direction, scene, 2048);
    for (int corner = 0; corner < 8; ++corner) {
      const glm::vec3 p{(corner & 1) ? scene.max.x : scene.min.x, (corner & 2) ? scene.max.y : scene.min.y,
                        (corner & 4) ? scene.max.z : scene.min.z};
      const glm::vec3 ndc = project(shadow.viewProjection, p);
      INFO("light ", direction.x, ",", direction.y, ",", direction.z, "; corner ", corner);
      CHECK(std::fabs(ndc.x) <= 1.0f);
      CHECK(std::fabs(ndc.y) <= 1.0f);
      CHECK(ndc.z >= 0.0f);
      CHECK(ndc.z <= 1.0f);
    }
  }
}

TEST_CASE("reverse-Z: points nearer the light have larger depth; the centre is in the middle of the map") {
  const Aabb scene = box({-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
  const glm::vec3 light{0.0f, -1.0f, 0.0f};  // straight down: "up" must not be parallel to it
  const auto shadow = directionalShadowMatrix(light, scene, 1024);
  const f32 top = project(shadow.viewProjection, {0.0f, 1.0f, 0.0f}).z;
  const f32 bottom = project(shadow.viewProjection, {0.0f, -1.0f, 0.0f}).z;
  CHECK(top > bottom);  // the top is hit first
  const glm::vec3 centre = project(shadow.viewProjection, {0.0f, 0.0f, 0.0f});
  CHECK(std::fabs(centre.x) < 2.0f / 1024.0f * 1.01f);  // within one texel (snapping)
  CHECK(std::fabs(centre.y) < 2.0f / 1024.0f * 1.01f);
  CHECK(centre.z == doctest::Approx(0.5f).epsilon(0.01));
  CHECK(!std::isnan(shadow.viewProjection[0][0]));
}

TEST_CASE("the texel size follows the scene: a sphere's diameter over the map size") {
  const Aabb scene = box({-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f});  // diagonal 4 * sqrt(3)
  const auto shadow = directionalShadowMatrix({0.0f, -1.0f, 0.0f}, scene, 2048);
  CHECK(shadow.texelWorldSize == doctest::Approx(4.0f * std::sqrt(3.0f) * 1.02f / 2048.0f));
}

TEST_CASE("snapping: moving the scene a fraction of a texel moves the map in whole texels") {
  // A fixed world point's position within its texel must not change as the
  // scene's bounds drift: that is what keeps shadow edges from shimmering.
  const glm::vec3 light{-0.45f, -1.0f, -0.55f};
  const glm::vec3 point{0.3f, 0.7f, -0.2f};
  constexpr u32 kMap = 512;
  f32 firstFraction = -1.0f;
  for (int step = 0; step < 20; ++step) {
    const glm::vec3 drift{0.0013f * static_cast<f32>(step), 0.0f, 0.0007f * static_cast<f32>(step)};
    const auto shadow = directionalShadowMatrix(light, box(glm::vec3(-2.0f) + drift, glm::vec3(2.0f) + drift), kMap);
    const glm::vec3 ndc = project(shadow.viewProjection, point);
    const f32 texels = (ndc.x * 0.5f + 0.5f) * static_cast<f32>(kMap);
    const f32 fraction = texels - std::floor(texels);
    if (firstFraction < 0.0f) firstFraction = fraction;
    INFO("step ", step, ": ", texels, " texels");
    CHECK(fraction == doctest::Approx(firstFraction).epsilon(0.002));
  }
}

TEST_CASE("an empty scene and a zero light direction still give a usable matrix") {
  const auto shadow = directionalShadowMatrix({0.0f, 0.0f, 0.0f}, Aabb{}, 256);
  const glm::vec3 origin = project(shadow.viewProjection, {0.0f, 0.0f, 0.0f});
  CHECK(std::fabs(origin.x) <= 1.0f);
  CHECK(origin.z == doctest::Approx(0.5f).epsilon(0.02));
  CHECK(shadow.texelWorldSize > 0.0f);
}
