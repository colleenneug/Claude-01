#include "NetView.h"

#include "Gl.h"
#include "Hostile.h"
#include "Hud.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace netview {
namespace {

void part(std::vector<DrawItem>& out, const DrawItem& base, const Mesh& mesh, const glm::mat4& parent,
          glm::vec3 at, glm::vec3 scale, float rxDeg = 0.0f) {
  glm::mat4 m = glm::translate(parent, at);
  if (rxDeg != 0.0f) m = glm::rotate(m, glm::radians(rxDeg), glm::vec3(1, 0, 0));
  DrawItem it = base;
  it.mesh = &mesh;
  it.model = glm::scale(m, scale);
  out.push_back(it);
}

// A colour of their own for each player, from their id, so two strangers in
// the same instance can be told apart at a glance.
glm::vec3 colourFor(uint64_t id) {
  const float h = (float)(id % 360) / 360.0f;
  auto ch = [h](float o) {
    float x = std::fabs(std::fmod(h * 6.0f + o, 6.0f) - 3.0f) - 1.0f;
    return std::clamp(x, 0.0f, 1.0f);
  };
  const glm::vec3 c(ch(0.0f), ch(4.0f), ch(2.0f));
  return glm::mix(glm::vec3(0.42f), c, 0.55f);
}

const glm::vec3 kSquad(0.35f, 0.95f, 1.0f);   // squad mates: the Division's cyan
const glm::vec3 kOther(1.0f, 0.70f, 0.30f);   // everyone else: amber

struct Row {
  enum Kind { Invite, Here, Friend, Leave } kind;
  size_t index;
  std::string label, detail, keys;
};

std::vector<Row> rows(const net::Client& c) {
  std::vector<Row> out;
  // const_cast-free: invites() is non-const only so it can be popped.
  auto& invites = const_cast<net::Client&>(c).invites();
  for (size_t i = 0; i < invites.size(); i++) {
    const net::Invite& in = invites[i];
    out.push_back({Row::Invite, i, in.name,
                   in.squad ? "INVITED YOU TO THEIR SQUAD" : "WANTS TO BE FRIENDS", "ENTER ACCEPT   BACKSPACE DECLINE"});
  }
  for (size_t i = 0; i < c.remotes().size(); i++) {
    const net::Remote& r = c.remotes()[i];
    bool isFriend = false;
    for (const net::Friend& f : c.friends()) isFriend |= f.id == r.id;
    bool inSquad = false;
    for (const net::SquadMember& m : c.squad()) inSquad |= m.id == r.id;
    std::string keys;
    if (!inSquad) keys += "I SQUAD INVITE";
    if (!isFriend) keys += std::string(keys.empty() ? "" : "   ") + "F ADD FRIEND";
    out.push_back({Row::Here, i, r.name, inSquad ? "IN YOUR SQUAD" : (isFriend ? "FRIEND, HERE" : "HERE"), keys});
  }
  for (size_t i = 0; i < c.friends().size(); i++) {
    const net::Friend& f = c.friends()[i];
    std::string where = f.online ? (f.instance.empty() ? std::string("ONLINE") : f.instance) : std::string("OFFLINE");
    if (f.online && !f.activity.empty() && f.activity != "hub") where += "  (" + f.activity + ")";
    out.push_back({Row::Friend, i, f.name, where, f.online ? "I SQUAD INVITE" : ""});
  }
  if (!c.squad().empty()) out.push_back({Row::Leave, 0, "LEAVE SQUAD", "", "ENTER"});
  return out;
}

std::string upper(std::string s) {
  for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
  return s;
}

}  // namespace

void collectRemotes(const net::Client& client, float time, std::vector<DrawItem>& out) {
  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& sph = HostileGeometry::unitSphere();
  for (const net::Remote& r : client.remotes()) {
    const bool downed = (r.flags & 2u) != 0 || r.hp <= 0.0f;
    glm::mat4 root = glm::translate(glm::mat4(1.0f), r.pos);
    root = glm::rotate(root, glm::radians(-(r.yaw + 90.0f)), glm::vec3(0, 1, 0));
    if (downed) {
      root = glm::translate(root, glm::vec3(0, 0.25f, 0));
      root = glm::rotate(root, glm::radians(-82.0f), glm::vec3(1, 0, 0));
    }

    DrawItem plate;
    plate.material = MaterialType::Armour;
    plate.tint = colourFor(r.id);
    plate.metallic = 0.7f;
    plate.roughness = 0.35f;
    plate.wear = 0.6f;
    DrawItem under = plate;
    under.tint = glm::vec3(0.10f, 0.11f, 0.13f);
    under.metallic = 0.2f;
    under.roughness = 0.7f;
    DrawItem lit = plate;
    lit.material = MaterialType::Emissive;
    lit.emissive = r.squadmate ? kSquad : kOther;
    if (r.id == client.leader() && client.leader() != 0) lit.emissive = glm::vec3(1.0f, 0.82f, 0.30f) * (1.4f + 0.6f * std::sin(time * 6.0f));
    lit.emissiveIntensity = 2.2f;
    lit.castShadow = false;

    const float swing = std::sin(r.walk) * 28.0f;
    // Legs, hinged at the hip so they swing.
    for (int s = -1; s <= 1; s += 2) {
      glm::mat4 hip = glm::translate(root, glm::vec3(0.13f * s, 0.92f, 0.0f));
      hip = glm::rotate(hip, glm::radians(swing * (float)s), glm::vec3(1, 0, 0));
      part(out, under, box, hip, {0, -0.25f, 0}, {0.17f, 0.50f, 0.19f});
      part(out, plate, box, hip, {0, -0.66f, -0.02f}, {0.16f, 0.42f, 0.20f});
      part(out, plate, box, hip, {0, -0.20f, -0.10f}, {0.14f, 0.22f, 0.04f});   // thigh plate
      part(out, under, box, hip, {0, -0.89f, -0.05f}, {0.17f, 0.08f, 0.28f});   // boot
    }
    // Torso, chest plate, the light strip across it, and the pack.
    part(out, under, box, root, {0, 1.02f, 0}, {0.40f, 0.22f, 0.24f});
    part(out, plate, box, root, {0, 1.30f, 0}, {0.46f, 0.40f, 0.28f});
    part(out, lit, box, root, {0, 1.36f, -0.145f}, {0.30f, 0.035f, 0.01f});
    part(out, under, box, root, {0, 1.30f, 0.20f}, {0.32f, 0.36f, 0.14f});
    // Shoulders.
    for (int s = -1; s <= 1; s += 2) part(out, plate, sph, root, {0.29f * s, 1.47f, 0}, {0.20f, 0.17f, 0.22f});
    // Head and visor, tilted with where they look.
    glm::mat4 neck = glm::translate(root, glm::vec3(0, 1.62f, 0));
    neck = glm::rotate(neck, glm::radians(std::clamp(r.pitch, -60.0f, 60.0f) * 0.5f), glm::vec3(1, 0, 0));
    part(out, plate, sph, neck, {0, 0.12f, 0}, {0.26f, 0.28f, 0.27f});
    DrawItem visor = lit;
    visor.emissiveIntensity = 1.4f;
    part(out, visor, box, neck, {0, 0.13f, -0.12f}, {0.20f, 0.07f, 0.05f});
    // Arms up at the gun, which points where they aim.
    glm::mat4 aim = glm::translate(root, glm::vec3(0, 1.36f, 0));
    aim = glm::rotate(aim, glm::radians(std::clamp(r.pitch, -80.0f, 80.0f)), glm::vec3(1, 0, 0));
    for (int s = -1; s <= 1; s += 2) part(out, under, box, aim, {0.20f * s, -0.04f, -0.22f}, {0.09f, 0.09f, 0.40f});
    const bool big = r.weapon == "launcher" || r.weapon == "lmg";
    const float len = r.weapon == "sniper" ? 1.05f : big ? 0.95f : r.weapon == "pistol" ? 0.30f : 0.72f;
    DrawItem gun = under;
    gun.tint = glm::vec3(0.18f, 0.19f, 0.21f);
    gun.metallic = 0.85f;
    gun.roughness = 0.3f;
    const float thick = big ? 0.15f : 0.09f;
    part(out, gun, box, aim, {0.08f, -0.02f, -0.30f - len * 0.5f}, {thick, thick * 1.3f, len});
    if (r.firedT > 0.0f) {
      DrawItem flash = lit;
      flash.emissive = glm::vec3(1.0f, 0.75f, 0.35f);
      flash.emissiveIntensity = 7.0f;
      part(out, flash, sph, aim, {0.08f, -0.02f, -0.32f - len}, {0.16f, 0.16f, 0.22f});
    }
  }
}

bool socialInput(GLFWwindow* window, Social& social, net::Client& client) {
  if (!client.enabled()) {
    social.open = false;
    return false;
  }
  const int keys[8] = {GLFW_KEY_O, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_ENTER,
                       GLFW_KEY_F, GLFW_KEY_I, GLFW_KEY_BACKSPACE, GLFW_KEY_TAB};
  bool hit[8];
  for (int k = 0; k < 8; k++) {
    const bool down = glfwGetKey(window, keys[k]) == GLFW_PRESS;
    hit[k] = down && !social.prev[k];
    social.prev[k] = down;
  }
  if (hit[0]) social.open = !social.open;
  if (!social.open) return false;
  std::vector<Row> list = rows(client);
  if (list.empty()) {
    social.cursor = 0;
    return true;
  }
  if (hit[1]) social.cursor = (social.cursor + (int)list.size() - 1) % (int)list.size();
  if (hit[2] || hit[7]) social.cursor = (social.cursor + 1) % (int)list.size();
  social.cursor = std::clamp(social.cursor, 0, (int)list.size() - 1);
  const Row& row = list[social.cursor];
  auto& invites = client.invites();
  switch (row.kind) {
    case Row::Invite:
      if ((hit[3] || hit[6]) && row.index < invites.size()) {
        if (hit[3]) client.accept(invites[row.index]);
        invites.erase(invites.begin() + (long)row.index);
      }
      break;
    case Row::Here: {
      const net::Remote& r = client.remotes()[row.index];
      if (hit[4]) client.requestFriend(r.id);
      if (hit[5]) client.inviteToSquad(r.id);
      break;
    }
    case Row::Friend: {
      const net::Friend& f = client.friends()[row.index];
      if (hit[5] && f.online) client.inviteToSquad(f.id);
      break;
    }
    case Row::Leave:
      if (hit[3]) client.leaveSquad();
      break;
  }
  return true;
}

void drawHud(Hud& hud, int screenW, int screenH, const net::Client& client, const glm::mat4& viewProj,
             const glm::vec3& eye, const Social& social, const Pvp& pvp) {
  if (!client.enabled()) return;
  hud.begin(screenW, screenH);
  const glm::vec4 dim(0.72f, 0.78f, 0.86f, 0.9f);
  const glm::vec4 bright(0.92f, 0.96f, 1.0f, 0.98f);
  const glm::vec4 cyan(kSquad, 0.98f);
  const glm::vec4 amber(kOther, 0.95f);

  // ---- names over heads
  for (const net::Remote& r : client.remotes()) {
    glm::vec2 at;
    const glm::vec3 head = r.pos + glm::vec3(0, 2.15f, 0);
    if (!Hud::worldToScreen(viewProj, head, screenW, screenH, at)) continue;
    const float dist = glm::length(head - eye);
    // The leader of a PvP match is singing, and the song carries: everyone
    // sees where they are, through walls, at any range.
    if (pvp.on && r.id == client.leader()) {
      const glm::vec4 gold(1.0f, 0.82f, 0.30f, 0.95f);
      hud.textCentered(at.x, at.y - 22, "SINGING", 1.5f, gold);
      hud.textCentered(at.x, at.y, r.name, 1.8f, gold);
      char m[16];
      std::snprintf(m, sizeof(m), "%dM", (int)dist);
      hud.textCentered(at.x, at.y + 18, m, 1.3f, gold);
      continue;
    }
    if (dist > 120.0f) continue;
    const float a = 1.0f - std::clamp((dist - 20.0f) / 100.0f, 0.0f, 0.7f);
    const float scale = dist > 30.0f ? 1.4f : 1.8f;
    const glm::vec4 col = r.squadmate ? glm::vec4(kSquad, a) : glm::vec4(kOther, a);
    const float w = Hud::textWidth(r.name, scale);
    hud.rect(at.x - w * 0.5f - 6, at.y - 4, w + 12, scale * 7.0f + 8, glm::vec4(0.03f, 0.04f, 0.06f, 0.5f * a));
    hud.textCentered(at.x, at.y, r.name, scale, col);
    // Health under the name for squad mates — the people you are covering.
    if (r.squadmate) {
      const float bw = std::max(w, 50.0f);
      hud.rect(at.x - bw * 0.5f, at.y + scale * 7.0f + 6, bw, 3, glm::vec4(0.1f, 0.1f, 0.12f, 0.7f * a));
      hud.rect(at.x - bw * 0.5f, at.y + scale * 7.0f + 6, bw * std::clamp(r.hp, 0.0f, 1.0f), 3, col);
    }
  }

  // ---- corner panel: where you are and who you are with
  float x = screenW - 330.0f, y = 120.0f;
  hud.rect(x - 12, y - 10, 318, 30 + 22.0f * (float)(client.squad().empty() ? 1 : client.squad().size() + 1),
           glm::vec4(0.03f, 0.04f, 0.06f, 0.5f));
  const std::string where = client.connected()
                                ? (client.instance().empty() ? std::string("CONNECTING") : client.instance())
                                : std::string("SERVER NOT ANSWERING");
  hud.text(x, y, where, 1.7f, client.connected() ? bright : amber);
  y += 24;
  if (client.squad().empty()) {
    hud.text(x, y, "NO SQUAD  -  O FOR SOCIAL", 1.4f, dim);
  } else {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SQUAD LINK %d / %d", (int)client.squad().size(), client.squadLimit());
    hud.text(x, y, buf, 1.5f, cyan);
    y += 22;
    for (const net::SquadMember& m : client.squad()) {
      hud.text(x + 10, y, (m.id == client.id() ? "> " : "  ") + m.name + "   " + upper(m.place), 1.35f, dim);
      y += 22;
    }
  }

  // ---- PvP: the score along the top, the floor timer, the result
  if (pvp.on) {
    const float cx = screenW * 0.5f;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "THE PIT  -  FIRST TO %d", client.scoreLimit());
    hud.rect(cx - 210, 12, 420, 26.0f + 22.0f * (float)std::min<size_t>(client.score().size(), 6),
             glm::vec4(0.03f, 0.04f, 0.06f, 0.55f));
    hud.textCentered(cx, 18, buf, 1.6f, amber);
    float sy = 42;
    for (size_t i = 0; i < client.score().size() && i < 6; i++) {
      const net::ScoreRow& row = client.score()[i];
      const bool me = row.id == client.id();
      const bool lead = row.id == client.leader();
      std::snprintf(buf, sizeof(buf), "%s%-14s %3d", lead ? "> " : "  ", row.name.c_str(), row.downs);
      hud.textCentered(cx, sy, buf, 1.5f,
                       lead ? glm::vec4(1.0f, 0.82f, 0.30f, 0.98f) : me ? cyan : dim);
      sy += 22;
    }
    if (client.leader() == client.id())
      hud.textCentered(cx, screenH - 120.0f, "YOU ARE SINGING - EVERYONE CAN SEE YOU", 1.8f,
                       glm::vec4(1.0f, 0.82f, 0.30f, 0.95f));
    if (pvp.respawnIn > 0.0f) {
      std::snprintf(buf, sizeof(buf), "DOWN  -  UP AGAIN IN %.1f", pvp.respawnIn);
      hud.rect(0, screenH * 0.5f - 30, (float)screenW, 60, glm::vec4(0.25f, 0.03f, 0.03f, 0.45f));
      hud.textCentered(cx, screenH * 0.5f - 10, buf, 2.6f, bright);
    }
    if (pvp.hitMarker > 0.0f) {
      const float a = std::clamp(pvp.hitMarker * 4.0f, 0.0f, 1.0f);
      const float cy = screenH * 0.5f;
      for (int k = 0; k < 4; k++) {
        const float dx = (k & 1) ? 9.0f : -15.0f, dy = (k & 2) ? 9.0f : -15.0f;
        hud.rect(cx + dx, cy + dy, 6, 6, glm::vec4(1.0f, 0.36f, 0.30f, a));
      }
    }
    if (!client.winner().empty()) {
      const std::string line = client.winner() == client.name() ? "YOU WIN THE PIT" : upper(client.winner()) + " WINS THE PIT";
      hud.rect(0, screenH * 0.36f - 16, (float)screenW, 70, glm::vec4(0.03f, 0.04f, 0.06f, 0.75f));
      hud.textCentered(cx, screenH * 0.36f, line, 3.2f, glm::vec4(1.0f, 0.82f, 0.30f, 0.98f));
      hud.textCentered(cx, screenH * 0.36f + 34, "NEXT MATCH IN A MOMENT", 1.5f, dim);
    }
  }

  // ---- notices
  float ny = screenH * 0.30f;
  for (const auto& n : client.notices()) {
    const float a = std::clamp(n.second, 0.0f, 1.0f);
    hud.textCentered(screenW * 0.5f, ny, n.first, 1.8f, glm::vec4(cyan.r, cyan.g, cyan.b, a));
    ny += 24;
  }

  // ---- the social screen
  if (social.open) {
    const float w = std::min(820.0f, screenW - 64.0f);
    const float left = screenW * 0.5f - w * 0.5f, top = screenH * 0.16f;
    std::vector<Row> list = rows(client);
    const float h = 120.0f + 34.0f * std::max<size_t>(list.size(), 1) + 40.0f;
    hud.rect(left, top, w, h, glm::vec4(0.03f, 0.04f, 0.06f, 0.9f));
    hud.rect(left, top, w, 3, cyan);
    hud.text(left + 24, top + 20, "SOCIAL", 3.0f, cyan);
    hud.text(left + 24 + Hud::textWidth("SOCIAL", 3.0f) + 20, top + 28, client.name() + "  -  " + where, 1.6f, dim);
    float ry = top + 76;
    std::string section;
    if (list.empty()) hud.text(left + 24, ry, "NOBODY ELSE IS HERE, AND NO FRIENDS YET.", 1.7f, dim);
    for (size_t i = 0; i < list.size(); i++) {
      const Row& r = list[i];
      const char* sec = r.kind == Row::Invite ? "INVITES" : r.kind == Row::Here ? "HERE" : r.kind == Row::Friend ? "FRIENDS" : "";
      if (section != sec && *sec) {
        section = sec;
        hud.text(left + 24, ry, sec, 1.4f, amber);
        ry += 20;
      }
      const bool on = (int)i == social.cursor;
      if (on) hud.rect(left + 14, ry - 6, w - 28, 30, glm::vec4(0.35f, 0.95f, 1.0f, 0.12f));
      hud.text(left + 30, ry, r.label, 1.8f, on ? bright : dim);
      hud.text(left + 260, ry + 2, r.detail, 1.4f, dim);
      if (on && !r.keys.empty()) hud.text(left + w - 24 - Hud::textWidth(r.keys, 1.4f), ry + 2, r.keys, 1.4f, cyan);
      ry += 30;
    }
    hud.textCentered(screenW * 0.5f, top + h - 30, "UP DOWN OR TAB  CHOOSE      O  CLOSE", 1.5f, dim);
  }
  hud.end();
}

}  // namespace netview
