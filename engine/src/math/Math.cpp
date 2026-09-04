#include "forge/math/Math.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace forge {

const Vec3 Vec3::Zero{0, 0, 0};
const Vec3 Vec3::One{1, 1, 1};
const Vec3 Vec3::Up{0, 1, 0};
const Vec3 Vec3::Down{0, -1, 0};
const Vec3 Vec3::Forward{0, 0, -1};
const Vec3 Vec3::Back{0, 0, 1};
const Vec3 Vec3::Right{1, 0, 0};
const Vec3 Vec3::Left{-1, 0, 0};
const Quat Quat::Identity{0, 0, 0, 1};

// -----------------------------------------------------------------
//  Colour
// -----------------------------------------------------------------

Color Color::fromSRGB(float r, float g, float b, float a) {
    return {srgbToLinear(r), srgbToLinear(g), srgbToLinear(b), a};
}

Color Color::fromHex(const std::string& hex) {
    const char* s = hex.c_str();
    if (*s == '#') ++s;
    size_t n = std::strlen(s);
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    int r = 255, g = 255, b = 255, a = 255;
    if (n == 3 || n == 4) {
        // #rgb expands each nibble to a byte, so #f0a is #ff00aa.
        r = nib(s[0]) * 17; g = nib(s[1]) * 17; b = nib(s[2]) * 17;
        if (n == 4) a = nib(s[3]) * 17;
    } else if (n >= 6) {
        r = nib(s[0]) * 16 + nib(s[1]);
        g = nib(s[2]) * 16 + nib(s[3]);
        b = nib(s[4]) * 16 + nib(s[5]);
        if (n >= 8) a = nib(s[6]) * 16 + nib(s[7]);
    }
    return fromSRGB(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

std::string Color::toHex() const {
    auto b8 = [](float v) { return (int)clampf(std::round(linearToSrgb(v) * 255.0f), 0.0f, 255.0f); };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b8(r), b8(g), b8(b));
    return buf;
}

// -----------------------------------------------------------------
//  Quat
// -----------------------------------------------------------------

Quat Quat::fromAxisAngle(const Vec3& axis, float degrees_) {
    Vec3 a = axis.normalized();
    float h = radians(degrees_) * 0.5f;
    float s = std::sin(h);
    return {a.x * s, a.y * s, a.z * s, std::cos(h)};
}

Quat Quat::fromTo(const Vec3& from, const Vec3& to) {
    Vec3 f = from.normalized(), t = to.normalized();
    float d = dot(f, t);
    if (d >= 1.0f - 1e-6f) return Identity;
    if (d <= -1.0f + 1e-6f) {
        // Antiparallel: any perpendicular axis is a valid 180° rotation.
        Vec3 axis = cross(Vec3{1, 0, 0}, f);
        if (axis.lengthSq() < 1e-6f) axis = cross(Vec3{0, 1, 0}, f);
        axis.normalize();
        return {axis.x, axis.y, axis.z, 0.0f};
    }
    Vec3 c = cross(f, t);
    float s = std::sqrt((1.0f + d) * 2.0f);
    return Quat{c.x / s, c.y / s, c.z / s, s * 0.5f}.normalized();
}

Quat Quat::lookRotation(const Vec3& forward, const Vec3& up) {
    Vec3 f = forward.normalized();
    if (f.isNearlyZero()) return Identity;
    Vec3 u = up;
    // Degenerate when looking straight up or down; pick another reference.
    if (std::fabs(dot(f, u.normalized())) > 0.9995f) u = Vec3{0, 0, 1};
    Vec3 r = cross(f, u).normalized();
    if (r.isNearlyZero()) r = Vec3{1, 0, 0};
    Vec3 realUp = cross(r, f);

    // Basis columns are (right, up, -forward) because -Z is forward.
    Mat4 m;
    m.at(0, 0) = r.x;      m.at(0, 1) = r.y;      m.at(0, 2) = r.z;
    m.at(1, 0) = realUp.x; m.at(1, 1) = realUp.y; m.at(1, 2) = realUp.z;
    m.at(2, 0) = -f.x;     m.at(2, 1) = -f.y;     m.at(2, 2) = -f.z;

    Vec3 t, s; Quat q;
    m.decompose(t, q, s);
    return q;
}

Quat Quat::slerp(const Quat& a, const Quat& b, float t) {
    float cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat end = b;
    if (cosom < 0.0f) { cosom = -cosom; end = {-b.x, -b.y, -b.z, -b.w}; }
    float sa, sb;
    if (1.0f - cosom > 1e-5f) {
        float omega = std::acos(cosom);
        float sinom = std::sin(omega);
        sa = std::sin((1.0f - t) * omega) / sinom;
        sb = std::sin(t * omega) / sinom;
    } else {
        // Nearly identical: lerp and renormalise, which avoids dividing
        // by a vanishing sine.
        sa = 1.0f - t; sb = t;
    }
    return Quat{a.x * sa + end.x * sb, a.y * sa + end.y * sb,
                a.z * sa + end.z * sb, a.w * sa + end.w * sb}.normalized();
}

// -----------------------------------------------------------------
//  Rotator
// -----------------------------------------------------------------

float Rotator::normalizeAngle(float deg) {
    deg = std::fmod(deg, 360.0f);
    if (deg > 180.0f) deg -= 360.0f;
    else if (deg <= -180.0f) deg += 360.0f;
    return deg;
}

Rotator Rotator::normalized() const {
    return {normalizeAngle(pitch), normalizeAngle(yaw), normalizeAngle(roll)};
}

Rotator Rotator::delta(const Rotator& a, const Rotator& b) {
    return Rotator{b.pitch - a.pitch, b.yaw - a.yaw, b.roll - a.roll}.normalized();
}

Rotator Rotator::lerp(const Rotator& a, const Rotator& b, float t) {
    Rotator d = delta(a, b);
    return (a + d * t).normalized();
}

Quat Rotator::toQuat() const {
    // Yaw then pitch then roll, applied in that order to the object:
    // q = Yaw * Pitch * Roll.
    float hy = radians(yaw) * 0.5f, hp = radians(pitch) * 0.5f, hr = radians(roll) * 0.5f;
    float cy = std::cos(hy), sy = std::sin(hy);
    float cp = std::cos(hp), sp = std::sin(hp);
    float cr = std::cos(hr), sr = std::sin(hr);
    Quat qy{0, sy, 0, cy};
    Quat qp{sp, 0, 0, cp};
    Quat qr{0, 0, sr, cr};
    return (qy * qp * qr).normalized();
}

Rotator Rotator::fromQuat(const Quat& qin) {
    Quat q = qin.normalized();
    // Extract the YXZ Euler set matching toQuat's composition order.
    float sinP = 2.0f * (q.w * q.x - q.y * q.z);
    Rotator r;
    if (sinP >= 0.99999f) {
        // Gimbal lock looking straight up: roll and yaw become the same
        // axis, so fold everything into yaw and leave roll at zero.
        r.pitch = 90.0f;
        r.yaw = degrees(std::atan2(2.0f * (q.x * q.y + q.w * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z)));
        r.roll = 0.0f;
    } else if (sinP <= -0.99999f) {
        r.pitch = -90.0f;
        r.yaw = degrees(std::atan2(2.0f * (q.x * q.y + q.w * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z)));
        r.roll = 0.0f;
    } else {
        r.pitch = degrees(std::asin(sinP));
        r.yaw   = degrees(std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)));
        r.roll  = degrees(std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.x * q.x + q.z * q.z)));
    }
    return r.normalized();
}

Rotator Rotator::fromDirection(const Vec3& dir) {
    Vec3 d = dir.normalized();
    if (d.isNearlyZero()) return {};
    Rotator r;
    r.yaw = degrees(std::atan2(-d.x, -d.z));
    r.pitch = degrees(std::asin(clampf(d.y, -1.0f, 1.0f)));
    r.roll = 0.0f;
    return r;
}

// -----------------------------------------------------------------
//  Mat4
// -----------------------------------------------------------------

Mat4 Mat4::translation(const Vec3& t) {
    Mat4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

Mat4 Mat4::scaling(const Vec3& s) {
    Mat4 r;
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

Mat4 Mat4::rotation(const Quat& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    Mat4 r;
    r.m[0] = 1 - (yy + zz); r.m[1] = xy + wz;       r.m[2] = xz - wy;
    r.m[4] = xy - wz;       r.m[5] = 1 - (xx + zz); r.m[6] = yz + wx;
    r.m[8] = xz + wy;       r.m[9] = yz - wx;       r.m[10] = 1 - (xx + yy);
    return r;
}

Mat4 Mat4::compose(const Vec3& t, const Quat& q, const Vec3& s) {
    Mat4 r = rotation(q);
    // Scale is applied first (innermost), so it multiplies the columns.
    r.m[0] *= s.x; r.m[1] *= s.x; r.m[2] *= s.x;
    r.m[4] *= s.y; r.m[5] *= s.y; r.m[6] *= s.y;
    r.m[8] *= s.z; r.m[9] *= s.z; r.m[10] *= s.z;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

Mat4 Mat4::perspective(float fovYDegrees, float aspect, float zNear, float zFar) {
    float f = 1.0f / std::tan(radians(fovYDegrees) * 0.5f);
    Mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.m[0] = f / std::max(aspect, 1e-4f);
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

Mat4 Mat4::orthographic(float l, float rr, float b, float t, float zNear, float zFar) {
    Mat4 r;
    r.m[0] = 2.0f / (rr - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (zFar - zNear);
    r.m[12] = -(rr + l) / (rr - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(zFar + zNear) / (zFar - zNear);
    return r;
}

Mat4 Mat4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    Vec3 f = (target - eye).normalized();
    if (f.isNearlyZero()) f = Vec3::Forward;
    Vec3 s = cross(f, up).normalized();
    if (s.isNearlyZero()) s = cross(f, Vec3{0, 0, 1}).normalized();
    Vec3 u = cross(s, f);
    Mat4 r;
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r.m[c * 4 + row] = m[0 * 4 + row] * o.m[c * 4 + 0] +
                               m[1 * 4 + row] * o.m[c * 4 + 1] +
                               m[2 * 4 + row] * o.m[c * 4 + 2] +
                               m[3 * 4 + row] * o.m[c * 4 + 3];
        }
    }
    return r;
}

Vec4 Mat4::operator*(const Vec4& v) const {
    return {
        m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
        m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
        m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
        m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
}

Vec3 Mat4::transformPoint(const Vec3& p) const {
    return {m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}

Vec3 Mat4::transformVector(const Vec3& v) const {
    return {m[0] * v.x + m[4] * v.y + m[8]  * v.z,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z};
}

Vec3 Mat4::transformNormal(const Vec3& n) const {
    // Inverse-transpose of the upper 3x3. Built by hand rather than
    // through inverse() so the w row never enters the arithmetic.
    float a = m[0], b = m[4], c = m[8];
    float d = m[1], e = m[5], f = m[9];
    float g = m[2], h = m[6], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12f) return transformVector(n).normalized();
    float inv = 1.0f / det;
    // Cofactor matrix / det, then transposed — which for the normal
    // matrix cancels back to the cofactors in row order.
    float c00 = (e * i - f * h) * inv, c01 = (f * g - d * i) * inv, c02 = (d * h - e * g) * inv;
    float c10 = (c * h - b * i) * inv, c11 = (a * i - c * g) * inv, c12 = (b * g - a * h) * inv;
    float c20 = (b * f - c * e) * inv, c21 = (c * d - a * f) * inv, c22 = (a * e - b * d) * inv;
    return Vec3{c00 * n.x + c10 * n.y + c20 * n.z,
                c01 * n.x + c11 * n.y + c21 * n.z,
                c02 * n.x + c12 * n.y + c22 * n.z}.normalized();
}

Mat4 Mat4::transposed() const {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            r.m[row * 4 + c] = m[c * 4 + row];
    return r;
}

float Mat4::determinant() const {
    const float* a = m;
    float s0 = a[0] * a[5] - a[4] * a[1];
    float s1 = a[0] * a[9] - a[8] * a[1];
    float s2 = a[0] * a[13] - a[12] * a[1];
    float s3 = a[4] * a[9] - a[8] * a[5];
    float s4 = a[4] * a[13] - a[12] * a[5];
    float s5 = a[8] * a[13] - a[12] * a[9];
    float c5 = a[10] * a[15] - a[14] * a[11];
    float c4 = a[6] * a[15] - a[14] * a[7];
    float c3 = a[6] * a[11] - a[10] * a[7];
    float c2 = a[2] * a[15] - a[14] * a[3];
    float c1 = a[2] * a[11] - a[10] * a[3];
    float c0 = a[2] * a[7] - a[6] * a[3];
    return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
}

Mat4 Mat4::inverse() const {
    const float* a = m;
    float s0 = a[0] * a[5] - a[4] * a[1];
    float s1 = a[0] * a[9] - a[8] * a[1];
    float s2 = a[0] * a[13] - a[12] * a[1];
    float s3 = a[4] * a[9] - a[8] * a[5];
    float s4 = a[4] * a[13] - a[12] * a[5];
    float s5 = a[8] * a[13] - a[12] * a[9];
    float c5 = a[10] * a[15] - a[14] * a[11];
    float c4 = a[6] * a[15] - a[14] * a[7];
    float c3 = a[6] * a[11] - a[10] * a[7];
    float c2 = a[2] * a[15] - a[14] * a[3];
    float c1 = a[2] * a[11] - a[10] * a[3];
    float c0 = a[2] * a[7] - a[6] * a[3];

    float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    if (std::fabs(det) < 1e-20f) return Mat4::identity();
    float d = 1.0f / det;

    Mat4 r;
    r.m[0]  = ( a[5] * c5 - a[9] * c4 + a[13] * c3) * d;
    r.m[1]  = (-a[1] * c5 + a[9] * c2 - a[13] * c1) * d;
    r.m[2]  = ( a[1] * c4 - a[5] * c2 + a[13] * c0) * d;
    r.m[3]  = (-a[1] * c3 + a[5] * c1 - a[9]  * c0) * d;
    r.m[4]  = (-a[4] * c5 + a[8] * c4 - a[12] * c3) * d;
    r.m[5]  = ( a[0] * c5 - a[8] * c2 + a[12] * c1) * d;
    r.m[6]  = (-a[0] * c4 + a[4] * c2 - a[12] * c0) * d;
    r.m[7]  = ( a[0] * c3 - a[4] * c1 + a[8]  * c0) * d;
    r.m[8]  = ( a[7] * s5 - a[11] * s4 + a[15] * s3) * d;
    r.m[9]  = (-a[3] * s5 + a[11] * s2 - a[15] * s1) * d;
    r.m[10] = ( a[3] * s4 - a[7]  * s2 + a[15] * s0) * d;
    r.m[11] = (-a[3] * s3 + a[7]  * s1 - a[11] * s0) * d;
    r.m[12] = (-a[6] * s5 + a[10] * s4 - a[14] * s3) * d;
    r.m[13] = ( a[2] * s5 - a[10] * s2 + a[14] * s1) * d;
    r.m[14] = (-a[2] * s4 + a[6]  * s2 - a[14] * s0) * d;
    r.m[15] = ( a[2] * s3 - a[6]  * s1 + a[10] * s0) * d;
    return r;
}

Mat4 Mat4::inverseRigid() const {
    Mat4 r;
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            r.m[c * 4 + row] = m[row * 4 + c];
    Vec3 t{m[12], m[13], m[14]};
    r.m[12] = -(r.m[0] * t.x + r.m[4] * t.y + r.m[8]  * t.z);
    r.m[13] = -(r.m[1] * t.x + r.m[5] * t.y + r.m[9]  * t.z);
    r.m[14] = -(r.m[2] * t.x + r.m[6] * t.y + r.m[10] * t.z);
    return r;
}

void Mat4::decompose(Vec3& t, Quat& q, Vec3& s) const {
    t = {m[12], m[13], m[14]};
    Vec3 cx{m[0], m[1], m[2]}, cy{m[4], m[5], m[6]}, cz{m[8], m[9], m[10]};
    s = {cx.length(), cy.length(), cz.length()};
    // A negative determinant means an odd number of mirrored axes; fold
    // the flip into X so the rotation stays a proper rotation.
    if (determinant() < 0.0f) s.x = -s.x;
    if (s.x > kSmall) cx = cx / s.x;
    if (s.y > kSmall) cy = cy / s.y;
    if (s.z > kSmall) cz = cz / s.z;

    float trace = cx.x + cy.y + cz.z;
    if (trace > 0.0f) {
        float k = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(cy.z - cz.y) / k, (cz.x - cx.z) / k, (cx.y - cy.x) / k, 0.25f * k};
    } else if (cx.x > cy.y && cx.x > cz.z) {
        float k = std::sqrt(1.0f + cx.x - cy.y - cz.z) * 2.0f;
        q = {0.25f * k, (cy.x + cx.y) / k, (cz.x + cx.z) / k, (cy.z - cz.y) / k};
    } else if (cy.y > cz.z) {
        float k = std::sqrt(1.0f + cy.y - cx.x - cz.z) * 2.0f;
        q = {(cy.x + cx.y) / k, 0.25f * k, (cz.y + cy.z) / k, (cz.x - cx.z) / k};
    } else {
        float k = std::sqrt(1.0f + cz.z - cx.x - cy.y) * 2.0f;
        q = {(cz.x + cx.z) / k, (cz.y + cy.z) / k, 0.25f * k, (cx.y - cy.x) / k};
    }
    q.normalize();
}

// -----------------------------------------------------------------
//  Transform
// -----------------------------------------------------------------

Transform Transform::fromMatrix(const Mat4& m) {
    Transform t;
    m.decompose(t.position, t.rotation, t.scale);
    return t;
}

Transform Transform::operator*(const Transform& child) const {
    Transform out;
    out.scale = scale * child.scale;
    out.rotation = (rotation * child.rotation).normalized();
    out.position = position + rotation * (child.position * scale);
    return out;
}

Transform Transform::inverse() const {
    Transform out;
    Vec3 invScale{
        std::fabs(scale.x) > kSmall ? 1.0f / scale.x : 0.0f,
        std::fabs(scale.y) > kSmall ? 1.0f / scale.y : 0.0f,
        std::fabs(scale.z) > kSmall ? 1.0f / scale.z : 0.0f};
    out.rotation = rotation.inverse();
    out.scale = invScale;
    out.position = out.rotation * (-position) * invScale;
    return out;
}

Transform Transform::lerp(const Transform& a, const Transform& b, float t) {
    Transform out;
    out.position = forge::lerp(a.position, b.position, t);
    out.rotation = Quat::slerp(a.rotation, b.rotation, t);
    out.scale = forge::lerp(a.scale, b.scale, t);
    return out;
}

// -----------------------------------------------------------------
//  Bounds
// -----------------------------------------------------------------

Box Box::transformed(const Mat4& mx) const {
    if (!valid()) return {};
    // The transformed AABB is the centre transformed, plus the absolute
    // value of the basis times the extents — cheaper and tighter than
    // pushing eight corners through, and exact for affine transforms.
    Vec3 c = mx.transformPoint(center());
    Vec3 e = extents();
    Vec3 ex{
        std::fabs(mx.m[0]) * e.x + std::fabs(mx.m[4]) * e.y + std::fabs(mx.m[8])  * e.z,
        std::fabs(mx.m[1]) * e.x + std::fabs(mx.m[5]) * e.y + std::fabs(mx.m[9])  * e.z,
        std::fabs(mx.m[2]) * e.x + std::fabs(mx.m[6]) * e.y + std::fabs(mx.m[10]) * e.z};
    return {c - ex, c + ex};
}

Frustum Frustum::fromMatrix(const Mat4& vp) {
    // Gribb-Hartmann: each plane is a row of the view-projection matrix
    // added to or subtracted from the w row.
    Frustum f;
    auto row = [&](int i) { return Vec4{vp.m[0 + i], vp.m[4 + i], vp.m[8 + i], vp.m[12 + i]}; };
    Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Vec4 raw[6] = {
        {r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w},   // left
        {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w},   // right
        {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w},   // bottom
        {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w},   // top
        {r3.x + r2.x, r3.y + r2.y, r3.z + r2.z, r3.w + r2.w},   // near
        {r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w}};  // far
    for (int i = 0; i < 6; ++i) {
        Vec3 n{raw[i].x, raw[i].y, raw[i].z};
        float len = n.length();
        if (len > kSmall) { n = n / len; f.planes[i] = {n, raw[i].w / len}; }
        else f.planes[i] = {Vec3{0, 1, 0}, 1e30f};
    }
    return f;
}

bool Frustum::intersects(const Box& b) const {
    if (!b.valid()) return false;
    for (const Plane& p : planes) {
        // The box is outside only when its most-positive corner along the
        // plane normal is still behind the plane.
        Vec3 v{p.normal.x >= 0 ? b.max.x : b.min.x,
               p.normal.y >= 0 ? b.max.y : b.min.y,
               p.normal.z >= 0 ? b.max.z : b.min.z};
        if (p.distanceTo(v) < 0.0f) return false;
    }
    return true;
}

bool Frustum::intersects(const Sphere& s) const {
    for (const Plane& p : planes)
        if (p.distanceTo(s.center) < -s.radius) return false;
    return true;
}

// -----------------------------------------------------------------
//  Intersections
// -----------------------------------------------------------------

bool rayBox(const Ray& r, const Box& b, float maxDist, float& outT, Vec3& outNormal) {
    float tmin = 0.0f, tmax = maxDist;
    int axis = 0; float nsign = -1.0f;
    for (int i = 0; i < 3; ++i) {
        float o = r.origin[i], d = r.direction[i];
        float lo = b.min[i], hi = b.max[i];
        if (std::fabs(d) < 1e-8f) {
            if (o < lo || o > hi) return false;
            continue;
        }
        float inv = 1.0f / d;
        float t1 = (lo - o) * inv, t2 = (hi - o) * inv;
        float s = -1.0f;
        if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }
        if (t1 > tmin) { tmin = t1; axis = i; nsign = s; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    outT = tmin;
    outNormal = Vec3::Zero;
    outNormal[axis] = nsign;
    return true;
}

bool raySphere(const Ray& r, const Vec3& c, float radius, float maxDist, float& outT) {
    Vec3 oc = r.origin - c;
    float b = dot(oc, r.direction);
    float cc = dot(oc, oc) - radius * radius;
    if (cc > 0.0f && b > 0.0f) return false;
    float disc = b * b - cc;
    if (disc < 0.0f) return false;
    float t = -b - std::sqrt(disc);
    if (t < 0.0f) t = 0.0f;
    if (t > maxDist) return false;
    outT = t;
    return true;
}

bool rayTriangle(const Ray& r, const Vec3& a, const Vec3& b, const Vec3& c,
                 float maxDist, float& outT, Vec3& outNormal) {
    // Möller-Trumbore.
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p = cross(r.direction, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;
    float invDet = 1.0f / det;
    Vec3 tv = r.origin - a;
    float u = dot(tv, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = cross(tv, e1);
    float v = dot(r.direction, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = dot(e2, q) * invDet;
    if (t < 0.0f || t > maxDist) return false;
    outT = t;
    outNormal = cross(e1, e2).normalized();
    if (dot(outNormal, r.direction) > 0.0f) outNormal = -outNormal;
    return true;
}

bool rayPlane(const Ray& r, const Plane& p, float& outT) {
    float denom = dot(p.normal, r.direction);
    if (std::fabs(denom) < 1e-8f) return false;
    float t = -(dot(p.normal, r.origin) + p.d) / denom;
    if (t < 0.0f) return false;
    outT = t;
    return true;
}

Vec3 closestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p) {
    Vec3 ab = b - a;
    float len2 = ab.lengthSq();
    if (len2 < 1e-12f) return a;
    float t = clampf(dot(p - a, ab) / len2, 0.0f, 1.0f);
    return a + ab * t;
}

// -----------------------------------------------------------------
//  Noise and random
// -----------------------------------------------------------------

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

uint32_t hashString(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

static float hash2f(int x, int y, uint32_t seed) {
    uint32_t h = hash32((uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2654435761u);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
}

float valueNoise(float x, float y, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    // Quintic fade: continuous second derivative, so fbm made of this
    // does not show grid creases under lighting.
    float u = xf * xf * xf * (xf * (xf * 6.0f - 15.0f) + 10.0f);
    float v = yf * yf * yf * (yf * (yf * 6.0f - 15.0f) + 10.0f);
    float a = hash2f(xi, yi, seed), b = hash2f(xi + 1, yi, seed);
    float c = hash2f(xi, yi + 1, seed), d = hash2f(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}

float fbm(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x * freq, y * freq, seed + (uint32_t)i * 131u) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

uint32_t Random::nextUInt() {
    // xorshift32
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
}
float Random::unit() { return (float)(nextUInt() >> 8) / (float)(1 << 24); }
float Random::range(float lo, float hi) { return lo + (hi - lo) * unit(); }
int Random::rangeInt(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(nextUInt() % (uint32_t)(hi - lo + 1));
}
Vec3 Random::onSphere() {
    float z = range(-1.0f, 1.0f);
    float a = range(0.0f, kTwoPi);
    float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {r * std::cos(a), r * std::sin(a), z};
}
Vec3 Random::inSphere() { return onSphere() * std::cbrt(unit()); }

// -----------------------------------------------------------------
//  Strings
// -----------------------------------------------------------------

std::string toLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += (char)std::tolower((unsigned char)c);
    return out;
}

bool containsCI(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

} // namespace forge
