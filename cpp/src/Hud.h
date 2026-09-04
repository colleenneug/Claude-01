#pragma once
#include "Gl.h"
#include "Shader.h"

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
  void draw(int screenW, int screenH, float hpFrac, float ammoFrac, int ammoInMag, int magSize,
            bool reloading, float reloadFrac, float hitMarkerT, float damageFlashT,
            float waveFrac, bool bossAlive, float bossHpFrac, bool missionComplete, bool missionFailed);

private:
  Shader shader_;
  GLuint vao_ = 0, vbo_ = 0;
  int screenW_ = 1, screenH_ = 1;
};
