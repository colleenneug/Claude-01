#pragma once
// =============================================================================
//  playground/render/Environment.h — the world around the scene: what the
//  sky looks like, and the light it throws on everything (no Vulkan here).
//
//  IMAGE-BASED LIGHTING (IBL)
//    Real objects are lit by everything around them, not just the sun: the
//    blue sky from above, the ground from below, and shiny surfaces mirror
//    their surroundings. The renderer turns an ENVIRONMENT into three maps
//    (render/src/IblBaker.h, on the GPU, whenever the environment changes):
//      - the environment itself, a cube map (the skybox);
//      - an IRRADIANCE map: for each direction, the light a matte surface
//        facing that way receives (a very blurred copy);
//      - a PREFILTERED SPECULAR map: for each roughness (mip level), the
//        environment as a surface of that roughness reflects it, sharp at
//        mip 0, blurry at the last.
//    Plus a small table that does not depend on the environment (BRDF LUT).
//
//  Two sources:
//    ProceduralSky  a gradient sky computed in a shader: no file, cheap to
//                   build, so it is the default and what the tests use;
//    HdrImage       a photographed panorama (.hdr), for realistic light.
// =============================================================================

#include "playground/core/Types.h"

#include <vector>

#include <glm/vec3.hpp>

namespace playground::render {

// A sky from colours: zenith above, horizon all round, ground below, with
// a soft glow around the sun. Linear RGB; values above 1 are fine (HDR).
// The sun's disk itself is drawn by the sky pass (FrameData::sun), so it
// moves with the light and is not counted twice in the lighting.
struct ProceduralSky {
  glm::vec3 zenith{0.16f, 0.30f, 0.62f};
  glm::vec3 horizon{0.70f, 0.76f, 0.84f};
  glm::vec3 ground{0.20f, 0.18f, 0.16f};
  glm::vec3 sunDirection{-0.45f, -1.0f, -0.55f};  // the way the light travels, as DirectionalLight
  glm::vec3 glowColour{1.0f, 0.85f, 0.65f};
  f32 glowStrength = 0.8f;  // brightness of the halo round the sun
};

// A panorama in EQUIRECTANGULAR layout: x runs once round (longitude, the
// middle column looking along -z), y from straight up (top row) to straight
// down. Linear RGB(A) floats, as Radiance .hdr files hold.
struct HdrImage {
  u32 width = 0;
  u32 height = 0;
  std::vector<f32> rgba;  // width * height * 4
};

// How finely the environment is precomputed: larger is sharper, slower to
// build. Sizes are cube face sizes in texels; `samples` per texel for the
// blurred maps.
struct EnvironmentQuality {
  u32 cubeSize = 128;
  u32 specularSize = 64;
  u32 irradianceSize = 16;
  u32 samples = 64;
};
// Fast: a smooth procedural sky needs no more (and headless tests stay quick).
inline constexpr EnvironmentQuality kFastEnvironment{.cubeSize = 64, .specularSize = 32, .irradianceSize = 8,
                                                     .samples = 32};
// For photographed panoramas: sharp mirrors, smooth blur.
inline constexpr EnvironmentQuality kDetailedEnvironment{.cubeSize = 512, .specularSize = 128, .irradianceSize = 32,
                                                         .samples = 256};

}  // namespace playground::render
