#include "Hostile.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------- shared geometry

namespace {
Mesh g_unitBox, g_unitSphere, g_unitCylinder, g_unitTaper;
bool g_built = false;
}

void HostileGeometry::ensure() {
  if (g_built) return;
  g_unitBox = Mesh::box(1.0f, 1.0f, 1.0f);
  // Unit diameter, like the box and the cylinders below, so a part's scale
  // always means the size of the thing rather than half of it.
  g_unitSphere = Mesh::sphere(0.5f, 10, 8);
  // Unit diameter, not unit radius, so a part's scale reads as the size of
  // the thing rather than twice it — the same convention Mesh::box uses.
  g_unitCylinder = Mesh::cylinder(0.5f, 0.5f, 1.0f, 10);
  g_unitTaper = Mesh::cylinder(0.36f, 0.5f, 1.0f, 10);
  g_built = true;
}
void HostileGeometry::destroyShared() {
  if (!g_built) return;
  g_unitBox.destroy();
  g_unitSphere.destroy();
  g_unitCylinder.destroy();
  g_unitTaper.destroy();
  g_built = false;
}
const Mesh& HostileGeometry::unitBox() { return g_unitBox; }
const Mesh& HostileGeometry::unitSphere() { return g_unitSphere; }
const Mesh& HostileGeometry::unitCylinder() { return g_unitCylinder; }
const Mesh& HostileGeometry::unitTaper() { return g_unitTaper; }

// ------------------------------------------------------------------- spawn

void Hostile::spawn(const EnemyType* t, glm::vec3 at, bool boss, float hpMult) {
  type = t;
  pos = at;
  vel = glm::vec3(0.0f);
  yaw = 0.0f;
  maxHp = t->hp * hpMult;
  hp = maxHp;
  state = HostileState::Idle;
  // Not zero: a zero cooldown means the first frame a hostile acquires you
  // is also the frame it lands a hit, so a squad that spawns already inside
  // its own firing range chips a fifth of your health off before you have
  // even turned around — and in unison, since they all start at the same
  // value. Staggering the first shot per-instance (deterministically, off
  // the spawn point, like avoidSide below) buys a moment to react and
  // spreads the incoming fire out into something readable.
  cooldown = t->attackRate * (0.75f + std::fmod(std::abs(at.x * 3.71f + at.z * 9.17f), 1.0f) * 0.9f);
  deathT = 0.0f;
  bob = 0.0f;
  hitFlash = 0.0f;
  isBoss = boss;
  bossScale = boss ? 1.35f : 1.0f;
  stuckT = 0.0f;
  stunT = 0.0f;
  // Deterministic per-instance handedness for the stuck-avoidance steer
  // below, from the spawn position rather than a global RNG — cheap, and
  // two hostiles that spawn at different points reliably pick differently.
  avoidSide = (std::fmod(std::abs(at.x * 12.9898f + at.z * 78.233f), 1.0f) < 0.5f) ? 1.0f : -1.0f;
}

// -------------------------------------------------------------------- AI

bool Hostile::update(float dt, const glm::vec3& playerPos, const Level& level) {
  hitFlash = std::max(0.0f, hitFlash - dt * 4.0f);

  // Stunned: it stands where it is and does not shoot. Its own attack timer
  // keeps running down, so the first thing it does when it comes back is not
  // an instant hit — the window the ability bought would be worth nothing if
  // the whole squad fired the moment it closed.
  if (stunT > 0.0f) {
    stunT -= dt;
    cooldown = std::max(cooldown, 0.35f);
    vel = glm::vec3(0.0f);
    bob += dt * 0.6f;
    return false;
  }

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

  // Comfortably wider than any mission's spawn ring, so a wave advances on
  // you from the drop instead of standing around until you wander within
  // exactly its radius. It was 26m, which happened to equal a spawn ring
  // and left that whole wave permanently Idle — a mission you could stand
  // still in forever, and one that could never be completed without
  // hunting every straggler down. Arena waves should come to you.
  const float alertRadius = 40.0f;
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

  const float h = type->height * bossScale, r = type->radius * bossScale;
  const float planar = glm::length(glm::vec2(vel.x, vel.z));
  const float stride = std::min(1.0f, planar / std::max(0.1f, type->speed));
  const float swing = std::sin(bob) * std::min(0.62f, 0.16f + stride * 0.20f);

  float deathTip = 0.0f, deathSink = 0.0f, deathFade = 0.0f;
  if (state == HostileState::Dying) {
    float t = std::min(1.0f, deathT / 0.9f);
    deathTip = -t * 85.0f;
    deathSink = -t * 0.5f;
    deathFade = t;
  }

  // The rig is built from the feet up, so every part's height is the same
  // fraction of `height` the browser build uses and the two stay in step.
  // The death topple pivots around hip height rather than the feet, because
  // a body rotating about its heels reads as a falling plank.
  glm::mat4 root = glm::translate(glm::mat4(1.0f), pos + glm::vec3(0, deathSink, 0));
  root = glm::rotate(root, yaw, glm::vec3(0, 1, 0));
  if (deathTip != 0.0f) {
    root = glm::translate(root, glm::vec3(0, h * 0.46f, 0));
    root = glm::rotate(root, glm::radians(deathTip), glm::vec3(1, 0, 0));
    root = glm::translate(root, glm::vec3(0, -h * 0.46f, 0));
  }

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

  if (type->flying) collectFlyer(out, base, root, h, r, stride);
  else collectWalker(out, base, root, h, r, swing, stride);
}

namespace {
// A joint: a translation, then up to one rotation about each axis, in the
// order the browser rigs apply them. Parts hang off the result, and child
// joints are built from it, which is what makes an elbow follow a shoulder.
glm::mat4 joint(const glm::mat4& parent, glm::vec3 offset,
                float rxRad = 0.0f, float ryRad = 0.0f, float rzRad = 0.0f) {
  glm::mat4 m = glm::translate(parent, offset);
  if (ryRad != 0.0f) m = glm::rotate(m, ryRad, glm::vec3(0, 1, 0));
  if (rxRad != 0.0f) m = glm::rotate(m, rxRad, glm::vec3(1, 0, 0));
  if (rzRad != 0.0f) m = glm::rotate(m, rzRad, glm::vec3(0, 0, 1));
  return m;
}
}  // namespace

// ---------------------------------------------------------------- the walker
//
// Ported from buildWalker/poseWalker in src/js/fps/hostiles.js. The old rig
// here was a pelvis, a chest, a head and four limbs — readable, which is what
// a shooter needs, but it read as a placeholder, because what makes a machine
// look like a machine is parts that overlap: plates sloping off the shoulders
// into a collar, a head sunk into it rather than parked on top, ribs down the
// front instead of one slab, and something venting at the back.
//
// Head centre sits at 93% of height and the torso at 55%, because that is
// where headCentre()/bodyCentre() put the hit spheres. The silhouette is
// built around those two points rather than the other way round — a rig whose
// head is not where the head hitbox is means headshots land on air.
void Hostile::collectWalker(std::vector<DrawItem>& out, const DrawItem& base,
                            const glm::mat4& root, float h, float r,
                            float swing, float stride) const {
  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& sph = HostileGeometry::unitSphere();
  const Mesh& cyl = HostileGeometry::unitCylinder();
  const Mesh& tpr = HostileGeometry::unitTaper();
  const bool heavy = isBoss || type->elite;
  const float w = heavy ? 1.25f : 1.0f;

  DrawItem glow = base;
  glow.material = MaterialType::Emissive;
  glow.emissive = hitFlash > 0.05f ? glm::vec3(1.0f, 0.2f, 0.15f) : type->glow;
  glow.emissiveIntensity = hitFlash > 0.05f ? hitFlash * 2.0f : 1.15f;
  // Stunned, its optics gutter: the only way to tell at a glance which of a
  // dozen frames the pulse actually caught.
  if (stunned()) {
    glow.emissive = glm::vec3(0.35f, 0.55f, 1.0f);
    glow.emissiveIntensity = 0.25f + 0.2f * std::sin(bob * 9.0f);
  }

  // Rise and fall twice per stride: a body that stays at one height while its
  // legs swing is the other half of why a fake walk looks fake.
  const float hipLift = std::fabs(std::sin(bob)) * h * 0.02f * stride;
  glm::mat4 hips = joint(root, {0, h * 0.46f + hipLift, 0}, 0.0f, swing * 0.10f);
  part(out, base, box, hips, {0, 0, 0}, {0, 0, 0}, {r * 1.5f * w, h * 0.10f, r * 1.0f});

  // Lean into the movement, with a slow breath when standing still so an
  // idle enemy is not a statue.
  const float lean = stride < 0.05f ? 0.07f + std::sin(bob * 0.6f) * 0.012f
                                    : 0.07f + stride * 0.11f;
  glm::mat4 spine = joint(hips, {0, h * 0.05f, 0}, lean);
  part(out, base, box, spine, {0, h * 0.055f, 0}, {0, 0, 0}, {r * 1.30f * w, h * 0.13f, r * 0.92f});

  // The torso counter-rotates against the legs.
  glm::mat4 chest = joint(spine, {0, h * 0.14f, 0}, 0.0f, -swing * 0.16f);
  part(out, base, box, chest, {0, 0, 0}, {0, 0, 0}, {r * 1.95f * w, h * 0.20f, r * 1.15f});
  part(out, base, box, chest, {0, h * 0.11f, heavy ? -r * 0.05f : 0.0f}, {0, 0, 0},
       {r * 1.70f * w, h * 0.05f, r * 1.05f});
  for (int i = 0; i < 3; i++) {
    part(out, base, box, chest, {0, -h * 0.055f + i * h * 0.045f, 0}, {0, 0, 0},
         {r * 1.86f * w, h * 0.022f, r * 1.18f});                                  // ribs
  }
  part(out, glow, cyl, chest, {0, h * 0.005f, -r * 0.60f}, {90, 0, 0},
       {r * 0.26f, r * 0.05f, r * 0.26f});                                         // reactor seam

  // Back pack and its vents.
  part(out, base, box, chest, {0, h * 0.01f, r * 0.72f}, {0, 0, 0},
       {r * 1.30f * w, h * 0.17f, r * 0.34f});
  for (int side = -1; side <= 1; side += 2) {
    float s = (float)side;
    part(out, base, tpr, chest, {s * r * 0.48f, h * 0.10f, r * 0.74f}, {0, 0, 0},
         {r * 0.32f, h * 0.09f, r * 0.32f});
    part(out, glow, tpr, chest, {s * r * 0.48f, h * 0.155f, r * 0.74f}, {0, 0, 0},
         {r * 0.176f, h * 0.050f, r * 0.176f});
  }

  // Collar, and plates sloping up from each shoulder into it. Without them
  // the head reads as a box parked on a shelf; with them the shoulders lead
  // the eye up to it.
  part(out, base, tpr, chest, {0, h * 0.13f, 0}, {0, 0, 0}, {r * 1.44f, h * 0.06f, r * 1.44f});
  for (int side = -1; side <= 1; side += 2) {
    float s = (float)side;
    part(out, base, box, chest, {s * r * 0.52f, h * 0.115f, 0}, {0, 0, s * 24.0f},
         {r * 0.60f, r * 0.30f, r * 0.66f});
  }
  part(out, base, tpr, chest, {0, h * 0.170f, 0}, {0, 0, 0}, {r * 0.84f, h * 0.085f, r * 0.84f});

  // Head: skull, brow, a lit visor slit, and a jaw sunk into the collar. The
  // offset is whatever puts the skull's centre at 93% of height once the hip,
  // spine, chest and neck joints have all been walked through.
  glm::mat4 neck = joint(chest, {0, h * 0.155f, 0});
  glm::mat4 headJ = joint(neck, {0, h * 0.125f - hipLift, 0}, 0.0f, -swing * 0.17f);
  part(out, base, box, headJ, {0, 0, 0}, {0, 0, 0}, {r * 0.94f, r * 0.86f, r * 0.98f});
  part(out, base, box, headJ, {0, r * 0.30f, -r * 0.34f}, {0, 0, 0}, {r * 1.00f, r * 0.18f, r * 0.34f});
  part(out, glow, box, headJ, {0, r * 0.02f, -r * 0.45f}, {0, 0, 0}, {r * 0.58f, r * 0.11f, r * 0.06f});
  part(out, base, box, headJ, {0, -r * 0.30f, -r * 0.12f}, {0, 0, 0}, {r * 0.62f, r * 0.24f, r * 0.66f});

  // Arms and legs. The right elbow is kept so a forearm cannon can hang off
  // it below without rebuilding the chain.
  glm::mat4 rightElbow(1.0f);
  for (int side = -1; side <= 1; side += 2) {
    float s = (float)side;
    float leg = swing * s;

    glm::mat4 shoulder = joint(chest, {s * r * (heavy ? 0.94f : 0.84f), h * 0.055f, 0},
                               -leg * 0.72f, 0.0f, s * (0.06f + stride * 0.05f));
    part(out, base, sph, shoulder, {s * r * 0.10f, h * 0.012f, 0}, {0, 0, 0},
         {r * 1.06f, r * 0.81f, r * 1.09f});                                        // pauldron
    part(out, base, tpr, shoulder, {0, -h * 0.095f, 0}, {0, 0, 0}, {r * 0.34f, h * 0.19f, r * 0.34f});

    glm::mat4 elbow = joint(shoulder, {0, -h * 0.19f, 0}, -0.32f - std::max(0.0f, leg) * 0.55f);
    part(out, base, sph, elbow, {0, 0, 0}, {0, 0, 0}, {r * 0.34f, r * 0.34f, r * 0.34f});
    part(out, base, tpr, elbow, {0, -h * 0.09f, 0}, {0, 0, 0}, {r * 0.30f, h * 0.18f, r * 0.30f});
    part(out, base, box, elbow, {0, -h * 0.195f, -r * 0.04f}, {0, 0, 0},
         {r * 0.22f, r * 0.26f, r * 0.20f});                                        // hand
    if (side > 0) rightElbow = elbow;

    // A knee that bends backwards is the single clearest tell of a fake walk,
    // so the bend is clamped to one direction and never goes fully straight.
    glm::mat4 hip = joint(hips, {s * r * 0.45f, -h * 0.02f, 0}, leg);
    part(out, base, sph, hip, {0, 0, 0}, {0, 0, 0}, {r * 0.52f, r * 0.52f, r * 0.52f});
    part(out, base, tpr, hip, {0, -h * 0.115f, 0}, {0, 0, 0}, {r * 0.48f, h * 0.23f, r * 0.48f});

    glm::mat4 knee = joint(hip, {0, -h * 0.23f, 0}, std::max(0.0f, -leg) * 1.7f + 0.06f);
    part(out, base, sph, knee, {0, 0, 0}, {0, 0, 0}, {r * 0.42f, r * 0.42f, r * 0.42f});
    part(out, base, tpr, knee, {0, -h * 0.115f, 0}, {0, 0, 0}, {r * 0.40f, h * 0.23f, r * 0.40f});
    part(out, base, box, knee, {0, -h * 0.195f, -r * 0.14f}, {0, 0, 0},
         {r * 0.34f, h * 0.045f, r * 0.72f});                                       // foot
  }

  // Ranged types carry the weapon rather than mime it: a shoulder mount for
  // the heavies, a forearm cannon for everyone else.
  if (type->ranged) {
    if (heavy) {
      glm::mat4 mount = joint(chest, {r * 1.05f, h * 0.10f, 0});
      part(out, base, box, mount, {0, r * 0.30f, -h * 0.02f}, {0, 0, 0},
           {r * 0.46f, r * 0.42f, h * 0.30f});
      part(out, base, tpr, mount, {0, r * 0.30f, -h * 0.17f}, {90, 0, 0},
           {r * 0.24f, h * 0.24f, r * 0.24f});
      part(out, glow, cyl, mount, {0, r * 0.30f, -h * 0.29f}, {90, 0, 0},
           {r * 0.26f, r * 0.05f, r * 0.26f});
    } else {
      part(out, base, box, rightElbow, {0, -h * 0.10f, -r * 0.28f}, {0, 0, 0},
           {r * 0.37f, r * 0.34f, h * 0.24f});
      part(out, base, tpr, rightElbow, {0, -h * 0.13f, -r * 0.55f}, {90, 0, 0},
           {r * 0.24f, h * 0.24f, r * 0.24f});
      part(out, glow, cyl, rightElbow, {0, -h * 0.13f, -r * 0.72f}, {90, 0, 0},
           {r * 0.18f, r * 0.05f, r * 0.18f});
    }
  }
}

// ----------------------------------------------------------------- the flyer
//
// Ported from buildFlyer/poseFlyer in src/js/fps/hostiles.js. Everything a
// walker uses to read as a body, replaced with something that reads as a
// machine with nothing inside it: a core in a split cowl, a ring of segments
// that turns around it, one big optic, three fins, a thruster underneath, and
// two manipulator arms hanging below to give it a sense of scale.
void Hostile::collectFlyer(std::vector<DrawItem>& out, const DrawItem& base,
                           const glm::mat4& root, float h, float r,
                           float speedFrac) const {
  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& sph = HostileGeometry::unitSphere();
  const Mesh& cyl = HostileGeometry::unitCylinder();
  const Mesh& tpr = HostileGeometry::unitTaper();

  DrawItem glow = base;
  glow.material = MaterialType::Emissive;
  glow.emissive = hitFlash > 0.05f ? glm::vec3(1.0f, 0.2f, 0.15f) : type->glow;
  glow.emissiveIntensity = hitFlash > 0.05f ? hitFlash * 2.0f : 1.15f;
  // Stunned, its optics gutter: the only way to tell at a glance which of a
  // dozen frames the pulse actually caught.
  if (stunned()) {
    glow.emissive = glm::vec3(0.35f, 0.55f, 1.0f);
    glow.emissiveIntensity = 0.25f + 0.2f * std::sin(bob * 9.0f);
  }

  // It hovers rather than stands, so it hangs off one joint at 62% of height
  // and rolls a little as it drifts. That is also where headCentre() is
  // closest to, which keeps a shot at the optic landing on the hit sphere.
  glm::mat4 core = joint(root, {0, h * 0.62f, 0},
                         0.06f + std::min(0.3f, speedFrac * 0.24f), 0.0f,
                         std::sin(bob * 0.7f) * 0.10f);

  part(out, base, sph, core, {0, 0, 0}, {0, 0, 0}, {r * 1.24f, r * 1.07f, r * 1.39f});
  part(out, base, tpr, core, {0, r * 0.34f, 0}, {0, 0, 0}, {r * 1.32f, r * 0.30f, r * 1.32f});
  // The lower cowl flares the other way, so its taper is flipped end for end
  // rather than given a negative scale — a mirrored scale reverses winding
  // and the face would be culled.
  part(out, base, tpr, core, {0, -r * 0.36f, 0}, {0, 0, 180}, {r * 1.20f, r * 0.34f, r * 1.20f});

  // The optic, recessed in a rim so it reads as a lens rather than a dot.
  part(out, base, tpr, core, {0, 0, -r * 0.62f}, {90, 0, 0}, {r * 0.56f, r * 0.16f, r * 0.56f});
  part(out, glow, cyl, core, {0, 0, -r * 0.70f}, {90, 0, 0}, {r * 0.38f, r * 0.05f, r * 0.38f});

  for (int i = 0; i < 3; i++) {
    float a = (float)i / 3.0f * 6.2831853f;
    part(out, base, box, core, {std::sin(a) * r * 0.66f, -r * 0.05f, std::cos(a) * r * 0.66f},
         {0, glm::degrees(a), 12.6f}, {r * 0.09f, r * 0.44f, r * 0.62f});           // fins
  }

  // The ring: separate segments rather than a torus, so it reads as machinery
  // when it turns. Alternating segments are lit.
  const float spin = bob * 2.2f;
  for (int i = 0; i < 8; i++) {
    float a = (float)i / 8.0f * 6.2831853f + spin;
    part(out, (i % 2) ? glow : base, box, core,
         {std::sin(a) * r * 1.02f, 0, std::cos(a) * r * 1.02f},
         {0, glm::degrees(a), 0}, {r * 0.30f, r * 0.11f, r * 0.16f});
  }

  part(out, base, tpr, core, {0, -r * 0.62f, 0}, {0, 0, 0}, {r * 0.56f, r * 0.26f, r * 0.56f});
  part(out, glow, tpr, core, {0, -r * 0.82f, 0}, {0, 0, 180}, {r * 0.34f, r * 0.14f, r * 0.34f});

  for (int side = -1; side <= 1; side += 2) {
    float s = (float)side;
    glm::mat4 shoulder = joint(core, {s * r * 0.34f, -r * 0.5f, -r * 0.1f},
                               0.5f + std::sin(bob * 0.9f + s) * 0.10f);
    part(out, base, tpr, shoulder, {0, -r * 0.21f, 0}, {0, 0, 0}, {r * 0.12f, r * 0.42f, r * 0.12f});
    glm::mat4 elbow = joint(shoulder, {0, -r * 0.42f, 0}, -0.9f + std::sin(bob * 1.3f + s) * 0.12f);
    part(out, base, box, elbow, {0, -r * 0.10f, 0}, {0, 0, 0}, {r * 0.08f, r * 0.20f, r * 0.08f});
  }
}
