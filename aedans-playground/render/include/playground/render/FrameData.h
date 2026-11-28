#pragma once
// =============================================================================
//  playground/render/FrameData.h — what to draw this frame, as plain data.
//
//  Two kinds of things are drawn: 3D MESHES (lit, textured; see
//  render/Resources.h), then 2D SPRITES (flat colour). Both use the same
//  camera and depth buffer, so sprites and meshes hide each other correctly.
//
//  The renderer never reads the ECS. Each frame the game copies what is
//  visible into a FrameData ("extraction", engine::extractSprites), and the
//  renderer draws that. The two sides share no pointers, which keeps them
//  decoupled and is what later lets rendering run on its own thread.
//
//  COORDINATES: world units, +x right, +y up, +z towards the viewer.
//  `viewProjection` (usually engine::Camera::viewProjection) maps them to
//  the screen; left empty, the renderer uses one unit per pixel with (0, 0)
//  at the window's centre. Sprites' `depth` is their z: with the depth test,
//  a larger z is in front whatever the draw order; equal z falls back to
//  draw order (later on top).
// =============================================================================

#include "playground/core/Types.h"
#include "playground/render/Resources.h"
#include "playground/render/Shadow.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/mat4x4.hpp>

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace playground::render {

struct SpriteInstance {
  glm::vec2 position{0.0f};          // centre, world units
  glm::vec2 size{32.0f};             // full width and height, world units
  f32 rotation = 0.0f;               // radians, counter-clockwise
  glm::vec4 colour{1.0f};            // linear RGBA; alpha blends
  f32 depth = 0.0f;                  // world z: larger is nearer the viewer
};

// How the HDR scene is squeezed into the screen's 0..1 (the composite pass).
enum class ToneMap : u8 {
  None,        // exposure only; anything above 1 clips to white
  Aces,        // ACES filmic: film-like contrast, highlights roll off to white
  PbrNeutral,  // Khronos PBR Neutral: base colours stay true, only bright light rolls off
};

// The glow round very bright light (render/shaders/bloom.frag).
struct BloomSettings {
  bool enabled = false;
  f32 threshold = 1.0f;  // light below this (linear, after nothing) does not glow
  f32 knee = 0.5f;       // softness of the threshold, in the same units
  f32 strength = 0.1f;   // how much of the glow is added to the scene
};

// The sun's shadows (render/Shadow.h, render/shaders/shadow.vert, pbr.frag).
struct ShadowSettings {
  bool enabled = false;
  f32 strength = 1.0f;      // 1: shadowed sunlight is gone; 0: no shadow
  // Against SHADOW ACNE (surfaces shadowing themselves in stripes, because
  // the map's texels are coarser than the surface): the depth stored in the
  // map is pushed away from the light (hardware depth bias, constant and by
  // slope), and the point looked up is moved out along the surface normal
  // (normal offset, in shadow-map texels). Too much of either and shadows
  // detach from their objects (PETER-PANNING).
  f32 depthBias = 1.5f;
  f32 slopeBias = 2.0f;
  f32 normalOffset = 1.5f;
  // What the map must cover. Empty: every mesh drawn this frame.
  std::optional<Aabb> bounds;
};

struct FrameData {
  std::vector<MeshInstance> meshes;         // opaque 3D meshes, any order (the depth buffer sorts them)
  std::vector<SpriteInstance> sprites;      // equal depth: drawn in order, later on top
  std::optional<glm::mat4> viewProjection;  // world -> clip (reverse-Z); empty = pixels
  glm::vec2 viewportSize{0.0f};             // framebuffer pixels, filled in before extraction

  // Lighting, for meshes (sprites are unlit).
  glm::vec3 cameraPosition{0.0f, 0.0f, 1000.0f};  // world; shiny highlights depend on it
  DirectionalLight sun;
  glm::vec3 ambient{0.0f};           // a flat extra light from everywhere, linear RGB (a fill; usually 0)
  f32 environmentIntensity = 1.0f;   // the environment's light on meshes (Renderer::setEnvironment); 0 = off

  ShadowSettings shadows;

  // Presentation, for 3D scenes. Meshes and the sky render into an HDR
  // image; the composite pass then applies bloom, exposure and tone mapping
  // on the way to the screen. Sprites are drawn after that, untouched.
  bool drawSky = false;  // the environment behind everything, with the sun's disk
  // Draw the developer UI over the finished frame (Renderer::enableUi):
  // ImGui::GetDrawData() of this frame. Set by the Application each frame.
  bool drawUi = false;
  f32 exposure = 1.0f;
  ToneMap toneMap = ToneMap::None;
  BloomSettings bloom;

  // Keeps the lists' capacity (no allocation per frame); resets the view.
  // Lighting and presentation (sun, ambient, environment, sky, exposure,
  // tone map) stay as they were: they are scene state, not per-frame data.
  void clear() noexcept {
    meshes.clear();
    sprites.clear();
    viewProjection.reset();
  }
};

// Converts a projection with depth 0 (near) .. 1 (far) into reverse-Z,
// 1 (near) .. 0 (far): z_clip' = w_clip - z_clip. See engine/Camera.h.
[[nodiscard]] inline glm::mat4 reverseZ() noexcept {
  glm::mat4 m(1.0f);
  m[2][2] = -1.0f;  // column 2, row 2
  m[3][2] = 1.0f;   // column 3, row 2
  return m;
}

// One world unit per pixel, (0, 0) at the centre, reverse-Z over z -1000..1000:
// the view used when a frame sets no viewProjection.
[[nodiscard]] inline glm::mat4 pixelViewProjection(glm::vec2 viewportSize) noexcept {
  const glm::vec2 half = 0.5f * glm::max(viewportSize, glm::vec2{1.0f});
  return reverseZ() * glm::orthoRH_ZO(-half.x, half.x, -half.y, half.y, -1000.0f, 1000.0f);
}

// The half-size of the world area actually shown in a viewport of
// `viewportHalf` pixels so that all of `required` fits and nothing is
// stretched: the shorter fit wins, and the other axis shows extra world
// (bars at the sides of a wide window, above and below a tall one).
// `required` of zero means "one unit per pixel".
[[nodiscard]] inline glm::vec2 fitView(glm::vec2 viewportHalf, glm::vec2 required) noexcept {
  if (required.x <= 0.0f || required.y <= 0.0f || viewportHalf.x <= 0.0f || viewportHalf.y <= 0.0f) {
    return viewportHalf;
  }
  const float unitsPerPixel = std::max(required.x / viewportHalf.x, required.y / viewportHalf.y);
  return viewportHalf * unitsPerPixel;
}

}  // namespace playground::render
