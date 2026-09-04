#pragma once
#include "Framebuffer.h"
#include "Shader.h"
#include <array>

// Five-level downsample / tent-upsample bloom, the same construction as the
// browser build's engine.js: a soft-knee bright pass, the thirteen-tap
// "Next Generation Post Processing" downsample filter (a plain box filter
// flickers on small bright things at these sizes), and a 3x3 tent upsample
// blended additively on the way back up so the result has a wide smooth
// skirt instead of a visible ring.
class Bloom {
public:
  static constexpr int LEVELS = 5;

  void create(int baseW, int baseH);
  void resize(int baseW, int baseH);
  void destroy();

  // sceneColor is the full-res HDR scene texture. Returns the level-0
  // mip, i.e. the finished bloom buffer to add into the composite.
  GLuint render(GLuint sceneColor, GLuint fullscreenTriVAO);

  int activeLevels = LEVELS;  // quality knob: fewer levels, cheaper bloom

private:
  std::array<Framebuffer, LEVELS> mips_;
  Shader bright_, down_, up_;
};
