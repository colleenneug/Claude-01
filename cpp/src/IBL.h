#pragma once
#include "Gl.h"
#include "Shader.h"

// Image-based lighting, built the same way as the browser build's
// fps/envmap.js: a small room of flat-lit panels — sky above, ground
// below, a bright key panel, a couple of coloured accents — captured into
// a cubemap. Without it every metal surface reflects nothing and reads as
// grey plastic; this is what makes it read as metal instead.
//
// The prefilter here is a box-filtered mip chain (glGenerateMipmap) sampled
// at a roughness-driven LOD, not full GGX importance sampling — a
// deliberately cheap approximation that is nonetheless a completely real,
// commonly shipped technique for a scene this low-frequency (a handful of
// large flat panels, no sharp reflected detail to lose). A full split-sum
// prefilter would be the natural next step if the reflections need to
// carry more detail than that.
class IBL {
public:
  void build(int faceSize = 128);
  void destroy();

  GLuint cubemap() const { return cubemap_; }
  int maxMipLevel() const { return maxMip_; }

private:
  GLuint cubemap_ = 0;
  GLuint captureFbo_ = 0, captureRbo_ = 0;
  int maxMip_ = 0;
  Shader captureShader_;
};
