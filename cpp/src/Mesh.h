#pragma once
#include "Gl.h"
#include <vector>

// Every material in this project shades procedurally from world position and
// world normal (see shaders/pbr.frag) rather than from a UV-mapped texture,
// so a vertex carries only what that needs: position, normal, and — for
// terrain only — a per-vertex blend weight between the two stone layers.
// Nothing here owns texture coordinates because nothing samples one.
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  float blend = 0.0f;
};

class Mesh {
public:
  void upload(const std::vector<Vertex>& verts, const std::vector<unsigned>& indices);
  void draw() const;
  void destroy();

  // ---------- generators ----------
  // A box centred on the origin. Used for armour plates, crates, pillars,
  // and every part of the sentinel rig in Scene.cpp.
  static Mesh box(float w, float h, float d);
  static Mesh cylinder(float rTop, float rBottom, float h, int segments);
  static Mesh sphere(float r, int rings, int segments);
  // A subdivided ground plane with a per-vertex blend weight painted by
  // fractal value noise, exactly the role aBlend plays in the browser
  // build's planets.js — patches of two different stone layers rather
  // than a single tiled material repeating to the horizon.
  static Mesh terrainPlane(float size, int segments, float noiseScale);

private:
  GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
  GLsizei indexCount_ = 0;
};
