#include "Hostile.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------- shared geometry

namespace {
Mesh g_unitBox, g_unitSphere;
bool g_built = false;
}

void HostileGeometry::ensure() {
  if (g_built) return;
  g_unitBox = Mesh::box(1.0f, 1.0f, 1.0f);
  g_unitSphere = Mesh::sphere(1.0f, 10, 8);
  g_built = true;
}
void HostileGeometry::destroyShared() {
  if (!g_built) return;
  g_unitBox.destroy();
  g_unitSphere.destroy();
  g_built = false;
}
const Mesh& HostileGeometry::unitBox() { return g_unitBox; }
const Mesh& HostileGeometry::unitSphere() { return g_unitSphere; }

// ------------------------------------------------------------------- spawn

void Hostile::spawn(const EnemyType* t, glm::vec3 at, bool boss, float hpMult) {
  type = t;
  pos = at;
  vel = glm::vec3(0.0f);
  yaw = 0.0f;
  maxHp = t->hp * hpMult;
  hp = maxHp;
  state = HostileState::Idle;
  cooldown = 0.0f;
  deathT = 0.0f;
  bob = 0.0f;
  hitFlash = 0.0f;
  isBoss = boss;
  bossScale = boss ? 1.35f : 1.0f;
  stuckT = 0.0f;
  // Deterministic per-instance handedness for the stuck-avoidance steer
  // below, from the spawn position rather than a global RNG — cheap, and
  // two hostiles that spawn at different points reliably pick differently.
  avoidSide = (std::fmod(std::abs(at.x * 12.9898f + at.z * 78.233f), 1.0f) < 0.5f) ? 1.0f : -1.0f;
}

// -------------------------------------------------------------------- AI

bool Hostile::update(float dt, const glm::vec3& playerPos, const Level& level) {
  hitFlash = std::max(0.0f, hitFlash - dt * 4.0f);

  if (state == HostileState::Dying) {
    deathT += dt;
    if (deathT > 1.4f) state = HostileState::Gone;
    return false;
  }
  if (state == HostileState::Gone) return false;

  glm::vec3 toPlayer = playerPos - pos;
  toPlayer.y = 0.0f;
  float dist = glm::length(toPlayer);
  glm::vec3 dir = dist > 1e-4f ? toPlayer / dist : glm::vec3(0, 0, 1);

  const float alertRadius = 26.0f;
  bool attackReady = cooldown <= 0.0f;

  switch (state) {
    case HostileState::Idle:
      if (dist < alertRadius) state = HostileState::Chase;
      break;

    case HostileState::Chase: {
      if (dist <= type->attackRange) { state = HostileState::Attack; break; }
      float speed = type->speed;
      // Blend in a perpendicular bias once stuck — see the stuckT comment
      // in Hostile.h. steerDir, not dir, is what actually drives velocity;
      // dir (the true bearing to the player) still drives facing, so a
      // stuck hostile visibly sidesteps rather than turning to face the
      // direction it's sliding.
      glm::vec3 steerDir = dir;
      if (stuckT > 0.25f) {
        glm::vec3 perp(-dir.z, 0.0f, dir.x);
        steerDir = glm::normalize(dir + perp * (avoidSide * 1.3f));
      }
      vel.x = steerDir.x * speed;
      vel.z = steerDir.z * speed;
      glm::vec3 before = pos;
      pos += vel * dt;
      level.resolve(pos, type->radius, type->height);
      float moved = glm::length(glm::vec2(pos.x, pos.z) - glm::vec2(before.x, before.z));
      if (moved < speed * dt * 0.35f) stuckT += dt; else stuckT = std::max(0.0f, stuckT - dt * 2.0f);
      yaw = std::atan2(dir.x, dir.z);
      break;
    }

    case HostileState::Attack: {
      // face the player and, for a ranged type, keep a little distance;
      // a melee type keeps closing so it actually lands the hit.
      yaw = std::atan2(dir.x, dir.z);
      if (dist > type->attackRange * 1.15f) { state = HostileState::Chase; break; }
      if (!type->ranged && dist > type->radius + 0.6f) {
        glm::vec3 steerDir = dir;
        if (stuckT > 0.25f) {
          glm::vec3 perp(-dir.z, 0.0f, dir.x);
          steerDir = glm::normalize(dir + perp * (avoidSide * 1.3f));
        }
        vel.x = steerDir.x * type->speed * 0.6f;
        vel.z = steerDir.z * type->speed * 0.6f;
        glm::vec3 before = pos;
        pos += vel * dt;
        level.resolve(pos, type->radius, type->height);
        float moved = glm::length(glm::vec2(pos.x, pos.z) - glm::vec2(before.x, before.z));
        if (moved < type->speed * 0.6f * dt * 0.35f) stuckT += dt; else stuckT = std::max(0.0f, stuckT - dt * 2.0f);
      } else {
        vel = glm::vec3(0.0f);
      }
      break;
    }

    default: break;
  }

  bob += dt * (1.4f + glm::length(glm::vec2(vel.x, vel.z)) * 1.6f);

  if (state == HostileState::Attack) {
    cooldown -= dt;
    if (attackReady && cooldown <= 0.0f) {
      cooldown = type->attackRate;
      return true;   // Game applies type->damage to the player this frame
    }
  }
  return false;
}

bool Hostile::takeDamage(float amount) {
  if (state == HostileState::Dying || state == HostileState::Gone) return false;
  hp -= amount;
  hitFlash = 1.0f;
  if (hp <= 0.0f) {
    hp = 0.0f;
    state = HostileState::Dying;
    deathT = 0.0f;
    return true;
  }
  if (state == HostileState::Idle) state = HostileState::Chase;   // getting shot is alerting
  return false;
}

// ------------------------------------------------------------------- pose

namespace {
void part(std::vector<DrawItem>& out, const DrawItem& base, const Mesh& mesh,
          const glm::mat4& parent, glm::vec3 localPos, glm::vec3 eulerDeg, glm::vec3 scale) {
  glm::mat4 m = glm::translate(parent, localPos);
  if (eulerDeg.y != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.y), glm::vec3(0, 1, 0));
  if (eulerDeg.x != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.x), glm::vec3(1, 0, 0));
  if (eulerDeg.z != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.z), glm::vec3(0, 0, 1));
  m = glm::scale(m, scale);
  DrawItem it = base;
  it.mesh = &mesh;
  it.model = m;
  out.push_back(it);
}
}  // namespace

void Hostile::collect(std::vector<DrawItem>& out) const {
  if (state == HostileState::Gone) return;
  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& sph = HostileGeometry::unitSphere();

  float h = type->height * bossScale, r = type->radius * bossScale;
  float stride = std::min(1.0f, glm::length(glm::vec2(vel.x, vel.z)) / std::max(0.1f, type->speed));
  float swing = std::sin(bob) * std::min(0.6f, 0.16f + stride * 0.5f);

  float deathTip = 0.0f, deathSink = 0.0f, deathFade = 0.0f;
  if (state == HostileState::Dying) {
    float t = std::min(1.0f, deathT / 0.9f);
    deathTip = -t * 85.0f;
    deathSink = -t * 0.5f;
    deathFade = t;
  }

  glm::mat4 root = glm::translate(glm::mat4(1.0f), pos + glm::vec3(0, h * 0.5f + deathSink, 0));
  root = glm::rotate(root, yaw, glm::vec3(0, 1, 0));
  root = glm::rotate(root, glm::radians(deathTip), glm::vec3(1, 0, 0));

  DrawItem base;
  base.tint = type->colour;
  base.metallic = 0.85f;
  base.roughness = 0.25f;
  base.wear = isBoss ? 1.5f : 1.1f;
  base.anisoStrength = 0.4f;
  base.material = MaterialType::Armour;
  // The hit flash and death fade both ride the armour's own emissive slot —
  // a red pulse on a hit, fading opacity as it dies. This project's
  // materials don't support real transparency sorting for a handful of
  // enemies, so "fading" is approximated by dimming towards the tint's own
  // dark end rather than true alpha blending.
  base.emissive = glm::vec3(1.0f, 0.15f, 0.1f);
  base.emissiveIntensity = hitFlash * 1.6f;
  if (deathFade > 0.0f) base.tint *= (1.0f - deathFade * 0.7f);

  part(out, base, box, root, {0, -h * 0.42f, 0}, {0, 0, 0}, {r * 1.5f, h * 0.10f, r * 1.0f});      // pelvis
  part(out, base, box, root, {0, -h * 0.32f, 0}, {stride * 6.0f, 0, 0}, {r * 1.9f, h * 0.20f, r * 1.1f}); // chest

  glm::vec3 headPos{0, h * 0.02f, -r * 0.05f};
  part(out, base, box, root, headPos, {0, -swing * 10.0f, 0}, {r * 0.8f, r * 0.74f, r * 0.85f});   // head

  DrawItem visor = base;
  visor.material = MaterialType::Emissive;
  visor.emissive = hitFlash > 0.05f ? glm::vec3(1.0f, 0.2f, 0.15f) : type->glow;
  visor.emissiveIntensity = hitFlash > 0.05f ? hitFlash * 2.0f : 1.3f;
  part(out, visor, box, root, headPos + glm::vec3(0, 0, -r * 0.42f), {0, 0, 0},
       {r * 0.5f, r * 0.16f, r * 0.06f});

  DrawItem core = base;
  core.mesh = &sph;
  core.material = MaterialType::Emissive;
  core.emissive = type->glow;
  core.emissiveIntensity = 0.85f;
  part(out, core, sph, root, {0, -h * 0.28f, -r * 0.55f}, {0, 0, 0}, {r * 0.22f, r * 0.22f, r * 0.13f});

  for (int side = -1; side <= 1; side += 2) {
    float s = (float)side;
    float legSwing = swing * s;
    float kneeBend = std::max(0.0f, -legSwing) * 90.0f + 4.0f;

    part(out, base, sph, root, {s * r * 0.55f, -h * 0.05f, 0}, {0, 0, -s * 10.0f},
         {r * 0.42f, r * 0.30f, r * 0.38f});                                        // pauldron
    part(out, base, box, root, {s * r * 0.48f, -h * 0.22f, 0}, {-legSwing * 40.0f, 0, 0},
         {r * 0.22f, h * 0.19f, r * 0.22f});                                        // upper arm
    part(out, base, box, root, {s * r * 0.48f, -h * 0.42f, -r * 0.10f}, {-18.0f - legSwing * 30.0f, 0, 0},
         {r * 0.19f, h * 0.17f, r * 0.19f});                                        // forearm

    glm::vec3 hip{s * r * 0.42f, -h * 0.44f, 0};
    glm::mat4 hipM = glm::rotate(glm::translate(root, hip), glm::radians(legSwing * 55.0f), glm::vec3(1, 0, 0));
    DrawItem thigh = base; thigh.mesh = &box;
    thigh.model = glm::scale(glm::translate(hipM, {0, -h * 0.12f, 0}), {r * 0.24f, h * 0.19f, r * 0.24f});
    out.push_back(thigh);

    glm::mat4 kneeM = glm::rotate(glm::translate(hipM, {0, -h * 0.24f, 0}), glm::radians(kneeBend), glm::vec3(1, 0, 0));
    DrawItem shin = base; shin.mesh = &box;
    shin.model = glm::scale(glm::translate(kneeM, {0, -h * 0.11f, 0}), {r * 0.19f, h * 0.17f, r * 0.19f});
    out.push_back(shin);

    DrawItem foot = base; foot.mesh = &box;
    foot.model = glm::scale(glm::translate(kneeM, {0, -h * 0.22f, -r * 0.12f}), {r * 0.34f, h * 0.045f, r * 0.7f});
    out.push_back(foot);
  }
}
