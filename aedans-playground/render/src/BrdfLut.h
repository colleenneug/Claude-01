#pragma once
// =============================================================================
//  render/src/BrdfLut.h — the split-sum BRDF table for image-based lighting.
//
//  Shiny reflections of the environment need, per pixel, an integral of
//  "incoming light x the surface's GGX response" over the hemisphere. The
//  SPLIT-SUM approximation (Karis, Unreal Engine 4, 2013) splits it in two:
//
//    1. the light, pre-blurred per roughness into a cube map's mip levels
//       (IblPrecompute: done on the GPU whenever the environment changes);
//    2. the rest, which does not depend on the environment at all, only on
//       how glancing the view is (n.v) and how rough the surface is. That
//       is this table: for each (n.v, roughness), a SCALE and a BIAS with
//       which  reflectance = F0 * scale + bias  (F0: reflectance head-on).
//
//  Made once, on the CPU (a few milliseconds), uploaded as a two-channel
//  half-float texture. Pure maths, so it is tested without a GPU.
// =============================================================================

#include "playground/core/Types.h"

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace playground::render {

// The i-th of n points of the Hammersley sequence: evenly spread over the
// unit square, better than random for few samples.
[[nodiscard]] glm::vec2 hammersley(u32 i, u32 n) noexcept;

// A half-vector around +z distributed like GGX with this roughness
// (glTF's perceptual roughness; alpha = roughness squared).
[[nodiscard]] glm::vec3 importanceSampleGgx(glm::vec2 xi, f32 roughness) noexcept;

// The (scale, bias) for one (n.v, roughness), with `samples` samples.
[[nodiscard]] glm::vec2 integrateBrdf(f32 nDotV, f32 roughness, u32 samples) noexcept;

// size x size entries, row-major: x = n.v, y = roughness, both at texel
// centres ((i + 0.5) / size), so the GPU's linear filtering reads them right.
[[nodiscard]] std::vector<glm::vec2> makeBrdfLut(u32 size, u32 samples);

}  // namespace playground::render
