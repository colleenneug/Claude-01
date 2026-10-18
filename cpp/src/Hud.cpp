#include "Hud.h"
#include "Content.h"
#include "Font.h"
#include "Hub.h"
#include "Loadout.h"
#include "Post.h"
#include "Profile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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

  textShader_.load("shaders/hud_text.vert", "shaders/hud_text.frag");
  glGenVertexArrays(1, &textVao_);
  glGenBuffers(1, &textVbo_);
  glBindVertexArray(textVao_);
  glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
  glBindVertexArray(0);
}

void Hud::destroy() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (textVbo_) glDeleteBuffers(1, &textVbo_);
  if (textVao_) glDeleteVertexArrays(1, &textVao_);
  vbo_ = vao_ = textVbo_ = textVao_ = 0;
  textVboCapacity_ = 0;
}

float Hud::textWidth(const std::string& s, float scale) {
  if (s.empty()) return 0.0f;
  // 5 columns per glyph plus a 1-column gap, with no trailing gap.
  return (float)s.size() * 6.0f * scale - scale;
}

bool Hud::worldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                        int screenW, int screenH, glm::vec2& out) {
  glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
  if (clip.w <= 0.0001f) return false;
  glm::vec3 ndc = glm::vec3(clip) / clip.w;
  if (ndc.x < -1.4f || ndc.x > 1.4f || ndc.y < -1.4f || ndc.y > 1.4f) return false;
  out.x = (ndc.x * 0.5f + 0.5f) * (float)screenW;
  out.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)screenH;
  return true;
}

float Hud::wrappedCentered(float cx, float y, float maxWidth, const std::string& s,
                           float scale, glm::vec4 colour, float lineHeight) {
  // Lay the same lines out as wrapped(), but centre each one on `cx`. A
  // caption left-aligned under a letterbox reads as a subtitle track that
  // came loose.
  std::string line;
  size_t i = 0;
  std::vector<std::string> lines;
  while (i <= s.size()) {
    size_t sp = s.find(' ', i);
    std::string word = s.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
    std::string cand = line.empty() ? word : line + " " + word;
    if (!line.empty() && textWidth(cand, scale) > maxWidth) {
      lines.push_back(line);
      line = word;
    } else {
      line = cand;
    }
    if (sp == std::string::npos) break;
    i = sp + 1;
  }
  if (!line.empty()) lines.push_back(line);
  for (const std::string& l : lines) {
    textCentered(cx, y, l, scale, colour);
    y += lineHeight;
  }
  return y;
}

float Hud::wrapped(float x, float y, float maxWidth, const std::string& s,
                   float scale, glm::vec4 colour, float lineHeight) {
  std::string line;
  size_t i = 0;
  while (i <= s.size()) {
    // Take the next word, including the space that ended it.
    size_t sp = s.find(' ', i);
    std::string word = s.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
    std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && textWidth(candidate, scale) > maxWidth) {
      text(x, y, line, scale, colour);
      y += lineHeight;
      line = word;
    } else {
      line = candidate;
    }
    if (sp == std::string::npos) break;
    i = sp + 1;
  }
  if (!line.empty()) {
    text(x, y, line, scale, colour);
    y += lineHeight;
  }
  return y;
}

void Hud::text(float x, float y, const std::string& s, float scale, glm::vec4 colour) {
  for (char c : s) {
    const Glyph* g = findGlyph(c);
    if (g) {
      for (int row = 0; row < 7; row++) {
        uint8_t bits = g->rows[row];
        for (int col = 0; col < 5; col++) {
          if (!(bits & (1 << (4 - col)))) continue;
          float px = x + col * scale, py = y + row * scale;
          float x1 = px + scale, y1 = py + scale;
          const float quad[6][2] = {{px, py}, {x1, py}, {x1, y1}, {px, py}, {x1, y1}, {px, y1}};
          for (auto& v : quad) {
            textVerts_.push_back(v[0]);
            textVerts_.push_back(v[1]);
            textVerts_.push_back(colour.r);
            textVerts_.push_back(colour.g);
            textVerts_.push_back(colour.b);
            textVerts_.push_back(colour.a);
          }
        }
      }
    }
    x += 6.0f * scale;
  }
}

void Hud::textCentered(float cx, float y, const std::string& s, float scale, glm::vec4 colour) {
  text(cx - textWidth(s, scale) * 0.5f, y, s, scale, colour);
}

void Hud::flushText() {
  if (textVerts_.empty()) return;

  textShader_.use();
  textShader_.set("uScreen", glm::vec2((float)screenW_, (float)screenH_));
  glBindVertexArray(textVao_);
  glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
  // Grow the buffer when a frame needs more room than the last one did, but
  // reuse it (glBufferSubData) whenever it already fits, so a steady HUD
  // isn't reallocating GPU memory every single frame.
  size_t bytes = textVerts_.size() * sizeof(float);
  if (bytes > textVboCapacity_) {
    glBufferData(GL_ARRAY_BUFFER, bytes, textVerts_.data(), GL_DYNAMIC_DRAW);
    textVboCapacity_ = bytes;
  } else {
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, textVerts_.data());
  }
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(textVerts_.size() / 6));

  // Leave the rect shader bound: callers may still queue rect()s after a
  // flush in a later frame, and begin() rebinds anyway.
  shader_.use();
  glBindVertexArray(vao_);
}

void Hud::begin(int screenW, int screenH) {
  screenW_ = screenW; screenH_ = screenH;
  textVerts_.clear();
  // The HUD lays out in screen pixels, so it owns the viewport for its own
  // pass rather than trusting whatever the last pass left set — the Hub
  // screen doesn't run the 3D path that would otherwise reset it, and a
  // stale viewport from an off-screen pass squeezes the whole HUD into a
  // corner of the window.
  glViewport(0, 0, screenW, screenH);
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
  // Text goes last so it always sits on top of the bars and panels behind
  // it, and so the whole frame's glyphs ride in one buffer upload.
  flushText();
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}

void Hud::draw(int screenW, int screenH, const State& s) {
  begin(screenW, screenH);
  float cx = screenW * 0.5f, cy = screenH * 0.5f;
  float hpFrac = s.maxHp > 0.0f ? s.hp / s.maxHp : 0.0f;
  const glm::vec4 dim(0.62f, 0.68f, 0.74f, 0.85f);

  // ---- a cutscene owns the frame: bars in, caption under, and nothing
  // else. Drawing the ammo counter over an establishing shot is what makes a
  // cutscene look like the game paused rather than like a cut.
  if (s.inCutscene) {
    float f = std::clamp(s.cutsceneFade, 0.0f, 1.0f);
    float bar = screenH * 0.12f;
    rect(0, 0, (float)screenW, bar, glm::vec4(0, 0, 0, 1.0f));
    rect(0, screenH - bar, (float)screenW, bar, glm::vec4(0, 0, 0, 1.0f));
    // The picture itself fades up from black behind the bars.
    if (f < 0.999f) {
      rect(0, bar, (float)screenW, screenH - bar * 2.0f, glm::vec4(0, 0, 0, 1.0f - f));
    }
    if (!s.cutsceneCaption.empty()) {
      wrappedCentered(cx, screenH - bar + 22.0f, screenW * 0.72f, s.cutsceneCaption, 2.1f,
                      glm::vec4(0.92f, 0.95f, 1.0f, 0.95f * f), 26.0f);
    }
    // In the top bar, not the bottom one: a caption long enough to wrap onto
    // a second line lands exactly where this used to be, and the two drew
    // over each other.
    textCentered(cx, bar * 0.5f - 7.0f, "SPACE TO SKIP", 1.5f,
                 glm::vec4(0.55f, 0.60f, 0.68f, 0.7f * f));
    end();
    return;
  }

  // ---- crosshair: four ticks with a fixed gap, brighter and squarer for
  // a moment after a confirmed hit. Each tick is drawn over a slightly
  // larger dark one: the accent colour is near-white, and this game's
  // terrain is bright sunlit dust, against which an unoutlined white
  // crosshair disappears completely exactly when you need to aim.
  bool hit = s.hitMarkerT > 0.0f;
  glm::vec4 xcol = hit ? glm::vec4(1.0f, 0.85f, 0.3f, std::min(1.0f, s.hitMarkerT * 3.0f))
                       : glm::vec4(s.accent, 0.9f);
  glm::vec4 xout(0.0f, 0.0f, 0.0f, 0.55f);
  float gap = 9.0f, len = hit ? 8.0f : 6.0f, thick = 2.0f;
  auto tick = [&](float x, float y, float w, float h) {
    rect(x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, xout);
    rect(x, y, w, h, xcol);
  };
  tick(cx - gap - len, cy - thick * 0.5f, len, thick);
  tick(cx + gap, cy - thick * 0.5f, len, thick);
  tick(cx - thick * 0.5f, cy - gap - len, thick, len);
  tick(cx - thick * 0.5f, cy + gap, thick, len);
  // A centre dot, also outlined — the four ticks alone leave a hole right
  // where the shot actually goes.
  rect(cx - 1.5f, cy - 1.5f, 3.0f, 3.0f, xout);
  rect(cx - 0.5f, cy - 0.5f, 1.0f, 1.0f, xcol);

  // ---- pickup note, just under the crosshair where the eye already is
  if (s.pickupAlpha > 0.001f && !s.pickupNote.empty()) {
    float a = std::clamp(s.pickupAlpha, 0.0f, 1.0f);
    bool health = s.pickupNote.find("INTEGRITY") != std::string::npos;
    glm::vec4 col = health ? glm::vec4(0.45f, 0.95f, 0.55f, a) : glm::vec4(0.95f, 0.8f, 0.35f, a);
    textCentered(cx, cy + 34, s.pickupNote, 2.2f, col);
  }

  // ---- down. The screen has gone quiet and dropped to the floor, which
  // without this reads as a crash rather than as the harness working.
  if (s.downed) {
    rect(0, 0, (float)screenW, (float)screenH, glm::vec4(0.35f, 0.02f, 0.02f, 0.35f));
    textCentered(cx, cy - 50.0f, "DOWN", 6.0f, glm::vec4(1.0f, 0.86f, 0.82f, 0.95f));
    char dbuf[64];
    std::snprintf(dbuf, sizeof(dbuf), "TRAUMA HARNESS - %d LEFT", s.harnessLeft);
    textCentered(cx, cy + 16.0f, dbuf, 2.2f, glm::vec4(1.0f, 0.55f, 0.48f, 0.9f));
    float w = 260.0f;
    rect(cx - w * 0.5f, cy + 56.0f, w, 6, glm::vec4(0.2f, 0.05f, 0.05f, 0.9f));
    rect(cx - w * 0.5f, cy + 56.0f, w * std::clamp(1.0f - s.downedFor / std::max(0.01f, s.downedMax), 0.0f, 1.0f), 6,
         glm::vec4(1.0f, 0.5f, 0.42f, 0.95f));
  }

  // ---- what you are doing, under the mission name. One line that changes
  // as you cross the place, with the control it wants under it.
  if (!s.objective.empty()) {
    text(28, 108, "OBJECTIVE", 1.5f, glm::vec4(0.58f, 0.64f, 0.72f, 0.8f));
    text(28, 128, s.objective, 2.4f, glm::vec4(s.accent, 0.96f));
    if (!s.objectiveHint.empty())
      wrapped(28, 160, screenW * 0.42f, s.objectiveHint, 1.6f,
              glm::vec4(0.72f, 0.78f, 0.86f, 0.85f), 20.0f);
  }

  // ---- the tutorial's current step, high and centred where the eye goes
  // when it does not yet know where else to look.
  if (!s.tutorialPrompt.empty()) {
    float ty = screenH * 0.22f;
    float w = std::max(textWidth(s.tutorialPrompt, 4.0f), textWidth(s.tutorialHint, 1.8f)) + 56.0f;
    rect(cx - w * 0.5f, ty - 18, w, 86, glm::vec4(0.04f, 0.05f, 0.07f, 0.72f));
    textCentered(cx, ty, s.tutorialPrompt, 4.0f, glm::vec4(s.accent, 0.98f));
    if (!s.tutorialHint.empty())
      textCentered(cx, ty + 42, s.tutorialHint, 1.8f, glm::vec4(0.78f, 0.84f, 0.92f, 0.9f));
    // A bar rather than a tick: several steps are "keep doing this for a
    // moment" and want to show that they are filling.
    float pw = w - 40.0f;
    rect(cx - pw * 0.5f, ty + 62, pw, 4, glm::vec4(0.16f, 0.20f, 0.26f, 0.9f));
    rect(cx - pw * 0.5f, ty + 62, pw * std::clamp(s.tutorialProgress, 0.0f, 1.0f), 4,
         glm::vec4(s.accent, 0.95f));
  }

  // ---- health bar + readout, bottom-left
  // Raised to leave room underneath for the ability charge and the
  // doctrine line, which used to run off the bottom of the frame.
  float bx = 28, by = screenH - 84, bw = 260, bh = 18;
  rect(bx - 3, by - 3, bw + 6, bh + 6, glm::vec4(0, 0, 0, 0.45f));
  rect(bx, by, bw, bh, glm::vec4(0.12f, 0.03f, 0.03f, 0.9f));
  glm::vec3 hpCol = hpFrac > 0.5f ? s.accent
                   : hpFrac > 0.25f ? glm::vec3(0.9f, 0.75f, 0.25f)
                                    : glm::vec3(0.9f, 0.25f, 0.2f);
  rect(bx, by, bw * std::clamp(hpFrac, 0.0f, 1.0f), bh, glm::vec4(hpCol, 0.95f));

  // The barrier sits on top of the bar, in its own colour, scaled against the
  // same width — so a full barrier over half health reads as "you have more
  // than you look like you have" without pretending your maximum moved.
  if (s.overshield > 0.01f && s.overshieldMax > 0.01f && s.maxHp > 0.0f) {
    float frac = std::clamp(s.overshield / s.maxHp, 0.0f, 1.0f);
    rect(bx, by - 7, bw * frac, 5, glm::vec4(0.55f, 0.85f, 1.0f, 0.95f));
  }

  char buf[96];
  std::snprintf(buf, sizeof(buf), "%d / %d", (int)std::lround(s.hp), (int)std::lround(s.maxHp));
  text(bx, by - 16, buf, 2.0f, glm::vec4(hpCol, 0.95f));
  text(bx + bw - textWidth("INTEGRITY", 1.6f), by - 15, "INTEGRITY", 1.6f, dim);

  // Harness charges, as pips beside the health readout: how many more times
  // you can go down before the mission is over. Beside rather than under,
  // because under is where the ability bar and the doctrine line already are.
  for (int i = 0; i < s.harnessMax; i++) {
    bool left = i < s.harnessLeft;
    rect(bx + 130.0f + (float)i * 13.0f, by - 12, 9, 10,
         left ? glm::vec4(0.95f, 0.55f, 0.45f, 0.9f) : glm::vec4(0.25f, 0.16f, 0.15f, 0.8f));
  }

  // ---- field ability charge, a short bar under the health bar. Green and
  // labelled with its key when it is ready, dim and filling when it is not.
  {
    const float aw = 120.0f, ah = 8.0f;
    float ay = by + bh + 8.0f;
    rect(bx - 3, ay - 3, aw + 6, ah + 6, glm::vec4(0, 0, 0, 0.45f));
    rect(bx, ay, aw, ah, glm::vec4(0.08f, 0.10f, 0.13f, 0.9f));
    glm::vec4 col = s.abilityReady ? glm::vec4(0.45f, 0.95f, 0.65f, 0.95f)
                                   : glm::vec4(0.35f, 0.50f, 0.68f, 0.9f);
    rect(bx, ay, aw * std::clamp(s.abilityFrac, 0.0f, 1.0f), ah, col);
    std::string label = s.abilityReady ? "Q " + s.abilityName : s.abilityName;
    text(bx + aw + 10, ay - 1, label, 1.5f, s.abilityReady ? col : dim);
    if (!s.className.empty()) text(bx, ay + ah + 8, s.className, 1.5f, dim);
  }

  // ---- ammo pips + counts, bottom-right. One small rect per round in the
  // mag, capped so a huge magazine doesn't paint a wall of pips; the real
  // numbers sit above it now that there's text to print them with. None of
  // it while your hands are empty.
  if (!s.armed) {
    textCentered(screenW - 150.0f, screenH - 62.0f, "UNARMED", 2.2f,
                 glm::vec4(0.85f, 0.45f, 0.38f, 0.9f));
  } else {
  int shown = std::min(s.magSize, 30);
  float pipW = 6, pipGap = 3, totalW = shown * (pipW + pipGap) - pipGap;
  float px0 = screenW - 28 - totalW, py0 = screenH - 54;
  int litCount = s.magSize > 0 ? (int)std::round((float)s.ammoInMag / s.magSize * shown) : 0;
  for (int i = 0; i < shown; i++) {
    bool lit = i < litCount;
    rect(px0 + i * (pipW + pipGap), py0, pipW, 22,
         lit ? glm::vec4(s.accent, 0.95f) : glm::vec4(0.2f, 0.2f, 0.22f, 0.6f));
  }

  std::snprintf(buf, sizeof(buf), "%d", s.ammoInMag);
  float magW = textWidth(buf, 4.0f);
  float reserveX = screenW - 28;
  std::string reserveStr = "/ " + std::to_string(s.reserveAmmo);
  float resW = textWidth(reserveStr, 2.0f);
  text(reserveX - resW, py0 - 20, reserveStr, 2.0f, dim);
  // Both holsters, above the ammo counter: the one in your hands lit and
  // keyed, the stowed one dimmed with its key next to it. Showing only the
  // live weapon makes a second slot something you have to remember you have.
  if (!s.weaponName.empty()) {
    const std::string live = s.slot == 1 ? s.sidearmName : s.primaryName;
    const std::string stowed = s.slot == 1 ? s.primaryName : s.sidearmName;
    const char* liveKey = s.slot == 1 ? "2" : "1";
    const char* stowKey = s.slot == 1 ? "1" : "2";

    char row[96];
    std::snprintf(row, sizeof(row), "%s  %s", liveKey,
                  (live.empty() ? s.weaponName : live).c_str());
    float nameW = textWidth(row, 1.8f);
    text(reserveX - nameW, py0 - 52, row, 1.8f, glm::vec4(s.accent, 0.9f));

    if (!stowed.empty()) {
      std::snprintf(row, sizeof(row), "%s  %s", stowKey, stowed.c_str());
      float stowW = textWidth(row, 1.5f);
      text(reserveX - stowW, py0 - 72, row, 1.5f, glm::vec4(0.55f, 0.60f, 0.68f, 0.7f));
    }
  }
  text(reserveX - resW - 8 - magW, py0 - 30, buf, 4.0f,
       glm::vec4(s.ammoInMag == 0 ? glm::vec3(0.9f, 0.3f, 0.25f) : s.accent, 0.95f));

  if (s.reloading) {
    rect(px0, py0 + 26, totalW, 4, glm::vec4(0.1f, 0.1f, 0.1f, 0.6f));
    rect(px0, py0 + 26, totalW * std::clamp(s.reloadFrac, 0.0f, 1.0f), 4,
         glm::vec4(1.0f, 0.8f, 0.4f, 0.9f));
    text(px0, py0 + 34, "RELOADING", 1.8f, glm::vec4(1.0f, 0.8f, 0.4f, 0.9f));
  }

  // ---- mission name + wave progress, top-centre
  float wx = cx - 160, wy = 34, ww = 320, wh = 10;
  if (!s.missionName.empty()) {
    textCentered(cx, wy - 20, s.missionName, 2.0f, glm::vec4(s.accent, 0.9f));
  }
  rect(wx - 2, wy - 2, ww + 4, wh + 4, glm::vec4(0, 0, 0, 0.4f));
  rect(wx, wy, ww, wh, glm::vec4(0.1f, 0.12f, 0.14f, 0.85f));
  rect(wx, wy, ww * std::clamp(s.waveFrac, 0.0f, 1.0f), wh, glm::vec4(0.35f, 0.7f, 0.9f, 0.9f));
  std::snprintf(buf, sizeof(buf), "%d%%", (int)std::lround(std::clamp(s.waveFrac, 0.0f, 1.0f) * 100.0f));
  text(wx + ww + 10, wy, buf, 1.8f, dim);
  text(wx - 10 - textWidth("CLEARED", 1.8f), wy, "CLEARED", 1.8f, dim);

  if (s.bossAlive) {
    // Far enough below the wave bar that the boss's name has room to sit
    // above its own bar without landing on either.
    float bwx = cx - 220, bwy = 82, bww = 440, bwh = 14;
    rect(bwx - 2, bwy - 2, bww + 4, bwh + 4, glm::vec4(0, 0, 0, 0.45f));
    rect(bwx, bwy, bww, bwh, glm::vec4(0.15f, 0.05f, 0.05f, 0.9f));
    rect(bwx, bwy, bww * std::clamp(s.bossHpFrac, 0.0f, 1.0f), bwh,
         glm::vec4(0.85f, 0.2f, 0.25f, 0.95f));
    if (!s.bossName.empty()) {
      textCentered(cx, bwy - 20, s.bossName, 2.2f, glm::vec4(0.98f, 0.55f, 0.5f, 0.98f));
    }
  }

  }

  // ---- comms: the story, one staged line at a time, above the health bar
  if (s.commsAlpha > 0.001f && !s.commsLine.empty()) {
    float a = std::clamp(s.commsAlpha, 0.0f, 1.0f);
    const float scale = 2.0f, lineH = 24.0f;
    float speakerW = s.commsSpeaker.empty() ? 0.0f : textWidth(s.commsSpeaker + ":", scale) + 10.0f;
    // Wrapped, not clipped. The ark's story is written in sentences, and a
    // sentence at this scale is wider than the screen — the read-in lines ran
    // off the right edge mid-word before this.
    const float boxLeft = 24.0f, pad = 12.0f;
    float maxW = screenW - boxLeft - pad * 2.0f - 24.0f - speakerW;
    std::vector<std::string> lines;
    {
      std::string cur;
      size_t i = 0;
      while (i <= s.commsLine.size()) {
        size_t sp = s.commsLine.find(' ', i);
        std::string word = s.commsLine.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        std::string cand = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && textWidth(cand, scale) > maxW) {
          lines.push_back(cur);
          cur = word;
        } else {
          cur = cand;
        }
        if (sp == std::string::npos) break;
        i = sp + 1;
      }
      if (!cur.empty()) lines.push_back(cur);
    }

    float boxH = 12.0f + lineH * (float)lines.size();
    float ty = screenH - 118.0f - boxH;
    float widest = 0.0f;
    for (size_t i = 0; i < lines.size(); i++) {
      float w = textWidth(lines[i], scale) + (i == 0 ? speakerW : 0.0f);
      widest = std::max(widest, w);
    }
    rect(boxLeft, ty - 8, std::max(widest, 120.0f) + pad * 2.0f, boxH, glm::vec4(0, 0, 0, 0.42f * a));
    rect(boxLeft, ty - 8, 3, boxH, glm::vec4(s.accent, 0.9f * a));
    if (!s.commsSpeaker.empty()) {
      text(boxLeft + 12, ty, s.commsSpeaker + ":", scale, glm::vec4(s.accent, a));
    }
    for (size_t i = 0; i < lines.size(); i++) {
      text(boxLeft + 12 + (i == 0 ? speakerW : 0.0f), ty + lineH * (float)i, lines[i], scale,
           glm::vec4(0.92f, 0.95f, 1.0f, a));
    }
  }

  // ---- full-screen feedback: damage flash, mission complete/fail banner
  if (s.damageFlashT > 0.0f) {
    rect(0, 0, (float)screenW, (float)screenH,
         glm::vec4(0.5f, 0.02f, 0.02f, std::min(0.5f, s.damageFlashT)));
  }
  if (s.missionComplete || s.missionFailed) {
    bool won = s.missionComplete;
    rect(0, 0, (float)screenW, (float)screenH,
         won ? glm::vec4(0.15f, 0.5f, 0.3f, 0.18f) : glm::vec4(0.5f, 0.1f, 0.1f, 0.22f));
    glm::vec4 banner = won ? glm::vec4(0.6f, 1.0f, 0.75f, 0.98f) : glm::vec4(1.0f, 0.55f, 0.5f, 0.98f);
    rect(0, cy - 46, (float)screenW, 92, glm::vec4(0, 0, 0, 0.5f));
    textCentered(cx, cy - 28, won ? "MISSION COMPLETE" : "MISSION FAILED", 5.0f, banner);
    if (s.xpEarned > 0) {
      char xbuf[64];
      std::snprintf(xbuf, sizeof(xbuf), "+%d XP", s.xpEarned);
      textCentered(cx, cy + 18, xbuf, 2.4f, glm::vec4(0.62f, 0.88f, 1.0f, 0.98f));
      textCentered(cx, cy + 48, "PRESS ENTER TO RETURN", 2.0f, dim);
    } else {
      textCentered(cx, cy + 22, "PRESS ENTER TO RETURN", 2.0f, dim);
    }
  }

  // ---- where to go. Hidden in a cutscene, which has its own framing, and
  // once the mission is over and there is nowhere left to be.
  if (!s.inCutscene && !s.missionComplete && !s.missionFailed && !s.downed) {
    drawWaypoint(s.viewProj, s.eye, s.waypoint);
  }
  end();
}

void Hud::drawPromotion(int screenW, int screenH, const std::string& rankName,
                        const std::string& unlockLine, int stipend, float t) {
  if (t <= 0.0f || rankName.empty()) return;
  begin(screenW, screenH);

  const float a = std::clamp(t, 0.0f, 1.0f);   // the last second is the fade
  const float cx = screenW * 0.5f;
  const float top = screenH * 0.14f;

  rect(0, top, (float)screenW, 132, glm::vec4(0.02f, 0.05f, 0.08f, 0.82f * a));
  rect(0, top, (float)screenW, 2, glm::vec4(0.40f, 0.78f, 1.0f, 0.95f * a));
  rect(0, top + 130, (float)screenW, 2, glm::vec4(0.40f, 0.78f, 1.0f, 0.95f * a));

  textCentered(cx, top + 18, "PROMOTED", 2.0f, glm::vec4(0.62f, 0.88f, 1.0f, 0.9f * a));
  textCentered(cx, top + 44, rankName, 5.0f, glm::vec4(0.95f, 0.98f, 1.0f, 0.98f * a));
  if (!unlockLine.empty()) {
    wrappedCentered(cx, top + 92, screenW * 0.7f, unlockLine, 1.7f,
                    glm::vec4(0.70f, 0.80f, 0.90f, 0.9f * a), 22.0f);
  }
  if (stipend > 0) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "+%d CHITS", stipend);
    textCentered(cx, top + 114, buf, 1.8f, glm::vec4(0.95f, 0.85f, 0.35f, 0.95f * a));
  }

  end();
}

void Hud::drawSpace(int screenW, int screenH, const SpaceState& s) {
  begin(screenW, screenH);

  const glm::vec4 dim(0.58f, 0.66f, 0.76f, 0.9f);
  const glm::vec4 accent(s.accent, 0.95f);
  const glm::vec4 live(0.45f, 0.95f, 0.6f, 0.98f);
  float cx = screenW * 0.5f, cy = screenH * 0.5f;
  char buf[96];

  // Flight reticle: a ring of ticks rather than the weapon crosshair, so
  // the two modes never look like the same thing.
  glm::vec4 ring(s.accent, 0.5f);
  for (int i = 0; i < 4; i++) {
    float a = i * 1.5708f;
    float dx = std::cos(a) * 22.0f, dy = std::sin(a) * 22.0f;
    rect(cx + dx - 1.5f, cy + dy - 1.5f, 3.0f, 3.0f, ring);
  }
  rect(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, accent);

  // Throttle readout, bottom-left.
  float bx = 40, by = screenH - 76, bw = 220, bh = 12;
  text(bx, by - 18, "THRUST", 1.8f, dim);
  rect(bx, by, bw, bh, glm::vec4(0.10f, 0.12f, 0.15f, 0.8f));
  float frac = s.maxSpeed > 0.0f ? std::clamp(s.speed / s.maxSpeed, 0.0f, 1.0f) : 0.0f;
  rect(bx, by, bw * frac, bh, accent);
  std::snprintf(buf, sizeof(buf), "%d U/S", (int)std::lround(s.speed));
  text(bx + bw + 12, by - 2, buf, 2.0f, accent);

  // Nearest destination, top-centre.
  if (!s.nearestName.empty()) {
    textCentered(cx, 40, s.nearestName, 2.6f, accent);
    std::snprintf(buf, sizeof(buf), "%d UNITS", (int)std::lround(s.nearestDistance));
    textCentered(cx, 74, buf, 2.0f, dim);
    if (s.missionCleared) textCentered(cx, 100, "CLEARED", 1.8f, live);
  }

  // Engage prompt.
  if (s.inRange && s.outOfReach) {
    // Said, rather than the prompt simply not appearing: a world you can fly
    // right up to and not land on is either a bug or a reason to buy a ship,
    // and the screen should say which.
    const char* verb = "OUT OF REACH FOR THIS SHIP";
    float w = textWidth(verb, 2.6f);
    rect(cx - w * 0.5f - 18, cy + 92, w + 36, 70, glm::vec4(0, 0, 0, 0.55f));
    textCentered(cx, cy + 104, verb, 2.6f, glm::vec4(0.98f, 0.55f, 0.40f, 0.98f));
    textCentered(cx, cy + 136, s.reachNeeded, 1.7f, dim);
  } else if (s.inRange) {
    const char* verb = s.isStation ? "PRESS E TO DOCK AT THE CRADLE" : "PRESS E TO LAND";
    float w = textWidth(verb, 2.6f);
    rect(cx - w * 0.5f - 18, cy + 92, w + 36, 44, glm::vec4(0, 0, 0, 0.5f));
    textCentered(cx, cy + 104, verb, 2.6f, live);
  }
  if (!s.shipName.empty()) text(bx, by - 44, s.shipName, 2.0f, accent);

  textCentered(cx, screenH - 42.0f,
               "W S THRUST   A D STRAFE   SPACE / CTRL UP DOWN   SHIFT BOOST   MOUSE STEER",
               1.8f, dim);

  end();
}

void Hud::drawSlotSelect(int screenW, int screenH, const SlotSummary slots[3], int selected,
                         int deletePending) {
  begin(screenW, screenH);

  const glm::vec4 dim(0.58f, 0.64f, 0.72f, 0.9f);
  const glm::vec4 bright(0.90f, 0.95f, 1.0f, 0.98f);
  const glm::vec4 gold(0.95f, 0.85f, 0.35f, 0.98f);
  const glm::vec4 live(0.45f, 0.95f, 0.6f, 0.98f);
  const glm::vec4 warn(0.98f, 0.45f, 0.4f, 0.98f);

  float cx = screenW * 0.5f;
  char buf[96];

  textCentered(cx, 92, "EREBUS CRADLE", 6.0f, bright);
  textCentered(cx, 150, "SELECT AN OPERATIVE RECORD", 2.0f, dim);

  float x = std::max(60.0f, cx - 320.0f);
  float w = std::min(640.0f, screenW - 120.0f);
  float y = 212.0f;
  const float rowH = 96.0f;

  for (int i = 0; i < 3; i++) {
    const SlotSummary& s = slots[i];
    bool sel = (i == selected);

    rect(x, y, w, rowH - 14, sel ? glm::vec4(0.16f, 0.22f, 0.30f, 0.85f)
                                 : glm::vec4(0.10f, 0.12f, 0.15f, 0.7f));
    rect(x, y, 4, rowH - 14, sel ? bright : glm::vec4(0.3f, 0.35f, 0.4f, 0.8f));
    if (sel) text(x - 26, y + 26, ">", 2.6f, bright);

    std::snprintf(buf, sizeof(buf), "SLOT %d", i + 1);
    text(x + 22, y + 14, buf, 2.4f, sel ? bright : dim);

    if (!s.used) {
      text(x + 22, y + 46, "EMPTY - START A NEW RECORD", 2.0f, dim);
    } else {
      std::snprintf(buf, sizeof(buf), "%d CHITS", s.chits);
      text(x + 22, y + 46, buf, 2.0f, gold);
      std::snprintf(buf, sizeof(buf), "%d CLEARED", s.missionsCleared);
      text(x + 200, y + 46, buf, 2.0f, live);
      if (!s.weaponName.empty()) text(x + 380, y + 46, s.weaponName, 2.0f, dim);
    }
    y += rowH;
  }

  if (deletePending >= 0) {
    std::snprintf(buf, sizeof(buf), "DELETE SLOT %d? Y TO CONFIRM, N TO CANCEL", deletePending + 1);
    textCentered(cx, screenH - 108.0f, buf, 2.4f, warn);
  } else {
    textCentered(cx, screenH - 108.0f, "1 2 3 SELECT   ENTER CONTINUE   D DELETE", 2.0f, dim);
  }

  end();
}

void Hud::drawStation(int screenW, int screenH, const StationState& s) {
  begin(screenW, screenH);

  const glm::vec4 dim(0.58f, 0.64f, 0.72f, 0.85f);
  const glm::vec4 bright(0.90f, 0.95f, 1.0f, 0.95f);
  float cx = screenW * 0.5f, cy = screenH * 0.5f;

  // A dot, not a crosshair: you are not aiming at anything in here.
  rect(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f, glm::vec4(0.85f, 0.92f, 1.0f, 0.55f));

  text(28, 34, s.title.empty() ? std::string("THE CRADLE") : s.title, 2.6f, bright);
  if (!s.deck.empty()) text(28, 66, s.deck, 1.8f, dim);

  // ---- nameplates over the crew with posts. Fading them with distance is
  // what keeps a concourse with five of them in it from becoming a wall of
  // text: the far ones say where somebody is, the near one says who.
  for (const StationState::Nameplate& n : s.nameplates) {
    glm::vec2 at;
    if (!worldToScreen(s.viewProj, n.worldPos, screenW, screenH, at)) continue;
    float dist = glm::length(n.worldPos - s.eye);
    float a = 1.0f - std::clamp((dist - 14.0f) / 34.0f, 0.0f, 0.75f);
    float scale = dist > 24.0f ? 1.5f : 1.9f;
    float w = std::max(textWidth(n.name, scale), textWidth(n.title, scale * 0.78f));
    rect(at.x - w * 0.5f - 8, at.y - 6, w + 16, scale > 1.6f ? 42.0f : 34.0f,
         glm::vec4(0.03f, 0.04f, 0.06f, 0.55f * a));
    textCentered(at.x, at.y, n.name, scale, glm::vec4(n.colour, 0.95f * a));
    if (!n.flag.empty()) {
      // Never faded as hard as the name: "somebody owes you money" is the
      // one thing on a nameplate worth reading from across the concourse.
      const float fa = std::max(a, 0.75f);
      const glm::vec4 fc = n.flagReady ? glm::vec4(0.98f, 0.84f, 0.30f, 0.98f * fa)
                                       : glm::vec4(0.55f, 0.85f, 1.0f, 0.85f * fa);
      const float fw = textWidth(n.flag, scale * 0.8f);
      rect(at.x - fw * 0.5f - 6, at.y - scale * 13.0f - 4, fw + 12, scale * 9.0f + 6,
           glm::vec4(0.03f, 0.04f, 0.06f, 0.6f * fa));
      textCentered(at.x, at.y - scale * 12.0f, n.flag, scale * 0.8f, fc);
    }
    if (!n.title.empty())
      textCentered(at.x, at.y + scale * 11.0f, n.title, scale * 0.78f,
                   glm::vec4(0.72f, 0.78f, 0.86f, 0.8f * a));
  }

  // ---- what you are standing in front of
  if (!s.terminalName.empty() && s.talkingTo.empty()) {
    glm::vec4 col(s.terminalColour, 0.98f);
    float w = textWidth(s.terminalName, 3.0f);
    rect(cx - w * 0.5f - 16, cy + 44, w + 32, 40, glm::vec4(0.05f, 0.07f, 0.10f, 0.72f));
    textCentered(cx, cy + 54, s.terminalName, 3.0f, col);
    if (!s.terminalLine.empty()) textCentered(cx, cy + 92, s.terminalLine, 1.7f, dim);
    textCentered(cx, cy + 116, "PRESS E TO " + (s.terminalAction.empty() ? std::string("USE")
                                                                         : s.terminalAction),
                 2.0f, col);
  }

  // ---- the conversation itself, along the bottom, where a subtitle goes
  if (!s.talkingTo.empty()) {
    glm::vec4 col(s.talkColour, 0.98f);
    float boxTop = screenH - 214.0f;
    rect(48, boxTop, screenW - 96.0f, 132.0f, glm::vec4(0.04f, 0.05f, 0.07f, 0.86f));
    rect(48, boxTop, 4, 132.0f, col);

    text(72, boxTop + 18, s.talkingTo, 2.6f, col);
    if (!s.talkTitle.empty())
      text(72 + textWidth(s.talkingTo, 2.6f) + 18, boxTop + 24, s.talkTitle, 1.6f, dim);
    wrapped(72, boxTop + 58, screenW - 192.0f, s.talkLine, 2.0f,
            glm::vec4(0.90f, 0.94f, 1.0f, 0.97f), 24.0f);

    char buf[48];
    std::snprintf(buf, sizeof(buf), "%d / %d", s.talkIndex + 1, s.talkCount);
    text(screenW - 72 - textWidth(buf, 1.6f), boxTop + 20, buf, 1.6f, dim);
    std::string more = s.talkIndex + 1 < s.talkCount ? "E CONTINUE   ESC LEAVE"
                       : s.talkOpensPost                ? "E TO THE DESK   ESC LEAVE"
                                                        : "E LEAVE";
    text(screenW - 72 - textWidth(more, 1.7f), boxTop + 104, more, 1.7f, col);
  }

  // ---- a bounty that came good on the mission you just got back from
  if (!s.bountyNote.empty() && s.bountyNoteT > 0.0f && s.talkingTo.empty()) {
    const float a = std::clamp(s.bountyNoteT, 0.0f, 1.0f);
    const float w = textWidth(s.bountyNote, 2.0f);
    rect(cx - w * 0.5f - 18, 104, w + 36, 36, glm::vec4(0.06f, 0.05f, 0.02f, 0.8f * a));
    rect(cx - w * 0.5f - 18, 138, w + 36, 2, glm::vec4(0.98f, 0.84f, 0.30f, 0.9f * a));
    textCentered(cx, 114, s.bountyNote, 2.0f, glm::vec4(0.98f, 0.86f, 0.40f, 0.98f * a));
  }

  textCentered(cx, screenH - 40.0f,
               s.talkingTo.empty()
                   ? (s.canUndock ? "WASD MOVE   SHIFT SPRINT   SPACE JUMP   E USE   Q AIRLOCK"
                                  : "WASD MOVE   SHIFT SPRINT   SPACE JUMP   E USE")
                   : "E CONTINUE   ESC LEAVE",
               1.9f, dim);
  // ---- where to go next. Not while somebody is talking to you: the arrow is
  // for walking, and the conversation is the thing you walked to.
  if (s.talkingTo.empty()) {
    drawWaypoint(s.viewProj, s.eye, s.waypoint);
    if (!s.nextStep.empty()) {
      const glm::vec4 gold(1.0f, 0.82f, 0.30f, 0.95f);
      text(28, 92, s.nextStep, 1.7f, gold);
    }
  }
  end();
}

void Hud::drawLoadout(int screenW, int screenH, const Content& content,
                      const Profile& profile, const Loadout& loadout) {
  begin(screenW, screenH);

  const glm::vec4 dim(0.58f, 0.64f, 0.72f, 0.9f);
  const glm::vec4 bright(0.90f, 0.95f, 1.0f, 0.98f);
  const glm::vec4 gold(0.95f, 0.85f, 0.35f, 0.98f);
  const glm::vec4 equippedCol(0.45f, 0.95f, 0.60f, 0.98f);
  const glm::vec4 ownedCol(0.45f, 0.70f, 1.00f, 0.95f);
  const glm::vec4 lockedCol(0.55f, 0.58f, 0.62f, 0.85f);
  const glm::vec4 rankCol(0.72f, 0.60f, 0.95f, 0.95f);
  const glm::vec4 poorCol(0.85f, 0.35f, 0.32f, 0.85f);

  // A wash rather than a blackout: what is underneath is context — the room
  // you are in, or the thing about to shoot you — and it should stay legible.
  rect(0, 0, (float)screenW, (float)screenH, glm::vec4(0.02f, 0.03f, 0.05f, 0.82f));

  const float cx = screenW * 0.5f;
  char buf[128];

  // ---- masthead
  text(48, 40, "KIT", 4.0f, bright);
  text(48, 78, "CHANGE WHAT YOU ARE CARRYING", 1.8f, dim);
  {
    // The collection: how many of the weapons that exist this record has.
    // Most of them are not on sale anywhere, and this is the line that says
    // there is more out there.
    std::snprintf(buf, sizeof(buf), "WEAPONS %d / %d", weaponsOwned(profile, content),
                  (int)content.weaponIds().size());
    text(48 + textWidth("KIT", 4.0f) + 28, 48, buf, 1.8f, gold);
  }

  std::snprintf(buf, sizeof(buf), "%d CHITS", profile.chits);
  float chitsW = textWidth(buf, 2.6f);
  text(screenW - 48 - chitsW, 42, buf, 2.6f, gold);

  const int rankIdx = content.rankIndexForXp(profile.xp);
  if (rankIdx >= 0) {
    const std::string& rname = content.ranks()[(size_t)rankIdx].name;
    float rw = textWidth(rname, 1.9f);
    text(screenW - 48 - rw, 72, rname, 1.9f, glm::vec4(0.62f, 0.88f, 1.0f, 0.98f));
  }
  rect(48, 100, screenW - 96.0f, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.5f));

  // ---- four columns. Equal widths across the usable span, so the layout
  // does not need per-column tuning when a content drop adds a fifth weapon.
  const char* headings[] = {"PRIMARY", "SIDEARM", "ARMOUR", "SHADER"};
  const float left = 48.0f;
  const float span = (screenW - 96.0f) / 4.0f;
  const float rowH = 24.0f;

  for (int c = 0; c < 4; c++) {
    const Loadout::Column col = (Loadout::Column)c;
    const std::vector<std::string>& list = loadout.ids(col);
    const bool active = loadout.column() == col;
    const float x = left + span * (float)c;
    const float colRight = x + span - 18.0f;

    // The active column is lit and backed; the others are dimmed but still
    // readable, because half the reason to open this is to compare.
    // Long columns scroll: as many rows as fit above the stat line, with the
    // caret kept in the middle of the window.
    const int fit = std::max(4, (int)((screenH - 154.0f - 150.0f) / rowH));
    const int sel = loadout.row(col);
    int first = 0;
    if ((int)list.size() > fit) {
      first = std::clamp(sel - fit / 2, 0, (int)list.size() - fit);
    }
    const int lastRow = std::min((int)list.size(), first + fit);
    if (active) {
      rect(x - 8, 116, span - 6.0f,
           34.0f + rowH * (float)std::max(1, lastRow - first),
           glm::vec4(0.10f, 0.14f, 0.20f, 0.85f));
    }
    const float fade = active ? 1.0f : 0.45f;

    glm::vec4 head = active ? glm::vec4(0.80f, 0.90f, 1.0f, 0.98f)
                            : glm::vec4(0.70f, 0.78f, 0.88f, 0.55f);
    text(x, 126, headings[c], 1.9f, head);

    float y = 154.0f;
    if (first > 0) text(colRight - textWidth("MORE", 1.3f), 128, "MORE", 1.3f, dim);
    for (size_t i = (size_t)first; i < (size_t)lastRow; i++) {
      const std::string& id = list[i];
      const Loadout::Availability a = loadout.availability(col, id);

      glm::vec4 col4 = a == Loadout::Availability::Equipped       ? equippedCol
                     : a == Loadout::Availability::Owned          ? ownedCol
                     : a == Loadout::Availability::RankLocked     ? rankCol
                     : a == Loadout::Availability::WrongDoctrine  ? rankCol
                     : a == Loadout::Availability::TooPoor        ? poorCol
                                                                  : lockedCol;
      col4.a *= fade;

      if (active && (int)i == loadout.row(col)) {
        rect(x - 4, y - 5, span - 16.0f, rowH - 2, glm::vec4(0.18f, 0.26f, 0.36f, 0.9f));
        text(x - 2, y, ">", 1.9f, bright);
      }

      std::string name = id;
      if (col == Loadout::Column::Primary || col == Loadout::Column::Sidearm) {
        if (const WeaponDef* d = content.weapon(id)) name = d->name;
      } else if (col == Loadout::Column::Armour) {
        if (const ArmorDef* d = content.armor(id)) name = d->name;
      } else if (const CosmeticDef* d = content.cosmetic(id)) {
        name = d->name;
      }

      text(x + 14, y, name, 1.8f, col4);

      const std::string gate = loadout.gateLabel(col, id);
      if (!gate.empty()) {
        float gw = textWidth(gate, 1.4f);
        glm::vec4 gcol = a == Loadout::Availability::Equipped      ? equippedCol
                       : a == Loadout::Availability::Owned         ? ownedCol
                       : a == Loadout::Availability::Affordable    ? gold
                       : a == Loadout::Availability::TooPoor       ? poorCol
                                                                   : rankCol;
        gcol.a *= fade;
        text(colRight - gw, y + 2, gate, 1.4f, gcol);
      }
      y += rowH;
    }
    if (lastRow < (int)list.size()) text(colRight - textWidth("MORE", 1.3f), y, "MORE", 1.3f, dim);
  }

  // ---- the stat line for whatever the caret is on. Numbers, because
  // "MAUL-12" tells you nothing you did not already know and "8 x 17 at 11m"
  // tells you whether to take it.
  {
    const Loadout::Column col = loadout.column();
    const std::vector<std::string>& list = loadout.ids(col);
    if (!list.empty()) {
      const std::string& id = list[(size_t)std::clamp(loadout.row(col), 0, (int)list.size() - 1)];
      std::string line;
      if (col == Loadout::Column::Primary || col == Loadout::Column::Sidearm) {
        if (const WeaponDef* d = content.weapon(id)) {
          std::snprintf(buf, sizeof(buf),
                        "%.0f DAMAGE%s   x%.1f HEAD   %d ROUND MAG   %d RESERVE   %.0fM   %.2fS",
                        d->damage, d->pellets > 1 ? (" x" + std::to_string(d->pellets) + " PELLETS").c_str() : "",
                        d->headshotMultiplier, d->magSize, d->reserveAmmo, d->range, d->fireInterval);
          line = buf;
          if (d->pierce) line += "   PIERCES";
          if (!d->maker.empty()) line = d->maker + "   " + line;
          const std::string from = weaponSourceLabel(*d, content);
          if (!from.empty()) line += "   " + from;
        }
      } else if (col == Loadout::Column::Armour) {
        if (const ArmorDef* d = content.armor(id)) {
          std::snprintf(buf, sizeof(buf), "+%.0f INTEGRITY   %.0f%% DAMAGE ABSORBED",
                        d->hpBonus, d->damageReduction * 100.0f);
          line = buf;
        }
      } else {
        line = "HUD ACCENT ONLY - NO MECHANICAL EFFECT";
      }
      if (!line.empty()) {
        rect(48, screenH - 132.0f, screenW - 96.0f, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.4f));
        text(48, screenH - 118.0f, line, 1.6f, glm::vec4(0.72f, 0.80f, 0.90f, 0.92f));
      }
    }
  }

  // ---- what the last Enter did, fading out. A menu that silently refuses is
  // a menu you think is broken.
  if (!loadout.notice().empty() && loadout.noticeAge() < 3.2f) {
    float a = std::clamp(3.2f - loadout.noticeAge(), 0.0f, 1.0f);
    textCentered(cx, screenH - 88.0f, loadout.notice(), 2.0f,
                 glm::vec4(0.95f, 0.88f, 0.55f, 0.95f * a));
  }

  textCentered(cx, screenH - 44.0f,
               "A D OR LEFT RIGHT  CATEGORY    W S OR UP DOWN  SELECT    ENTER  EQUIP    G  CLOSE",
               1.7f, dim);
  // Said plainly, because it is a rule you will meet the hard way otherwise.
  textCentered(cx, screenH - 22.0f, "THE WORLD KEEPS RUNNING WHILE THIS IS OPEN", 1.4f,
               glm::vec4(0.85f, 0.55f, 0.48f, 0.8f));

  end();
}

void Hud::drawPost(int screenW, int screenH, const Content& content,
                   const Profile& profile, const Post& post) {
  const CrewDef* host = post.host();
  if (!host) return;
  begin(screenW, screenH);

  const glm::vec4 dim(0.58f, 0.64f, 0.72f, 0.9f);
  const glm::vec4 bright(0.90f, 0.95f, 1.0f, 0.98f);
  const glm::vec4 gold(0.95f, 0.85f, 0.35f, 0.98f);
  const glm::vec4 green(0.45f, 0.95f, 0.60f, 0.98f);
  const glm::vec4 blue(0.45f, 0.70f, 1.00f, 0.95f);
  const glm::vec4 grey(0.55f, 0.58f, 0.62f, 0.85f);
  const glm::vec4 violet(0.72f, 0.60f, 0.95f, 0.95f);
  const glm::vec4 red(0.85f, 0.35f, 0.32f, 0.9f);
  const glm::vec4 accent(host->colour, 0.98f);

  rect(0, 0, (float)screenW, (float)screenH, glm::vec4(0.02f, 0.03f, 0.05f, 0.84f));
  const float cx = screenW * 0.5f;
  const float left = 48.0f, right = screenW - 48.0f;
  char buf[160];

  // ---- masthead: whose desk this is
  rect(left, 40, 6, 52, accent);
  text(left + 18, 40, host->name, 4.0f, accent);
  text(left + 18, 76, host->title, 1.8f, dim);

  std::snprintf(buf, sizeof(buf), "%d CHITS", profile.chits);
  text(right - textWidth(buf, 2.6f), 42, buf, 2.6f, gold);
  const int rankIdx = content.rankIndexForXp(profile.xp);
  if (rankIdx >= 0) {
    const std::string& rname = content.ranks()[(size_t)rankIdx].name;
    text(right - textWidth(rname, 1.9f), 72, rname, 1.9f, glm::vec4(0.62f, 0.88f, 1.0f, 0.98f));
  }
  rect(left, 104, right - left, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.5f));

  // ---- the two tabs
  int ready = 0;
  for (const std::string& id : post.bounties()) {
    if (bountyStatus(profile, content, id) == BountyStatus::Ready) ready++;
  }
  const std::string serviceTab = post.serviceRows() > 0 ? post.serviceName() : "";
  std::string bountyTab = "BOUNTIES";
  if (ready > 0) bountyTab += " - " + std::to_string(ready) + " READY";
  {
    float x = left;
    auto tab = [&](const std::string& label, bool active, glm::vec4 lit) {
      if (label.empty()) return;
      const float w = textWidth(label, 2.0f) + 28.0f;
      rect(x, 116, w, 32, active ? glm::vec4(0.12f, 0.17f, 0.24f, 0.95f)
                                 : glm::vec4(0.06f, 0.08f, 0.11f, 0.7f));
      if (active) rect(x, 146, w, 2, lit);
      text(x + 14, 125, label, 2.0f, active ? lit : glm::vec4(lit.r, lit.g, lit.b, 0.5f));
      x += w + 10.0f;
    };
    tab(serviceTab, post.tab() == Post::Tab::Service, accent);
    tab(bountyTab, post.tab() == Post::Tab::Bounties, ready > 0 ? gold : bright);
  }

  const float top = 172.0f;
  const float rowH = 46.0f;
  std::string footBlurb;

  auto caret = [&](float y, bool on) {
    if (!on) return;
    rect(left - 6, y - 8, right - left + 12, rowH - 6, glm::vec4(0.16f, 0.23f, 0.32f, 0.9f));
    text(left - 2, y, ">", 2.0f, bright);
  };

  if (post.tab() == Post::Tab::Service) {
    if (post.serviceIsCounter()) {
      // A counter: their own stock in the hub. What is on it is listed here
      // so it is clear before you open it that Ondo's rack is not Voss's.
      if (post.refused()) {
        text(left, top, post.refusal().heading, 2.6f, red);
        wrapped(left, top + 34, right - left, post.refusal().why, 1.7f, dim, 22.0f);
      } else {
        caret(top, true);
        const std::string door = host->shop == CrewShop::Gear      ? "BROWSE THE RACK"
                               : host->shop == CrewShop::Route     ? "OPEN THE ROUTE"
                                                                   : "READ THE BOARD";
        text(left + 18, top, door, 2.4f, accent);
        text(right - textWidth("ENTER", 1.8f), top + 4, "ENTER", 1.8f, gold);
      }
      float y = top + 70.0f;
      if (!host->stock.empty()) {
        text(left, y, "CARRIES", 1.6f, dim);
        y += 26.0f;
        std::string line;
        for (const std::string& id : host->stock) {
          std::string name;
          if (const WeaponDef* w = content.weapon(id)) name = w->name;
          else if (const ArmorDef* a = content.armor(id)) name = a->name;
          else if (const CosmeticDef* c = content.cosmetic(id)) name = c->name + " SHADER";
          else if (const MissionDef* m = content.mission(id)) name = m->name;
          else if (id == "track:earth") name = "THE STRIDER PROGRAMME";
          else if (id == "track:cradle") name = "THE ARK, ALL TWENTY-TWO SECTORS";
          else if (id.rfind("track:", 0) == 0) name = id.substr(6) + " ROUTE";
          else continue;
          line += (line.empty() ? "" : "   ") + name;
        }
        wrapped(left, y, right - left, line, 1.7f, glm::vec4(0.78f, 0.85f, 0.93f, 0.92f), 24.0f);
      }
    } else if (host->shop == CrewShop::Upgrades) {
      const std::vector<std::string>& ids = post.upgrades();
      float y = top;
      for (size_t i = 0; i < ids.size(); i++) {
        const UpgradeDef* u = content.upgrade(ids[i]);
        if (!u) continue;
        const bool on = (int)i == post.row();
        caret(y, on);
        const UpgradeStatus st = upgradeStatus(profile, content, ids[i]);
        glm::vec4 col = st == UpgradeStatus::Owned      ? green
                      : st == UpgradeStatus::Affordable ? bright
                      : st == UpgradeStatus::TooPoor    ? red
                                                        : grey;
        text(left + 18, y, u->name, 2.0f, col);
        text(left + 18, y + 20, upgradeEffectLabel(*u), 1.5f,
             glm::vec4(col.r, col.g, col.b, col.a * 0.75f));
        std::string tag;
        glm::vec4 tagCol = gold;
        switch (st) {
          case UpgradeStatus::Owned: tag = "ON YOUR RECORD"; tagCol = green; break;
          case UpgradeStatus::NeedsPrior: {
            const UpgradeDef* prior = content.upgrade(u->prerequisite);
            tag = "AFTER " + (prior ? prior->name : u->prerequisite);
            tagCol = grey;
            break;
          }
          case UpgradeStatus::RankLocked: {
            const RankDef* r = content.rank(u->rankRequired);
            tag = r ? r->name : "LOCKED";
            tagCol = violet;
            break;
          }
          case UpgradeStatus::TooPoor: tag = std::to_string(u->cost) + " CHITS"; tagCol = red; break;
          case UpgradeStatus::Affordable: tag = std::to_string(u->cost) + " CHITS"; break;
        }
        text(right - textWidth(tag, 1.7f), y + 4, tag, 1.7f, tagCol);
        if (on) footBlurb = u->blurb;
        y += rowH;
      }
    } else if (host->shop == CrewShop::Shipyard) {
      const std::vector<std::string>& ids = post.ships();
      float y = top;
      for (size_t i = 0; i < ids.size(); i++) {
        const ShipDef* d = content.ship(ids[i]);
        if (!d) continue;
        const bool on = (int)i == post.row();
        caret(y, on);
        const ShipStatus st = shipStatus(profile, content, ids[i]);
        glm::vec4 col = st == ShipStatus::Flying     ? green
                      : st == ShipStatus::Owned      ? blue
                      : st == ShipStatus::Affordable ? bright
                      : st == ShipStatus::TooPoor    ? red
                                                     : grey;
        text(left + 18, y, d->name, 2.0f, col);
        std::snprintf(buf, sizeof(buf), "%d U/S   BOOST X%.1f   HANDLING %.1f   REACH %d",
                      (int)std::lround(d->speed), d->boost, d->handling, (int)std::lround(d->reach));
        text(left + 18, y + 20, buf, 1.5f, glm::vec4(col.r, col.g, col.b, col.a * 0.75f));
        std::string tag;
        glm::vec4 tagCol = gold;
        switch (st) {
          case ShipStatus::Flying: tag = "YOUR SHIP"; tagCol = green; break;
          case ShipStatus::Owned: tag = "IN THE HANGAR"; tagCol = blue; break;
          case ShipStatus::NotRated: tag = d->starter ? "ISSUED AFTER THE CAMPAIGN" : "AFTER THE CAMPAIGN"; tagCol = grey; break;
          case ShipStatus::RankLocked: {
            const RankDef* r = content.rank(d->rankRequired);
            tag = r ? r->name : "LOCKED";
            tagCol = violet;
            break;
          }
          case ShipStatus::TooPoor: tag = std::to_string(d->cost) + " CHITS"; tagCol = red; break;
          case ShipStatus::Affordable: tag = std::to_string(d->cost) + " CHITS"; break;
        }
        text(right - textWidth(tag, 1.7f), y + 4, tag, 1.7f, tagCol);
        if (on) footBlurb = d->blurb;
        y += rowH;
      }
    } else if (host->shop == CrewShop::Appraisal) {
      // One list, two halves: the black market first (the Rook selling to
      // you), then the appraisal (you selling to the Rook). Scrolled, since
      // a record with a lot of spare gear can have more of it than fits.
      const std::vector<std::string>& market = post.blackMarket();
      const std::vector<Appraisal>& list = post.appraisals();
      const int total = post.serviceRows();
      const int fit = std::max(3, (int)((screenH - 160.0f - top) / rowH));
      const int first = total > fit ? std::clamp(post.row() - fit / 2, 0, total - fit) : 0;
      const int last = std::min(total, first + fit);
      float y = top;
      for (int r = first; r < last; r++) {
        caret(y, r == post.row());
        if (r < (int)market.size()) {
          const WeaponDef* w = content.weapon(market[(size_t)r]);
          if (!w) { y += rowH; continue; }
          const bool owned = profile.ownsWeapon(w->id);
          const bool rankOk = content.rankReached(w->rankRequired, profile.xp);
          const bool docOk = w->classRequired.empty() || w->classRequired == profile.classId;
          const bool rich = profile.chits >= w->cost;
          glm::vec4 col = owned ? green : (!rankOk || !docOk) ? grey : rich ? bright : red;
          text(left + 18, y, w->name, 2.0f, col);
          std::snprintf(buf, sizeof(buf), "BLACK MARKET - %s - TIER %d", w->maker.empty() ? "UNMARKED" : w->maker.c_str(), w->tier);
          text(left + 18, y + 20, buf, 1.5f, glm::vec4(0.82f, 0.62f, 1.0f, 0.8f));
          std::string tag;
          glm::vec4 tagCol = gold;
          if (owned) { tag = "IN YOUR KIT"; tagCol = green; }
          else if (!docOk) { tag = "NOT YOUR DOCTRINE"; tagCol = grey; }
          else if (!rankOk) { const RankDef* rk = content.rank(w->rankRequired); tag = rk ? rk->name : "LOCKED"; tagCol = violet; }
          else { tag = "BUY " + std::to_string(w->cost) + " CHITS"; tagCol = rich ? gold : red; }
          text(right - textWidth(tag, 1.7f), y + 4, tag, 1.7f, tagCol);
          if (r == post.row()) footBlurb = w->blurb.empty() ? "Nobody asks where it came from. That is the price." : w->blurb;
        } else if (list.empty()) {
          text(left + 18, y, "NOTHING ON YOU I WOULD TAKE", 2.0f, grey);
          text(left + 18, y + 20, "BUY SOMETHING, STOP USING IT, AND BRING IT TO ME", 1.5f, dim);
          if (r == post.row()) footBlurb = "Issue gear is worth nothing to anybody, and I do not buy what you are wearing.";
        } else {
          const Appraisal& a = list[(size_t)(r - (int)market.size())];
          const char* kind = a.kind == Appraisal::Kind::Weapon   ? "SELL - WEAPON"
                           : a.kind == Appraisal::Kind::Armour   ? "SELL - ARMOUR"
                                                                 : "SELL - SHADER";
          text(left + 18, y, a.name, 2.0f, bright);
          text(left + 18, y + 20, kind, 1.5f, dim);
          std::snprintf(buf, sizeof(buf), "SELL FOR %d CHITS", a.value);
          text(right - textWidth(buf, 1.7f), y + 4, buf, 1.7f, gold);
          if (r == post.row()) footBlurb = "Half what you paid. You can buy it back, at full price, from whoever sold it to you.";
        }
        y += rowH;
      }
    }
  } else {
    const std::vector<std::string>& ids = post.bounties();
    if (ids.empty()) {
      text(left, top, "NOTHING ON THE BOARD", 2.4f, grey);
      text(left, top + 34, "EVERYTHING THEY HAD FOR YOU HAS BEEN PAID", 1.7f, dim);
    }
    float y = top;
    for (size_t i = 0; i < ids.size(); i++) {
      const BountyDef* b = content.bounty(ids[i]);
      if (!b) continue;
      const bool on = (int)i == post.row();
      caret(y, on);
      const BountyStatus st = bountyStatus(profile, content, ids[i]);
      const Profile::ActiveBounty* a = profile.activeBounty(ids[i]);
      const int have = a ? a->progress : 0;

      glm::vec4 col = st == BountyStatus::Ready      ? gold
                    : st == BountyStatus::Active     ? blue
                    : st == BountyStatus::Available  ? bright
                                                     : grey;
      text(left + 18, y, b->name, 2.0f, col);
      std::snprintf(buf, sizeof(buf), "%d %s", b->count, bountyGoalLabel(*b, content).c_str());
      text(left + 18, y + 20, buf, 1.5f, glm::vec4(col.r, col.g, col.b, col.a * 0.75f));

      // Progress, as a bar: "7 of 12" is a number, but a bar three-quarters
      // full is a reason to fly one more mission.
      if (st == BountyStatus::Active || st == BountyStatus::Ready) {
        const float bw = 160.0f, bx = cx + 40.0f;
        rect(bx, y + 6, bw, 8, glm::vec4(0.12f, 0.15f, 0.20f, 0.9f));
        rect(bx, y + 6, bw * std::clamp((float)have / (float)b->count, 0.0f, 1.0f), 8, col);
        std::snprintf(buf, sizeof(buf), "%d/%d", have, b->count);
        text(bx + bw + 10, y + 4, buf, 1.5f, col);
      }

      std::string tag;
      glm::vec4 tagCol = gold;
      switch (st) {
        case BountyStatus::Ready: tag = "TURN IN"; break;
        case BountyStatus::Active: tag = "TAKEN"; tagCol = blue; break;
        case BountyStatus::Available: tag = "TAKE"; tagCol = bright; break;
        case BountyStatus::RankLocked: {
          const RankDef* r = content.rank(b->rankRequired);
          tag = r ? r->name : "LOCKED";
          tagCol = violet;
          break;
        }
        case BountyStatus::Done: tag = "PAID"; tagCol = green; break;
      }
      text(right - textWidth(tag, 1.7f), y, tag, 1.7f, tagCol);
      std::string pay = std::to_string(b->rewardChits) + " CHITS  " +
                        std::to_string(b->rewardXp) + " XP";
      if (!b->rewardItem.empty()) pay += "  + GEAR";
      text(right - textWidth(pay, 1.4f), y + 22, pay, 1.4f, glm::vec4(0.85f, 0.80f, 0.55f, 0.8f));

      if (on) {
        footBlurb = b->blurb;
        const size_t colon = b->rewardItem.find(':');
        if (colon != std::string::npos) {
          const std::string kind = b->rewardItem.substr(0, colon);
          const std::string what = b->rewardItem.substr(colon + 1);
          std::string item;
          if (kind == "weapon" && content.weapon(what)) item = content.weapon(what)->name;
          else if (kind == "armor" && content.armor(what)) item = content.armor(what)->name;
          else if (kind == "cosmetic" && content.cosmetic(what)) item = content.cosmetic(what)->name;
          if (!item.empty()) footBlurb += "  Pays in gear: " + item + ".";
        }
      }
      y += rowH;
    }
  }

  if (!footBlurb.empty()) {
    rect(left, screenH - 150.0f, right - left, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.4f));
    wrapped(left, screenH - 136.0f, right - left, footBlurb, 1.6f,
            glm::vec4(0.72f, 0.80f, 0.90f, 0.92f), 20.0f);
  }

  if (!post.notice().empty() && post.noticeAge() < 3.2f) {
    float a = std::clamp(3.2f - post.noticeAge(), 0.0f, 1.0f);
    textCentered(cx, screenH - 84.0f, post.notice(), 2.0f,
                 glm::vec4(0.95f, 0.88f, 0.55f, 0.95f * a));
  }

  textCentered(cx, screenH - 44.0f,
               "TAB OR A D  SWITCH    W S  SELECT    E OR ENTER  TAKE / TURN IN / BUY    ESC  LEAVE",
               1.7f, dim);
  textCentered(cx, screenH - 22.0f,
               "BOUNTIES COUNT ACROSS EVERY MISSION YOU FLY - COME BACK HERE TO BE PAID", 1.4f,
               glm::vec4(0.62f, 0.80f, 0.95f, 0.8f));
  end();
}

void Hud::drawCreate(int screenW, int screenH, const Content& content,
                     const std::vector<std::string>& classIds, int selected) {
  begin(screenW, screenH);

  const glm::vec4 dim(0.58f, 0.64f, 0.72f, 0.9f);
  const glm::vec4 bright(0.90f, 0.95f, 1.0f, 0.98f);

  float x = 64.0f;
  text(x, 46, "RECOVERY DIVISION", 2.0f, dim);
  text(x, 72, "SELECT A DOCTRINE", 4.2f, bright);
  rect(x, 126, screenW - 128.0f, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.5f));

  // Three columns, one per doctrine, so they are read against each other
  // rather than one at a time.
  const int n = (int)classIds.size();
  if (n <= 0) {
    textCentered(screenW * 0.5f, screenH * 0.5f, "NO DOCTRINES IN CONTENT/CLASSES", 2.4f, bright);
    end();
    return;
  }
  const float gap = 24.0f;
  const float colW = (screenW - 128.0f - gap * (n - 1)) / (float)n;

  for (int i = 0; i < n; i++) {
    const ClassDef* c = content.playerClass(classIds[i]);
    float cx = x + i * (colW + gap);
    bool on = i == selected;
    glm::vec3 accent = c ? c->accent : glm::vec3(0.8f, 0.85f, 0.92f);

    rect(cx, 152, colW, screenH - 240.0f,
         on ? glm::vec4(0.13f, 0.17f, 0.23f, 0.9f) : glm::vec4(0.07f, 0.08f, 0.11f, 0.7f));
    // A lit edge rather than a caret: three columns need the selection to
    // read from across the screen, not from one glyph.
    rect(cx, 152, on ? 5.0f : 2.0f, screenH - 240.0f, glm::vec4(accent, on ? 0.95f : 0.35f));

    float ty = 174.0f;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[%d]", i + 1);
    text(cx + 18, ty, buf, 1.8f, dim);
    text(cx + 60, ty - 4, c ? c->name : classIds[i], 3.0f, glm::vec4(accent, on ? 1.0f : 0.75f));
    ty += 34.0f;
    if (c) {
      text(cx + 18, ty, c->role, 1.7f, dim);
      ty += 28.0f;
      ty = wrapped(cx + 18, ty, colW - 36.0f, c->tagline, 1.7f,
                   glm::vec4(0.80f, 0.86f, 0.93f, on ? 0.95f : 0.6f), 20.0f);
      ty += 18.0f;

      text(cx + 18, ty, "ISSUED", 1.5f, dim);
      ty += 20.0f;
      const WeaponDef* w = content.weapon(c->weaponId);
      text(cx + 18, ty, w ? w->name : c->weaponId, 2.2f, glm::vec4(accent, on ? 0.95f : 0.6f));
      ty += 30.0f;

      text(cx + 18, ty, "ABILITY", 1.5f, dim);
      ty += 20.0f;
      text(cx + 18, ty, c->abilityName, 2.0f, glm::vec4(accent, on ? 0.95f : 0.6f));
      ty += 24.0f;
      ty = wrapped(cx + 18, ty, colW - 36.0f, c->abilityDesc, 1.5f,
                   glm::vec4(0.72f, 0.78f, 0.86f, on ? 0.9f : 0.55f), 18.0f);
      ty += 18.0f;

      text(cx + 18, ty, "PASSIVE", 1.5f, dim);
      ty += 20.0f;
      text(cx + 18, ty, c->perkName, 2.0f, glm::vec4(accent, on ? 0.95f : 0.6f));
      ty += 24.0f;
      wrapped(cx + 18, ty, colW - 36.0f, c->perk, 1.5f,
              glm::vec4(0.72f, 0.78f, 0.86f, on ? 0.9f : 0.55f), 18.0f);
    }
  }

  textCentered(screenW * 0.5f, screenH - 58.0f,
               "1 2 3 OR LEFT RIGHT SELECT   ENTER CONFIRM   ESC BACK", 2.0f, dim);
  end();
}

void Hud::drawHub(int screenW, int screenH, const Content& content, const Hub& hub, const Profile& profile) {
  begin(screenW, screenH);

  const glm::vec4 dim(0.58f, 0.64f, 0.72f, 0.9f);
  const glm::vec4 bright(0.90f, 0.95f, 1.0f, 0.98f);
  const glm::vec4 gold(0.95f, 0.85f, 0.35f, 0.98f);
  const glm::vec4 equippedCol(0.45f, 0.95f, 0.6f, 0.98f);
  const glm::vec4 ownedCol(0.45f, 0.7f, 1.0f, 0.95f);
  const glm::vec4 lockedCol(0.55f, 0.58f, 0.62f, 0.8f);
  const glm::vec4 unaffordable(0.85f, 0.35f, 0.32f, 0.85f);

  float x = 48.0f;
  // The screen is two columns: gear on the left, the route on the right. Both
  // right-align their trailing labels, so the gear column needs its own right
  // edge — against the screen's, its prices land on top of the route list.
  const float gearRight = screenW * 0.46f;
  char buf[96];

  // ---- masthead. Whoever's counter this is gets their name on it.
  const bool hosted = !hub.hostName().empty();
  text(x, 40, hosted ? hub.hostName() : std::string("THE CRADLE"), 4.0f,
       hosted ? glm::vec4(hub.hostColour(), 0.98f) : bright);
  text(x, 78, hosted ? hub.hostTitle() : std::string("ORBITAL STAGING - SELECT LOADOUT AND DESTINATION"),
       1.8f, dim);
  // Dim the half you did not come for. Both stay usable — walking back down
  // three decks for a rifle you forgot would be a punishment, not a hub.
  const float gearFade = hub.focus() == Hub::Focus::Route ? 0.45f : 1.0f;
  const float routeFade = hub.focus() == Hub::Focus::Gear ? 0.45f : 1.0f;
  std::snprintf(buf, sizeof(buf), "%d CHITS", profile.chits);
  float chitsW = textWidth(buf, 2.6f);
  text(screenW - 48 - chitsW, 42, buf, 2.6f, gold);

  // ---- rank, under the chits: where you are on the ladder and how far the
  // next rung is. A progression you cannot see the shape of is a number.
  const int rankIdx = content.rankIndexForXp(profile.xp);
  if (rankIdx >= 0) {
    const std::vector<RankDef>& ladder = content.ranks();
    const RankDef& now = ladder[(size_t)rankIdx];
    const RankDef* next = rankIdx + 1 < (int)ladder.size() ? &ladder[(size_t)rankIdx + 1] : nullptr;

    float rw = textWidth(now.name, 2.0f);
    text(screenW - 48 - rw, 68, now.name, 2.0f, glm::vec4(0.62f, 0.88f, 1.0f, 0.98f));

    const float barW = 190.0f, barX = screenW - 48.0f - barW;
    rect(barX, 90, barW, 5, glm::vec4(0.16f, 0.20f, 0.26f, 0.9f));
    if (next) {
      float span = (float)std::max(1, next->xp - now.xp);
      float frac = std::clamp((float)(profile.xp - now.xp) / span, 0.0f, 1.0f);
      rect(barX, 90, barW * frac, 5, glm::vec4(0.40f, 0.78f, 1.0f, 0.95f));
      std::snprintf(buf, sizeof(buf), "%d / %d XP", profile.xp, next->xp);
    } else {
      rect(barX, 90, barW, 5, glm::vec4(0.95f, 0.82f, 0.35f, 0.95f));
      std::snprintf(buf, sizeof(buf), "%d XP - TOP OF THE LADDER", profile.xp);
    }
    float xw = textWidth(buf, 1.5f);
    text(screenW - 48 - xw, 100, buf, 1.5f, dim);
  }

  rect(x, 120, screenW - 96.0f, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.5f));

  float y = 132.0f;
  const float rowH = 25.0f;

  // One row per item: a status pip, the name, and either its cost or what
  // it does. The selected item in each category gets a caret and a lit
  // backing bar — the keyboard-only hub (Hub.h) needs an unmistakable
  // "this is what 1/2/3 will act on next" cue.
  // What rank an item is released at, as a name, or empty if it is either
  // unrestricted or already reached. Shown in place of the price, because a
  // price you cannot pay with chits is not a price.
  auto rankGate = [&](const std::string& rankId) -> std::string {
    if (rankId.empty() || content.rankReached(rankId, profile.xp)) return "";
    const RankDef* r = content.rank(rankId);
    return r ? r->name : "";
  };

  auto drawCategory = [&](const char* label, char key, const std::vector<std::string>& ids, int selected,
                           const std::string& equippedId, auto ownsFn, auto costFn, auto statFn,
                           auto rankFn) {
    std::snprintf(buf, sizeof(buf), "[%c] %s", key, label);
    text(x, y, buf, 1.9f, glm::vec4(0.75f, 0.85f, 0.95f, 0.95f));
    y += 21.0f;

    for (int i = 0; i < (int)ids.size(); i++) {
      const std::string& id = ids[i];
      bool isEquipped = (id == equippedId);
      bool owned = ownsFn(id);
      int cost = costFn(id);
      bool affordable = profile.chits >= cost;
      std::string gate = owned ? std::string() : rankGate(rankFn(id));
      // A doctrine's own weapon is shown to everyone — you should be able to
      // see what the other two carry — but it says whose it is instead of a
      // price, and it will not be sold to you.
      if (gate.empty() && !owned) {
        const WeaponDef* wd = content.weapon(id);
        if (wd && !wd->classRequired.empty() && wd->classRequired != profile.classId) {
          const ClassDef* cd = content.playerClass(wd->classRequired);
          gate = cd ? cd->name + " ISSUE" : "NOT YOUR ISSUE";
        }
      }
      const bool rankLocked = !gate.empty();
      glm::vec4 col = isEquipped  ? equippedCol
                    : owned       ? ownedCol
                    : rankLocked  ? glm::vec4(0.48f, 0.42f, 0.62f, 0.85f)
                    : affordable  ? lockedCol
                                  : unaffordable;

      if (i == selected) {
        rect(x + 8, y - 5, gearRight - x - 8.0f, rowH - 3, glm::vec4(0.16f, 0.22f, 0.30f, 0.75f));
        text(x + 14, y, ">", 2.0f, bright);
      }
      col.a *= gearFade;
      rect(x + 34, y + 1, 9, 12, col);

      text(x + 54, y, statFn(id), 2.0f, col);

      std::string right;
      if (isEquipped) right = "EQUIPPED";
      else if (owned) right = "OWNED";
      else if (rankLocked) right = gate;
      else {
        std::snprintf(buf, sizeof(buf), "%d CHITS", cost);
        right = buf;
      }
      float rw = textWidth(right, 1.6f);
      text(gearRight - rw, y + 2, right, 1.6f,
           isEquipped   ? equippedCol
           : owned      ? ownedCol
           : rankLocked ? glm::vec4(0.72f, 0.60f, 0.95f, 0.95f)
           : affordable ? gold
                        : unaffordable);
      y += rowH;
    }
    y += 9.0f;
  };

  drawCategory("PRIMARY", '1', hub.weaponIds(), hub.weaponIndex(), profile.equippedWeapon,
               [&](const std::string& id) { return profile.ownsWeapon(id); },
               [&](const std::string& id) { const WeaponDef* d = content.weapon(id); return d ? d->cost : 0; },
               [&](const std::string& id) {
                 const WeaponDef* d = content.weapon(id);
                 return d ? d->name : id;
               },
               [&](const std::string& id) {
                 const WeaponDef* d = content.weapon(id);
                 return d ? d->rankRequired : std::string();
               });
  drawCategory("ARMOUR", '2', hub.armorIds(), hub.armorIndex(), profile.equippedArmor,
               [&](const std::string& id) { return profile.ownsArmor(id); },
               [&](const std::string& id) { const ArmorDef* d = content.armor(id); return d ? d->cost : 0; },
               [&](const std::string& id) {
                 const ArmorDef* d = content.armor(id);
                 return d ? d->name : id;
               },
               [&](const std::string& id) {
                 const ArmorDef* d = content.armor(id);
                 return d ? d->rankRequired : std::string();
               });
  drawCategory("SHADER", '3', hub.cosmeticIds(), hub.cosmeticIndex(), profile.equippedCosmetic,
               [&](const std::string& id) { return profile.ownsCosmetic(id); },
               [&](const std::string& id) { const CosmeticDef* d = content.cosmetic(id); return d ? d->cost : 0; },
               [&](const std::string& id) {
                 const CosmeticDef* d = content.cosmetic(id);
                 return d ? d->name : id;
               },
               [&](const std::string& id) {
                 const CosmeticDef* d = content.cosmetic(id);
                 return d ? d->rankRequired : std::string();
               });

  // ---- the route, in a column of its own. Twenty-two destinations do not
  // fit under three gear lists at the old row height, and shrinking the rows
  // until they do makes the whole screen unreadable. So the route lives on
  // the right, windowed to whatever fits, with the selected sector's
  // objective and briefing underneath it — which is also where the browser
  // build puts them (src/js/fps/hub.js).
  const float rx = screenW * 0.52f;
  const float rw2 = screenW - rx - 48.0f;
  float ry = 118.0f;
  text(rx, ry, "[TAB] ROUTE", 2.0f, glm::vec4(0.75f, 0.85f, 0.95f, 0.95f));
  ry += 24.0f;

  const auto& missions = hub.missionIds();
  const float missionRowH = 26.0f;
  // Leave room for the briefing block below the list.
  const int visible = std::max(4, (int)((screenH - 300.0f - ry) / missionRowH));
  int first = hub.missionIndex() - visible / 2;
  first = std::max(0, std::min(first, (int)missions.size() - visible));
  int last = std::min((int)missions.size(), first + visible);

  for (int i = first; i < last; i++) {
    const MissionDef* m = content.mission(missions[i]);
    bool completed = profile.hasCompleted(missions[i]);
    bool locked = hub.missionLocked(i);
    glm::vec4 col = locked     ? glm::vec4(0.42f, 0.44f, 0.48f, 0.8f)
                  : completed  ? equippedCol
                               : glm::vec4(0.8f, 0.85f, 0.92f, 0.95f);
    if (i == hub.missionIndex()) {
      rect(rx + 4, ry - 4, rw2 - 8, missionRowH - 4, glm::vec4(0.16f, 0.22f, 0.30f, 0.75f));
      text(rx + 8, ry, ">", 2.0f, bright);
    }
    col.a *= routeFade;
    rect(rx + 26, ry, 8, 12, col);

    // The route is numbered so its order reads as a route rather than a menu.
    std::string label = m ? m->name : missions[i];
    if (m && m->campaignIndex > 0) {
      std::snprintf(buf, sizeof(buf), "%02d %s", m->campaignIndex, label.c_str());
      label = buf;
    }
    text(rx + 42, ry, label, 1.9f, col);

    std::string right = locked ? (i >= hub.campaignCount() ? "AFTER THE CAMPAIGN" : "LOCKED")
                               : (completed ? "CLEARED" : "OPEN");
    float rww = textWidth(right, 1.6f);
    text(screenW - 58 - rww, ry + 2, right, 1.6f,
         locked ? glm::vec4(0.5f, 0.5f, 0.54f, 0.8f) : (completed ? equippedCol : gold));
    ry += missionRowH;
  }
  if (last < (int)missions.size() || first > 0) {
    std::snprintf(buf, sizeof(buf), "%d OF %d", hub.missionIndex() + 1, (int)missions.size());
    text(rx + 42, ry + 2, buf, 1.6f, dim);
  }

  // ---- the selected sector's objective and briefing, wrapped to the column.
  const MissionDef* sel = content.mission(hub.selectedMission());
  if (sel) {
    float by = screenH - 268.0f;
    rect(rx, by - 14, rw2, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.5f));
    if (!sel->objective.empty()) {
      text(rx, by, "OBJECTIVE", 1.6f, glm::vec4(0.75f, 0.85f, 0.95f, 0.8f));
      by += 20.0f;
      by = wrapped(rx, by, rw2, sel->objective, 1.9f, gold, 22.0f);
      by += 10.0f;
    }
    if (!sel->brief.empty()) {
      text(rx, by, "BRIEFING", 1.6f, glm::vec4(0.75f, 0.85f, 0.95f, 0.8f));
      by += 20.0f;
      wrapped(rx, by, rw2, sel->brief, 1.7f, glm::vec4(0.72f, 0.78f, 0.86f, 0.95f), 20.0f);
    }
  }

  // ---- footer hint
  textCentered(screenW * 0.5f, screenH - 42.0f,
               "1 2 3 CYCLE GEAR   TAB CYCLE ROUTE   ENTER DEPLOY   Q UNDOCK", 2.0f, dim);

  end();
}

// ---------------------------------------------------------------- waypoints

void Hud::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 colour) {
  // Sort by y, then fill two-pixel rows between the long edge and the two
  // short ones. Coarse, and the HUD is already a grid of square pixels.
  if (a.y > b.y) std::swap(a, b);
  if (b.y > c.y) std::swap(b, c);
  if (a.y > b.y) std::swap(a, b);
  const float step = 2.0f;
  auto lerpX = [](glm::vec2 p, glm::vec2 q, float y) {
    if (std::fabs(q.y - p.y) < 1e-3f) return p.x;
    return p.x + (q.x - p.x) * (y - p.y) / (q.y - p.y);
  };
  for (float y = a.y; y <= c.y; y += step) {
    float x0 = lerpX(a, c, y);
    float x1 = y < b.y ? lerpX(a, b, y) : lerpX(b, c, y);
    if (x0 > x1) std::swap(x0, x1);
    rect(x0, y, std::max(1.0f, x1 - x0), step, colour);
  }
}

void Hud::drawWaypoint(const glm::mat4& viewProj, const glm::vec3& eye, const Waypoint& w) {
  if (!w.on) return;
  const float W = (float)screenW_, H = (float)screenH_;
  const glm::vec4 gold(1.0f, 0.82f, 0.30f, 0.96f);
  const glm::vec4 shade(0.03f, 0.03f, 0.02f, 0.62f);
  const float dist = glm::length(w.at - eye);
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s  %dM", w.label.c_str(), (int)std::lround(dist));

  const glm::vec4 clip = viewProj * glm::vec4(w.at, 1.0f);
  const bool behind = clip.w <= 0.05f;
  // Dividing by |w| rather than w keeps the side right for a point behind
  // the camera: one behind and to the right still comes out on the right.
  const float iw = 1.0f / std::max(std::fabs(clip.w), 0.05f);
  glm::vec2 ndc(clip.x * iw, clip.y * iw);
  const bool onScreen = !behind && std::fabs(ndc.x) < 0.92f && std::fabs(ndc.y) < 0.88f;

  if (onScreen) {
    if (dist < w.minDistance) return;
    const glm::vec2 p((ndc.x * 0.5f + 0.5f) * W, (1.0f - (ndc.y * 0.5f + 0.5f)) * H);
    // A small arrow pointing down at it, and the label above.
    const float y = p.y - 34.0f;
    triangle({p.x - 9.0f, y}, {p.x + 9.0f, y}, {p.x, y + 13.0f}, gold);
    const float tw = textWidth(buf, 1.7f);
    rect(p.x - tw * 0.5f - 6, y - 24, tw + 12, 20, shade);
    textCentered(p.x, y - 20, buf, 1.7f, gold);
    return;
  }

  // Off screen: an arrow on the edge, pointing the way to turn.
  glm::vec2 d(ndc.x, -ndc.y);
  const glm::vec2 c(W * 0.5f, H * 0.5f);
  const float mx = W * 0.5f - 70.0f, my = H * 0.5f - 90.0f;
  glm::vec2 p;
  if (behind) {
    // Behind you: a flat arrow on the side to turn to, low on the screen.
    // Straight behind has no side, so it picks one rather than sit dead
    // centre pointing at your feet, which reads as "down" rather than "turn".
    const float side = d.x < -0.02f ? -1.0f : 1.0f;
    d = glm::vec2(side, 0.0f);
    p = glm::vec2(c.x + side * mx, H * 0.68f);
  } else {
    if (glm::length(d) < 1e-4f) d = glm::vec2(1.0f, 0.0f);
    d = glm::normalize(d);
    const float t = std::min(std::fabs(d.x) > 1e-4f ? mx / std::fabs(d.x) : 1e9f,
                             std::fabs(d.y) > 1e-4f ? my / std::fabs(d.y) : 1e9f);
    p = c + d * t;
  }
  const glm::vec2 n(-d.y, d.x);
  // Long and narrow, so its direction is never in doubt. A shadow first, so a
  // gold arrow over a bright floor still has an edge.
  const glm::vec2 tip = p + d * 30.0f, b0 = p - d * 12.0f + n * 14.0f, b1 = p - d * 12.0f - n * 14.0f;
  const glm::vec2 o(2.0f, 2.0f);
  triangle(tip + o, b0 + o, b1 + o, shade);
  triangle(tip, b0, b1, gold);
  const float tw = textWidth(buf, 1.6f);
  // The label sits on the inward side of the arrow, far enough in that its
  // own width clears the arrow whichever edge it is on.
  const glm::vec2 lp = p - d * (26.0f + std::fabs(d.x) * tw * 0.5f + std::fabs(d.y) * 14.0f);
  float lx = std::clamp(lp.x - tw * 0.5f, 10.0f, W - tw - 10.0f);
  float ly = std::clamp(lp.y - 8.0f, 96.0f, H - 70.0f);
  rect(lx - 6, ly - 4, tw + 12, 20, shade);
  text(lx, ly, buf, 1.6f, gold);
}

// ------------------------------------------------------------ leave mission

void Hud::drawLeaveConfirm(int screenW, int screenH, bool restarts) {
  begin(screenW, screenH);
  const float cx = screenW * 0.5f, cy = screenH * 0.5f;
  const glm::vec4 gold(1.0f, 0.82f, 0.30f, 0.98f);
  const glm::vec4 dim(0.72f, 0.78f, 0.86f, 0.95f);
  const float w = std::min(760.0f, screenW - 64.0f), h = 190.0f;
  rect(cx - w * 0.5f, cy - h * 0.5f, w, h, glm::vec4(0.03f, 0.04f, 0.06f, 0.9f));
  rect(cx - w * 0.5f, cy - h * 0.5f, w, 3, gold);
  textCentered(cx, cy - h * 0.5f + 24, restarts ? "START BLOCK D AGAIN?" : "LEAVE THE MISSION?", 3.0f, gold);
  wrappedCentered(cx, cy - h * 0.5f + 70, w - 60,
                  restarts ? "There is nowhere to go back to yet - this is the first morning. "
                             "Leaving starts Block D from the top."
                           : "You keep the experience, bounty progress and any weapons you found. "
                             "It is not cleared, and it stays open to fly again from the same counter.",
                  1.7f, dim, 22.0f);
  textCentered(cx, cy + h * 0.5f - 36, "Y  LEAVE        N OR ESC  STAY", 2.0f, gold);
  end();
}
