#include "IBL.h"
#include "Mesh.h"
#include <cmath>
#include <vector>

namespace {
struct Panel { glm::vec3 colour; float intensity; glm::vec3 pos; glm::vec3 scale; };

const glm::vec3 kDirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
const glm::vec3 kUps[6]  = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};

glm::mat4 faceProj() { return glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 60.0f); }
glm::mat4 faceView(int f) { return glm::lookAt(glm::vec3(0.0f), kDirs[f], kUps[f]); }

GLuint makeCube(int size, int mips) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
  for (int m = 0; m < mips; m++) {
    int s = std::max(1, size >> m);
    for (int i = 0; i < 6; i++) {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m, GL_RGBA16F, s, s, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, mips - 1);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  return tex;
}

// Every pass in here changes the viewport, the bound framebuffer and a few
// enables. Leaving any of them changed is a bug even while every caller
// happens to paper over it — see "The viewport bug" in docs/NATIVE_RENDERER.md,
// which is this file's own history. So the state is saved once on the way in
// and put back on the way out, whatever happened in between.
struct StateGuard {
  GLint viewport[4] = {0, 0, 0, 0};
  GLint fbo = 0;
  GLboolean cull = GL_FALSE, depth = GL_FALSE;
  StateGuard() {
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    cull = glIsEnabled(GL_CULL_FACE);
    depth = glIsEnabled(GL_DEPTH_TEST);
  }
  ~StateGuard() {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  }
};
}  // namespace

void IBL::create(int faceSize) {
  faceSize_ = faceSize;
  roomShader_.load("shaders/ibl_capture.vert", "shaders/ibl_capture.frag");
  skyShader_.load("shaders/fullscreen.vert", "shaders/ibl_sky.frag");
  prefilterShader_.load("shaders/fullscreen.vert", "shaders/ibl_prefilter.frag");
  irradianceShader_.load("shaders/fullscreen.vert", "shaders/ibl_irradiance.frag");

  // Without this a cube map is six separate pictures, and a lookup that
  // lands near an edge filters against that face alone: every face boundary
  // shows as a faint line across anything smooth and shiny.
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

  int envMips = (int)std::floor(std::log2((float)faceSize_)) + 1;
  env_ = makeCube(faceSize_, envMips);
  spec_ = makeCube(faceSize_, kSpecMips);
  irradiance_ = makeCube(kIrradianceSize, 1);

  glGenFramebuffers(1, &captureFbo_);
  glGenRenderbuffers(1, &captureRbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
  glBindRenderbuffer(GL_RENDERBUFFER, captureRbo_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize_, faceSize_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRbo_);
  // The filter passes draw into mips smaller than the capture's depth
  // buffer, and need no depth at all — so they get a framebuffer without one
  // rather than relying on mismatched attachment sizes being tolerated.
  glGenFramebuffers(1, &filterFbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  holding_ = Holding::Nothing;
  glCheck("IBL::create");
}

void IBL::useRoom(GLuint fullscreenVao) {
  if (holding_ == Holding::Room) return;
  StateGuard guard;
  captureRoom();
  filter(fullscreenVao);
  holding_ = Holding::Room;
}

void IBL::useSky(const Sky& sky, GLuint fullscreenVao) {
  if (holding_ == Holding::Sky && sky == sky_) return;
  StateGuard guard;
  captureSky(sky, fullscreenVao);
  filter(fullscreenVao);
  holding_ = Holding::Sky;
  sky_ = sky;
}

void IBL::captureRoom() {
  // The room: what is overhead, what is underfoot, and the two or three
  // coloured things big enough to show up in a reflection.
  std::vector<Panel> panels = {
    {{0.227f, 0.290f, 0.369f}, 1.0f, {0, 0, 0}, {20, 20, 20}},       // sky
    {{0.078f, 0.102f, 0.133f}, 1.0f, {0, -10.5f, 0}, {20, 1, 20}},   // ground
    {{1.0f, 0.953f, 0.878f}, 5.5f, {0, 9.2f, 0}, {9, 0.3f, 9}},      // key light, overhead
    {{0.184f, 0.455f, 0.784f}, 3.0f, {0, -6.8f, 1.6f}, {7, 5, 7}},   // Earthlight, underneath
    {{0.369f, 0.918f, 1.0f}, 1.4f, {-8, 1.2f, -3.2f}, {7, 5, 7}},    // cyan accent
    {{1.0f, 0.706f, 0.329f}, 1.2f, {8, 1.6f, 3.2f}, {7, 5, 7}},      // amber accent
  };

  Mesh box = Mesh::box(1.0f, 1.0f, 1.0f);

  glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
  glViewport(0, 0, faceSize_, faceSize_);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);  // the camera sits inside these panels
  roomShader_.use();
  roomShader_.set("uProj", faceProj());

  for (int f = 0; f < 6; f++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, env_, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    roomShader_.set("uView", faceView(f));
    for (auto& p : panels) {
      glm::mat4 model = glm::translate(glm::mat4(1.0f), p.pos);
      model = glm::scale(model, p.scale);
      roomShader_.set("uModel", model);
      roomShader_.set("uColour", p.colour * p.intensity);
      box.draw();
    }
  }
  box.destroy();
  glCheck("IBL::captureRoom");
}

void IBL::captureSky(const Sky& sky, GLuint fullscreenVao) {
  glBindFramebuffer(GL_FRAMEBUFFER, filterFbo_);
  glViewport(0, 0, faceSize_, faceSize_);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  skyShader_.use();
  skyShader_.set("uSkyZenith", sky.zenith);
  skyShader_.set("uSkyHorizon", sky.horizon);
  skyShader_.set("uSkySunDir", sky.sunDir);
  skyShader_.set("uSkySunColour", sky.sunColour);
  skyShader_.set("uSkyGround", sky.ground);
  skyShader_.set("uCloudCover", sky.cloudCover);
  skyShader_.set("uSkyTime", 0.0f);
  skyShader_.set("uSkyIntensity", sky.intensity);
  glBindVertexArray(fullscreenVao);
  for (int f = 0; f < 6; f++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, env_, 0);
    skyShader_.set("uInvViewProj", glm::inverse(faceProj() * faceView(f)));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glCheck("IBL::captureSky");
}

void IBL::filter(GLuint fullscreenVao) {
  // The filters read the capture at whatever mip matches the size of the
  // lobe they are integrating, so it needs a mip chain of its own first.
  glBindTexture(GL_TEXTURE_CUBE_MAP, env_);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

  glBindFramebuffer(GL_FRAMEBUFFER, filterFbo_);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glBindVertexArray(fullscreenVao);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, env_);

  prefilterShader_.use();
  prefilterShader_.set("uEnv", 0);
  prefilterShader_.set("uEnvSize", (float)faceSize_);
  for (int mip = 0; mip < kSpecMips; mip++) {
    int size = std::max(1, faceSize_ >> mip);
    glViewport(0, 0, size, size);
    prefilterShader_.set("uRoughness", (float)mip / (float)(kSpecMips - 1));
    for (int f = 0; f < 6; f++) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, spec_, mip);
      prefilterShader_.set("uInvViewProj", glm::inverse(faceProj() * faceView(f)));
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
  }

  irradianceShader_.use();
  irradianceShader_.set("uEnv", 0);
  irradianceShader_.set("uEnvSize", (float)faceSize_);
  glViewport(0, 0, kIrradianceSize, kIrradianceSize);
  for (int f = 0; f < 6; f++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, irradiance_, 0);
    irradianceShader_.set("uInvViewProj", glm::inverse(faceProj() * faceView(f)));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glCheck("IBL::filter");
}

void IBL::destroy() {
  if (env_) glDeleteTextures(1, &env_);
  if (spec_) glDeleteTextures(1, &spec_);
  if (irradiance_) glDeleteTextures(1, &irradiance_);
  if (captureRbo_) glDeleteRenderbuffers(1, &captureRbo_);
  if (captureFbo_) glDeleteFramebuffers(1, &captureFbo_);
  if (filterFbo_) glDeleteFramebuffers(1, &filterFbo_);
  env_ = spec_ = irradiance_ = captureRbo_ = captureFbo_ = filterFbo_ = 0;
  holding_ = Holding::Nothing;
}
