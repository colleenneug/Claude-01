#pragma once

#include "Draw.h"
#include "NetClient.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

class Hud;
struct GLFWwindow;

// How online play shows up: the other Striders in your instance drawn in the
// world, their names over their heads, a corner panel with your instance and
// squad, and the social screen (O) for friends, squad invites and who is here.
namespace netview {

// Everyone else in the instance, as draw items for SceneSource::extras.
void collectRemotes(const net::Client& client, float time, std::vector<DrawItem>& out);

// The social screen. Open with O; arrows move, keys act on the row.
struct Social {
  bool open = false;
  int cursor = 0;
  bool prev[8] = {};
  std::string message;   // "PRESS F TO ADD ..." hints change with the row
};

// Reads the keys for the screen (O toggles it; while open, Up/Down or Tab
// choose a row, Enter accepts, Backspace declines, F adds a friend, I sends
// a squad invite). Returns true while the screen is open.
bool socialInput(GLFWwindow* window, Social& social, net::Client& client);

// Nameplates over other players, the corner panel, notices, and the social
// screen when it is open. Draws its own Hud batch.
void drawHud(Hud& hud, int screenW, int screenH, const net::Client& client, const glm::mat4& viewProj,
             const glm::vec3& eye, const Social& social);

}  // namespace netview
