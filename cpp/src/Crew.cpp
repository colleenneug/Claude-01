#include "Crew.h"
#include "Hostile.h"     // HostileGeometry: the four shared unit primitives
#include <algorithm>
#include <cmath>

namespace {

// Crew routes: a loop of waypoints each, walked at a stroll. Laid across the
// concourse and the galleries rather than around the edges, because people
// you have to walk around are what make a room feel used. Straight from the
// browser build's ROUTES.
const std::vector<std::vector<glm::vec3>> kRoutes = {
  {{-14, 0, -20}, {-14, 0, 16}, {6, 0, 16}, {6, 0, -20}},
  {{10, 0, 22}, {-10, 0, 22}, {-10, 0, -6}, {10, 0, -6}},
  {{0, 0, -40}, {0, 0, -30}, {8, 0, -22}, {-8, 0, -22}},
  {{-18, 7, 24}, {18, 7, 24}, {18, 7, -24}, {-18, 7, -24}},
  {{24, 7, 10}, {24, 7, -10}, {40, 7, -4}, {40, 7, 8}},
  {{-24, 7, -10}, {-24, 7, 10}, {-40, 7, 8}, {-40, 7, -4}},
  {{-18, 14, 22}, {18, 14, 22}, {18, 14, -22}, {-18, 14, -22}},
  {{6, 0, 38}, {6, 0, 28}, {-6, 0, 28}, {-6, 0, 38}},
};

// People who stand still: leaning on rails, talking in pairs, queueing at
// flight control, one in the cupola watching the Earth go past.
struct Idler { float x, y, z, yaw; };
const Idler kIdlers[] = {
  {-15.5f, 7, -8, 90}, {-15.5f, 7, -5, 80},        // two at the gallery rail
  {15.5f, 7, 12, -90},
  {-15.5f, 14, 6, 90}, {15.5f, 14, -6, -90},
  {26, 0, 2, -90}, {29, 0, 5, -126},               // a pair in the quartermaster's bay
  {-3, 0, 30, 172}, {3, 0, 31, 189},               // queueing at flight control
  {-33, 14, -18, 34},                              // one in the cupola, watching
};

// Crew wear working clothes, not armour: a handful of muted coat colours, so
// a room full of them reads as a crew rather than as a squad.
const glm::vec3 kCoats[] = {
  {0.34f, 0.38f, 0.46f}, {0.44f, 0.39f, 0.33f}, {0.28f, 0.40f, 0.42f},
  {0.47f, 0.42f, 0.47f}, {0.37f, 0.33f, 0.28f}, {0.31f, 0.36f, 0.40f},
};
const glm::vec3 kAccents[] = {
  {0.55f, 0.72f, 0.92f}, {0.92f, 0.70f, 0.42f}, {0.60f, 0.90f, 0.72f},
  {0.85f, 0.58f, 0.72f}, {0.72f, 0.78f, 0.88f},
};

// Deterministic per-index picks. A station that looks different every time
// you dock is a station you cannot learn your way around.
float hash01(unsigned n) {
  n = (n ^ 61u) ^ (n >> 16);
  n *= 9u;
  n = n ^ (n >> 4);
  n *= 0x27d4eb2du;
  n = n ^ (n >> 15);
  return (float)(n & 0xffffffu) / (float)0x1000000u;
}

void part(std::vector<DrawItem>& out, const DrawItem& base, const Mesh& mesh,
          const glm::mat4& parent, glm::vec3 localPos, glm::vec3 scale,
          glm::vec3 eulerDeg = glm::vec3(0.0f)) {
  glm::mat4 m = glm::translate(parent, localPos);
  if (eulerDeg.y != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.y), glm::vec3(0, 1, 0));
  if (eulerDeg.x != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.x), glm::vec3(1, 0, 0));
  if (eulerDeg.z != 0.0f) m = glm::rotate(m, glm::radians(eulerDeg.z), glm::vec3(0, 0, 1));
  DrawItem it = base;
  it.mesh = &mesh;
  it.model = glm::scale(m, scale);
  out.push_back(it);
}

glm::mat4 joint(const glm::mat4& parent, glm::vec3 offset, float rxRad = 0.0f, float rzRad = 0.0f) {
  glm::mat4 m = glm::translate(parent, offset);
  if (rxRad != 0.0f) m = glm::rotate(m, rxRad, glm::vec3(1, 0, 0));
  if (rzRad != 0.0f) m = glm::rotate(m, rzRad, glm::vec3(0, 0, 1));
  return m;
}

}  // namespace

void Crew::init(const Content& content) {
  people_.clear();
  HostileGeometry::ensure();

  // The ones with posts, from content/crew/*.cfg.
  for (const std::string& id : content.crewIds()) {
    const CrewDef* def = content.crew(id);
    if (!def) continue;
    Person p;
    p.def = def;
    p.pos = def->position;
    p.yaw = glm::radians(def->facingDegrees);
    p.tint = kCoats[people_.size() % (sizeof(kCoats) / sizeof(kCoats[0]))];
    p.accent = def->colour;
    p.phase = hash01((unsigned)people_.size() * 17u) * 6.2831853f;
    people_.push_back(p);
  }

  unsigned seed = 1000;
  for (const auto& route : kRoutes) {
    Person p;
    p.route = route;
    p.pos = route[0];
    p.leg = 1;
    p.speed = 1.5f + hash01(seed++) * 0.8f;
    p.phase = hash01(seed++) * 6.2831853f;
    p.tint = kCoats[seed % (sizeof(kCoats) / sizeof(kCoats[0]))];
    p.accent = kAccents[seed % (sizeof(kAccents) / sizeof(kAccents[0]))];
    seed++;
    people_.push_back(p);
  }

  for (const Idler& i : kIdlers) {
    Person p;
    p.pos = glm::vec3(i.x, i.y, i.z);
    p.yaw = glm::radians(i.yaw);
    p.phase = hash01(seed++) * 6.2831853f;
    p.tint = kCoats[seed % (sizeof(kCoats) / sizeof(kCoats[0]))];
    p.accent = kAccents[seed % (sizeof(kAccents) / sizeof(kAccents[0]))];
    seed++;
    people_.push_back(p);
  }
}

void Crew::update(float dt) {
  time_ += dt;
  for (Person& p : people_) {
    if (p.route.size() < 2) continue;
    glm::vec3 target = p.route[p.leg % p.route.size()];
    glm::vec3 d = target - p.pos;
    d.y = 0.0f;
    float dist = glm::length(d);
    if (dist < 0.25f) {
      p.leg = (p.leg + 1) % p.route.size();
      continue;
    }
    d /= dist;
    p.pos += d * p.speed * dt;
    // Turn toward where they are going rather than snapping: a person who
    // pivots on the spot reads as a turret.
    float want = std::atan2(d.z, d.x);
    float diff = std::remainder(want - p.yaw, 6.2831853f);
    p.yaw += diff * std::min(1.0f, 6.0f * dt);
  }
}

void Crew::collect(std::vector<DrawItem>& out) const {
  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& sph = HostileGeometry::unitSphere();
  const Mesh& cyl = HostileGeometry::unitCylinder();
  const Mesh& tpr = HostileGeometry::unitTaper();

  for (size_t i = 0; i < people_.size(); i++) {
    const Person& p = people_[i];

    DrawItem base;
    base.material = MaterialType::Armour;
    base.tint = p.tint;
    // Cloth and working plastic, not plate: low metallic and rough, which is
    // most of why these read as people and the hostiles read as machines.
    base.metallic = 0.08f;
    base.roughness = 0.78f;
    base.wear = 0.5f;
    base.castShadow = true;

    DrawItem skin = base;
    skin.tint = glm::vec3(0.42f, 0.33f, 0.27f);
    skin.roughness = 0.85f;

    DrawItem lit = base;
    lit.material = MaterialType::Emissive;
    lit.emissive = p.accent;
    lit.emissiveIntensity = 1.6f;

    // Walkers bob and swing; someone standing at a post breathes and shifts
    // their weight. Both come off the same phase so nobody is in lockstep.
    const bool walking = p.route.size() >= 2;
    const float t = time_ * (walking ? p.speed * 2.6f : 1.1f) + p.phase;
    const float swing = walking ? std::sin(t) * 0.55f : std::sin(t) * 0.05f;
    const float bob = walking ? std::fabs(std::sin(t)) * 0.035f : std::sin(t * 0.6f) * 0.012f;

    glm::mat4 root = glm::translate(glm::mat4(1.0f), p.pos + glm::vec3(0.0f, bob, 0.0f));
    root = glm::rotate(root, p.yaw, glm::vec3(0, 1, 0));

    const float H = 1.78f;   // a person, not an archetype: everyone is one height

    // Hips, torso, and a coat that flares below the belt.
    glm::mat4 hips = joint(root, {0.0f, H * 0.50f, 0.0f});
    part(out, base, box, hips, {0, 0, 0}, {0.30f, 0.14f, 0.20f});
    glm::mat4 chest = joint(hips, {0.0f, H * 0.14f, 0.0f}, 0.0f, -swing * 0.06f);
    part(out, base, box, chest, {0, 0, 0}, {0.36f, 0.30f, 0.22f});
    part(out, base, tpr, chest, {0, -H * 0.11f, 0}, {0.46f, H * 0.20f, 0.34f});   // coat skirt
    part(out, lit, box, chest, {0.09f, 0.03f, -0.115f}, {0.05f, 0.02f, 0.012f});  // badge

    // Shoulders, collar, head. The collar is what stops the head reading as
    // a ball balanced on a box.
    part(out, base, tpr, chest, {0, H * 0.10f, 0}, {0.26f, 0.055f, 0.24f});
    part(out, skin, cyl, chest, {0, H * 0.125f, 0}, {0.11f, 0.075f, 0.11f});      // neck
    glm::mat4 head = joint(chest, {0.0f, H * 0.205f, 0.0f}, 0.0f, std::sin(t * 0.37f) * 0.05f);
    part(out, skin, sph, head, {0, 0, 0}, {0.20f, 0.235f, 0.21f});
    part(out, base, sph, head, {0, 0.085f, 0.012f}, {0.205f, 0.145f, 0.215f});    // hair

    for (int side = -1; side <= 1; side += 2) {
      float s = (float)side;
      float leg = swing * s;

      glm::mat4 shoulder = joint(chest, {s * 0.20f, H * 0.075f, 0.0f}, -leg * 0.55f, s * 0.07f);
      part(out, base, tpr, shoulder, {0, -H * 0.075f, 0}, {0.105f, H * 0.16f, 0.105f});
      glm::mat4 elbow = joint(shoulder, {0, -H * 0.155f, 0}, -0.22f - std::max(0.0f, leg) * 0.4f);
      part(out, base, tpr, elbow, {0, -H * 0.07f, 0}, {0.092f, H * 0.15f, 0.092f});
      part(out, skin, sph, elbow, {0, -H * 0.15f, 0}, {0.085f, 0.095f, 0.085f});  // hand

      glm::mat4 hip = joint(hips, {s * 0.095f, -H * 0.02f, 0.0f}, leg * 0.9f);
      part(out, base, tpr, hip, {0, -H * 0.11f, 0}, {0.135f, H * 0.22f, 0.135f});
      glm::mat4 knee = joint(hip, {0, -H * 0.22f, 0}, std::max(0.0f, -leg) * 1.5f + 0.04f);
      part(out, base, tpr, knee, {0, -H * 0.11f, 0}, {0.115f, H * 0.22f, 0.115f});
      part(out, base, box, knee, {0, -H * 0.205f, -0.045f}, {0.115f, 0.055f, 0.26f});  // boot
    }
  }
}

const Crew::Person* Crew::nearestPost(const glm::vec3& at, float range) const {
  const Person* best = nullptr;
  float bestDist = range;
  for (const Person& p : people_) {
    if (!p.def) continue;                                 // the walkers are going somewhere
    if (std::abs(at.y - p.pos.y) > 2.0f) continue;        // ...and not across decks
    float d = glm::length(glm::vec2(at.x - p.pos.x, at.z - p.pos.z));
    if (d < bestDist) { bestDist = d; best = &p; }
  }
  return best;
}
