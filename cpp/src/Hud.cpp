#include "Hud.h"
#include "Content.h"
#include "Font.h"
#include "Hub.h"
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

  // ---- health bar + readout, bottom-left
  float bx = 28, by = screenH - 54, bw = 260, bh = 18;
  rect(bx - 3, by - 3, bw + 6, bh + 6, glm::vec4(0, 0, 0, 0.45f));
  rect(bx, by, bw, bh, glm::vec4(0.12f, 0.03f, 0.03f, 0.9f));
  glm::vec3 hpCol = hpFrac > 0.5f ? s.accent
                   : hpFrac > 0.25f ? glm::vec3(0.9f, 0.75f, 0.25f)
                                    : glm::vec3(0.9f, 0.25f, 0.2f);
  rect(bx, by, bw * std::clamp(hpFrac, 0.0f, 1.0f), bh, glm::vec4(hpCol, 0.95f));

  char buf[96];
  std::snprintf(buf, sizeof(buf), "%d / %d", (int)std::lround(s.hp), (int)std::lround(s.maxHp));
  text(bx, by - 16, buf, 2.0f, glm::vec4(hpCol, 0.95f));
  text(bx + bw - textWidth("INTEGRITY", 1.6f), by - 15, "INTEGRITY", 1.6f, dim);

  // ---- ammo pips + counts, bottom-right. One small rect per round in the
  // mag, capped so a huge magazine doesn't paint a wall of pips; the real
  // numbers sit above it now that there's text to print them with.
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

  // ---- comms: the story, one staged line at a time, above the health bar
  if (s.commsAlpha > 0.001f && !s.commsLine.empty()) {
    float a = std::clamp(s.commsAlpha, 0.0f, 1.0f);
    float ty = screenH - 130;
    float lineW = textWidth(s.commsLine, 2.0f);
    float speakerW = s.commsSpeaker.empty() ? 0.0f : textWidth(s.commsSpeaker + ":", 2.0f) + 10.0f;
    rect(24, ty - 8, std::max(lineW + speakerW, 120.0f) + 20, 30, glm::vec4(0, 0, 0, 0.42f * a));
    rect(24, ty - 8, 3, 30, glm::vec4(s.accent, 0.9f * a));
    if (!s.commsSpeaker.empty()) {
      text(36, ty, s.commsSpeaker + ":", 2.0f, glm::vec4(s.accent, a));
    }
    text(36 + speakerW, ty, s.commsLine, 2.0f, glm::vec4(0.92f, 0.95f, 1.0f, a));
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
    textCentered(cx, cy + 22, "PRESS ENTER TO RETURN TO THE CRADLE", 2.0f, dim);
  }

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
  char buf[96];

  // ---- masthead
  text(x, 40, "THE CRADLE", 4.0f, bright);
  text(x, 78, "ORBITAL STAGING - SELECT LOADOUT AND DESTINATION", 1.8f, dim);
  std::snprintf(buf, sizeof(buf), "%d CHITS", profile.chits);
  float chitsW = textWidth(buf, 2.6f);
  text(screenW - 48 - chitsW, 42, buf, 2.6f, gold);
  rect(x, 96, screenW - 96.0f, 2, glm::vec4(0.35f, 0.45f, 0.55f, 0.5f));

  float y = 118.0f;
  const float rowH = 30.0f;

  // One row per item: a status pip, the name, and either its cost or what
  // it does. The selected item in each category gets a caret and a lit
  // backing bar — the keyboard-only hub (Hub.h) needs an unmistakable
  // "this is what 1/2/3 will act on next" cue.
  auto drawCategory = [&](const char* label, char key, const std::vector<std::string>& ids, int selected,
                           const std::string& equippedId, auto ownsFn, auto costFn, auto statFn) {
    std::snprintf(buf, sizeof(buf), "[%c] %s", key, label);
    text(x, y, buf, 2.0f, glm::vec4(0.75f, 0.85f, 0.95f, 0.95f));
    y += 24.0f;

    for (int i = 0; i < (int)ids.size(); i++) {
      const std::string& id = ids[i];
      bool isEquipped = (id == equippedId);
      bool owned = ownsFn(id);
      int cost = costFn(id);
      bool affordable = profile.chits >= cost;
      glm::vec4 col = isEquipped ? equippedCol
                    : owned      ? ownedCol
                    : affordable ? lockedCol
                                 : unaffordable;

      if (i == selected) {
        rect(x + 8, y - 4, screenW - 96.0f - 16.0f, rowH - 4, glm::vec4(0.16f, 0.22f, 0.30f, 0.75f));
        text(x + 14, y + 2, ">", 2.2f, bright);
      }
      rect(x + 34, y + 2, 10, 14, col);

      text(x + 54, y + 2, statFn(id), 2.2f, col);

      std::string right;
      if (isEquipped) right = "EQUIPPED";
      else if (owned) right = "OWNED";
      else {
        std::snprintf(buf, sizeof(buf), "%d CHITS", cost);
        right = buf;
      }
      float rw = textWidth(right, 1.8f);
      text(screenW - 48 - rw, y + 4, right, 1.8f,
           isEquipped ? equippedCol : (owned ? ownedCol : (affordable ? gold : unaffordable)));
      y += rowH;
    }
    y += 12.0f;
  };

  drawCategory("PRIMARY", '1', hub.weaponIds(), hub.weaponIndex(), profile.equippedWeapon,
               [&](const std::string& id) { return profile.ownsWeapon(id); },
               [&](const std::string& id) { const WeaponDef* d = content.weapon(id); return d ? d->cost : 0; },
               [&](const std::string& id) {
                 const WeaponDef* d = content.weapon(id);
                 return d ? d->name : id;
               });
  drawCategory("ARMOUR", '2', hub.armorIds(), hub.armorIndex(), profile.equippedArmor,
               [&](const std::string& id) { return profile.ownsArmor(id); },
               [&](const std::string& id) { const ArmorDef* d = content.armor(id); return d ? d->cost : 0; },
               [&](const std::string& id) {
                 const ArmorDef* d = content.armor(id);
                 return d ? d->name : id;
               });
  drawCategory("SHADER", '3', hub.cosmeticIds(), hub.cosmeticIndex(), profile.equippedCosmetic,
               [&](const std::string& id) { return profile.ownsCosmetic(id); },
               [&](const std::string& id) { const CosmeticDef* d = content.cosmetic(id); return d ? d->cost : 0; },
               [&](const std::string& id) {
                 const CosmeticDef* d = content.cosmetic(id);
                 return d ? d->name : id;
               });

  // ---- destinations. Free to launch, and green once cleared at least
  // once (Profile::recordMissionComplete only pays the reward the first
  // time, so the tint doubles as "already paid out").
  text(x, y, "[TAB] DESTINATION", 2.0f, glm::vec4(0.75f, 0.85f, 0.95f, 0.95f));
  y += 24.0f;
  const auto& missions = hub.missionIds();
  for (int i = 0; i < (int)missions.size(); i++) {
    const MissionDef* m = content.mission(missions[i]);
    bool completed = profile.hasCompleted(missions[i]);
    glm::vec4 col = completed ? equippedCol : glm::vec4(0.8f, 0.85f, 0.92f, 0.95f);
    if (i == hub.missionIndex()) {
      rect(x + 8, y - 4, screenW - 96.0f - 16.0f, rowH - 4, glm::vec4(0.16f, 0.22f, 0.30f, 0.75f));
      text(x + 14, y + 2, ">", 2.2f, bright);
    }
    rect(x + 34, y + 2, 10, 14, col);
    text(x + 54, y + 2, m ? m->name : missions[i], 2.2f, col);

    std::string right = completed ? "CLEARED" : "NEW";
    if (m && !completed) {
      std::snprintf(buf, sizeof(buf), "NEW - %d CHITS", m->rewardChits);
      right = buf;
    }
    float rw = textWidth(right, 1.8f);
    text(screenW - 48 - rw, y + 4, right, 1.8f, completed ? equippedCol : gold);
    y += rowH;
  }

  // ---- footer hint
  textCentered(screenW * 0.5f, screenH - 42.0f,
               "1 2 3 CYCLE GEAR   TAB CYCLE DESTINATION   ENTER DEPLOY", 2.0f, dim);

  end();
}
