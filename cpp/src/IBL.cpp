#include "IBL.h"
#include "Mesh.h"
#include <cmath>
#include <vector>

namespace {
struct Panel { glm::vec3 colour; float intensity; glm::vec3 pos; glm::vec3 scale; };
}

void IBL::build(int faceSize) {
  captureShader_.load("shaders/ibl_capture.vert", "shaders/ibl_capture.frag");

  glGenTextures(1, &cubemap_);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);
  for (int i = 0; i < 6; i++) {
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F, faceSize, faceSize, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &captureFbo_);
  glGenRenderbuffers(1, &captureRbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, captureFbo_);
  glBindRenderbuffer(GL_RENDERBUFFER, captureRbo_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRbo_);

  // The room: a station-like preset lifted straight from envmap.js's
  // "station" entry — what is overhead, what is underfoot, and the two or
  // three coloured things big enough to show up in a reflection.
  std::vector<Panel> panels = {
    {{0.227f, 0.290f, 0.369f}, 1.0f, {0, 0, 0}, {20, 20, 20}},       // sky
    {{0.078f, 0.102f, 0.133f}, 1.0f, {0, -10.5f, 0}, {20, 1, 20}},   // ground
    {{1.0f, 0.953f, 0.878f}, 5.5f, {0, 9.2f, 0}, {9, 0.3f, 9}},      // key light, overhead
    {{0.184f, 0.455f, 0.784f}, 3.0f, {0, -6.8f, 1.6f}, {7, 5, 7}},   // Earthlight, underneath
    {{0.369f, 0.918f, 1.0f}, 1.4f, {-8, 1.2f, -3.2f}, {7, 5, 7}},    // cyan accent
    {{1.0f, 0.706f, 0.329f}, 1.2f, {8, 1.6f, 3.2f}, {7, 5, 7}},      // amber accent
  };

  Mesh box = Mesh::box(1.0f, 1.0f, 1.0f);

  glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 60.0f);
  glm::vec3 dirs[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  glm::vec3 ups[6]  = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};

  glViewport(0, 0, faceSize, faceSize);
  glDisable(GL_CULL_FACE);  // the camera sits inside these panels
  captureShader_.use();
  captureShader_.set("uProj", proj);

  for (int f = 0; f < 6; f++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, cubemap_, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f), dirs[f], ups[f]);
    captureShader_.set("uView", view);
    for (auto& p : panels) {
      glm::mat4 model = glm::translate(glm::mat4(1.0f), p.pos);
      model = glm::scale(model, p.scale);
      captureShader_.set("uModel", model);
      captureShader_.set("uColour", p.colour * p.intensity);
      box.draw();
    }
  }
  box.destroy();

  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  maxMip_ = (int)std::floor(std::log2((float)faceSize));

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glEnable(GL_CULL_FACE);
  glCheck("IBL::build");
}

void IBL::destroy() {
  if (cubemap_) glDeleteTextures(1, &cubemap_);
  if (captureRbo_) glDeleteRenderbuffers(1, &captureRbo_);
  if (captureFbo_) glDeleteFramebuffers(1, &captureFbo_);
}
