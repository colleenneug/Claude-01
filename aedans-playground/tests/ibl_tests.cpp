// Tests for the image-based lighting maths that runs on the CPU: the
// Hammersley sequence, GGX sampling and the split-sum BRDF table. No GPU.
// White-box: render/src/BrdfLut.h is private to the render layer.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "BrdfLut.h"

#include <cmath>

#include <glm/geometric.hpp>

using namespace playground;
using render::integrateBrdf;

TEST_CASE("Hammersley points spread over the unit square: first coordinate i/n, second the mirrored bits") {
  CHECK(render::hammersley(0, 8) == glm::vec2(0.0f, 0.0f));
  CHECK(render::hammersley(1, 8).y == doctest::Approx(0.5f));
  CHECK(render::hammersley(2, 8).y == doctest::Approx(0.25f));
  CHECK(render::hammersley(3, 8).y == doctest::Approx(0.75f));
  CHECK(render::hammersley(4, 8).x == doctest::Approx(0.5f));
}

TEST_CASE("GGX samples are unit half-vectors in the upper hemisphere; smoother surfaces keep them nearer the normal") {
  f32 smoothSum = 0.0f;
  f32 roughSum = 0.0f;
  for (u32 i = 0; i < 64; ++i) {
    const glm::vec2 xi = render::hammersley(i, 64);
    const glm::vec3 smooth = render::importanceSampleGgx(xi, 0.1f);
    const glm::vec3 rough = render::importanceSampleGgx(xi, 0.9f);
    CHECK(glm::length(smooth) == doctest::Approx(1.0f));
    CHECK(rough.z >= 0.0f);
    smoothSum += smooth.z;
    roughSum += rough.z;
  }
  CHECK(smoothSum / 64.0f > 0.98f);  // nearly all straight up
  CHECK(roughSum / 64.0f < smoothSum / 64.0f - 0.1f);
}

TEST_CASE("the BRDF table: a smooth surface seen head-on reflects exactly F0 (scale 1, bias 0)") {
  const glm::vec2 headOn = integrateBrdf(1.0f, 0.02f, 256);
  CHECK(headOn.x == doctest::Approx(1.0f).epsilon(0.02));
  CHECK(headOn.y == doctest::Approx(0.0f).scale(1.0).epsilon(0.02));
}

TEST_CASE("the BRDF table: glancing views add reflection (Fresnel); roughness takes some away") {
  // Seen at a glancing angle, even a dull surface reflects: the bias grows.
  CHECK(integrateBrdf(0.1f, 0.3f, 256).y > integrateBrdf(0.9f, 0.3f, 256).y);
  // A rough surface, seen at a glancing angle, loses energy to shadowing between
  // its microfacets: scale + bias (all reflected light for F0 = 1) drops below 1.
  const glm::vec2 rough = integrateBrdf(0.3f, 0.9f, 256);
  const glm::vec2 smooth = integrateBrdf(0.3f, 0.1f, 256);
  CHECK(rough.x + rough.y < smooth.x + smooth.y);
  CHECK(rough.x + rough.y < 1.0f);
}

TEST_CASE("makeBrdfLut: size x size entries at texel centres, all within [0, 1]") {
  const auto table = render::makeBrdfLut(16, 64);
  REQUIRE(table.size() == 256);
  for (const glm::vec2 v : table) {
    CHECK(v.x >= 0.0f);
    CHECK(v.y >= 0.0f);
    CHECK(v.x + v.y <= 1.001f);  // never more light out than in
  }
  // Entry (x, y) is n.v = (x + 0.5) / 16, roughness = (y + 0.5) / 16.
  const glm::vec2 expected = integrateBrdf(5.5f / 16.0f, 9.5f / 16.0f, 64);
  CHECK(table[9 * 16 + 5].x == doctest::Approx(expected.x));
  CHECK(table[9 * 16 + 5].y == doctest::Approx(expected.y));
}
