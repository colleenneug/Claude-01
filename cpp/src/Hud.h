#pragma once
#include "Gl.h"
#include "Shader.h"

class Content;
class Hub;
struct Profile;

// A minimal ortho 2D overlay: solid-colour rectangles only, no text
// rendering. This project has no offline way to fetch a font-rendering
// library, so v1's HUD communicates entirely through bars, pips and colour
// rather than numbers or mission names — a deliberate, documented scope
// cut (see docs/NATIVE_RENDERER.md), not an oversight. Everything it draws
// is genuinely wired to live game state (actual HP, actual ammo, actual
// wave progress), which is the part that mattered for this pass.
class Hud {
public:
  void create();
  void destroy();

  // Call once per frame after the 3D composite, before swapping buffers.
  void begin(int screenW, int screenH);
  void rect(float x, float y, float w, float h, glm::vec4 colour);
  void end();

  // The actual HUD, assembled from rect(): health bar, ammo pips,
  // reload sweep, a crosshair that opens under recoil, a hit marker flash,
  // a wave-progress bar, and a boss health bar when one is alive.
  // `accent` is the equipped cosmetic's colour (Game::hudAccent) — it tints
  // the crosshair, the ammo pips, and the health bar's "full" tier; the
  // health bar's low/critical tiers stay fixed amber/red regardless of
  // cosmetic, since that's a warning colour, not a fashion choice.
  void draw(int screenW, int screenH, float hpFrac, float ammoFrac, int ammoInMag, int magSize,
            bool reloading, float reloadFrac, float hitMarkerT, float damageFlashT,
            float waveFrac, bool bossAlive, float bossHpFrac, bool missionComplete, bool missionFailed,
            glm::vec3 accent = glm::vec3(0.85f, 0.95f, 1.0f));

  // The hub screen: a chits bar, then one row of swatches per gear
  // category (green = equipped, blue = owned, dim grey = affordable but
  // not owned, dim red = can't afford — see Hub.h for how cycling one
  // equips-or-buys it), then a row of mission swatches (green if already
  // cleared once). The white outline marks the currently-cycling-through
  // selection in each row, which is not necessarily the equipped item.
  void drawHub(int screenW, int screenH, const Content& content, const Hub& hub, const Profile& profile);

private:
  Shader shader_;
  GLuint vao_ = 0, vbo_ = 0;
  int screenW_ = 1, screenH_ = 1;
};
