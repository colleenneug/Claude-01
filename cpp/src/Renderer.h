#pragma once
#include "Gl.h"
#include "Shader.h"
#include "Framebuffer.h"
#include "CascadedShadowMap.h"
#include "Bloom.h"
#include "IBL.h"
#include "Scene.h"
#include "Camera.h"
#include <vector>

// Orchestrates one frame:
//
//   3x cascaded shadow depth passes
//     -> scene pass (HDR, linear, no tone mapping yet — see composite.frag
//        for why that ordering matters)
//     -> dust motes, additive, into the same HDR buffer
//     -> bloom (soft-knee bright pass, 5-level down/up)
//     -> half-res depth-of-field blur (only while aiming)
//     -> composite: DoF blend, bloom, volumetric fog, ACES, sRGB,
//        aberration, grain, vignette
//     -> screen
//
// This mirrors the browser build's fps/engine.js pass order and math
// almost line for line — the same techniques, ported from a hand-rolled
// WebGL post chain to a hand-rolled desktop OpenGL one.
class Renderer {
public:
  void create(int width, int height);
  void resize(int width, int height);
  void destroy();

  void renderFrame(const SceneSource& scene, const Camera& camera, float time, float dt);

  // Diagnostic only (gated behind EREBUS_DEBUG_PIXEL in main.cpp): reads
  // back the screen-centre pixel from both the pre-tonemap linear HDR scene
  // buffer and the final default-framebuffer image, and prints both. Lets
  // a report of "the screen is just dark/black" on hardware we can't test
  // on directly be told apart into "nothing is reaching the scene buffer"
  // (a geometry/lighting bug) vs "the scene has real radiance but the
  // composite pass is crushing it to black" (a tonemap/exposure bug) —
  // rather than guessing from a description alone.
  void debugPrintCenterPixel() const;

  // ---------- adaptive quality (ported from the browser build's engine.js
  // TIERS/trackFrame) ----------
  // Cascaded shadows and a multi-pass composite aren't free; on hardware
  // that can't afford the full pipeline, a plain frame arriving 60 times a
  // second beats a beautiful one arriving 20 times a second. Watches a
  // smoothed frame time and steps shadow-map resolution, bloom level count,
  // whether depth-of-field is allowed to run at all, and dust-mote count
  // down (and, much more reluctantly, back up) to fit. Falling is fast —
  // stepping up requires staying fast for far longer, scaled by how many
  // times it has already fallen — so it can't sit oscillating between two
  // tiers. Off by default only if the caller explicitly disables it or
  // forces a tier (see EREBUS_QUALITY_TIER/EREBUS_QUALITY_AUTO in main.cpp),
  // which also doubles as a diagnostic: if a simpler pipeline renders
  // correctly on hardware where the full one doesn't, that narrows down
  // which pass is actually broken there.
  struct QualityTier {
    const char* name;
    int shadow[3];
    int bloomLevels;
    bool dofAllowed;
    float motesFraction;
  };
  static constexpr int kQualityTiers = 4;
  static const QualityTier kTiers[kQualityTiers];

  void setAutoQuality(bool on) { autoQuality_ = on; }
  void setQualityTier(int i) { applyQualityTier(i); }
  int qualityTier() const { return tier_; }
  const char* qualityTierName() const { return kTiers[tier_].name; }

  int shadowDrawCalls = 0;  // filled in each frame, for the on-screen HUD

private:
  void renderShadowCascades(const SceneSource& scene, const Camera& camera);
  void renderSceneToHdr(const SceneSource& scene, const Camera& camera);
  void renderMotes(const SceneSource& scene, const Camera& camera, float time);
  void renderDof();
  void renderComposite(const Camera& camera, const SceneSource& scene, GLuint bloomTex, float time);

  void trackFrameTime(float dtSeconds);
  void applyQualityTier(int i);

  int tier_ = 0;
  bool autoQuality_ = true;
  bool dofAllowed_ = true;
  float motesFraction_ = 1.0f;
  float emaMs_ = 16.0f;
  int slowFrames_ = 0, fastFrames_ = 0, downgrades_ = 0;

  int width_ = 0, height_ = 0;
  // Refreshed each frame from SceneSource::viewDistance(). A mission arena
  // is tens of metres and open space is tens of thousands: one shared far
  // plane would either clip the planets out of existence or spend all the
  // depth precision (and every shadow cascade) on empty air.
  float farPlane_ = 500.0f;

  Shader depthShader_, pbrShader_, motesShader_, dofShader_, compositeShader_;
  Framebuffer sceneHdr_;   // full-res HDR + depth texture
  Framebuffer dofBuffer_;  // half-res blurred copy, blended in by roughness of focus

  CascadedShadowMap csm_;
  Bloom bloom_;
  IBL ibl_;

  GLuint fsTriVao_ = 0;  // an empty VAO; fullscreen.vert builds the triangle from gl_VertexID

  std::vector<DrawItem> drawList_;  // scratch, refilled every frame
};
