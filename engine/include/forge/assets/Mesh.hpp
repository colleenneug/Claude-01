// ============================================================
//  Meshes.
//
//  Geometry is generated rather than loaded. A mesh asset is a shape
//  kind plus its parameters, so the whole content set of a project is
//  a few kilobytes of JSON and there are no binary files to lose. The
//  vertex buffer is rebuilt from those parameters on load, and again
//  whenever a parameter changes in the editor.
//
//  An OBJ loader is provided as well, for bringing outside geometry in.
// ============================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "forge/core/Json.hpp"
#include "forge/math/Math.hpp"

namespace forge {

struct Vertex {
    Vec3 position;
    Vec3 normal{0, 1, 0};
    Vec2 uv;
    Color color{1, 1, 1, 1};
};

enum class ShapeKind {
    Box, Sphere, Cylinder, Cone, Capsule, Plane, Torus, Wedge,
    Pyramid, Stairs, Arch, Ring, Custom
};

// The parameters a shape is generated from. Not every field applies to
// every shape; the generator reads what it needs.
struct ShapeParams {
    Vec3 size{1, 1, 1};        // box, plane, wedge, stairs, arch
    float radius = 0.5f;       // sphere, cylinder, cone, capsule, torus, ring
    float innerRadius = 0.25f; // torus tube, ring hole
    float height = 1.0f;       // cylinder, cone, capsule, arch
    int segments = 24;         // radial subdivision
    int rings = 12;            // vertical subdivision
    int steps = 8;             // stairs
    Vec2 uvScale{1, 1};

    Json toJson() const;
    static ShapeParams fromJson(const Json& j);
};

class Mesh {
public:
    std::string name;
    ShapeKind kind = ShapeKind::Box;
    ShapeParams params;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Box bounds;

    void rebuild();
    void computeBounds();
    // Averages face normals per vertex. Used after a custom build, and by
    // the OBJ loader when the file carries no normals.
    void computeNormals();
    size_t triangleCount() const { return indices.size() / 3; }

    // Ray against the triangles, for editor picking of a mesh the
    // collision proxy does not describe closely enough.
    bool raycastLocal(const Ray& ray, float maxDist, float& outT, Vec3& outNormal) const;

    Json toJson() const;
    static std::unique_ptr<Mesh> fromJson(const Json& j);

    static const char* shapeName(ShapeKind k);
    static ShapeKind shapeFromName(const std::string& s);
    static std::vector<const char*> allShapeNames();

    // ---- generators ----
    static std::unique_ptr<Mesh> makeShape(const std::string& name, ShapeKind kind, const ShapeParams& p);
    static std::unique_ptr<Mesh> makeBox(const Vec3& size);
    static std::unique_ptr<Mesh> makeSphere(float radius, int segments, int rings);
    static std::unique_ptr<Mesh> makeCylinder(float radius, float height, int segments);
    static std::unique_ptr<Mesh> makeCone(float radius, float height, int segments);
    static std::unique_ptr<Mesh> makeCapsule(float radius, float height, int segments, int rings);
    static std::unique_ptr<Mesh> makePlane(const Vec3& size, int segments);
    static std::unique_ptr<Mesh> makeTorus(float radius, float tube, int segments, int rings);
    static std::unique_ptr<Mesh> makeWedge(const Vec3& size);
    static std::unique_ptr<Mesh> makePyramid(const Vec3& size);
    static std::unique_ptr<Mesh> makeStairs(const Vec3& size, int steps);
    static std::unique_ptr<Mesh> makeArch(const Vec3& size, float radius, int segments);
    static std::unique_ptr<Mesh> makeRing(float radius, float inner, int segments);

    static std::unique_ptr<Mesh> loadObj(const std::string& path, std::string* error = nullptr);

    // Helpers used by the generators and by anything building geometry.
    void addQuad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& normal,
                 const Vec2& uvScale = Vec2{1, 1});
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& normal);
};

} // namespace forge
