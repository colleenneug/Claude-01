#pragma once
#include "Gl.h"
#include "Shader.h"
#include <string>
#include <vector>

class Content;
class Hub;
struct Profile;

// An ortho 2D overlay: solid-colour rectangles plus text drawn from the
// hand-authored 5x7 bitmap font in Font.h (this project has no offline way
// to fetch a font-rendering library, so glyphs are bit patterns expanded
// into quads rather than a rasterized atlas). Everything it draws is wired
// to live game state — actual HP, actual ammo, actual mission names.
class Hud {
public:
  void create();
  void destroy();

  // Call once per frame after the 3D composite, before swapping buffers.
  // begin() resets the text batch; end() flushes it in a single draw call.
  void begin(int screenW, int screenH);
  void rect(float x, float y, float w, float h, glm::vec4 colour);
  void end();

  // Queues `s` at (x, y) — pixels, origin top-left, y being the glyph's
  // top edge — in the batch flushed by end(). `scale` is the size of one
  // font pixel, so a glyph is 5*scale wide and 7*scale tall. Case
  // insensitive; unsupported characters render as blank space.
  void text(float x, float y, const std::string& s, float scale, glm::vec4 colour);
  // Same, horizontally centred on `cx`.
  void textCentered(float cx, float y, const std::string& s, float scale, glm::vec4 colour);
  // Advance width of `s`, for laying out around a string.
  static float textWidth(const std::string& s, float scale);
  // Lays `s` out across at most `maxWidth` pixels, breaking on spaces, and
  // returns the y just past the last line. Mission briefings are prose
  // written for the browser build, where the browser wrapped it; here
  // nothing wraps it unless this does.
  float wrapped(float x, float y, float maxWidth, const std::string& s,
                float scale, glm::vec4 colour, float lineHeight);
  // The same layout, centred on `cx` — for a cutscene caption, which
  // left-aligned under a letterbox reads as a subtitle track come loose.
  float wrappedCentered(float cx, float y, float maxWidth, const std::string& s,
                        float scale, glm::vec4 colour, float lineHeight);

  // Projects a world point to pixels. Returns false when it is behind the
  // camera, which is not a detail: a point behind you projects to a
  // perfectly plausible on-screen position with the sign flipped, so
  // without this check every nameplate has a mirror image floating over
  // your shoulder.
  static bool worldToScreen(const glm::mat4& viewProj, const glm::vec3& world,
                            int screenW, int screenH, glm::vec2& out);

  // Everything the mission HUD reads, in one struct rather than twenty
  // positional arguments — it grew past the point where a call site was
  // readable once names, counts and comms lines joined the bars.
  struct State {
    float hp = 100.0f, maxHp = 100.0f;
    int ammoInMag = 0, magSize = 0, reserveAmmo = 0;
    bool reloading = false;
    float reloadFrac = 0.0f;
    float hitMarkerT = 0.0f, damageFlashT = 0.0f;

    std::string missionName;
    float waveFrac = 0.0f;
    bool bossAlive = false;
    float bossHpFrac = 0.0f;
    std::string bossName;
    bool missionComplete = false, missionFailed = false;

    // One line of staged comms traffic — how the browser build carries its
    // story (see src/js/fps/hud.js) — faded in and out by `commsAlpha`.
    std::string commsSpeaker, commsLine;
    float commsAlpha = 0.0f;

    // "+24 AMMO" style note just under the crosshair after a pickup.
    std::string pickupNote;
    float pickupAlpha = 0.0f;

    // The field ability's recharge, 0..1, and whether it is ready. An
    // ability on a nine-second cooldown that the screen says nothing about
    // is an ability nobody presses.
    float abilityFrac = 1.0f;
    bool abilityReady = true;
    std::string abilityName = "PHASE STEP";

    // The Bulwark's barrier, shown as its own tier stacked on the health bar
    // rather than folded into it: it does not regenerate and it does not
    // count towards your maximum, so showing it as extra health would lie.
    float overshield = 0.0f, overshieldMax = 0.0f;

    // What you are holding. Shown by the ammo counter, because at a glance
    // "six rounds" means something completely different on a breaching
    // shotgun than on a suppressed carbine.
    std::string weaponName;
    // The doctrine's name, top-left under the health bar.
    std::string className;

    // The tutorial's current step: what it is asking for, how to do it,
    // and how far through the step you are. Empty outside a tutorial.
    std::string tutorialPrompt, tutorialHint;
    float tutorialProgress = 0.0f;

    // What you are doing right now, top-left under the mission name, and the
    // control hint under that. Changes quietly as you cross a site: no
    // banner, no pause.
    std::string objective, objectiveHint;

    // Empty hands: no ammo counter and no weapon name, because there is
    // nothing to count.
    bool armed = true;

    // The trauma harness: how many charges are left, and whether you are
    // on the ground right now waiting for one.
    // Career experience banked in this mission so far — shown on the
    // debrief, because a number that only goes up should be visible the
    // moment it went up.
    int xpEarned = 0;

    int harnessLeft = 0, harnessMax = 0;
    bool downed = false;
    float downedFor = 0.0f, downedMax = 1.0f;

    // A cutscene, if one is playing: the letterbox closes to `cutsceneFade`
    // and the caption rides the bottom bar.
    std::string cutsceneCaption;
    float cutsceneFade = 0.0f;
    bool inCutscene = false;

    // The equipped cosmetic's colour (Game::hudAccent): tints the
    // crosshair, ammo pips and the health bar's "full" tier. The health
    // bar's low/critical tiers stay fixed amber/red regardless — that's a
    // warning colour, not a fashion choice.
    glm::vec3 accent{0.85f, 0.95f, 1.0f};
  };

  // The mission HUD: health bar and readout, ammo pips and counts, reload
  // sweep, a crosshair with a hit-marker flash, wave progress, a boss bar
  // when one is alive, comms lines, and the end-of-mission banner.
  void draw(int screenW, int screenH, const State& s);

  // The hub screen: a chits bar, then one row of swatches per gear
  // category (green = equipped, blue = owned, dim grey = affordable but
  // not owned, dim red = can't afford — see Hub.h for how cycling one
  // equips-or-buys it), then a row of mission swatches (green if already
  // cleared once). The white outline marks the currently-cycling-through
  // selection in each row, which is not necessarily the equipped item.
  void drawHub(int screenW, int screenH, const Content& content, const Hub& hub, const Profile& profile);

  // A promotion, over whatever is already on screen. Drawn wherever you
  // happen to land after the mission that earned it rather than on the
  // debrief you are in the middle of dismissing — a promotion nobody sees
  // is not a promotion. `t` counts down; it fades out over the last second.
  void drawPromotion(int screenW, int screenH, const std::string& rankName,
                     const std::string& unlockLine, int stipend, float t);

  // Walking around the Cradle: where you are, what you can walk up to, and
  // the controls. Deliberately sparse — the station is the one place in the
  // game nothing is shooting at you, and a full combat HUD over it would say
  // otherwise.
  struct StationState {
    std::string deck;            // "DECK A - CONCOURSE"
    std::string terminalName;    // empty when nothing is in reach
    std::string terminalLine;
    std::string terminalAction;  // "TALK", "BROWSE", "UNDOCK" — what E does here
    glm::vec3 terminalColour{0.6f, 0.9f, 1.0f};

    // A name and title floating over each crew member with a post, so you can
    // see who is where from across the concourse instead of walking up to
    // everyone to find out. Projected from world space — see worldToScreen.
    struct Nameplate {
      glm::vec3 worldPos{0.0f};
      std::string name, title;
      glm::vec3 colour{0.8f, 0.9f, 1.0f};
    };
    std::vector<Nameplate> nameplates;
    glm::mat4 viewProj{1.0f};
    glm::vec3 eye{0.0f};

    // One line of conversation, shown while you are talking to someone.
    std::string talkingTo, talkTitle, talkLine;
    int talkIndex = 0, talkCount = 0;
    glm::vec3 talkColour{0.8f, 0.9f, 1.0f};
  };
  void drawStation(int screenW, int screenH, const StationState& s);

  // The record-creation screen: pick a doctrine. The browser build asks the
  // same question in the same place (its screen-create), and for the same
  // reason — the doctrine decides the weapon, the ability and the passive, so
  // it has to be answered before there is anything to play.
  void drawCreate(int screenW, int screenH, const Content& content,
                  const std::vector<std::string>& classIds, int selected);

  // One slot's line on the save-select screen: either a summary of the
  // profile in it, or EMPTY.
  struct SlotSummary {
    bool used = false;
    int chits = 0;
    int missionsCleared = 0;
    std::string weaponName;
  };

  // The save-select screen shown before the hub: three slots, the selected
  // one carrying a caret, with a confirm prompt when a delete is pending.
  void drawSlotSelect(int screenW, int screenH, const SlotSummary slots[3], int selected,
                      int deletePending);

  // The flight HUD: speed, what's nearest and how far, and the prompt when
  // you're close enough to land or dock.
  struct SpaceState {
    std::string nearestName;
    float nearestDistance = 0.0f;
    bool inRange = false;
    bool isStation = false;      // changes LAND to DOCK
    bool missionCleared = false; // nearest world already completed once
    float speed = 0.0f, maxSpeed = 1.0f;
    glm::vec3 accent{0.85f, 0.95f, 1.0f};
  };
  void drawSpace(int screenW, int screenH, const SpaceState& s);

private:
  void flushText();

  Shader shader_;
  GLuint vao_ = 0, vbo_ = 0;
  int screenW_ = 1, screenH_ = 1;

  // Text batch: every lit font pixel from every text() call this frame,
  // expanded on the CPU into two triangles (6 vertices x 6 floats: 2 for
  // position in pixels, 4 for colour) and uploaded once. One draw call for
  // the whole frame's text keeps a wall of glyphs from turning into
  // thousands of tiny draws on weak integrated hardware.
  //
  // Known tradeoff: full quads per lit pixel is the simple encoding, and a
  // text-heavy screen like the hub comes to a megabyte or so of vertex data
  // per frame. That's cheap next to the 3D pass and the hub has no 3D pass
  // at all, so it hasn't been worth optimizing; instancing one quad with a
  // per-instance position/colour would cut it about sixfold if it ever is.
  Shader textShader_;
  GLuint textVao_ = 0, textVbo_ = 0;
  size_t textVboCapacity_ = 0;
  std::vector<float> textVerts_;
};
