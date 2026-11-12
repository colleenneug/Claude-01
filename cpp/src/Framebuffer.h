#pragma once
#include "Gl.h"

// A 2D render target: one colour attachment (optionally HDR half-float) and
// an optional depth texture (sampled directly by the depth-of-field and fog
// passes, so it's a texture rather than a renderbuffer whenever the caller
// needs to read it back).
class Framebuffer {
public:
  // What the colour attachment holds. Most targets are a picture (RGBA); the
  // occlusion and sun-shaft passes produce one number per pixel, and the
  // exposure meter one number per frame, and four channels for one number is
  // three channels of bandwidth spent on nothing.
  enum class Format { Rgba8, Rgba16f, R8, R16f };

  void create(int w, int h, bool hdr, bool depthTexture);
  // No depth attachment at all: a post-processing target that is only ever
  // drawn into with a fullscreen triangle has no use for one.
  void createColour(int w, int h, Format format, bool mipmapped = false);
  void resize(int w, int h);
  void bind() const;
  static void bindScreen(int w, int h);
  void destroy();

  GLuint colorTexture() const { return colorTex_; }
  GLuint depthTexture() const { return depthTex_; }
  int width() const { return w_; }
  int height() const { return h_; }

private:
  void rebuild();
  GLuint fbo_ = 0, colorTex_ = 0, depthTex_ = 0, depthRbo_ = 0;
  int w_ = 0, h_ = 0;
  bool hdr_ = false, wantDepthTex_ = false;
  Format format_ = Format::Rgba8;
  bool colourOnly_ = false, mipmapped_ = false;
};
