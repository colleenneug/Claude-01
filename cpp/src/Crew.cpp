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

// Irises. Mostly brown, because mostly they are.
const glm::vec3 kEyes[] = {
  {0.16f, 0.09f, 0.05f}, {0.22f, 0.13f, 0.07f}, {0.12f, 0.07f, 0.04f},
  {0.20f, 0.12f, 0.06f}, {0.17f, 0.24f, 0.30f}, {0.20f, 0.26f, 0.18f},
  {0.26f, 0.28f, 0.30f},
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
  p.eyes     = kEyes[pickIndex(countOf(kEyes), 79u)];

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
    //
    // A stroll, which is what people crossing a concourse do: a little over a
    // metre a second. At the two metres a second this used to be, everybody
    // in the room was late for something.
    p.speed = (1.02f + hash01(seed * 5u + 1u) * 0.34f) * (0.88f + (p.height - 1.58f));
    p.phase = hash01(seed * 5u + 2u) * 6.2831853f;
    p.stride = p.phase;
    seed++;
    people_.push_back(p);
  }

  for (size_t k = 0; k < idlerCount; k++) {
    const Idler& i = idlers[k];
    Person p;
    p.pos = glm::vec3(i.x, i.y, i.z);
    // Negated. The tables above came across from the browser build as
    // written, but a yaw there and a yaw here turn opposite ways for a figure
    // whose front is -Z — the named crew's `facing` values were flipped when
    // they were ported, and these were not. So the pair "at the gallery rail"
    // stood with their backs to the rail and the drop, looking at a wall.
    p.yaw = glm::radians(-i.yaw);
    rollAppearance(p, seed);
    p.phase = hash01(seed * 5u + 2u) * 6.2831853f;
    seed++;
    people_.push_back(p);
  }
}

// Half the angle a thigh swings through, in radians, and how far round the
// step cycle a walker goes per metre covered. They are one fact stated
// twice: with legs this long swinging this far, a full cycle (two steps)
// carries the body about 1.45 metres, so the cycle has to advance 2*pi in
// that distance. Change one without the other and feet slide.
namespace {
const float kThighSwing = 0.46f;
// The body, as fractions of its own height, taken from where the joints are
// on real people (ankle 0.04, knee 0.28, hip 0.52, shoulder 0.81, chin 0.87)
// rather than from what looked about right. The first version had the hip at
// 0.48 and the shoulder at 0.72 with a small head on top: a figure a tenth
// shorter than its stated height, all leg, with its arms hanging from where
// its ribs should be.
const float kAnkle = 0.04f;
const float kShin = 0.240f;
const float kThigh = 0.240f;
const float kLegLength = kAnkle + kShin + kThigh;   // hip joint to sole: 0.52
const float kPelvis = kLegLength + 0.02f;           // centre of the hips block
const float kChest = 0.150f;                        // pelvis up to mid-chest
const float kShoulder = 0.120f;                     // mid-chest up to the shoulder joint
const float kChin = 0.180f;                         // mid-chest up to the chin
const float kUpperArm = 0.170f;
const float kForearm = 0.160f;
float strideRate(float height) {
  const float perCycle = 4.0f * kLegLength * height * std::sin(kThighSwing);
  return 6.2831853f / perCycle;
}
}  // namespace

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
    p.stride += p.speed * dt * strideRate(p.height);
    // Turn toward where they are going rather than snapping: a person who
    // pivots on the spot reads as a turret.
    //
    // The figure's front is -Z, and a rotation of `yaw` about Y carries -Z to
    // (-sin yaw, -cos yaw). So the yaw that faces direction d is
    // atan2(-d.x, -d.z). This used to be atan2(d.z, d.x), which is the right
    // formula for a figure that faces +X: everyone walked a quarter turn off
    // the way they were going, sliding sideways across the floor.
    float want = std::atan2(-d.x, -d.z);
    float diff = std::remainder(want - p.yaw, 6.2831853f);
    p.yaw += diff * std::min(1.0f, 6.0f * dt);
  }
}

// Conventions for everything below, worked out rather than guessed, because
// the first version settled them by looking at a render that was drawing
// every solid inside out and so had front and back confused:
//
//   - the figure's front is -Z.
//   - joint() rotates about X then Z. A limb hangs along -Y, and a POSITIVE
//     rotation about X carries -Y toward -Z: positive is FORWARD. That holds
//     for a thigh, an upper arm and a forearm alike.
//   - so an elbow, which folds forward, bends positive; and a knee, which
//     folds back, bends NEGATIVE. The knees used to bend positive. Legs that
//     fold forward at the knee are a bird's.
void Crew::collect(std::vector<DrawItem>& out) const {
  const Mesh& box = HostileGeometry::unitBox();
  const Mesh& sph = HostileGeometry::unitSphere();
  const Mesh& cyl = HostileGeometry::unitCylinder();
  const Mesh& tpr = HostileGeometry::unitTaper();

  for (size_t i = 0; i < people_.size(); i++) {
    const Person& p = people_[i];

    // Cloth, not plate: see MaterialType. Everything a person is made of is
    // shaded in their own frame, because they move.
    DrawItem base;
    base.material = MaterialType::Cloth;
    base.tint = p.tint;
    base.metallic = 0.0f;
    base.roughness = 0.82f;
    base.wear = 0.0f;
    base.castShadow = true;
    base.objectSpace = true;

    DrawItem legwear = base;
    legwear.tint = p.trousers;
    legwear.roughness = 0.76f;

    DrawItem skin = base;
    skin.material = MaterialType::Skin;
    skin.tint = p.skin;
    skin.roughness = 0.50f;   // skin is not cloth; a little sheen reads as skin

    // The eye sockets, the lower lid, under the jaw: skin a shade down.
    DrawItem shade = skin;
    shade.tint = p.skin * 0.78f;

    DrawItem lips = skin;
    lips.tint = p.skin * glm::vec3(0.80f, 0.56f, 0.54f);
    lips.roughness = 0.38f;

    DrawItem hairMat = base;
    hairMat.tint = p.hair;
    hairMat.roughness = 0.50f;

    // Eyes are wet, and it is the highlight on them that makes a face look
    // back at you. Hard and glossy, with nothing on them to chip.
    DrawItem sclera = base;
    sclera.material = MaterialType::Armour;
    sclera.tint = glm::vec3(0.72f, 0.71f, 0.70f);
    sclera.roughness = 0.14f;
    DrawItem iris = sclera;
    iris.tint = p.eyes;
    iris.roughness = 0.10f;
    DrawItem pupil = sclera;
    pupil.tint = glm::vec3(0.012f, 0.011f, 0.013f);
    pupil.roughness = 0.08f;

    DrawItem boot = base;
    boot.tint = p.trousers * 0.50f;
    boot.roughness = 0.46f;

    DrawItem lit = base;
    lit.material = MaterialType::Emissive;
    lit.emissive = p.accent;
    lit.emissiveIntensity = 1.6f;
    // A badge glows; it does not light the room. Twenty of them would
    // otherwise be twenty entries in the renderer's list of lamps.
    lit.lightScale = 0.0f;

    const float H = p.height;
    const float B = p.build;
    const bool walking = p.route.size() >= 2;

    // ------------------------------------------------- the step cycle
    // One number, `gait`, runs the whole body while walking. Standing still
    // it is zero and a slow idle clock takes over.
    const float gait = walking ? std::sin(p.stride) : 0.0f;
    const float gaitRate = walking ? std::cos(p.stride) : 0.0f;
    const float idle = time_ * 1.1f + p.phase;

    // The body rides over its legs like an inverted pendulum: highest when
    // the standing leg is upright, lowest when both feet are down and the
    // legs are spread. That is not a bob added for effect — it is the height
    // at which a straight leg of this length, at this angle, reaches the
    // floor, and using anything else leaves the feet in the air or the
    // ground.
    const float drop = walking ? kLegLength * H * (1.0f - std::cos(kThighSwing * gait)) : 0.0f;
    // Standing still, weight goes onto one hip and comes back. Very slow —
    // a five-second cycle — because a person shifting their weight every
    // second reads as somebody who needs the toilet.
    const float weightShift = walking ? 0.0f : std::sin(idle * 0.21f + p.phase) * 0.028f;
    const float breath = std::sin(time_ * 1.4f + p.phase * 1.7f);

    glm::mat4 root = glm::translate(glm::mat4(1.0f), p.pos - glm::vec3(0.0f, drop, 0.0f));
    root = glm::rotate(root, p.yaw, glm::vec3(0, 1, 0));
    root = glm::rotate(root, weightShift * 0.5f, glm::vec3(0, 0, 1));

    // Everyone is a different height, and the proportions scale with it
    // rather than the figure being the same person at a different zoom: the
    // head is very nearly a constant size in real people, so a tall figure
    // is mostly longer legs.
    const float headScale = 0.90f + (H - 1.58f) * 0.22f;   // barely grows

    // ------------------------------------------------- hips, torso, coat
    glm::mat4 hips = joint(root, {0.0f, H * kPelvis, 0.0f}, 0.0f, gait * 0.030f);
    // Rounded, like everything else under a coat: a box here put its corners
    // through the coat's skirt, as two dark wedges on the front of everyone.
    part(out, legwear, sph, hips, {0, 0, 0}, {0.31f * B, 0.19f, 0.22f * B});
    // Walking, the trunk leans a few degrees into it and the shoulders turn
    // against the hips: the arm goes forward with the opposite leg, and it
    // takes the shoulder with it.
    glm::mat4 chest = joint(hips, {0.0f, H * kChest + breath * 0.0025f, 0.0f},
                            walking ? -0.05f : 0.0f, -gait * 0.030f);
    if (walking) chest = glm::rotate(chest, gait * 0.09f, glm::vec3(0, 1, 0));
    // The trunk is a ribcage, and a ribcage is an egg: widest at the chest,
    // rounding in to the shoulders above and the waist below. Anything with a
    // flat top and square corners up here is a box wearing a coat, and it was
    // the single thing most responsible for these reading as mannequins.
    part(out, base, sph, chest, {0, H * 0.012f, 0}, {0.385f * B, H * 0.275f, 0.250f * B});
    // Across the top of it, the line of the shoulders: wider than the ribs
    // and shallow, so the arms hang from something.
    part(out, base, sph, chest, {0, H * 0.098f, 0}, {0.440f * B, H * 0.085f, 0.215f * B});
    part(out, base, tpr, chest, {0, -H * 0.125f, 0}, {0.44f * B, H * 0.22f, 0.32f * B});   // coat skirt
    part(out, lit, box, chest, {0.09f * B, H * 0.045f, -0.123f * B}, {0.05f, 0.02f, 0.012f});  // badge

    // Shoulders and collar. The collar is what stops the head reading as a
    // ball balanced on a box.
    part(out, base, tpr, chest, {0, H * 0.140f, 0}, {0.180f * B, 0.060f, 0.180f * B});
    // From inside the collar to inside the jaw, so there is never a gap to
    // see through between them.
    DrawItem neckMat = skin;
    neckMat.tint = p.skin * 0.86f;
    part(out, neckMat, cyl, chest, {0, H * 0.160f, 0}, {0.112f, H * 0.080f, 0.120f});

    // ------------------------------------------------- head
    // A slow drift plus an occasional glance. The glance is a smoothstep
    // pulse rather than a sine so it reads as a decision to look at
    // something, not as a metronome.
    const float glanceT = std::fmod(idle * 0.13f + p.phase, 1.0f);
    const float glanceWindow = glanceT < 0.16f ? glanceT / 0.16f : 0.0f;
    const float glance = glanceWindow * glanceWindow * (3.0f - 2.0f * glanceWindow);
    // Somebody walking looks where they are going, mostly. The head also
    // undoes the turn of the shoulders, or it would wag with every step.
    const float headYaw = std::sin(idle * 0.29f) * (walking ? 0.05f : 0.10f) +
                          glance * (hash01((unsigned)i * 977u) * 1.1f - 0.55f) * (walking ? 0.5f : 1.0f) -
                          (walking ? gait * 0.09f : 0.0f);
    const float headPitch = std::sin(idle * 0.19f + 1.3f) * 0.04f + (walking ? 0.04f : 0.0f);

    // Every primitive here is unit *diameter*, and part() takes the full
    // extent — so a feature's offset from the head's centre is a fraction of
    // the HALF extent, not of the size. Getting that wrong is what put the
    // first version's entire face floating five centimetres in front of the
    // skull, which at a distance read as a mask and up close read as a bug.
    const float hw = 0.168f * headScale;      // full width
    const float hh = 0.236f * headScale;      // full height: an eighth of a body
    const float hd = 0.204f * headScale;      // full depth
    const float hx = hw * 0.5f, hy = hh * 0.5f, hz = hd * 0.5f;

    glm::mat4 head = glm::translate(chest, glm::vec3(0.0f, H * kChin + hy, 0.0f));
    head = glm::rotate(head, headYaw, glm::vec3(0, 1, 0));
    head = glm::rotate(head, headPitch, glm::vec3(1, 0, 0));
    head = glm::rotate(head, std::sin(idle * 0.37f) * 0.03f, glm::vec3(0, 0, 1));

    part(out, skin, sph, head, {0, 0, 0}, {hw, hh, hd});
    // The jaw, narrowing downward. A sphere on its own is a head-shaped
    // balloon; the jawline is what the eye reads as a face before it has
    // found the eyes.
    part(out, skin, tpr, head, {0, -hy * 0.50f, -hz * 0.10f},
         {hw * 0.92f, hh * 0.30f, hd * 0.84f}, {180.0f, 0, 0});

    // A blink: about a tenth of a second, every few seconds, never two
    // people at once. An eye that never closes is the single most unnerving
    // thing a face can do, and it is free to fix.
    const float blinkT = std::fmod(time_ * (0.21f + hash01((unsigned)i * 131u) * 0.10f) + p.phase, 1.0f);
    const bool blinking = blinkT < 0.030f;
    const float eyeY = hy * 0.10f;

    for (int side = -1; side <= 1; side += 2) {
      const float sx = (float)side * hx * 0.44f;
      part(out, shade, sph, head, {sx, eyeY, -hz * 0.75f},
           {hw * 0.24f, hh * 0.10f, hd * 0.12f});                    // socket
      // The eyeball sits *in* the socket and shows through a slot between
      // two lids. With no lids the whole white is on show all the way round
      // the iris, which on a person means terror and on these meant an owl.
      part(out, sclera, sph, head, {sx, eyeY, -hz * 0.80f},
           {hw * 0.165f, hh * 0.052f, hd * 0.085f});
      part(out, iris, sph, head, {sx, eyeY + hh * 0.002f, -hz * 0.852f},
           {hw * 0.084f, hh * 0.050f, hd * 0.050f});
      part(out, pupil, sph, head, {sx, eyeY + hh * 0.002f, -hz * 0.874f},
           {hw * 0.040f, hh * 0.025f, hd * 0.035f});
      // Upper lid: rests over the top third of the iris, and comes down to
      // meet the lower one in a blink.
      part(out, skin, sph, head, {sx, eyeY + (blinking ? hh * 0.004f : hh * 0.027f), -hz * 0.815f},
           {hw * 0.195f, blinking ? hh * 0.066f : hh * 0.046f, hd * 0.105f});
      part(out, shade, sph, head, {sx, eyeY - hh * 0.029f, -hz * 0.805f},
           {hw * 0.175f, hh * 0.026f, hd * 0.090f});                 // lower lid
      // Eyebrow, in the hair's colour, tilted down toward the nose.
      part(out, hairMat, box, head, {sx, hy * 0.325f, -hz * 0.855f},
           {hw * 0.215f, hh * 0.020f, hd * 0.045f}, {0, 0, (float)side * 7.0f});
      // Ears, where the head is widest.
      part(out, skin, sph, head, {(float)side * hx * 0.94f, -hy * 0.04f, hz * 0.14f},
           {hw * 0.11f, hh * 0.20f, hd * 0.16f});
    }

    // Nose: the taper laid along -Z so its narrow end points forward.
    part(out, skin, tpr, head, {0, -hy * 0.10f, -hz * 0.82f},
         {hw * 0.17f, hd * 0.20f, hh * 0.24f}, {-90.0f, 0, 0});
    // Mouth: lips a little redder than the face, and barely there.
    part(out, lips, sph, head, {0, -hy * 0.50f, -hz * 0.80f},
         {hw * 0.30f, hh * 0.034f, hd * 0.075f});

    // Hair. Pushed back and up so the hairline sits above the brow and the
    // face is never inside it — four styles, because a room where everyone
    // has the same haircut is a room of clones however good the faces are.
    switch (p.hairStyle) {
      case 1:   // cropped: a thin cap that follows the skull
        part(out, hairMat, sph, head, {0, hy * 0.20f, hz * 0.12f},
             {hw * 1.06f, hh * 0.90f, hd * 1.05f});
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
        part(out, hairMat, sph, head, {0, hy * 0.24f, hz * 0.14f},
             {hw * 1.09f, hh * 0.92f, hd * 1.08f});
        break;
    }

    // ------------------------------------------------- limbs
    for (int side = -1; side <= 1; side += 2) {
      const float sd = (float)side;
      // This leg's thigh angle: forward positive. The two legs are half a
      // cycle apart, which is all "sd *" is doing.
      const float thigh = kThighSwing * gait * sd;
      // The leg that is travelling forward is off the ground, and has to
      // fold at the knee to clear it. The one on the ground stays nearly
      // straight, because it is holding the body up.
      const float swinging = std::max(0.0f, gaitRate * sd);
      const float kneeFold = 0.05f + 0.95f * swinging * std::sqrt(swinging);

      // Arms. Each swings against the leg on its own side. A posture
      // overrides the swing for someone standing still — arms hanging
      // straight down is the one pose nobody actually adopts.
      float shoulderPitch = walking ? -thigh * 0.62f : std::sin(idle * 0.5f + sd) * 0.015f;
      float shoulderRoll  = sd * 0.07f;
      // The arm bends a little more as it comes forward.
      float elbowBend     = 0.20f + std::max(0.0f, -thigh) * 0.75f;
      float forearmYaw    = 0.0f;

      if (!walking) {
        switch (p.posture) {
          case Posture::ArmsCrossed:
            // Upper arms a little forward, forearms folded flat across the
            // chest, one above the other. The inward yaw is what crosses
            // them; without it this is "holding a tray".
            shoulderPitch = 0.22f;
            shoulderRoll  = sd * 0.10f;
            elbowBend     = 1.48f + sd * 0.09f;
            forearmYaw    = -sd * 1.05f;
            break;
          case Posture::HandsBehindBack:
            // Upper arms back, forearms turned in behind the small of the
            // back. The forearm goes *backward* here, so its bend is
            // negative — not an elbow bending the wrong way, but the nearest
            // this rig gets to a shoulder rolled inward.
            shoulderPitch = -0.16f;
            shoulderRoll  = sd * 0.06f;
            elbowBend     = -0.95f;
            forearmYaw    = -sd * 0.78f;
            break;
          case Posture::HandOnHip:
            // One hand only. The asymmetry is the point — both hands on both
            // hips is a pose nobody holds while waiting for a shuttle.
            if (side < 0) {
              shoulderPitch = -0.08f;
              shoulderRoll  = sd * 0.50f;     // elbow out to the side
              elbowBend     = 0.50f;
              forearmYaw    = -sd * 0.75f;    // forearm back in to the hip
            }
            break;
          case Posture::Neutral:
          default:
            shoulderRoll = sd * (0.09f + weightShift * 0.6f);
            break;
        }
      }

      glm::mat4 shoulder = joint(chest, {sd * 0.205f * B, H * kShoulder, 0.0f},
                                 shoulderPitch, shoulderRoll);
      // Every limb is thickest where it leaves the body and narrows toward
      // its end: shoulder to elbow, elbow to wrist, hip to knee, knee to
      // ankle. The taper primitive is narrow at its top, so each is turned
      // over. The right way up they were bell-bottomed — wrists wider than
      // shoulders — with a ball stuck on top to hide where the arm began.
      const glm::vec3 flip(180.0f, 0.0f, 0.0f);
      // The round of the shoulder, the same width as the arm it caps.
      part(out, base, sph, shoulder, {0, 0, 0}, {0.118f * B, 0.118f * B, 0.118f * B});
      part(out, base, tpr, shoulder, {0, -H * kUpperArm * 0.5f, 0},
           {0.118f * B, H * kUpperArm, 0.118f * B}, flip);

      glm::mat4 elbow = joint(shoulder, {0, -H * kUpperArm, 0}, elbowBend);
      // About the elbow frame's Z, not its Y. After the bend the frame's Y
      // points along the forearm, so a rotation about it spins the forearm
      // on its own axis and nothing moves; rotating about Z is what swings
      // a bent forearm across the body.
      if (forearmYaw != 0.0f) elbow = glm::rotate(elbow, forearmYaw, glm::vec3(0, 0, 1));
      part(out, base, sph, elbow, {0, 0, 0}, {0.090f * B, 0.090f * B, 0.090f * B});
      part(out, base, tpr, elbow, {0, -H * kForearm * 0.5f, 0}, {0.094f * B, H * kForearm, 0.094f * B}, flip);
      // The hand: a palm and the fingers curled under it, flattened the way
      // a hand at rest is rather than balled like a fist.
      part(out, skin, sph, elbow, {0, -H * kForearm - 0.035f, 0}, {0.050f, 0.105f, 0.085f});
      part(out, skin, sph, elbow, {0, -H * kForearm - 0.085f, -0.012f}, {0.044f, 0.075f, 0.070f});

      // Legs. Trousers, and a boot that is a separate colour from them.
      glm::mat4 hip = joint(hips, {sd * 0.095f * B, -H * 0.02f, 0.0f}, thigh);
      part(out, legwear, tpr, hip, {0, -H * kThigh * 0.5f, 0}, {0.160f * B, H * kThigh, 0.160f * B}, flip);
      glm::mat4 knee = joint(hip, {0, -H * kThigh, 0}, -kneeFold);
      part(out, legwear, sph, knee, {0, 0, 0}, {0.120f * B, 0.120f * B, 0.120f * B});   // the knee itself
      part(out, legwear, tpr, knee, {0, -H * kShin * 0.5f, 0}, {0.124f * B, H * kShin, 0.124f * B}, flip);
      // The ankle takes back most of what the hip and knee turned the leg
      // through, so a foot on the ground lies on the ground instead of
      // digging its toe in.
      glm::mat4 ankle = joint(knee, {0, -H * kShin, 0}, -(thigh - kneeFold) * 0.85f);
      // The shin stops at the ankle, four percent of the body's height
      // above the floor; the boot is that tall plus enough to cover the
      // join, so its sole is exactly where the ground is. The feet used to
      // hang six centimetres in the air.
      const float bootH = H * kAnkle + 0.035f;
      part(out, boot, box, ankle, {0, -H * kAnkle + bootH * 0.5f, -0.048f}, {0.104f, bootH, 0.245f});
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
