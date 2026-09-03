// ============================================================
//  Forge — math primitives.
//
//  Header-only and value-semantic: a Vec3 is three floats and nothing
//  else, so an array of them is an array of floats and the renderer can
//  upload it without a repack.
//
//  Conventions, fixed once here because half the bugs in an engine come
//  from two files disagreeing about them:
//
//    - right-handed, Y up, -Z forward (a camera looks down -Z)
//    - Rotator is degrees, applied yaw (Y), then pitch (X), then roll (Z)
//    - matrices are column-major and multiply column vectors: M * v
//    - angles in the public API are degrees; radians never escape a file
// ============================================================
#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <string>

namespace forge {

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = kPi * 2.0f;
constexpr float kHalfPi  = kPi * 0.5f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kSmall   = 1e-6f;

inline float radians(float d) { return d * kDegToRad; }
inline float degrees(float r) { return r * kRadToDeg; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sqr(float v) { return v * v; }
inline float sign(float v) { return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f); }
inline bool nearlyEqual(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Frame-rate independent approach: `speed` is the fraction of the gap
// closed per second, so a constant tuned at 60 Hz behaves the same at 144.
inline float damp(float a, float b, float speed, float dt) {
    return lerpf(a, b, 1.0f - std::exp(-speed * dt));
}

// Move `a` toward `b` by at most `step`. Used wherever an ease would
// overshoot: acceleration, aim interpolation, health regeneration.
inline float approach(float a, float b, float step) {
    if (a < b) return std::min(a + step, b);
    return std::max(a - step, b);
}

// Remap and smooth, the two shaping functions gameplay reaches for most.
inline float invLerp(float a, float b, float v) { return std::fabs(b - a) < kSmall ? 0.0f : (v - a) / (b - a); }
inline float smoothStep(float a, float b, float v) { float t = saturate(invLerp(a, b, v)); return t * t * (3.0f - 2.0f * t); }

// -----------------------------------------------------------------
//  Vec2 / Vec3 / Vec4
// -----------------------------------------------------------------

struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    explicit constexpr Vec2(float s) : x(s), y(s) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const { float l = length(); return l > kSmall ? Vec2{x / l, y / l} : Vec2{}; }
};
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr Vec3(float s) : x(s), y(s), z(s) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator/(const Vec3& o) const { return {x / o.x, y / o.y, z / o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator*=(const Vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }
    float operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSq() const { return x * x + y * y + z * z; }
    Vec3 normalized() const { float l = length(); return l > kSmall ? Vec3{x / l, y / l, z / l} : Vec3{}; }
    void normalize() { float l = length(); if (l > kSmall) { x /= l; y /= l; z /= l; } }
    bool isNearlyZero(float eps = kSmall) const { return lengthSq() <= eps * eps; }
    // The horizontal part, which gameplay wants far more often than the
    // full vector: movement input, distance-to-target, facing.
    Vec3 flat() const { return {x, 0.0f, z}; }

    static const Vec3 Zero, One, Up, Down, Forward, Back, Right, Left;
};
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float distance(const Vec3& a, const Vec3& b) { return (a - b).length(); }
inline float distanceSq(const Vec3& a, const Vec3& b) { return (a - b).lengthSq(); }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 minv(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 maxv(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3 absv(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }
inline Vec3 clampv(const Vec3& v, const Vec3& lo, const Vec3& hi) { return minv(maxv(v, lo), hi); }
inline float maxComponent(const Vec3& v) { return std::max(v.x, std::max(v.y, v.z)); }
inline float minComponent(const Vec3& v) { return std::min(v.x, std::min(v.y, v.z)); }

// Remove the component of `v` that points into `n`. Every slide along a
// wall in the character controller is this one line.
inline Vec3 projectOnPlane(const Vec3& v, const Vec3& n) { return v - n * dot(v, n); }
inline Vec3 reflect(const Vec3& v, const Vec3& n) { return v - n * (2.0f * dot(v, n)); }
inline Vec3 clampLength(const Vec3& v, float maxLen) {
    float l = v.length();
    return (l > maxLen && l > kSmall) ? v * (maxLen / l) : v;
}
inline Vec3 dampv(const Vec3& a, const Vec3& b, float speed, float dt) {
    float t = 1.0f - std::exp(-speed * dt);
    return a + (b - a) * t;
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    Vec3 xyz() const { return {x, y, z}; }
    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    float operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
};
inline float dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// -----------------------------------------------------------------
//  Colour
// -----------------------------------------------------------------

// Stored linear. Anything that arrives as an sRGB hex string is converted
// on the way in, so shading never has to ask which space it is in.
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    Color operator*(float s) const { return {r * s, g * s, b * s, a}; }
    Color operator*(const Color& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
    Color operator+(const Color& o) const { return {r + o.r, g + o.g, b + o.b, a}; }
    Vec3 rgb() const { return {r, g, b}; }

    static Color fromSRGB(float r, float g, float b, float a = 1.0f);
    static Color fromHex(const std::string& hex);   // "#rrggbb" or "#rgb"
    std::string toHex() const;
    static Color lerp(const Color& a, const Color& b, float t) {
        return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t)};
    }
};

inline float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float c) {
    c = std::max(0.0f, c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// -----------------------------------------------------------------
//  Quat
// -----------------------------------------------------------------

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat fromAxisAngle(const Vec3& axis, float degrees_);
    // Shortest arc taking `from` to `to`, both assumed unit length.
    static Quat fromTo(const Vec3& from, const Vec3& to);
    static Quat lookRotation(const Vec3& forward, const Vec3& up = Vec3{0, 1, 0});

    Quat operator*(const Quat& o) const {
        return {
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z};
    }
    Vec3 operator*(const Vec3& v) const {
        // v + 2w(q x v) + 2(q x (q x v)) — the standard rotation without
        // building a matrix first.
        Vec3 q{x, y, z};
        Vec3 t = cross(q, v) * 2.0f;
        return v + t * w + cross(q, t);
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Quat inverse() const { return conjugate(); }   // unit quats only
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    Quat normalized() const { float l = length(); return l > kSmall ? Quat{x / l, y / l, z / l, w / l} : Quat{}; }
    void normalize() { float l = length(); if (l > kSmall) { x /= l; y /= l; z /= l; w /= l; } }

    Vec3 forward() const { return (*this) * Vec3{0, 0, -1}; }
    Vec3 right() const { return (*this) * Vec3{1, 0, 0}; }
    Vec3 up() const { return (*this) * Vec3{0, 1, 0}; }

    static Quat slerp(const Quat& a, const Quat& b, float t);
    static const Quat Identity;
};

// -----------------------------------------------------------------
//  Rotator — degrees, the rotation type designers actually type into
//  a details panel. Converts to and from Quat on demand.
// -----------------------------------------------------------------

struct Rotator {
    float pitch = 0;   // about X, nose up positive
    float yaw   = 0;   // about Y
    float roll  = 0;   // about Z
    constexpr Rotator() = default;
    constexpr Rotator(float p, float yw, float r) : pitch(p), yaw(yw), roll(r) {}

    Quat toQuat() const;
    static Rotator fromQuat(const Quat& q);
    // The rotation whose forward axis is `dir`. Roll is left at zero,
    // which is what a camera or an AI facing a target wants.
    static Rotator fromDirection(const Vec3& dir);

    Vec3 forward() const { return toQuat().forward(); }
    Vec3 right() const { return toQuat().right(); }
    Vec3 up() const { return toQuat().up(); }

    Rotator operator+(const Rotator& o) const { return {pitch + o.pitch, yaw + o.yaw, roll + o.roll}; }
    Rotator operator-(const Rotator& o) const { return {pitch - o.pitch, yaw - o.yaw, roll - o.roll}; }
    Rotator operator*(float s) const { return {pitch * s, yaw * s, roll * s}; }
    Rotator& operator+=(const Rotator& o) { pitch += o.pitch; yaw += o.yaw; roll += o.roll; return *this; }
    bool operator==(const Rotator& o) const { return pitch == o.pitch && yaw == o.yaw && roll == o.roll; }

    // Fold every angle into (-180, 180] so a yaw that has spun twenty
    // times still compares and interpolates sanely.
    Rotator normalized() const;
    static float normalizeAngle(float deg);
    // Shortest signed delta from a to b, per axis.
    static Rotator delta(const Rotator& a, const Rotator& b);
    static Rotator lerp(const Rotator& a, const Rotator& b, float t);
};

// -----------------------------------------------------------------
//  Mat4 — column-major, m[col][row] in the flat array as m[col*4+row].
// -----------------------------------------------------------------

struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Mat4 identity() { return {}; }
    static Mat4 translation(const Vec3& t);
    static Mat4 scaling(const Vec3& s);
    static Mat4 rotation(const Quat& q);
    static Mat4 compose(const Vec3& t, const Quat& r, const Vec3& s);
    static Mat4 perspective(float fovYDegrees, float aspect, float zNear, float zFar);
    static Mat4 orthographic(float l, float r, float b, float t, float zNear, float zFar);
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

    Mat4 operator*(const Mat4& o) const;
    Vec4 operator*(const Vec4& v) const;
    Vec3 transformPoint(const Vec3& p) const;
    Vec3 transformVector(const Vec3& v) const;   // ignores translation
    // For normals under non-uniform scale. Falls back to the upper 3x3
    // when the matrix is orthonormal, which is the common case.
    Vec3 transformNormal(const Vec3& n) const;

    Mat4 transposed() const;
    Mat4 inverse() const;
    // Rigid-transform inverse: transpose the rotation, negate the
    // translation. Only valid with no scale, but that covers view matrices.
    Mat4 inverseRigid() const;
    void decompose(Vec3& t, Quat& r, Vec3& s) const;
    float determinant() const;

    float& at(int col, int row) { return m[col * 4 + row]; }
    float at(int col, int row) const { return m[col * 4 + row]; }
};

// -----------------------------------------------------------------
//  Transform — the thing every SceneComponent stores.
// -----------------------------------------------------------------

struct Transform {
    Vec3 position{0, 0, 0};
    Quat rotation{};
    Vec3 scale{1, 1, 1};

    Mat4 toMatrix() const { return Mat4::compose(position, rotation, scale); }
    static Transform fromMatrix(const Mat4& m);

    // this applied after `parent` — i.e. parent's space to world.
    Transform operator*(const Transform& child) const;
    Transform inverse() const;
    Vec3 transformPoint(const Vec3& p) const { return position + rotation * (p * scale); }
    Vec3 transformVector(const Vec3& v) const { return rotation * (v * scale); }
    Vec3 inverseTransformPoint(const Vec3& p) const { return (rotation.inverse() * (p - position)) / scale; }

    Vec3 forward() const { return rotation.forward(); }
    Vec3 right() const { return rotation.right(); }
    Vec3 up() const { return rotation.up(); }
    static Transform lerp(const Transform& a, const Transform& b, float t);
};

// -----------------------------------------------------------------
//  Bounds and rays
// -----------------------------------------------------------------

struct Box {
    Vec3 min{ 1e30f,  1e30f,  1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    Box() = default;
    Box(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}
    static Box fromCenterExtents(const Vec3& c, const Vec3& e) { return {c - e, c + e}; }

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    Vec3 size() const { return max - min; }
    float volume() const { Vec3 s = size(); return s.x * s.y * s.z; }

    void expand(const Vec3& p) { min = minv(min, p); max = maxv(max, p); }
    void expand(const Box& b) { if (!b.valid()) return; min = minv(min, b.min); max = maxv(max, b.max); }
    Box grown(float d) const { return {min - Vec3{d, d, d}, max + Vec3{d, d, d}}; }
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    bool intersects(const Box& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y &&
               max.y >= o.min.y && min.z <= o.max.z && max.z >= o.min.z;
    }
    Vec3 closestPoint(const Vec3& p) const { return clampv(p, min, max); }
    // The AABB that encloses this box after `m` — the eight corners
    // transformed, which is what a static mesh's world bounds needs.
    Box transformed(const Mat4& m) const;
};

struct Sphere {
    Vec3 center{0, 0, 0};
    float radius = 0;
    bool contains(const Vec3& p) const { return distanceSq(center, p) <= radius * radius; }
    bool intersects(const Sphere& o) const { return distanceSq(center, o.center) <= sqr(radius + o.radius); }
};

struct Ray {
    Vec3 origin{0, 0, 0};
    Vec3 direction{0, 0, -1};   // expected unit length
    Vec3 at(float t) const { return origin + direction * t; }
};

struct Plane {
    Vec3 normal{0, 1, 0};
    float d = 0;   // plane is dot(normal, p) + d == 0
    float distanceTo(const Vec3& p) const { return dot(normal, p) + d; }
    static Plane fromPointNormal(const Vec3& p, const Vec3& n) { return {n, -dot(n, p)}; }
};

// A view frustum, built from a view-projection matrix, for culling.
struct Frustum {
    Plane planes[6];
    static Frustum fromMatrix(const Mat4& viewProj);
    bool intersects(const Box& b) const;
    bool intersects(const Sphere& s) const;
};

// -----------------------------------------------------------------
//  Intersection tests shared by physics and picking
// -----------------------------------------------------------------

// All return false, or true with `outT` set to the distance along the ray.
bool rayBox(const Ray& r, const Box& b, float maxDist, float& outT, Vec3& outNormal);
bool raySphere(const Ray& r, const Vec3& c, float radius, float maxDist, float& outT);
bool rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c, float maxDist, float& outT, Vec3& outNormal);
bool rayPlane(const Ray& r, const Plane& p, float& outT);

// Closest point on the segment [a,b] to p.
Vec3 closestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p);

// -----------------------------------------------------------------
//  Noise — one hash and one value-noise field, enough for procedural
//  terrain and textures without a dependency.
// -----------------------------------------------------------------

uint32_t hash32(uint32_t x);
uint32_t hashString(const std::string& s);
float valueNoise(float x, float y, uint32_t seed = 0);
float fbm(float x, float y, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f, uint32_t seed = 0);

// -----------------------------------------------------------------
//  Strings — just enough to keep every case-insensitive search in the
//  engine (the palette filter, the content browser, the script node
//  search) off its own hand-rolled lowering loop.
// -----------------------------------------------------------------

std::string toLower(const std::string& s);
// Whether `haystack` contains `needle`, ignoring case. An empty needle
// always matches, which is what lets a filter box start empty.
bool containsCI(const std::string& haystack, const std::string& needle);

// Small deterministic PRNG. Seeded streams keep procedural content stable
// between runs, which matters when a level references generated content.
class Random {
public:
    explicit Random(uint32_t seed = 0x9E3779B9u) : state_(seed ? seed : 1u) {}
    uint32_t nextUInt();
    float unit();                       // [0,1)
    float range(float lo, float hi);
    int rangeInt(int lo, int hi);       // inclusive
    Vec3 onSphere();
    Vec3 inSphere();
private:
    uint32_t state_;
};

} // namespace forge
