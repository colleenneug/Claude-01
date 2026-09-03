#include "Bloom.h"

void Bloom::create(int baseW, int baseH) {
  bright_.load("shaders/fullscreen.vert", "shaders/bright.frag");
  down_.load("shaders/fullscreen.vert", "shaders/downsample.frag");
  up_.load("shaders/fullscreen.vert", "shaders/upsample.frag");
  resize(baseW, baseH);
}

void Bloom::resize(int baseW, int baseH) {
  int w = std::max(1, baseW / 2), h = std::max(1, baseH / 2);
  for (int i = 0; i < LEVELS; i++) {
    if (mips_[i].width() == 0) mips_[i].create(w, h, true, false);
    else mips_[i].resize(w, h);
    w = std::max(1, w / 2);
    h = std::max(1, h / 2);
  }
}

void Bloom::destroy() {
  for (auto& m : mips_) m.destroy();
}

GLuint Bloom::render(GLuint sceneColor, GLuint fullscreenTriVAO) {
  glBindVertexArray(fullscreenTriVAO);
  glDisable(GL_DEPTH_TEST);

  // ---- bright pass, straight into the top mip
  mips_[0].bind();
  bright_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneColor);
  bright_.set("tScene", 0);
  bright_.set("uThreshold", 1.45f);
  bright_.set("uKnee", 0.5f);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // ---- down the chain
  int levels = std::max(1, std::min((int)LEVELS, activeLevels));
  for (int i = 1; i < levels; i++) {
    mips_[i].bind();
    down_.use();
    glBindTexture(GL_TEXTURE_2D, mips_[i - 1].colorTexture());
    down_.set("tSource", 0);
    down_.set("uTexel", glm::vec2(1.0f / mips_[i - 1].width(), 1.0f / mips_[i - 1].height()));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  // ---- back up, additive tent blend. Each level contributes less than the
  // one below it (uStrength 0.62), so the summed levels converge on ~2.5x
  // the top level rather than 5x — without that the result is a haze over
  // the whole frame instead of a bloom around bright things.
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  up_.use();
  up_.set("uRadius", 1.0f);
  up_.set("uStrength", 0.62f);
  for (int i = levels - 1; i > 0; i--) {
    mips_[i - 1].bind();
    glBindTexture(GL_TEXTURE_2D, mips_[i].colorTexture());
    up_.set("tSource", 0);
    up_.set("uTexel", glm::vec2(1.0f / mips_[i].width(), 1.0f / mips_[i].height()));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);

  return mips_[0].colorTexture();
}
