#include "BrdfLut.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

namespace playground::render {

glm::vec2 hammersley(u32 i, u32 n) noexcept {
  // Van der Corput: the bits of i mirrored around the binary point.
  u32 bits = i;
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return {static_cast<f32>(i) / static_cast<f32>(n), static_cast<f32>(bits) * 2.3283064365386963e-10f};
}

glm::vec3 importanceSampleGgx(glm::vec2 xi, f32 roughness) noexcept {
  const f32 a = roughness * roughness;
  const f32 phi = 2.0f * std::numbers::pi_v<f32> * xi.x;
  const f32 cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
  const f32 sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
  return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
}

glm::vec2 integrateBrdf(f32 nDotV, f32 roughness, u32 samples) noexcept {
  const glm::vec3 v{std::sqrt(std::max(0.0f, 1.0f - nDotV * nDotV)), 0.0f, nDotV};  // the normal is +z
  // Smith-Schlick visibility with the image-based-lighting k (Karis).
  const f32 k = roughness * roughness / 2.0f;
  const auto g1 = [k](f32 x) { return x / (x * (1.0f - k) + k); };
  f32 scale = 0.0f;
  f32 bias = 0.0f;
  for (u32 i = 0; i < samples; ++i) {
    const glm::vec3 h = importanceSampleGgx(hammersley(i, samples), roughness);
    const glm::vec3 l = 2.0f * glm::dot(v, h) * h - v;
    const f32 nDotL = std::max(l.z, 0.0f);
    if (nDotL <= 0.0f) continue;
    const f32 nDotH = std::max(h.z, 0.0f);
    const f32 vDotH = std::max(glm::dot(v, h), 0.0f);
    const f32 visibility = g1(nDotV) * g1(nDotL) * vDotH / (nDotH * nDotV);
    const f32 fresnel = std::pow(1.0f - vDotH, 5.0f);
    scale += (1.0f - fresnel) * visibility;
    bias += fresnel * visibility;
  }
  return glm::vec2(scale, bias) / static_cast<f32>(samples);
}

std::vector<glm::vec2> makeBrdfLut(u32 size, u32 samples) {
  std::vector<glm::vec2> table(static_cast<usize>(size) * size);
  for (u32 y = 0; y < size; ++y) {
    const f32 roughness = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size);
    for (u32 x = 0; x < size; ++x) {
      const f32 nDotV = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size);
      table[static_cast<usize>(y) * size + x] = integrateBrdf(nDotV, roughness, samples);
    }
  }
  return table;
}

}  // namespace playground::render
