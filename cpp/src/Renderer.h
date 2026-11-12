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
//   light probe      refreshed only when the scene's sky changes
//   shadow cascades  3 depth passes from the sun
//   depth pre-pass   everything opaque, depth only
//     -> ambient occlusion   half-res from that depth, then a depth-aware
//                            blur back up to full res
//     -> sun shafts          half-res march through the cascades
//   colour pass      HDR, linear; reads the two maps above; fog applied here
//   sky              one triangle at the far plane, behind everything
//   dust motes       additive
//   temporal AA      resolve against last frame's result
//   exposure meter   log-average luminance, adapted over time
//   bloom            soft-knee bright pass, 5-level down/up
//   depth of field   half-res, only while aiming
//   composite        sharpen, DoF blend, bloom, exposure, ACES, grade, sRGB,
//                    aberration, grain, vignette
//     -> screen
//
// The rule the order follows: anything that is part of the *world* — the
// sky, the air, the light — goes in before the temporal resolve, so it is
// anti-aliased, bloomed and exposed with everything else. Anything that is
// part of the *camera* goes in after.
class Renderer {
public:
  void create(int width, int height);
  void resize(int width, int height);
  void destroy();

  void renderFrame(const SceneSource& scene, const Camera& camera, float time, float dt);

  // The view-projection the last frame was drawn with. The HUD needs it to
  // put a label on a thing in the world — projecting with a matrix it
  // rebuilt itself would drift from what was actually rendered the moment
  // the FOV moved, which it does every time you aim. This is the matrix
  // *without* the sub-pixel jitter temporal AA adds: a label should not
  // shake by half a pixel every frame because the scene behind it does.
  const glm::mat4& lastViewProj() const { return lastViewProj_; }

  // Diagnostic only (gated behind EREBUS_DEBUG_PIXEL in main.cpp): reads
  // back the screen-centre pixel from both the pre-tonemap linear HDR scene
  // buffer and the final default-framebuffer image, and prints both, along
  // with the luminance the exposure meter has settled on. Lets a report of
  // "the screen is just dark/black" on hardware we can't test on directly
  // be told apart into "nothing is reaching the scene buffer" (a
  // geometry/lighting bug) vs "the scene has real radiance but the
  // composite pass is crushing it to black" (a tonemap/exposure bug) —
  // rather than guessing from a description alone.
  void debugPrintCenterPixel() const;

  // ---------- adaptive quality ----------
  // None of this is free; on hardware that can't afford the full pipeline,
  // a plain frame arriving 60 times a second beats a beautiful one arriving
  // 20 times a second. Watches a smoothed frame time and steps down (and,
  // much more reluctantly, back up) through the tiers below. Falling is
  // fast — stepping up requires staying fast for far longer, scaled by how
  // many times it has already fallen — so it can't sit oscillating between
  // two tiers. Off only if the caller explicitly disables it or forces a
  // tier (see EREBUS_QUALITY_TIER/EREBUS_QUALITY_AUTO in main.cpp), which
  // also doubles as a diagnostic: if a simpler pipeline renders correctly
  // on hardware where the full one doesn't, that narrows down which pass is
  // actually broken there.
  struct QualityTier {
    const char* name;
    int shadow[3];
    int bloomLevels;
    bool dofAllowed;
    float motesFraction;
    bool softShadows;        // contact-hardening penumbrae, vs a fixed 3x3
    bool ambientOcclusion;
    bool sunShafts;
    bool temporalAA;
    int maxLights;           // how many glowing things light their surroundings
  };
  static constexpr int kQualityTiers = 4;
  static const QualityTier kTiers[kQualityTiers];

  void setAutoQuality(bool on) { autoQuality_ = on; }
  void setQualityTier(int i) { applyQualityTier(i); }
  int qualityTier() const { return tier_; }
  const char* qualityTierName() const { return kTiers[tier_].name; }

  int shadowDrawCalls = 0;  // filled in each frame, for the on-screen HUD
  int lightsInUse() const { return lightCount_; }

private:
  // The matrices one frame is drawn with, worked out once at the top of
  // renderFrame and handed to every pass, so no two passes can disagree
  // about where the camera is.
  struct FrameView {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};           // as drawn: with this frame's jitter
    glm::mat4 viewProj{1.0f};       // as drawn
    glm::mat4 invViewProj{1.0f};    // as drawn
    glm::mat4 cleanViewProj{1.0f};  // without the jitter
  };

  void bindProbe(const SceneSource& scene);
  void gatherLights(const Camera& camera, int maxLights);
  void renderShadowCascades();
  void renderDepthPrepass(const FrameView& fv);
  void renderAmbientOcclusion(const FrameView& fv);
  void renderSunShafts(const SceneSource& scene, const Camera& camera, const FrameView& fv);
  void renderScene(const SceneSource& scene, const Camera& camera, const FrameView& fv);
  void renderSky(const SceneSource& scene, const Camera& camera, const FrameView& fv, float time);
  void renderMotes(const SceneSource& scene, const Camera& camera, const FrameView& fv, float time);
  GLuint resolveTemporal(const FrameView& fv);
  void meterExposure(GLuint sceneTex, float dt);
  void renderDof(GLuint sceneTex);
  void renderComposite(const Camera& camera, const SceneSource& scene, GLuint sceneTex, GLuint bloomTex, float time);
  void drawFullscreen() const;

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
  glm::mat4 lastViewProj_{1.0f};

  // What this frame turned on, decided once from the tier and the scene.
  bool aoOn_ = false, shaftsOn_ = false, taaOn_ = false, softShadows_ = false;

  // Temporal state: what the last frame looked like and where it was seen
  // from. Invalid after anything that makes the last frame not a picture of
  // the same place — a resize, a change of scene, a frame with TAA off.
  int frameIndex_ = 0;
  int historyIndex_ = 0;
  bool historyValid_ = false;
  bool adaptValid_ = false;
  int adaptIndex_ = 0;
  const SceneSource* lastScene_ = nullptr;
  glm::mat4 prevCleanViewProj_{1.0f};

  Shader depthShader_, prepassShader_, pbrShader_, skyShader_, motesShader_;
  Shader ssaoShader_, bilateralShader_, shaftShader_, taaShader_;
  Shader luminanceShader_, adaptShader_, dofShader_, compositeShader_;

  Framebuffer sceneHdr_;     // full-res HDR + depth texture
  Framebuffer aoRaw_, ao_;   // half-res estimate; full-res, blurred
  Framebuffer shaftRaw_, shaft_;
  Framebuffer history_[2];   // temporal AA ping-pong; the newer one is the frame
  Framebuffer luminance_;    // 64x64 log-luminance, mipmapped down to 1x1
  Framebuffer adapt_[2];     // 1x1 adapted log-luminance, ping-pong
  Framebuffer dofBuffer_;    // half-res blurred copy, blended in by roughness of focus
  GLuint whiteTex_ = 0;      // stands in for a map whose pass is switched off

  CascadedShadowMap csm_;
  Bloom bloom_;
  IBL ibl_;

  GLuint fsTriVao_ = 0;  // an empty VAO; fullscreen.vert builds the triangle from gl_VertexID

  std::vector<DrawItem> drawList_;  // scratch, refilled every frame

  static constexpr int kMaxLights = 16;   // must match MAX_LIGHTS in pbr.frag
  int lightCount_ = 0;
  glm::vec4 lightPos_[kMaxLights], lightColour_[kMaxLights], lightAxis_[kMaxLights];
};
