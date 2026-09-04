#include "forge/assets/Mesh.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "forge/core/Log.hpp"

namespace forge {

Json ShapeParams::toJson() const {
    Json j = Json::object();
    j.set("size", Json::fromVec3(size));
    j.set("radius", radius);
    j.set("innerRadius", innerRadius);
    j.set("height", height);
    j.set("segments", segments);
    j.set("rings", rings);
    j.set("steps", steps);
    j.set("uvScale", [&] { Json a = Json::array(); a.push(uvScale.x); a.push(uvScale.y); return a; }());
    return j;
}

ShapeParams ShapeParams::fromJson(const Json& j) {
    ShapeParams p;
    p.size = j["size"].asVec3(p.size);
    p.radius = j["radius"].asFloat(p.radius);
    p.innerRadius = j["innerRadius"].asFloat(p.innerRadius);
    p.height = j["height"].asFloat(p.height);
    p.segments = j["segments"].asInt(p.segments);
    p.rings = j["rings"].asInt(p.rings);
    p.steps = j["steps"].asInt(p.steps);
    if (j["uvScale"].size() >= 2) p.uvScale = {j["uvScale"][0].asFloat(1), j["uvScale"][1].asFloat(1)};
    return p;
}

// -----------------------------------------------------------------
//  Building blocks
// -----------------------------------------------------------------

void Mesh::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& n) {
    uint32_t base = (uint32_t)vertices.size();
    vertices.push_back({a, n, {0, 0}, {1, 1, 1, 1}});
    vertices.push_back({b, n, {1, 0}, {1, 1, 1, 1}});
    vertices.push_back({c, n, {0.5f, 1}, {1, 1, 1, 1}});
    indices.insert(indices.end(), {base, base + 1, base + 2});
}

void Mesh::addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& n,
                   const Vec2& uvScale) {
    uint32_t base = (uint32_t)vertices.size();
    vertices.push_back({a, n, {0, 0}, {1, 1, 1, 1}});
    vertices.push_back({b, n, {uvScale.x, 0}, {1, 1, 1, 1}});
    vertices.push_back({c, n, {uvScale.x, uvScale.y}, {1, 1, 1, 1}});
    vertices.push_back({d, n, {0, uvScale.y}, {1, 1, 1, 1}});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void Mesh::computeBounds() {
    bounds = Box{};
    for (const Vertex& v : vertices) bounds.expand(v.position);
    if (!bounds.valid()) bounds = Box{Vec3::Zero, Vec3::Zero};
}

void Mesh::computeNormals() {
    for (Vertex& v : vertices) v.normal = Vec3::Zero;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Vertex& a = vertices[indices[i]];
        Vertex& b = vertices[indices[i + 1]];
        Vertex& c = vertices[indices[i + 2]];
        // Unnormalised cross product weights each face by its area, which
        // is what keeps a fine tessellation from dragging the average.
        Vec3 n = cross(b.position - a.position, c.position - a.position);
        a.normal += n; b.normal += n; c.normal += n;
    }
    for (Vertex& v : vertices) {
        v.normal = v.normal.normalized();
        if (v.normal.isNearlyZero()) v.normal = Vec3::Up;
    }
}

bool Mesh::raycastLocal(const Ray& ray, float maxDist, float& outT, Vec3& outNormal) const {
    float best = maxDist;
    bool found = false;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        float t; Vec3 n;
        if (!rayTriangle(ray, vertices[indices[i]].position, vertices[indices[i + 1]].position,
                         vertices[indices[i + 2]].position, best, t, n))
            continue;
        best = t; outT = t; outNormal = n; found = true;
    }
    return found;
}

// -----------------------------------------------------------------
//  Generators
// -----------------------------------------------------------------

std::unique_ptr<Mesh> Mesh::makeBox(const Vec3& size) {
    auto m = std::make_unique<Mesh>();
    const Vec3 h = size * 0.5f;
    // Each face is its own quad with its own normal, so the cube has hard
    // edges rather than the rounded look shared vertices would give.
    m->addQuad({-h.x, -h.y,  h.z}, { h.x, -h.y,  h.z}, { h.x,  h.y,  h.z}, {-h.x,  h.y,  h.z}, { 0,  0,  1});
    m->addQuad({ h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x,  h.y, -h.z}, { h.x,  h.y, -h.z}, { 0,  0, -1});
    m->addQuad({ h.x, -h.y,  h.z}, { h.x, -h.y, -h.z}, { h.x,  h.y, -h.z}, { h.x,  h.y,  h.z}, { 1,  0,  0});
    m->addQuad({-h.x, -h.y, -h.z}, {-h.x, -h.y,  h.z}, {-h.x,  h.y,  h.z}, {-h.x,  h.y, -h.z}, {-1,  0,  0});
    m->addQuad({-h.x,  h.y,  h.z}, { h.x,  h.y,  h.z}, { h.x,  h.y, -h.z}, {-h.x,  h.y, -h.z}, { 0,  1,  0});
    m->addQuad({-h.x, -h.y, -h.z}, { h.x, -h.y, -h.z}, { h.x, -h.y,  h.z}, {-h.x, -h.y,  h.z}, { 0, -1,  0});
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeSphere(float radius, int segments, int rings) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(3, segments);
    rings = std::max(2, rings);
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / (float)rings;
        float phi = v * kPi;
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * kTwoPi;
            Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            m->vertices.push_back({n * radius, n, {u, 1.0f - v}, {1, 1, 1, 1}});
        }
    }
    const int stride = segments + 1;
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = (uint32_t)(y * stride + x), b = a + 1;
            uint32_t c = (uint32_t)((y + 1) * stride + x), d = c + 1;
            // The polar rings degenerate to triangles; skipping the
            // zero-area half avoids emitting slivers.
            if (y != 0) m->indices.insert(m->indices.end(), {a, c, b});
            if (y != rings - 1) m->indices.insert(m->indices.end(), {b, c, d});
        }
    }
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeCylinder(float radius, float height, int segments) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(3, segments);
    const float h = height * 0.5f;
    for (int i = 0; i < segments; ++i) {
        float t0 = (float)i / (float)segments * kTwoPi;
        float t1 = (float)(i + 1) / (float)segments * kTwoPi;
        Vec3 d0{std::cos(t0), 0, std::sin(t0)}, d1{std::cos(t1), 0, std::sin(t1)};
        Vec3 p0 = d0 * radius, p1 = d1 * radius;

        // Side, with per-corner normals so the barrel reads as smooth.
        uint32_t base = (uint32_t)m->vertices.size();
        float u0 = (float)i / (float)segments, u1 = (float)(i + 1) / (float)segments;
        m->vertices.push_back({{p0.x, -h, p0.z}, d0, {u0, 0}, {1, 1, 1, 1}});
        m->vertices.push_back({{p1.x, -h, p1.z}, d1, {u1, 0}, {1, 1, 1, 1}});
        m->vertices.push_back({{p1.x,  h, p1.z}, d1, {u1, 1}, {1, 1, 1, 1}});
        m->vertices.push_back({{p0.x,  h, p0.z}, d0, {u0, 1}, {1, 1, 1, 1}});
        m->indices.insert(m->indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});

        m->addTriangle({0, h, 0}, {p0.x, h, p0.z}, {p1.x, h, p1.z}, Vec3::Up);
        m->addTriangle({0, -h, 0}, {p1.x, -h, p1.z}, {p0.x, -h, p0.z}, Vec3::Down);
    }
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeCone(float radius, float height, int segments) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(3, segments);
    const float h = height * 0.5f;
    for (int i = 0; i < segments; ++i) {
        float t0 = (float)i / (float)segments * kTwoPi;
        float t1 = (float)(i + 1) / (float)segments * kTwoPi;
        Vec3 p0{std::cos(t0) * radius, -h, std::sin(t0) * radius};
        Vec3 p1{std::cos(t1) * radius, -h, std::sin(t1) * radius};
        Vec3 apex{0, h, 0};
        // The side normal tilts by the slope, so a squat cone is not lit
        // as though its walls were vertical.
        Vec3 mid = ((p0 + p1) * 0.5f);
        Vec3 sideN = Vec3{mid.x, 0, mid.z}.normalized() * height + Vec3{0, radius, 0};
        m->addTriangle(p0, p1, apex, sideN.normalized());
        m->addTriangle({0, -h, 0}, p1, p0, Vec3::Down);
    }
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeCapsule(float radius, float height, int segments, int rings) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(3, segments);
    rings = std::max(2, rings);
    // Total height includes the caps, so the cylinder in the middle is
    // what is left after the two hemispheres.
    const float cyl = std::max(0.0f, height - radius * 2.0f);
    const float halfCyl = cyl * 0.5f;

    auto ringVerts = [&](float y, float r, float v) {
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * kTwoPi;
            Vec3 dir{std::cos(theta), 0, std::sin(theta)};
            Vec3 pos = dir * r + Vec3{0, y, 0};
            Vec3 n = (y > halfCyl) ? (pos - Vec3{0, halfCyl, 0}).normalized()
                   : (y < -halfCyl) ? (pos - Vec3{0, -halfCyl, 0}).normalized()
                   : dir;
            m->vertices.push_back({pos, n, {u, v}, {1, 1, 1, 1}});
        }
    };

    int rows = 0;
    for (int i = 0; i <= rings; ++i) {                 // bottom cap
        float t = (float)i / (float)rings * kHalfPi;
        ringVerts(-halfCyl - std::cos(t) * radius, std::sin(t) * radius, (float)i / (float)rings * 0.25f);
        ++rows;
    }
    ringVerts(halfCyl, radius, 0.75f);                  // top of the barrel
    ++rows;
    for (int i = 1; i <= rings; ++i) {                  // top cap
        float t = (float)i / (float)rings * kHalfPi;
        ringVerts(halfCyl + std::sin(t) * radius, std::cos(t) * radius, 0.75f + (float)i / (float)rings * 0.25f);
        ++rows;
    }

    const int stride = segments + 1;
    for (int y = 0; y < rows - 1; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = (uint32_t)(y * stride + x), b = a + 1;
            uint32_t c = (uint32_t)((y + 1) * stride + x), d = c + 1;
            m->indices.insert(m->indices.end(), {a, c, b, b, c, d});
        }
    }
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makePlane(const Vec3& size, int segments) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(1, segments);
    const float hx = size.x * 0.5f, hz = size.z * 0.5f;
    for (int z = 0; z <= segments; ++z) {
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments, v = (float)z / (float)segments;
            // UVs repeat with world size, so two planes of different
            // dimensions show the same texel density.
            m->vertices.push_back({{lerpf(-hx, hx, u), 0.0f, lerpf(-hz, hz, v)},
                                   Vec3::Up, {u * size.x, v * size.z}, {1, 1, 1, 1}});
        }
    }
    const int stride = segments + 1;
    for (int z = 0; z < segments; ++z) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = (uint32_t)(z * stride + x), b = a + 1;
            uint32_t c = (uint32_t)((z + 1) * stride + x), d = c + 1;
            m->indices.insert(m->indices.end(), {a, c, b, b, c, d});
        }
    }
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeTorus(float radius, float tube, int segments, int rings) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(3, segments);
    rings = std::max(3, rings);
    for (int i = 0; i <= segments; ++i) {
        float u = (float)i / (float)segments, phi = u * kTwoPi;
        Vec3 centre{std::cos(phi) * radius, 0, std::sin(phi) * radius};
        for (int j = 0; j <= rings; ++j) {
            float v = (float)j / (float)rings, theta = v * kTwoPi;
            Vec3 n{std::cos(phi) * std::cos(theta), std::sin(theta), std::sin(phi) * std::cos(theta)};
            m->vertices.push_back({centre + n * tube, n, {u * 4.0f, v}, {1, 1, 1, 1}});
        }
    }
    const int stride = rings + 1;
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < rings; ++j) {
            uint32_t a = (uint32_t)(i * stride + j), b = a + 1;
            uint32_t c = (uint32_t)((i + 1) * stride + j), d = c + 1;
            m->indices.insert(m->indices.end(), {a, c, b, b, c, d});
        }
    }
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeWedge(const Vec3& size) {
    auto m = std::make_unique<Mesh>();
    const Vec3 h = size * 0.5f;
    // A ramp rising along +Z.
    Vec3 a{-h.x, -h.y, -h.z}, b{h.x, -h.y, -h.z}, c{h.x, -h.y, h.z}, d{-h.x, -h.y, h.z};
    Vec3 e{-h.x, h.y, h.z}, f{h.x, h.y, h.z};
    m->addQuad(a, b, c, d, Vec3::Down);
    m->addQuad(d, c, f, e, Vec3{0, size.z, -size.y}.normalized());   // the slope
    m->addQuad(c, b, f, f, Vec3::Right);
    m->addTriangle(b, f, c, Vec3::Right);
    m->addTriangle(a, d, e, Vec3::Left);
    m->addQuad(e, f, b, a, Vec3::Back * -1.0f);
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makePyramid(const Vec3& size) {
    auto m = std::make_unique<Mesh>();
    const Vec3 h = size * 0.5f;
    Vec3 apex{0, h.y, 0};
    Vec3 a{-h.x, -h.y, -h.z}, b{h.x, -h.y, -h.z}, c{h.x, -h.y, h.z}, d{-h.x, -h.y, h.z};
    m->addQuad(a, d, c, b, Vec3::Down);
    m->addTriangle(d, c, apex, Vec3{0, size.x, size.y}.normalized());
    m->addTriangle(c, b, apex, Vec3{size.z, size.x, 0}.normalized());
    m->addTriangle(b, a, apex, Vec3{0, size.x, -size.y}.normalized());
    m->addTriangle(a, d, apex, Vec3{-size.z, size.x, 0}.normalized());
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeStairs(const Vec3& size, int steps) {
    auto m = std::make_unique<Mesh>();
    steps = std::max(1, steps);
    const float w = size.x, totalH = size.y, totalD = size.z;
    const float stepH = totalH / (float)steps, stepD = totalD / (float)steps;
    for (int i = 0; i < steps; ++i) {
        // Each step is a box from the ground up, so the staircase is
        // solid rather than a floating ribbon.
        float top = stepH * (float)(i + 1);
        float z0 = -totalD * 0.5f + stepD * (float)i;
        float z1 = z0 + stepD;
        float hx = w * 0.5f;
        float y0 = -totalH * 0.5f, y1 = y0 + top;
        m->addQuad({-hx, y1, z0}, {hx, y1, z0}, {hx, y1, z1}, {-hx, y1, z1}, Vec3::Up);      // tread
        m->addQuad({-hx, y0, z0}, {-hx, y1, z0}, {hx, y1, z0}, {hx, y0, z0}, {0, 0, -1});   // riser
        m->addQuad({hx, y0, z0}, {hx, y1, z0}, {hx, y1, z1}, {hx, y0, z1}, Vec3::Right);
        m->addQuad({-hx, y0, z1}, {-hx, y1, z1}, {-hx, y1, z0}, {-hx, y0, z0}, Vec3::Left);
    }
    float hx = w * 0.5f, y0 = -totalH * 0.5f;
    m->addQuad({-hx, y0, -totalD * 0.5f}, {hx, y0, -totalD * 0.5f},
               {hx, y0, totalD * 0.5f}, {-hx, y0, totalD * 0.5f}, Vec3::Down);
    m->addQuad({-hx, y0, totalD * 0.5f}, {hx, y0, totalD * 0.5f},
               {hx, y0 + totalH, totalD * 0.5f}, {-hx, y0 + totalH, totalD * 0.5f}, {0, 0, 1});
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeArch(const Vec3& size, float radius, int segments) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(3, segments);
    const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
    // The opening cannot be wider than the block or taller than it minus
    // a lintel, or the arch would have nothing left to stand on.
    radius = clampf(radius, 0.05f, std::min(hx * 0.95f, size.y * 0.75f));
    const float springY = -hy + (size.y - radius);   // where the arc starts

    // Two legs, each a solid box from the ground to the springing line.
    for (int side = 0; side < 2; ++side) {
        const float sx = side == 0 ? -1.0f : 1.0f;
        const float outer = sx * hx, inner = sx * radius;
        const float x0 = std::min(outer, inner), x1 = std::max(outer, inner);
        m->addQuad({x0, -hy, hz}, {x1, -hy, hz}, {x1, springY, hz}, {x0, springY, hz}, {0, 0, 1});
        m->addQuad({x1, -hy, -hz}, {x0, -hy, -hz}, {x0, springY, -hz}, {x1, springY, -hz}, {0, 0, -1});
        m->addQuad({outer, -hy, -hz}, {outer, springY, -hz}, {outer, springY, hz}, {outer, -hy, hz}, {sx, 0, 0});
        m->addQuad({inner, -hy, hz}, {inner, springY, hz}, {inner, springY, -hz}, {inner, -hy, -hz}, {-sx, 0, 0});
        m->addQuad({x0, -hy, -hz}, {x1, -hy, -hz}, {x1, -hy, hz}, {x0, -hy, hz}, Vec3::Down);
    }

    // The span: for each arc segment, the face between the arc below and
    // the flat top above, plus the soffit the arc sweeps out.
    for (int i = 0; i < segments; ++i) {
        const float t0 = (float)i / (float)segments * kPi;
        const float t1 = (float)(i + 1) / (float)segments * kPi;
        const Vec3 a0{-std::cos(t0) * radius, springY + std::sin(t0) * radius, 0};
        const Vec3 a1{-std::cos(t1) * radius, springY + std::sin(t1) * radius, 0};

        m->addQuad({a0.x, a0.y, hz}, {a1.x, a1.y, hz}, {a1.x, hy, hz}, {a0.x, hy, hz}, {0, 0, 1});
        m->addQuad({a0.x, hy, -hz}, {a1.x, hy, -hz}, {a1.x, a1.y, -hz}, {a0.x, a0.y, -hz}, {0, 0, -1});

        // Soffit normal points down into the opening, away from the arc
        // centre, so the underside of the arch is lit as a ceiling.
        const Vec3 mid = (a0 + a1) * 0.5f;
        const Vec3 n = Vec3{-(mid.x), -(mid.y - springY), 0}.normalized();
        m->addQuad({a0.x, a0.y, -hz}, {a1.x, a1.y, -hz}, {a1.x, a1.y, hz}, {a0.x, a0.y, hz}, n);
        m->addQuad({a0.x, hy, hz}, {a1.x, hy, hz}, {a1.x, hy, -hz}, {a0.x, hy, -hz}, Vec3::Up);
    }
    m->computeBounds();
    return m;
}

std::unique_ptr<Mesh> Mesh::makeRing(float radius, float inner, int segments) {
    auto m = std::make_unique<Mesh>();
    segments = std::max(3, segments);
    inner = clampf(inner, 0.0f, radius * 0.99f);
    for (int i = 0; i < segments; ++i) {
        float t0 = (float)i / (float)segments * kTwoPi;
        float t1 = (float)(i + 1) / (float)segments * kTwoPi;
        Vec3 d0{std::cos(t0), 0, std::sin(t0)}, d1{std::cos(t1), 0, std::sin(t1)};
        m->addQuad(d0 * inner, d1 * inner, d1 * radius, d0 * radius, Vec3::Up);
        m->addQuad(d0 * radius, d1 * radius, d1 * inner, d0 * inner, Vec3::Down);
    }
    m->computeBounds();
    return m;
}

// -----------------------------------------------------------------

const char* Mesh::shapeName(ShapeKind k) {
    switch (k) {
        case ShapeKind::Box: return "Box";
        case ShapeKind::Sphere: return "Sphere";
        case ShapeKind::Cylinder: return "Cylinder";
        case ShapeKind::Cone: return "Cone";
        case ShapeKind::Capsule: return "Capsule";
        case ShapeKind::Plane: return "Plane";
        case ShapeKind::Torus: return "Torus";
        case ShapeKind::Wedge: return "Wedge";
        case ShapeKind::Pyramid: return "Pyramid";
        case ShapeKind::Stairs: return "Stairs";
        case ShapeKind::Arch: return "Arch";
        case ShapeKind::Ring: return "Ring";
        case ShapeKind::Custom: return "Custom";
    }
    return "Box";
}

ShapeKind Mesh::shapeFromName(const std::string& s) {
    for (int i = 0; i <= (int)ShapeKind::Custom; ++i)
        if (s == shapeName((ShapeKind)i)) return (ShapeKind)i;
    return ShapeKind::Box;
}

std::vector<const char*> Mesh::allShapeNames() {
    std::vector<const char*> out;
    for (int i = 0; i < (int)ShapeKind::Custom; ++i) out.push_back(shapeName((ShapeKind)i));
    return out;
}

std::unique_ptr<Mesh> Mesh::makeShape(const std::string& name, ShapeKind kind, const ShapeParams& p) {
    std::unique_ptr<Mesh> m;
    switch (kind) {
        case ShapeKind::Box:      m = makeBox(p.size); break;
        case ShapeKind::Sphere:   m = makeSphere(p.radius, p.segments, p.rings); break;
        case ShapeKind::Cylinder: m = makeCylinder(p.radius, p.height, p.segments); break;
        case ShapeKind::Cone:     m = makeCone(p.radius, p.height, p.segments); break;
        case ShapeKind::Capsule:  m = makeCapsule(p.radius, p.height, p.segments, p.rings); break;
        case ShapeKind::Plane:    m = makePlane(p.size, std::max(1, p.segments / 4)); break;
        case ShapeKind::Torus:    m = makeTorus(p.radius, p.innerRadius, p.segments, p.rings); break;
        case ShapeKind::Wedge:    m = makeWedge(p.size); break;
        case ShapeKind::Pyramid:  m = makePyramid(p.size); break;
        case ShapeKind::Stairs:   m = makeStairs(p.size, p.steps); break;
        case ShapeKind::Arch:     m = makeArch(p.size, p.radius, p.segments); break;
        case ShapeKind::Ring:     m = makeRing(p.radius, p.innerRadius, p.segments); break;
        case ShapeKind::Custom:   m = std::make_unique<Mesh>(); break;
    }
    m->name = name;
    m->kind = kind;
    m->params = p;
    return m;
}

void Mesh::rebuild() {
    if (kind == ShapeKind::Custom) { computeBounds(); return; }
    auto fresh = makeShape(name, kind, params);
    vertices = std::move(fresh->vertices);
    indices = std::move(fresh->indices);
    bounds = fresh->bounds;
}

Json Mesh::toJson() const {
    Json j = Json::object();
    j.set("name", name);
    j.set("shape", shapeName(kind));
    j.set("params", params.toJson());
    if (kind == ShapeKind::Custom) {
        // Only custom geometry stores vertices; everything else is
        // regenerated from its parameters, which is the whole point.
        Json vs = Json::array(), is = Json::array();
        for (const Vertex& v : vertices) {
            vs.push(v.position.x); vs.push(v.position.y); vs.push(v.position.z);
            vs.push(v.normal.x); vs.push(v.normal.y); vs.push(v.normal.z);
            vs.push(v.uv.x); vs.push(v.uv.y);
        }
        for (uint32_t i : indices) is.push((int)i);
        j.set("vertices", vs);
        j.set("indices", is);
    }
    return j;
}

std::unique_ptr<Mesh> Mesh::fromJson(const Json& j) {
    ShapeKind kind = shapeFromName(j["shape"].asString("Box"));
    ShapeParams p = ShapeParams::fromJson(j["params"]);
    auto m = makeShape(j["name"].asString("Mesh"), kind, p);
    if (kind == ShapeKind::Custom) {
        const Json& vs = j["vertices"];
        const Json& is = j["indices"];
        m->vertices.clear();
        m->indices.clear();
        for (size_t i = 0; i + 7 < vs.size(); i += 8) {
            Vertex v;
            v.position = {vs[i].asFloat(), vs[i + 1].asFloat(), vs[i + 2].asFloat()};
            v.normal = {vs[i + 3].asFloat(), vs[i + 4].asFloat(), vs[i + 5].asFloat()};
            v.uv = {vs[i + 6].asFloat(), vs[i + 7].asFloat()};
            m->vertices.push_back(v);
        }
        for (size_t i = 0; i < is.size(); ++i) m->indices.push_back((uint32_t)is[i].asInt());
        m->computeBounds();
    }
    return m;
}

// -----------------------------------------------------------------
//  OBJ import
// -----------------------------------------------------------------

std::unique_ptr<Mesh> Mesh::loadObj(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return nullptr; }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    auto mesh = std::make_unique<Mesh>();
    mesh->kind = ShapeKind::Custom;
    mesh->name = path;

    // Deduplicate by the face's own index triple, which is what keeps a
    // shared vertex shared and the buffer small.
    std::unordered_map<std::string, uint32_t> lookup;
    bool sawNormals = false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") { Vec3 p; ls >> p.x >> p.y >> p.z; positions.push_back(p); }
        else if (tag == "vn") { Vec3 n; ls >> n.x >> n.y >> n.z; normals.push_back(n); sawNormals = true; }
        else if (tag == "vt") { Vec2 t; ls >> t.x >> t.y; uvs.push_back(t); }
        else if (tag == "f") {
            std::vector<uint32_t> face;
            std::string tok;
            while (ls >> tok) {
                auto it = lookup.find(tok);
                if (it != lookup.end()) { face.push_back(it->second); continue; }

                int vi = 0, ti = 0, ni = 0;
                std::sscanf(tok.c_str(), "%d/%d/%d", &vi, &ti, &ni);
                if (ti == 0 && ni == 0) std::sscanf(tok.c_str(), "%d//%d", &vi, &ni);
                // OBJ indices are 1-based, and may be negative to mean
                // "counting back from the end".
                auto resolve = [](int idx, size_t n) -> long {
                    if (idx > 0) return idx - 1;
                    if (idx < 0) return (long)n + idx;
                    return -1;
                };
                Vertex v;
                long p = resolve(vi, positions.size());
                if (p >= 0 && (size_t)p < positions.size()) v.position = positions[(size_t)p];
                long t = resolve(ti, uvs.size());
                if (t >= 0 && (size_t)t < uvs.size()) v.uv = uvs[(size_t)t];
                long nn = resolve(ni, normals.size());
                if (nn >= 0 && (size_t)nn < normals.size()) v.normal = normals[(size_t)nn];

                uint32_t index = (uint32_t)mesh->vertices.size();
                mesh->vertices.push_back(v);
                lookup[tok] = index;
                face.push_back(index);
            }
            // Fan-triangulate, which is correct for any convex face.
            for (size_t i = 2; i < face.size(); ++i)
                mesh->indices.insert(mesh->indices.end(), {face[0], face[i - 1], face[i]});
        }
    }

    if (mesh->vertices.empty()) { if (error) *error = "no geometry in " + path; return nullptr; }
    if (!sawNormals) mesh->computeNormals();
    mesh->computeBounds();
    if (error) error->clear();
    return mesh;
}

} // namespace forge
