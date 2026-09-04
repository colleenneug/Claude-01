#include "Hud.h"
#include <algorithm>

void Hud::create() {
  shader_.load("shaders/hud.vert", "shaders/hud.frag");
  float verts[] = {0, 0, 1, 0, 1, 1, 0, 1};
  unsigned idx[] = {0, 1, 2, 0, 2, 3};
  GLuint ebo;
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
  glBindVertexArray(0);
}

void Hud::destroy() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

void Hud::begin(int screenW, int screenH) {
  screenW_ = screenW; screenH_ = screenH;
  glDisable(GL_DEPTH_TEST);
  // The vertex shader flips Y (screen space is Y-down, NDC is Y-up), which
  // reverses each quad's winding to clockwise — and Renderer::create()
  // leaves GL_CULL_FACE enabled globally for the 3D pass, with the GL
  // default front face (CCW). Left enabled here, every single HUD rect
  // was being silently back-face culled — nothing was ever actually
  // broken about the rects themselves, the whole pass was invisible.
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  shader_.use();
  shader_.set("uScreen", glm::vec2((float)screenW, (float)screenH));
  glBindVertexArray(vao_);
}

void Hud::rect(float x, float y, float w, float h, glm::vec4 colour) {
  shader_.set("uRect", glm::vec4(x, y, w, h));
  shader_.set("uColor", colour);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

void Hud::end() {
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}

void Hud::draw(int screenW, int screenH, float hpFrac, float ammoFrac, int ammoInMag, int magSize,
               bool reloading, float reloadFrac, float hitMarkerT, float damageFlashT,
               float waveFrac, bool bossAlive, float bossHpFrac, bool missionComplete, bool missionFailed) {
  begin(screenW, screenH);
  float cx = screenW * 0.5f, cy = screenH * 0.5f;

  // ---- crosshair: four ticks with a fixed gap, brighter and squarer for
  // a moment after a confirmed hit.
  bool hit = hitMarkerT > 0.0f;
  glm::vec4 xcol = hit ? glm::vec4(1.0f, 0.85f, 0.3f, std::min(1.0f, hitMarkerT * 3.0f))
                       : glm::vec4(0.85f, 0.95f, 1.0f, 0.85f);
  float gap = 9.0f, len = hit ? 8.0f : 6.0f, thick = 2.0f;
  rect(cx - gap - len, cy - thick * 0.5f, len, thick, xcol);
  rect(cx + gap, cy - thick * 0.5f, len, thick, xcol);
  rect(cx - thick * 0.5f, cy - gap - len, thick, len, xcol);
  rect(cx - thick * 0.5f, cy + gap, thick, len, xcol);

  // ---- health bar, bottom-left
  float bx = 28, by = screenH - 54, bw = 260, bh = 18;
  rect(bx - 3, by - 3, bw + 6, bh + 6, glm::vec4(0, 0, 0, 0.45f));
  rect(bx, by, bw, bh, glm::vec4(0.12f, 0.03f, 0.03f, 0.9f));
  glm::vec3 hpCol = hpFrac > 0.5f ? glm::vec3(0.55f, 0.85f, 0.45f)
                   : hpFrac > 0.25f ? glm::vec3(0.9f, 0.75f, 0.25f)
                                    : glm::vec3(0.9f, 0.25f, 0.2f);
  rect(bx, by, bw * std::clamp(hpFrac, 0.0f, 1.0f), bh, glm::vec4(hpCol, 0.95f));

  // ---- ammo pips, bottom-right — one small rect per round in the mag,
  // capped so a huge magazine doesn't paint a wall of pips.
  int shown = std::min(magSize, 30);
  float pipW = 6, pipGap = 3, totalW = shown * (pipW + pipGap) - pipGap;
  float px0 = screenW - 28 - totalW, py0 = screenH - 54;
  int litCount = magSize > 0 ? (int)std::round((float)ammoInMag / magSize * shown) : 0;
  for (int i = 0; i < shown; i++) {
    bool lit = i < litCount;
    rect(px0 + i * (pipW + pipGap), py0, pipW, 22,
         lit ? glm::vec4(0.9f, 0.85f, 0.5f, 0.95f) : glm::vec4(0.2f, 0.2f, 0.22f, 0.6f));
  }
  if (reloading) {
    rect(px0, py0 + 26, totalW, 4, glm::vec4(0.1f, 0.1f, 0.1f, 0.6f));
    rect(px0, py0 + 26, totalW * std::clamp(reloadFrac, 0.0f, 1.0f), 4, glm::vec4(1.0f, 0.8f, 0.4f, 0.9f));
  }

  // ---- wave progress, top-centre
  float wx = cx - 160, wy = 22, ww = 320, wh = 10;
  rect(wx - 2, wy - 2, ww + 4, wh + 4, glm::vec4(0, 0, 0, 0.4f));
  rect(wx, wy, ww, wh, glm::vec4(0.1f, 0.12f, 0.14f, 0.85f));
  rect(wx, wy, ww * std::clamp(waveFrac, 0.0f, 1.0f), wh, glm::vec4(0.35f, 0.7f, 0.9f, 0.9f));

  if (bossAlive) {
    float bwx = cx - 220, bwy = 40, bww = 440, bwh = 14;
    rect(bwx - 2, bwy - 2, bww + 4, bwh + 4, glm::vec4(0, 0, 0, 0.45f));
    rect(bwx, bwy, bww, bwh, glm::vec4(0.15f, 0.05f, 0.05f, 0.9f));
    rect(bwx, bwy, bww * std::clamp(bossHpFrac, 0.0f, 1.0f), bwh, glm::vec4(0.85f, 0.2f, 0.25f, 0.95f));
  }

  // ---- full-screen feedback: damage flash, mission complete/fail tint
  if (damageFlashT > 0.0f) {
    rect(0, 0, (float)screenW, (float)screenH, glm::vec4(0.5f, 0.02f, 0.02f, std::min(0.5f, damageFlashT)));
  }
  if (missionComplete) {
    rect(0, 0, (float)screenW, (float)screenH, glm::vec4(0.15f, 0.5f, 0.3f, 0.18f));
  } else if (missionFailed) {
    rect(0, 0, (float)screenW, (float)screenH, glm::vec4(0.5f, 0.1f, 0.1f, 0.22f));
  }

  end();
}
