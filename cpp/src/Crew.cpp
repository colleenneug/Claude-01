#include "Crew.h"
#include "Hostile.h"     // HostileGeometry: the four shared unit primitives
#include <algorithm>
#include <cmath>
#include <cstdlib>

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

// Kourou's routes. One hall with a mezzanine over each side, so these run up
// and down the muster floor and along the galleries rather than around an
// atrium. Nobody walks out of the pad door: it leads to a live flight line.
const std::vector<std::vector<glm::vec3>> kKourouRoutes = {
  {{-9, 0, -22}, {-9, 0, 16}, {-3, 0, 16}, {-3, 0, -22}},
  {{9, 0, 18}, {9, 0, -20}, {4, 0, -20}, {4, 0, 18}},
  {{-20, 0, -26}, {20, 0, -26}, {20, 0, -18}, {-20, 0, -18}},
  {{-22, 5.2f, -6}, {-22, 5.2f, 18}, {-16, 5.2f, 18}, {-16, 5.2f, -6}},
  {{22, 5.2f, 18}, {22, 5.2f, -6}, {16, 5.2f, -6}, {16, 5.2f, 18}},
  {{-6, 0, 24}, {6, 0, 24}, {6, 0, 19}, {-6, 0, 19}},
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

// Trousers are picked separately from the coat. A figure whose legs and torso
// are one colour reads as a jumpsuit, and a concourse of jumpsuits reads as a
// uniform — which is the opposite of what a civilian station should look like.
const glm::vec3 kTrousers[] = {
  {0.19f, 0.20f, 0.23f}, {0.26f, 0.24f, 0.21f}, {0.17f, 0.19f, 0.22f},
  {0.30f, 0.28f, 0.26f}, {0.21f, 0.23f, 0.27f},
};

// Skin. Authored in linear space, which is why these read darker as numbers
// than they look on screen — the renderer tonemaps after lighting, so a value
// picked to *look* right in a colour picker comes out chalky.
//
// A spread rather than a set of tints on one base: everybody being the same
// person at different exposures is worse than having three people in a room.
const glm::vec3 kSkin[] = {
  {0.76f, 0.60f, 0.49f},   // light
  {0.68f, 0.51f, 0.40f},
  {0.55f, 0.39f, 0.29f},
  {0.42f, 0.29f, 0.21f},
  {0.30f, 0.20f, 0.15f},   // deep
  {0.71f, 0.56f, 0.47f},
  {0.48f, 0.34f, 0.26f},
  {0.62f, 0.45f, 0.34f},
};

const glm::vec3 kHair[] = {
  {0.055f, 0.045f, 0.042f},  // black
  {0.11f, 0.075f, 0.055f},   // dark brown
  {0.19f, 0.13f, 0.085f},    // brown
  {0.26f, 0.14f, 0.075f},    // auburn
  {0.42f, 0.35f, 0.24f},     // dark blond
  {0.38f, 0.38f, 0.40f},     // grey
  {0.58f, 0.58f, 0.60f},     // white
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

// Everything that makes one person not another, rolled from their index. One
// function, so the named crew, the walkers and the idlers all get the same
// treatment — three code paths picking bodies three different ways is how a
// room ends up with the quartermaster as the only person with a face.
//
// Deterministic, like every other pick in this file: a crew that is differently
// shaped every time you dock is a crew you cannot learn your way around.
void rollAppearance(Crew::Person& p, unsigned seed) {
  auto pickIndex = [&](unsigned count, unsigned salt) {
    return (unsigned)(hash01(seed * 7919u + salt) * (float)count) % count;
  };
  auto countOf = [](auto& table) { return (unsigned)(sizeof(table) / sizeof(table[0])); };

  // Heights off a rough adult distribution rather than a flat range: averaging
  // two rolls bunches toward the middle, so most people are ordinary and the
  // tall and short ones actually read as tall and short.
  const float h01 = (hash01(seed * 31u + 3u) + hash01(seed * 31u + 101u)) * 0.5f;
  p.height = 1.58f + h01 * 0.34f;
  p.build  = 0.88f + hash01(seed * 17u + 7u) * 0.26f;

  p.skin     = kSkin[pickIndex(countOf(kSkin), 11u)];
  p.hair     = kHair[pickIndex(countOf(kHair), 23u)];
  p.trousers = kTrousers[pickIndex(countOf(kTrousers), 37u)];
  p.tint     = kCoats[pickIndex(countOf(kCoats), 53u)];
  p.accent   = kAccents[pickIndex(countOf(kAccents), 67u)];

  // Grey and white hair belong on older faces, so pair them with a slightly
  // heavier build and never with the tied-back style.
  const float hs = hash01(seed * 13u + 71u);
  p.hairStyle = hs < 0.40f ? 0 : hs < 0.60f ? 1 : hs < 0.85f ? 2 : 3;
  if (p.hair.r > 0.35f && p.hairStyle == 2) p.hairStyle = 0;

  const float ps = hash01(seed * 19u + 89u);
  p.posture = ps < 0.46f ? Crew::Posture::Neutral
            : ps < 0.68f ? Crew::Posture::ArmsCrossed
            : ps < 0.86f ? Crew::Posture::HandsBehindBack
                         : Crew::Posture::HandOnHip;
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

// ...and Kourou's idlers: candidates waiting to be called, two instructors
// talking on a gallery, somebody at the range window watching.
const Idler kKourouIdlers[] = {
  {-11.5f, 0, -14, 0}, {-11.5f, 0, -11, 12},     // waiting by the benches
  {11.0f, 0, -16, 0},
  {-14.0f, 5.2f, 4, 90}, {-14.0f, 5.2f, 7, 76},  // two on the port gallery
  {14.0f, 5.2f, -2, -90},
  {-3.5f, 0, 25.5f, 180}, {3.0f, 0, 25.5f, 180}, // at the range window
  {-18.0f, 0, 12, 90},
};

void Crew::init(const Content& content, const std::string& station) {
  people_.clear();
  HostileGeometry::ensure();

  const bool kourou = station == "kourou";
  const std::vector<std::vector<glm::vec3>>& routes = kourou ? kKourouRoutes : kRoutes;
  const Idler* idlers = kourou ? kKourouIdlers : kIdlers;
  const size_t idlerCount = kourou ? sizeof(kKourouIdlers) / sizeof(kKourouIdlers[0])
                                   : sizeof(kIdlers) / sizeof(kIdlers[0]);

  // The ones with posts, from content/crew/*.cfg — only the ones who work
  // here.
  for (const std::string& id : content.crewIds(station)) {
    const CrewDef* def = content.crew(id);
    if (!def) continue;
    Person p;
    p.def = def;
    p.pos = def->position;
    p.yaw = glm::radians(def->facingDegrees);
    // Seeded on the id rather than on the load order, so adding a sixth crew
    // file does not reshuffle the faces of the five who were already there.
    unsigned idSeed = 2166136261u;
    for (char ch : id) idSeed = (idSeed ^ (unsigned char)ch) * 16777619u;
    rollAppearance(p, idSeed);
    // ...but their accent is theirs: it is the colour of their post's light
    // and of their name on the prompt, and it is set in their own file.
    p.accent = def->colour;
    // Somebody working a counter is standing at it, not lounging.
    p.posture = def->desk ? Posture::Neutral : p.posture;
    p.phase = hash01(idSeed * 17u) * 6.2831853f;
    people_.push_back(p);
  }

  unsigned seed = 1000;
  for (const auto& route : routes) {
    Person p;
    p.route = route;
    p.pos = route[0];
    p.leg = 1;
    rollAppearance(p, seed);
    // A short person with short legs walks slower. Small, and it is the kind
    // of thing you do not notice until it is missing and a crowd moves like
    // one animation.
    p.speed = (1.5f + hash01(seed * 5u + 1u) * 0.8f) * (0.88f + (p.height - 1.58f));
    p.phase = hash01(seed * 5u + 2u) * 6.2831853f;
    seed++;
    people_.push_back(p);
  }

  for (size_t k = 0; k < idlerCount; k++) {
    const Idler& i = idlers[k];
    Person p;
    p.pos = glm::vec3(i.x, i.y, i.z);
    p.yaw = glm::radians(i.yaw);
    rollAppearance(p, seed);
    p.phase = hash01(seed * 5u + 2u) * 6.2831853f;
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

    DrawItem legwear = base;
    legwear.tint = p.trousers;

    DrawItem skin = base;
    skin.tint = p.skin;
    skin.roughness = 0.62f;   // skin is not cloth; a little sheen reads as skin
    skin.wear = 0.15f;

    // The brow, the eye sockets and under the jaw. One tint, a shade down
    // from the face, standing in for the shadow that a single directional
    // light at station scale never actually casts at this size.
    DrawItem shade = skin;
    shade.tint = p.skin * 0.80f;

    DrawItem hairMat = base;
    hairMat.tint = p.hair;
    hairMat.roughness = 0.55f;
    hairMat.metallic = 0.02f;

    DrawItem sclera = base;
    sclera.tint = glm::vec3(0.70f, 0.70f, 0.72f);
    sclera.roughness = 0.25f;
    sclera.metallic = 0.0f;

    DrawItem pupil = base;
    pupil.tint = glm::vec3(0.045f, 0.040f, 0.050f);
    pupil.roughness = 0.18f;

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

    // Standing still, weight goes onto one hip and comes back. Very slow —
    // a five-second cycle — because a person shifting their weight every
    // second reads as somebody who needs the toilet.
    const float weightShift = walking ? 0.0f : std::sin(t * 0.21f + p.phase) * 0.028f;

    glm::mat4 root = glm::translate(glm::mat4(1.0f),
                                    p.pos + glm::vec3(0.0f, bob, 0.0f));
    root = glm::rotate(root, p.yaw, glm::vec3(0, 1, 0));
    root = glm::rotate(root, weightShift * 0.5f, glm::vec3(0, 0, 1));

    // Everyone is a different height, and the proportions scale with it
    // rather than the figure being the same person at a different zoom: the
    // head is very nearly a constant size in real people, so a tall figure
    // is mostly longer legs.
    const float H = p.height;
    const float B = p.build;
    const float headScale = 0.90f + (H - 1.58f) * 0.22f;   // barely grows

    // ------------------------------------------------- hips, torso, coat
    glm::mat4 hips = joint(root, {0.0f, H * 0.50f, 0.0f});
    part(out, legwear, box, hips, {0, 0, 0}, {0.30f * B, 0.14f, 0.20f * B});
    glm::mat4 chest = joint(hips, {0.0f, H * 0.14f, 0.0f}, 0.0f, -swing * 0.06f);
    // A chest that tapers up into the shoulders rather than a slab: the box
    // was the single thing most responsible for these reading as mannequins.
    part(out, base, tpr, chest, {0, -H * 0.02f, 0}, {0.37f * B, H * 0.19f, 0.235f * B});
    part(out, base, box, chest, {0, H * 0.035f, 0}, {0.355f * B, H * 0.085f, 0.225f * B});
    part(out, base, tpr, chest, {0, -H * 0.11f, 0}, {0.46f * B, H * 0.20f, 0.34f * B});   // coat skirt
    part(out, lit, box, chest, {0.09f * B, 0.03f, -0.118f * B}, {0.05f, 0.02f, 0.012f});  // badge

    // Shoulders and collar. The collar is what stops the head reading as a
    // ball balanced on a box.
    part(out, base, tpr, chest, {0, H * 0.10f, 0}, {0.27f * B, 0.055f, 0.245f * B});
    // Tall enough to actually reach: the collar sits at H*0.10 and the head
    // at H*0.208, so a short neck leaves a gap you can see through.
    DrawItem neckMat = skin;
    neckMat.tint = p.skin * 0.74f;
    part(out, neckMat, cyl, chest, {0, H * 0.145f, 0}, {0.094f, H * 0.085f, 0.094f});

    // ------------------------------------------------- head
    // A slow drift plus an occasional glance. The glance is a smoothstep
    // pulse rather than a sine so it reads as a decision to look at
    // something, not as a metronome.
    const float glanceT = std::fmod(t * 0.13f + p.phase, 1.0f);
    const float glanceWindow = glanceT < 0.16f ? glanceT / 0.16f : 0.0f;
    const float glance = glanceWindow * glanceWindow * (3.0f - 2.0f * glanceWindow);
    const float headYaw = std::sin(t * 0.29f) * 0.10f +
                          glance * (hash01((unsigned)i * 977u) * 1.1f - 0.55f);
    const float headPitch = std::sin(t * 0.19f + 1.3f) * 0.05f;

    glm::mat4 head = glm::translate(chest, glm::vec3(0.0f, H * 0.208f, 0.0f));
    head = glm::rotate(head, headYaw, glm::vec3(0, 1, 0));
    head = glm::rotate(head, headPitch, glm::vec3(1, 0, 0));
    head = glm::rotate(head, std::sin(t * 0.37f) * 0.05f, glm::vec3(0, 0, 1));

    // Every primitive here is unit *diameter*, and part() takes the full
    // extent — so a feature's offset from the head's centre is a fraction of
    // the HALF extent, not of the size. Getting that wrong is what put the
    // first version's entire face floating five centimetres in front of the
    // skull, which at a distance read as a mask and up close read as a bug.
    const float hw = 0.166f * headScale;      // full width
    const float hh = 0.212f * headScale;      // full height
    const float hd = 0.196f * headScale;      // full depth
    const float hx = hw * 0.5f, hy = hh * 0.5f, hz = hd * 0.5f;

    part(out, skin, sph, head, {0, 0, 0}, {hw, hh, hd});
    // The jaw, narrowing downward. A sphere on its own is a head-shaped
    // balloon; the jawline is what the eye reads as a face before it has
    // found the eyes.
    part(out, skin, tpr, head, {0, -hy * 0.50f, -hz * 0.10f},
         {hw * 0.92f, hh * 0.30f, hd * 0.84f}, {180.0f, 0, 0});

    // Brow, sitting just under the surface. Skin a shade down rather than a
    // dark bar: at the size a head actually occupies on screen, a high
    // contrast feature reads as a mark on the face, not as a brow.
    part(out, shade, box, head, {0, hy * 0.30f, -hz * 0.78f},
         {hw * 0.60f, hh * 0.05f, hd * 0.16f});

    for (int side = -1; side <= 1; side += 2) {
      const float sx = (float)side * hx * 0.44f;
      part(out, shade, sph, head, {sx, hy * 0.10f, -hz * 0.76f},
           {hw * 0.24f, hh * 0.10f, hd * 0.12f});                    // socket
      part(out, sclera, sph, head, {sx, hy * 0.10f, -hz * 0.84f},
           {hw * 0.17f, hh * 0.066f, hd * 0.09f});
      part(out, pupil, sph, head, {sx, hy * 0.10f, -hz * 0.90f},
           {hw * 0.105f, hh * 0.060f, hd * 0.06f});
      // Ears, where the head is widest.
      part(out, skin, sph, head, {(float)side * hx * 0.94f, -hy * 0.04f, hz * 0.14f},
           {hw * 0.11f, hh * 0.20f, hd * 0.16f});
    }

    // Nose: the taper laid along -Z so its narrow end points forward.
    part(out, skin, tpr, head, {0, -hy * 0.10f, -hz * 0.82f},
         {hw * 0.17f, hd * 0.20f, hh * 0.24f}, {-90.0f, 0, 0});
    // Mouth, one shade down and barely there.
    part(out, shade, box, head, {0, -hy * 0.52f, -hz * 0.80f},
         {hw * 0.26f, hh * 0.030f, hd * 0.06f});

    // Hair. Pushed back and up so the hairline sits above the brow and the
    // face is never inside it — four styles, because a room where everyone
    // has the same haircut is a room of clones however good the faces are.
    switch (p.hairStyle) {
      case 1:   // cropped: a thin cap that follows the skull
        part(out, hairMat, sph, head, {0, hy * 0.16f, hz * 0.10f},
             {hw * 1.03f, hh * 0.86f, hd * 1.01f});
        break;
      case 2:   // tied back: volume on top, and a tail at the nape
        part(out, hairMat, sph, head, {0, hy * 0.26f, hz * 0.16f},
             {hw * 1.10f, hh * 0.82f, hd * 1.04f});
        part(out, hairMat, tpr, head, {0, -hy * 0.30f, hz * 0.82f},
             {hw * 0.34f, hh * 0.62f, hd * 0.30f}, {180.0f, 0, 0});
        break;
      case 3:   // under a cap: crown plus a peak over the brow
        part(out, base, sph, head, {0, hy * 0.30f, hz * 0.08f},
             {hw * 1.08f, hh * 0.72f, hd * 1.05f});
        part(out, base, box, head, {0, hy * 0.36f, -hz * 0.74f},
             {hw * 0.92f, hh * 0.05f, hd * 0.36f});
        break;
      default:  // short, hairline above the brow
        part(out, hairMat, sph, head, {0, hy * 0.24f, hz * 0.12f},
             {hw * 1.05f, hh * 0.88f, hd * 1.03f});
        break;
    }

    // ------------------------------------------------- limbs
    for (int side = -1; side <= 1; side += 2) {
      const float sd = (float)side;
      const float leg = swing * sd;

      // Arms. A posture overrides the swing for someone standing still —
      // arms hanging straight down is the one pose nobody actually adopts.
      // Sign convention, established by rendering it rather than deriving it:
      // a POSITIVE elbow bend brings the forearm forward. The rig's front is
      // -Z (the badge and the boots are both on that side), and the joint
      // rotation that follows from that is not the one the arithmetic
      // suggests, so this was settled with a screenshot.
      float shoulderPitch = -leg * 0.55f;
      float shoulderRoll  = sd * 0.07f;
      float elbowBend     = 0.22f + std::max(0.0f, leg) * 0.4f;
      float forearmYaw    = 0.0f;

      if (!walking) {
        switch (p.posture) {
          case Posture::ArmsCrossed:
            // Forearms brought up and folded across the chest. The inward yaw
            // is what crosses them; without it this is "holding a tray".
            shoulderPitch = 0.10f;
            shoulderRoll  = sd * 0.28f;
            elbowBend     = 1.46f;
            forearmYaw    = -sd * 0.95f;
            break;
          case Posture::HandsBehindBack:
            // Upper arms back, forearms folded across the small of the back.
            // Positive pitch, for the same reason the elbow bend is positive:
            // the sign that reads as "backward" here is not the one the
            // arithmetic suggests, and this was settled by looking at it.
            shoulderPitch = 0.42f;
            shoulderRoll  = sd * 0.05f;
            elbowBend     = 1.32f;
            forearmYaw    = -sd * 0.78f;
            break;
          case Posture::HandOnHip:
            // One hand only. The asymmetry is the point — both hands on both
            // hips is a pose nobody holds while waiting for a shuttle.
            if (side < 0) {
              shoulderPitch = 0.06f;
              shoulderRoll  = sd * 0.44f;
              elbowBend     = 1.38f;
              forearmYaw    = -sd * 0.72f;
            }
            break;
          case Posture::Neutral:
          default:
            shoulderRoll = sd * (0.09f + weightShift * 0.6f);
            break;
        }
      }

      glm::mat4 shoulder = joint(chest, {sd * 0.205f * B, H * 0.075f, 0.0f},
                                 shoulderPitch, shoulderRoll);
      part(out, base, tpr, shoulder, {0, -H * 0.075f, 0},
           {0.108f * B, H * 0.16f, 0.108f * B});

      glm::mat4 elbow = joint(shoulder, {0, -H * 0.155f, 0}, elbowBend);
      // About the elbow frame's Z, not its Y. After the forward bend the
      // frame's Y points along the forearm, so a rotation about it spins the
      // forearm on its own axis and nothing moves; the frame's Z is the one
      // still pointing roughly up, and rotating about that is what swings a
      // bent forearm across the body.
      if (forearmYaw != 0.0f) elbow = glm::rotate(elbow, forearmYaw, glm::vec3(0, 0, 1));
      // The forearm is bare below a rolled sleeve on some coats; keeping it
      // coat-coloured with a skin hand is the cheap version and reads fine at
      // the distance you ever see these from.
      part(out, base, tpr, elbow, {0, -H * 0.07f, 0}, {0.092f * B, H * 0.15f, 0.092f * B});
      part(out, skin, sph, elbow, {0, -H * 0.152f, 0}, {0.082f, 0.098f, 0.075f});   // hand

      // Legs. Trousers, and a boot that is a separate colour from them.
      glm::mat4 hip = joint(hips, {sd * 0.095f * B, -H * 0.02f, 0.0f}, leg * 0.9f);
      part(out, legwear, tpr, hip, {0, -H * 0.11f, 0}, {0.138f * B, H * 0.22f, 0.138f * B});
      glm::mat4 knee = joint(hip, {0, -H * 0.22f, 0}, std::max(0.0f, -leg) * 1.5f + 0.04f);
      part(out, legwear, tpr, knee, {0, -H * 0.11f, 0}, {0.117f * B, H * 0.22f, 0.117f * B});
      DrawItem boot = base;
      boot.tint = p.trousers * 0.55f;
      boot.roughness = 0.55f;
      part(out, boot, box, knee, {0, -H * 0.205f, -0.045f}, {0.115f, 0.062f, 0.26f});
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
