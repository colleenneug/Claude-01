#pragma once

#include "Draw.h"
#include "Input.h"
#include "NetClient.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

class Hud;
class Content;

// How online play shows up: the other Striders in your instance drawn in the
// world, their names over their heads, a corner panel with your instance and
// squad, and the social screen (O) for friends, squad invites and who is here.
namespace netview {

// Everyone else in the instance, as draw items for SceneSource::extras.
// With `content`, each player's gun is drawn as the weapon they hold, in
// its own look (see WeaponLook).
void collectRemotes(const net::Client& client, float time, std::vector<DrawItem>& out,
                    const Content* content = nullptr);

// Other players' ships in orbit, from `eye`: a small hull with an engine
// glow, and a beacon that grows with distance so a ship ten kilometres off is
// still a point of light rather than nothing.
void collectShips(const net::Client& client, float time, const glm::vec3& eye, std::vector<DrawItem>& out);

// The social screen. Open with O; arrows move, keys act on the row.
struct Social {
  bool open = false;
  int cursor = 0;
  bool prev[9] = {};
  // Set by J on a friend's row: go where they are. main.cpp launches it and
  // clears it.
  uint64_t joinId = 0;
  std::string joinPlace;
  // Typing: your tag's name, or a tag to look somebody up by.
  enum class Typing { None, Tag, Find } typing = Typing::None;
  std::string text;
  bool prevBackspace = false, prevEnter = false, prevEsc = false;
  // The last lookup's answer.
  uint64_t foundId = 0;
  std::string foundTag;
  double clock = 0.0;    // for the text cursor's blink
  std::string message;   // "PRESS F TO ADD ..." hints change with the row
};

// Reads the keys for the screen (O toggles it; while open, Up/Down or Tab
// choose a row, Enter accepts, Backspace declines, F adds a friend, I sends
// a squad invite, J joins a friend where they are). Returns true while the
// screen is open.
bool socialInput(const Input& in, Social& social, net::Client& client);

// Opens the social screen straight on "choose your tag" (a record with none).
void promptForTag(Social& social);

// What the HUD needs from a PvP fight that only the game knows.
struct Pvp {
  bool on = false;
  float respawnIn = 0.0f;   // seconds on the floor left, 0 when up
  float hitMarker = 0.0f;   // a round of yours just reached somebody
  bool space = false;       // in orbit: names at any range, distances in km
};

// Nameplates over other players, the corner panel, notices, the PvP
// scoreboard, and the social screen when it is open. Draws its own Hud batch.
void drawHud(Hud& hud, int screenW, int screenH, const net::Client& client, const glm::mat4& viewProj,
             const glm::vec3& eye, const Social& social, const Pvp& pvp = Pvp{});

// The Tuning's own panel: notes carried, the tap, the two teams' banks.
// Drawn online or off (offline it is your own total against the limit).
struct Tuning {
  int carried = 0, max = 15, bankedSelf = 0, limit = 60;
  float respawnIn = 0.0f, flash = 0.0f, tapDist = 0.0f;
  bool tapOnScreen = false;
  glm::vec2 tapAt{0.0f};
};
void drawTuning(Hud& hud, int screenW, int screenH, const net::Client* client, const Tuning& t);

}  // namespace netview
