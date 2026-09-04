#pragma once
#include "Gl.h"
#include "Shader.h"
#include "Framebuffer.h"
#include "CascadedShadowMap.h"
#include "Bloom.h"
#include "IBL.h"
#include "Game.h"
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

  void renderFrame(const Game& scene, const Camera& camera, float time, float dt);

  int shadowDrawCalls = 0;  // filled in each frame, for the on-screen HUD

private:
  void renderShadowCascades(const Game& scene, const Camera& camera);
  void renderSceneToHdr(const Game& scene, const Camera& camera);
  void renderMotes(const Game& scene, const Camera& camera, float time);
  void renderDof();
  void renderComposite(const Camera& camera, const Game& scene, GLuint bloomTex, float time);

  int width_ = 0, height_ = 0;

  Shader depthShader_, pbrShader_, motesShader_, dofShader_, compositeShader_;
  Framebuffer sceneHdr_;   // full-res HDR + depth texture
  Framebuffer dofBuffer_;  // half-res blurred copy, blended in by roughness of focus

  CascadedShadowMap csm_;
  Bloom bloom_;
  IBL ibl_;

  GLuint fsTriVao_ = 0;  // an empty VAO; fullscreen.vert builds the triangle from gl_VertexID

  std::vector<DrawItem> drawList_;  // scratch, refilled every frame
};
