#pragma once
#include "Gl.h"
#include <array>
#include <vector>

// Three cascaded shadow maps for one low sun, ported from the browser
// build's fps/csm.js. A single shadow camera covering a hundred metres at
// 2048px spends centimetres per texel and gives a low sun staircase edges;
// three cascades instead split the view distance and refit each slice
// tightly, so the near cascade still resolves a boot heel at arm's length.
//
// Each cascade is refit every frame to a bounding *sphere* around its slice
// of the view frustum (a sphere doesn't change size as the camera turns, so
// the shadow map doesn't resize and its texels don't shimmer), then the
// sphere's centre is snapped to that cascade's own texel grid so the edge
// doesn't crawl as you walk.
class CascadedShadowMap {
public:
  static constexpr int CASCADES = 3;

  void create(const std::array<int, CASCADES>& mapSizes);
  void destroy();

  // Refit all cascades to the current camera and sun direction. Call once
  // per frame before rendering any of the depth passes.
  void update(const glm::vec3& camPos, const glm::vec3& camForward, const glm::vec3& camUp,
              float fovYDeg, float aspect, float nearClip, const glm::vec3& sunDirection);

  void beginCascade(int i) const;

  GLuint depthTexture(int i) const { return depth_[i]; }
  const glm::mat4& viewProj(int i) const { return viewProj_[i]; }
  // (start, end) view-space depth each cascade is responsible for, used by
  // the main shader to pick which cascade — and how much to fade into the
  // next — a fragment falls in.
  glm::vec2 range(int i) const { return ranges_[i]; }
  int mapSize(int i) const { return mapSizes_[i]; }
  float fadeMetres() const { return fade_; }

private:
  std::array<GLuint, CASCADES> fbo_{}, depth_{};
  std::array<int, CASCADES> mapSizes_{};
  std::array<glm::mat4, CASCADES> viewProj_{};
  std::array<glm::vec2, CASCADES> ranges_{};
  float splits_[CASCADES + 1] = {};
  float fade_ = 2.5f;
  float lambda_ = 0.7f;     // blend between log and uniform splits
  float backOff_ = 90.0f;   // how far behind the slice the light sits
};
