#include "Scene.h"
#include <cmath>
#include <cstdlib>

// ---------------------------------------------------------------- Sentinel

void Sentinel::build() {
  // Sized in metres; shared by every instance (there is only one here, but
  // the geometry-sharing discipline mirrors hostiles.js, where dozens of
  // enemies share one geometry set per type).
  partBox_[0] = Mesh::box(0.46f, 0.62f, 0.30f);   // chest
  partBox_[1] = Mesh::box(0.24f, 0.26f, 0.26f);   // head
  partBox_[2] = Mesh::box(0.20f, 0.14f, 0.20f);   // pauldron
  partBox_[3] = Mesh::box(0.11f, 0.30f, 0.11f);   // upper arm / thigh
  partBox_[4] = Mesh::box(0.095f, 0.28f, 0.095f); // forearm / shin
  partBox_[5] = Mesh::box(0.34f, 0.16f, 0.20f);   // pelvis
  partBox_[6] = Mesh::box(0.16f, 0.05f, 0.26f);   // foot
  partBox_[7] = Mesh::box(0.09f, 0.06f, 0.03f);   // visor slit (emissive)
  sphereSmall_ = Mesh::sphere(0.10f, 8, 8);        // reactor core (emissive)
}

void Sentinel::destroy() {
  for (auto& m : partBox_) m.destroy();
  sphereSmall_.destroy();
}

namespace {
glm::mat4 xform(const glm::vec3& t, const glm::vec3& eulerDeg, const glm::vec3& scale = glm::vec3(1.0f)) {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), t);
  m = glm::rotate(m, glm::radians(eulerDeg.y), glm::vec3(0, 1, 0));
  m = glm::rotate(m, glm::radians(eulerDeg.x), glm::vec3(1, 0, 0));
  m = glm::rotate(m, glm::radians(eulerDeg.z), glm::vec3(0, 0, 1));
  if (scale != glm::vec3(1.0f)) m = glm::scale(m, scale);
  return m;
}
}  // namespace

void Sentinel::pose(const glm::vec3& origin, float yawDeg, float phase, float speed,
                     std::vector<DrawItem>& out) const {
  // Two-way gait: the knee only bends one way, the torso counter-rotates
  // against the legs, the whole rig rises and falls each step — the same
  // three things hostiles.js calls out as what separates a walk from two
  // limbs swinging past each other.
  float stride = std::min(1.0f, speed / 2.6f);
  float swing = std::sin(phase) * std::min(0.6f, 0.16f + speed * 0.20f);
  float bodyRise = std::abs(std::sin(phase)) * 0.05f * stride;

  glm::mat4 root = glm::translate(glm::mat4(1.0f), origin + glm::vec3(0, 0.95f + bodyRise, 0));
  root = glm::rotate(root, glm::radians(yawDeg + swing * 8.0f), glm::vec3(0, 1, 0));

  DrawItem base;
  base.tint = glm::vec3(0.55f, 0.58f, 0.5f);  // desert-worn olive plate
  base.metallic = 0.85f;
  base.roughness = 0.25f;
  base.wear = 1.3f;
  base.anisoStrength = 0.4f;
  base.material = MaterialType::Armour;

  auto place = [&](const Mesh& m, const glm::mat4& local) {
    DrawItem it = base;
    it.mesh = &m;
    it.model = root * local;
    out.push_back(it);
  };

  place(partBox_[5], xform({0, 0, 0}, {0, 0, 0}));                     // pelvis
  place(partBox_[0], xform({0, 0.38f, 0}, {stride * 6.0f, 0, 0}));     // chest, leans in with stride

  // head + visor
  glm::mat4 headBase = xform({0, 0.72f, -0.01f}, {0, -swing * 10.0f, 0});
  place(partBox_[1], headBase);
  {
    DrawItem visor = base;
    visor.mesh = &partBox_[7];
    visor.model = root * headBase * xform({0, 0.0f, -0.135f}, {0, 0, 0});
    visor.material = MaterialType::Emissive;
    visor.emissive = glm::vec3(0.62f, 0.91f, 1.0f);
    visor.emissiveIntensity = 1.4f;
    out.push_back(visor);
  }

  // reactor core, chest-mounted
  {
    DrawItem core = base;
    core.mesh = &sphereSmall_;
    core.model = root * xform({0, 0.40f, -0.16f}, {0, 0, 0}, glm::vec3(0.6f, 0.6f, 0.35f));
    core.material = MaterialType::Emissive;
    core.emissive = glm::vec3(1.0f, 0.61f, 0.24f);
    core.emissiveIntensity = 0.9f;
    out.push_back(core);
  }

  for (int side = -1; side <= 1; side += 2) {
    float s = (float)side;
    float legSwing = swing * s;
    float kneeBend = std::max(0.0f, -legSwing) * 90.0f + 4.0f;  // never bends backwards

    place(partBox_[2], xform({s * 0.30f, 0.62f, 0}, {0, 0, s * -10.0f}));           // pauldron
    place(partBox_[3], xform({s * 0.26f, 0.44f - legSwing * -0.02f, 0}, {-legSwing * 40.0f, 0, 0})); // upper arm
    place(partBox_[4], xform({s * 0.26f, 0.16f, -0.10f}, {-18.0f - legSwing * 30.0f, 0, 0}));        // forearm

    glm::mat4 hip = xform({s * 0.13f, -0.10f, 0}, {legSwing * 55.0f, 0, 0});
    DrawItem thigh = base; thigh.mesh = &partBox_[3];
    thigh.model = root * hip * xform({0, -0.15f, 0}, {0, 0, 0});
    out.push_back(thigh);

    glm::mat4 knee = hip * xform({0, -0.30f, 0}, {kneeBend, 0, 0});
    DrawItem shin = base; shin.mesh = &partBox_[4];
    shin.model = root * knee * xform({0, -0.14f, 0}, {0, 0, 0});
    out.push_back(shin);

    DrawItem foot = base; foot.mesh = &partBox_[6];
    foot.model = root * knee * xform({0, -0.28f, -0.05f}, {0, 0, 0});
    out.push_back(foot);
  }
}

// -------------------------------------------------------------------- Scene

void Scene::build() {
  // Layered terrain: two stone materials blended per vertex (see
  // Mesh::terrainPlane / shaders/pbr.frag), a detail bump layered on top in
  // the shader. Low reflectivity, high bump — the opposite of the armour.
  ground_ = Mesh::terrainPlane(80.0f, 96, 0.06f);

  crate_ = Mesh::box(1.1f, 1.1f, 1.1f);
  pillar_ = Mesh::cylinder(0.55f, 0.7f, 5.0f, 16);
  rock_ = Mesh::sphere(1.0f, 10, 8);

  srand(1337);
  auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };

  for (int i = 0; i < 10; i++) {
    float x = rnd(-28.0f, 28.0f), z = rnd(-28.0f, 28.0f);
    if (glm::length(glm::vec2(x, z)) < 6.0f) continue;
    float s = rnd(0.7f, 1.6f);
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(x, s * 0.55f, z));
    m = glm::rotate(m, rnd(0.0f, 6.28f), glm::vec3(0, 1, 0));
    m = glm::scale(m, glm::vec3(s));
    crates_.push_back(m);
  }
  glm::vec3 pillarSpots[4] = {{-14, 0, -14}, {14, 0, -14}, {-14, 0, 14}, {14, 0, 14}};
  for (auto& p : pillarSpots) pillars_.push_back(glm::translate(glm::mat4(1.0f), p + glm::vec3(0, 2.5f, 0)));

  for (int i = 0; i < 14; i++) {
    float x = rnd(-35.0f, 35.0f), z = rnd(-35.0f, 35.0f);
    if (glm::length(glm::vec2(x, z)) < 8.0f) continue;
    float s = rnd(0.5f, 1.8f);
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(x, s * 0.3f, z));
    m = glm::rotate(m, rnd(0.0f, 6.28f), glm::normalize(glm::vec3(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1))));
    m = glm::scale(m, glm::vec3(s, s * 0.7f, s));
    rocks_.push_back(m);
  }

  sentinel_.build();

  // Twenty degrees of elevation: low enough to throw shadows several times
  // the height of whatever casts them, high enough that upward-facing
  // surfaces still catch real light.
  sunDirection = glm::normalize(glm::vec3(-0.68f, -0.36f, -0.30f));

  // ---------- dust motes ----------
  std::vector<float> data;
  data.reserve(moteCount_ * 5);
  for (int i = 0; i < moteCount_; i++) {
    float x = rnd(-1.0f, 1.0f) * moteBox_ * 0.5f;
    float y = rnd(-1.0f, 1.0f) * moteBox_ * 0.5f;
    float z = rnd(-1.0f, 1.0f) * moteBox_ * 0.5f;
    float seed = rnd(0.0f, 1.0f);
    float scale = 0.35f + std::pow(rnd(0.0f, 1.0f), 3.0f) * 1.9f;
    data.insert(data.end(), {x, y, z, seed, scale});
  }
  glGenVertexArrays(1, &moteVao_);
  glGenBuffers(1, &moteVbo_);
  glBindVertexArray(moteVao_);
  glBindBuffer(GL_ARRAY_BUFFER, moteVbo_);
  glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
  glBindVertexArray(0);
}

void Scene::destroy() {
  ground_.destroy();
  crate_.destroy();
  pillar_.destroy();
  rock_.destroy();
  sentinel_.destroy();
  if (moteVbo_) glDeleteBuffers(1, &moteVbo_);
  if (moteVao_) glDeleteVertexArrays(1, &moteVao_);
}

void Scene::collect(float time, std::vector<DrawItem>& out) const {
  DrawItem groundItem;
  groundItem.mesh = &ground_;
  groundItem.model = glm::mat4(1.0f);
  groundItem.material = MaterialType::Terrain;
  groundItem.tint = glm::vec3(0.42f, 0.34f, 0.28f);
  groundItem.metallic = 0.0f;
  groundItem.roughness = 0.94f;
  out.push_back(groundItem);

  DrawItem crateBase;
  crateBase.mesh = &crate_;
  crateBase.material = MaterialType::Armour;
  crateBase.tint = glm::vec3(0.62f, 0.66f, 0.71f);
  crateBase.metallic = 0.85f;
  crateBase.roughness = 0.25f;
  crateBase.wear = 1.0f;
  crateBase.anisoStrength = 0.4f;
  for (auto& m : crates_) { DrawItem it = crateBase; it.model = m; out.push_back(it); }

  DrawItem pillarBase = crateBase;
  pillarBase.mesh = &pillar_;
  pillarBase.tint = glm::vec3(0.5f, 0.53f, 0.58f);
  pillarBase.wear = 0.6f;
  for (auto& m : pillars_) { DrawItem it = pillarBase; it.model = m; out.push_back(it); }

  DrawItem rockBase;
  rockBase.mesh = &rock_;
  rockBase.material = MaterialType::Rock;
  rockBase.tint = glm::vec3(0.36f, 0.30f, 0.24f);
  rockBase.metallic = 0.0f;
  rockBase.roughness = 0.95f;
  for (auto& m : rocks_) { DrawItem it = rockBase; it.model = m; out.push_back(it); }

  // The sentinel patrols a slow circle so the walk cycle and the aniso
  // highlight on its armour both have something to demonstrate.
  float t = time * 0.25f;
  glm::vec3 pos(std::cos(t) * 5.0f, 0.0f, std::sin(t) * 5.0f);
  float yaw = glm::degrees(t) + 90.0f;
  float speed = 2.0f;
  sentinel_.pose(pos, yaw, time * 3.4f, speed, out);
}
