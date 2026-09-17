#include "Space.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

bool Space::init(const Content& content) {
  bodies_.clear();

  // Every world comes from content/planets/*.cfg, same as every mission and
  // every gun: a monthly drop adds a planet file, not a code change.
  for (const std::string& id : content.planetIds()) {
    const PlanetDef* d = content.planet(id);
    if (!d) continue;
    Body b;
    b.id = d->id;
    b.name = d->name;
    b.pos = d->position;
    b.radius = d->radius;
    b.tint = d->colour;
    b.tint2 = d->colour2;
    b.capExtent = d->capExtent;
    b.missionId = d->missionId;
    b.isStation = d->station;
    bodies_.push_back(b);
  }

  if (bodies_.empty()) {
    std::fprintf(stderr, "[Space] no planets in content/planets — nowhere to fly\n");
    return false;
  }

  planetMesh_ = Mesh::sphere(1.0f, 32, 48);
  stationMesh_ = Mesh::box(1.0f, 1.0f, 1.0f);
  buildStars();

  // Start just off whichever body is the station, so a new pilot opens on
  // the place that explains itself rather than in empty space.
  placeNear("");
  velocity_ = glm::vec3(0.0f);

  std::printf("[Space] %zu destination(s) loaded\n", bodies_.size());
  loaded_ = true;
  return true;
}

void Space::buildStars() {
  // Reuses the mote shader: points scattered in a box that follows the
  // camera. At this box size they read as a starfield rather than dust.
  srand(1771);
  auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };
  std::vector<float> data;
  data.reserve(starCount_ * 5);
  for (int i = 0; i < starCount_; i++) {
    data.push_back(rnd(-1.0f, 1.0f) * starBox_ * 0.5f);
    data.push_back(rnd(-1.0f, 1.0f) * starBox_ * 0.5f);
    data.push_back(rnd(-1.0f, 1.0f) * starBox_ * 0.5f);
    data.push_back(rnd(0.0f, 1.0f));
    data.push_back(0.6f + std::pow(rnd(0.0f, 1.0f), 3.0f) * 2.6f);
  }
  glGenVertexArrays(1, &starVao_);
  glGenBuffers(1, &starVbo_);
  glBindVertexArray(starVao_);
  glBindBuffer(GL_ARRAY_BUFFER, starVbo_);
  glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
  glBindVertexArray(0);
}

void Space::destroy() {
  planetMesh_.destroy();
  stationMesh_.destroy();
  if (starVbo_) glDeleteBuffers(1, &starVbo_);
  if (starVao_) glDeleteVertexArrays(1, &starVao_);
  starVbo_ = starVao_ = 0;
  loaded_ = false;
}

void Space::placeNear(const std::string& bodyId) {
  const Body* target = nullptr;
  for (const Body& b : bodies_) {
    if (!bodyId.empty() && b.id == bodyId) { target = &b; break; }
    if (bodyId.empty() && b.isStation) { target = &b; break; }
  }
  if (!target && !bodies_.empty()) target = &bodies_[0];
  if (!target) return;

  // Off to one side and slightly above, far enough out to see the whole
  // body but inside engage range, so "where am I" answers itself.
  position_ = target->pos + glm::vec3(0.0f, target->radius * 0.5f, target->radius * 1.4f);
  velocity_ = glm::vec3(0.0f);
}

void Space::update(GLFWwindow* window, Camera& camera, float dt, bool boost,
                   bool forceThrust) {
  if (!loaded_) return;

  // The camera's look direction is the ship's heading: mouse flies it.
  glm::vec3 fwd = camera.forward();
  glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
  glm::vec3 up = glm::normalize(glm::cross(right, fwd));

  glm::vec3 wish(0.0f);
  if (forceThrust || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += fwd;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= fwd;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += right;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= right;
  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) wish += up;
  if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) wish -= up;

  float topSpeed = maxSpeed_ * (boost ? 2.6f : 1.0f);
  if (glm::length(wish) > 1e-4f) {
    wish = glm::normalize(wish);
    // Accelerate toward the wish velocity rather than adding raw impulse:
    // true frictionless flight means every tap is permanent and you spend
    // the session fighting your own momentum.
    glm::vec3 target = wish * topSpeed;
    velocity_ += (target - velocity_) * std::min(1.0f, dt * 1.6f);
  } else {
    // Coast to a stop when the stick is centred. Not physical; flyable.
    velocity_ -= velocity_ * std::min(1.0f, dt * 0.8f);
  }

  position_ += velocity_ * dt;

  // Bodies are solid: stop at the surface rather than flying through the
  // middle of a planet.
  for (const Body& b : bodies_) {
    glm::vec3 d = position_ - b.pos;
    float dist = glm::length(d);
    // Hold off proportionally to the body's own size rather than by a flat
    // margin. Parked at +12 the screen was a wall of planet with no
    // silhouette; a fixed 150 still filled the frame for a 520-unit world
    // while being miles away from a small station. A third of the radius
    // keeps the body reading as a sphere at any size, and stays inside
    // engageRangeFor() so the land prompt is up when you get there.
    float minDist = b.radius * standoffFactor(b);
    if (dist < minDist && dist > 1e-3f) {
      glm::vec3 n = d / dist;
      position_ = b.pos + n * minDist;
      // Remove only the component heading into the surface, so you slide
      // along it instead of sticking.
      float into = glm::dot(velocity_, n);
      if (into < 0.0f) velocity_ -= n * into;
    }
  }

  nearest_ = -1;
  nearestDist_ = 0.0f;
  float best = 1e9f;
  for (size_t i = 0; i < bodies_.size(); i++) {
    float surfaceDist = glm::length(position_ - bodies_[i].pos) - bodies_[i].radius;
    if (surfaceDist < best) {
      best = surfaceDist;
      nearest_ = (int)i;
      nearestDist_ = std::max(0.0f, surfaceDist);
    }
  }

  camera.position = position_;
}

void Space::collect(float time, std::vector<DrawItem>& out) const {
  for (const Body& b : bodies_) {
    DrawItem it;
    it.model = glm::translate(glm::mat4(1.0f), b.pos);

    if (b.isStation) {
      // The Cradle: a lit ring of blocks rather than one box, so it reads
      // as built rather than as a grey cube.
      it.mesh = &stationMesh_;
      it.material = MaterialType::Armour;
      it.tint = b.tint;
      it.metallic = 0.9f;
      it.roughness = 0.34f;
      it.wear = 0.5f;
      it.castShadow = false;

      DrawItem core = it;
      core.model = glm::scale(glm::translate(glm::mat4(1.0f), b.pos),
                              glm::vec3(b.radius * 1.5f, b.radius * 0.45f, b.radius * 0.45f));
      out.push_back(core);

      for (int i = 0; i < 6; i++) {
        float a = (float)i / 6.0f * 6.28318f + time * 0.05f;
        glm::vec3 spoke = b.pos + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * b.radius * 1.1f;
        DrawItem arm = it;
        arm.model = glm::rotate(glm::translate(glm::mat4(1.0f), spoke), a, glm::vec3(0, 1, 0));
        arm.model = glm::scale(arm.model, glm::vec3(b.radius * 0.5f, b.radius * 0.22f, b.radius * 0.22f));
        arm.tint = b.tint * 0.85f;
        out.push_back(arm);

        DrawItem lamp = it;
        lamp.material = MaterialType::Emissive;
        lamp.model = glm::scale(glm::translate(glm::mat4(1.0f), spoke),
                                glm::vec3(b.radius * 0.1f));
        lamp.tint = glm::vec3(0.55f, 0.85f, 1.0f);
        lamp.emissive = lamp.tint;
        lamp.emissiveIntensity = 4.0f;
        out.push_back(lamp);
      }
      continue;
    }

    it.mesh = &planetMesh_;
    it.material = MaterialType::Planet;
    it.model = glm::scale(it.model, glm::vec3(b.radius));
    it.tint = b.tint;
    it.tint2 = b.tint2;
    it.capExtent = b.capExtent;
    it.metallic = 0.0f;
    it.roughness = 0.95f;
    // A planet is the one thing in this game bigger than a shadow cascade;
    // it lights from the sun term alone.
    it.castShadow = false;
    out.push_back(it);
  }
}
