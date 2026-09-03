#include "Mesh.h"
#include <cmath>

void Mesh::upload(const std::vector<Vertex>& verts, const std::vector<unsigned>& indices) {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned), indices.data(), GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, blend));

  glBindVertexArray(0);
  indexCount_ = static_cast<GLsizei>(indices.size());
}

void Mesh::draw() const {
  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

void Mesh::destroy() {
  if (ebo_) glDeleteBuffers(1, &ebo_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  vao_ = vbo_ = ebo_ = 0;
  indexCount_ = 0;
}

Mesh Mesh::box(float w, float h, float d) {
  float x = w * 0.5f, y = h * 0.5f, z = d * 0.5f;
  // 6 faces, 4 verts each, flat-shaded (each face owns its own normal).
  struct Face { glm::vec3 n; glm::vec3 a, b, c, dd; };
  std::vector<Face> faces = {
    {{ 1, 0, 0}, {x,-y,-z}, {x,-y, z}, {x, y, z}, {x, y,-z}},   // +X
    {{-1, 0, 0}, {-x,-y, z}, {-x,-y,-z}, {-x, y,-z}, {-x, y, z}}, // -X
    {{ 0, 1, 0}, {-x, y,-z}, {x, y,-z}, {x, y, z}, {-x, y, z}},   // +Y
    {{ 0,-1, 0}, {-x,-y, z}, {x,-y, z}, {x,-y,-z}, {-x,-y,-z}},   // -Y
    {{ 0, 0, 1}, {x,-y, z}, {-x,-y, z}, {-x, y, z}, {x, y, z}},   // +Z
    {{ 0, 0,-1}, {-x,-y,-z}, {x,-y,-z}, {x, y,-z}, {-x, y,-z}},   // -Z
  };
  std::vector<Vertex> verts;
  std::vector<unsigned> idx;
  for (const auto& f : faces) {
    unsigned base = static_cast<unsigned>(verts.size());
    verts.push_back({f.a, f.n, 0.0f});
    verts.push_back({f.b, f.n, 0.0f});
    verts.push_back({f.c, f.n, 0.0f});
    verts.push_back({f.dd, f.n, 0.0f});
    idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  Mesh m;
  m.upload(verts, idx);
  return m;
}

Mesh Mesh::cylinder(float rTop, float rBottom, float h, int segments) {
  std::vector<Vertex> verts;
  std::vector<unsigned> idx;
  float halfH = h * 0.5f;
  float slope = (rBottom - rTop) / h;  // for a correct cone-side normal
  for (int i = 0; i <= segments; i++) {
    float t = (float)i / segments;
    float a = t * 2.0f * (float)M_PI;
    float ca = std::cos(a), sa = std::sin(a);
    glm::vec3 nrm = glm::normalize(glm::vec3(ca, slope, sa));
    verts.push_back({{ca * rTop, halfH, sa * rTop}, nrm, 0.0f});
    verts.push_back({{ca * rBottom, -halfH, sa * rBottom}, nrm, 0.0f});
  }
  for (int i = 0; i < segments; i++) {
    unsigned a = i * 2, b = a + 1, c = a + 2, d = a + 3;
    idx.insert(idx.end(), {a, b, c, b, d, c});
  }
  // simple caps (fan), flat shaded up/down
  unsigned topCentre = static_cast<unsigned>(verts.size());
  verts.push_back({{0, halfH, 0}, {0, 1, 0}, 0.0f});
  unsigned topStart = static_cast<unsigned>(verts.size());
  for (int i = 0; i <= segments; i++) {
    float a = (float)i / segments * 2.0f * (float)M_PI;
    verts.push_back({{std::cos(a) * rTop, halfH, std::sin(a) * rTop}, {0, 1, 0}, 0.0f});
  }
  for (int i = 0; i < segments; i++) idx.insert(idx.end(), {topCentre, topStart + i, topStart + i + 1});

  unsigned botCentre = static_cast<unsigned>(verts.size());
  verts.push_back({{0, -halfH, 0}, {0, -1, 0}, 0.0f});
  unsigned botStart = static_cast<unsigned>(verts.size());
  for (int i = 0; i <= segments; i++) {
    float a = (float)i / segments * 2.0f * (float)M_PI;
    verts.push_back({{std::cos(a) * rBottom, -halfH, std::sin(a) * rBottom}, {0, -1, 0}, 0.0f});
  }
  for (int i = 0; i < segments; i++) idx.insert(idx.end(), {botCentre, botStart + i + 1, botStart + i});

  Mesh m;
  m.upload(verts, idx);
  return m;
}

Mesh Mesh::sphere(float r, int rings, int segments) {
  std::vector<Vertex> verts;
  std::vector<unsigned> idx;
  for (int ring = 0; ring <= rings; ring++) {
    float v = (float)ring / rings;
    float phi = v * (float)M_PI;
    for (int seg = 0; seg <= segments; seg++) {
      float u = (float)seg / segments;
      float theta = u * 2.0f * (float)M_PI;
      glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
      verts.push_back({n * r, n, 0.0f});
    }
  }
  int stride = segments + 1;
  for (int ring = 0; ring < rings; ring++) {
    for (int seg = 0; seg < segments; seg++) {
      unsigned a = ring * stride + seg, b = a + stride;
      idx.insert(idx.end(), {a, b, a + 1, b, b + 1, a + 1});
    }
  }
  Mesh m;
  m.upload(verts, idx);
  return m;
}

namespace {
// Hash-based value noise, same construction as the JS terrain blend paint
// in planets.js: smoothed lattice noise, summed at three octaves (fbm).
float hash(float x, float y) {
  float n = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
  return n - std::floor(n);
}
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
float valueNoise(float x, float y) {
  float xi = std::floor(x), yi = std::floor(y);
  float xf = smooth(x - xi), yf = smooth(y - yi);
  float a = hash(xi, yi), b = hash(xi + 1, yi);
  float c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
  return (a * (1 - xf) + b * xf) * (1 - yf) + (c * (1 - xf) + d * xf) * yf;
}
float fbm(float x, float y) {
  return valueNoise(x, y) * 0.6f + valueNoise(x * 2.7f, y * 2.7f) * 0.27f +
         valueNoise(x * 6.1f, y * 6.1f) * 0.13f;
}
}  // namespace

Mesh Mesh::terrainPlane(float size, int segments, float noiseScale) {
  std::vector<Vertex> verts;
  std::vector<unsigned> idx;
  float half = size * 0.5f;
  for (int z = 0; z <= segments; z++) {
    for (int x = 0; x <= segments; x++) {
      float wx = (float)x / segments * size - half;
      float wz = (float)z / segments * size - half;
      float n = fbm(wx * noiseScale, wz * noiseScale);
      // Pushed towards its ends so the mask reads as patches of a different
      // material rather than a smear between the two — same shaping as the
      // browser build's paintBlend().
      float blend = glm::clamp((n - 0.42f) * 2.6f + 0.5f, 0.0f, 1.0f);
      verts.push_back({{wx, 0.0f, wz}, {0.0f, 1.0f, 0.0f}, blend});
    }
  }
  int stride = segments + 1;
  for (int z = 0; z < segments; z++) {
    for (int x = 0; x < segments; x++) {
      unsigned a = z * stride + x, b = a + stride;
      idx.insert(idx.end(), {a, b, a + 1, b, b + 1, a + 1});
    }
  }
  Mesh m;
  m.upload(verts, idx);
  return m;
}
