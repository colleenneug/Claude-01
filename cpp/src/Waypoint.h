#pragma once
#include "Draw.h"
#include "Hostile.h"
#include <cmath>
#include <vector>

// The arrow over wherever you are meant to be going: a downward-pointing
// chevron on a stem, gold and lit, bobbing and slowly turning so it reads
// from any side and from across a concourse. World geometry rather than a
// HUD sprite, so it is occluded by the floor above it like the thing it is
// standing over, and the HUD's own marker (Hud::drawWaypoint) carries the
// part that has to be visible through walls: the direction and the distance.
//
// Built from the shared unit box, the same one the crew and the hostiles
// are made of, so there is nothing new to load and nothing to free.
inline void collectWaypointArrow(std::vector<DrawItem>& out, glm::vec3 at, float time,
                                 glm::vec3 colour = glm::vec3(1.0f, 0.82f, 0.30f)) {
  const float bob = std::sin(time * 2.4f) * 0.18f;
  const float spin = time * 1.1f;
  glm::mat4 base = glm::translate(glm::mat4(1.0f), at + glm::vec3(0.0f, bob, 0.0f));
  base = glm::rotate(base, spin, glm::vec3(0.0f, 1.0f, 0.0f));

  DrawItem it;
  it.mesh = &HostileGeometry::unitBox();
  it.material = MaterialType::Emissive;
  it.tint = colour;
  it.emissive = colour;
  it.emissiveIntensity = 3.6f;
  it.castShadow = false;

  // the stem
  it.model = glm::scale(glm::translate(base, glm::vec3(0.0f, 1.05f, 0.0f)),
                        glm::vec3(0.16f, 0.9f, 0.16f));
  out.push_back(it);
  // the head: two bars leaning in to meet at the point, in each of two
  // crossed planes so it is a pyramid from above and a V from the side
  for (float plane : {0.0f, 1.5708f}) {
    for (float side : {-1.0f, 1.0f}) {
      glm::mat4 m = glm::rotate(base, plane, glm::vec3(0.0f, 1.0f, 0.0f));
      m = glm::translate(m, glm::vec3(side * 0.22f, 0.36f, 0.0f));
      m = glm::rotate(m, side * 0.75f, glm::vec3(0.0f, 0.0f, 1.0f));
      it.model = glm::scale(m, glm::vec3(0.15f, 0.62f, 0.15f));
      out.push_back(it);
    }
  }
}
