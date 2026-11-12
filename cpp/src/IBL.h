#pragma once
#include "Gl.h"
#include "Shader.h"

// Image-based lighting: the light that is not the sun. Everything a surface
// can see that is not a lamp — the sky, the ground, the far wall — lights it
// a little, and shows in it if it is shiny. Without this every shadow is
// black and every metal is grey plastic.
//
// There are two things a probe can be a picture of:
//
//   the sky   — outdoors. Painted from the same sky function the sky pass
//               draws (shaders/common/sky.glsl), so the fill light in a
//               shadow is the colour of the sky above it, and a mission that
//               changes its sky changes its light with it.
//   the room  — indoors. A small set of flat-lit panels: a pale ceiling, a
//               dark deck, a bright key panel, two coloured accents. This is
//               what the Cradle is lit by.
//
// Either way the picture is then filtered twice (see filter()): into a
// specular chain, each mip the scene seen through a GGX lobe of one
// roughness, and into a small irradiance cube, the cosine-weighted hemisphere
// a matte surface is lit by. That is the split-sum construction; it replaced
// a box-filtered mip chain that did both jobs approximately.
class IBL {
public:
  struct Sky {
    glm::vec3 zenith{0.0f}, horizon{0.0f};
    glm::vec3 sunDir{0.0f, -1.0f, 0.0f}, sunColour{1.0f};
    glm::vec3 ground{0.0f};
    float cloudCover = 0.0f;
    float intensity = 1.0f;
    bool operator==(const Sky& o) const {
      return zenith == o.zenith && horizon == o.horizon && sunDir == o.sunDir &&
             sunColour == o.sunColour && ground == o.ground &&
             cloudCover == o.cloudCover && intensity == o.intensity;
    }
  };

  void create(int faceSize = 128);
  void destroy();

  // Both are no-ops when the probe already holds what is asked for, so the
  // renderer can call the right one every frame and pay only on a change.
  void useRoom(GLuint fullscreenVao);
  void useSky(const Sky& sky, GLuint fullscreenVao);

  GLuint specular() const { return spec_; }
  GLuint irradiance() const { return irradiance_; }
  // The roughest mip of the specular chain: roughness 1 samples this one.
  int maxMipLevel() const { return kSpecMips - 1; }

private:
  static constexpr int kSpecMips = 6;
  static constexpr int kIrradianceSize = 16;

  void captureRoom();
  void captureSky(const Sky& sky, GLuint fullscreenVao);
  void filter(GLuint fullscreenVao);

  enum class Holding { Nothing, Room, Sky };
  Holding holding_ = Holding::Nothing;
  Sky sky_;

  int faceSize_ = 128;
  GLuint env_ = 0;         // the raw capture, with a plain mip chain
  GLuint spec_ = 0;        // GGX-prefiltered, one roughness per mip
  GLuint irradiance_ = 0;  // cosine-convolved
  GLuint captureFbo_ = 0, captureRbo_ = 0, filterFbo_ = 0;
  Shader roomShader_, skyShader_, prefilterShader_, irradianceShader_;
};
