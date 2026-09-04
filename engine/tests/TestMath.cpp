#include "Test.hpp"
#include "forge/math/Math.hpp"

using namespace forge;

TEST(vec3_basics) {
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK_VEC(a + b, (Vec3{5, 7, 9}), 1e-6);
    CHECK_NEAR(dot(a, b), 32.0f, 1e-5);
    CHECK_VEC(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}), (Vec3{0, 0, 1}), 1e-6);
    CHECK_NEAR(Vec3(3, 4, 0).length(), 5.0f, 1e-5);
    CHECK_NEAR(Vec3(3, 4, 0).normalized().length(), 1.0f, 1e-5);
    CHECK_VEC(Vec3(2, 5, 7).flat(), (Vec3{2, 0, 7}), 1e-6);
}

TEST(vec3_projection) {
    // Sliding along a wall: the component into the wall must vanish.
    Vec3 v{1, -1, 0};
    Vec3 n{0, 1, 0};
    Vec3 slid = projectOnPlane(v, n);
    CHECK_NEAR(dot(slid, n), 0.0f, 1e-6);
    CHECK_VEC(slid, (Vec3{1, 0, 0}), 1e-6);

    Vec3 bounced = reflect(Vec3{1, -1, 0}.normalized(), n);
    CHECK_NEAR(bounced.y, Vec3(1, 1, 0).normalized().y, 1e-5);
    CHECK_VEC(clampLength(Vec3{10, 0, 0}, 3.0f), (Vec3{3, 0, 0}), 1e-5);
}

TEST(quat_rotation) {
    Quat yaw90 = Quat::fromAxisAngle(Vec3::Up, 90.0f);
    // Yawing +90 about Y takes -Z (forward) to -X (left).
    CHECK_VEC(yaw90 * Vec3::Forward, (Vec3{-1, 0, 0}), 1e-5);
    CHECK_VEC(yaw90.forward(), (Vec3{-1, 0, 0}), 1e-5);

    Quat q = Quat::fromAxisAngle(Vec3{0.3f, 1.0f, 0.2f}, 137.0f);
    Vec3 v{1.3f, -2.0f, 0.7f};
    // Rotating then un-rotating must be the identity.
    CHECK_VEC(q.inverse() * (q * v), v, 1e-4);
    // Rotation preserves length.
    CHECK_NEAR((q * v).length(), v.length(), 1e-4);
}

TEST(quat_fromTo_and_look) {
    Vec3 a = Vec3{1, 2, 3}.normalized(), b = Vec3{-2, 0.5f, 1}.normalized();
    CHECK_VEC(Quat::fromTo(a, b) * a, b, 1e-4);
    // Antiparallel is the degenerate case worth pinning down.
    CHECK_VEC(Quat::fromTo(Vec3::Up, Vec3::Down) * Vec3::Up, Vec3::Down * 1.0f, 1e-4);

    Quat look = Quat::lookRotation(Vec3{0, 0, -1});
    CHECK_VEC(look.forward(), Vec3::Forward * 1.0f, 1e-4);
    Vec3 dir = Vec3{1, 0.4f, -0.6f}.normalized();
    CHECK_VEC(Quat::lookRotation(dir).forward(), dir, 1e-4);
}

TEST(quat_slerp) {
    Quat a = Quat::Identity;
    Quat b = Quat::fromAxisAngle(Vec3::Up, 90.0f);
    Quat mid = Quat::slerp(a, b, 0.5f);
    CHECK_NEAR(Rotator::fromQuat(mid).yaw, 45.0f, 1e-3);
    CHECK_NEAR(Quat::slerp(a, b, 0.0f).w, a.w, 1e-5);
    CHECK_NEAR(Rotator::fromQuat(Quat::slerp(a, b, 1.0f)).yaw, 90.0f, 1e-3);
}

TEST(rotator_roundtrip) {
    // Every rotator must survive a trip through quaternion space.
    const Rotator samples[] = {
        {0, 0, 0}, {30, 45, 0}, {-20, 170, 15}, {60, -90, -30},
        {12.5f, 200.0f, -175.0f}, {-89.0f, 33.0f, 0.0f}};
    for (const Rotator& r : samples) {
        Rotator back = Rotator::fromQuat(r.toQuat());
        // Compare through the forward vector: two rotators can differ in
        // representation and still be the same rotation.
        CHECK_VEC(back.forward(), r.forward(), 1e-4);
        CHECK_VEC(back.up(), r.up(), 1e-4);
    }
}

TEST(rotator_angles) {
    CHECK_NEAR(Rotator::normalizeAngle(370.0f), 10.0f, 1e-4);
    CHECK_NEAR(Rotator::normalizeAngle(-190.0f), 170.0f, 1e-4);
    CHECK_NEAR(Rotator::normalizeAngle(180.0f), 180.0f, 1e-4);
    // Crossing the wrap must take the short way round, not 350 degrees.
    CHECK_NEAR(Rotator::delta({0, 350, 0}, {0, 10, 0}).yaw, 20.0f, 1e-3);
    CHECK_NEAR(Rotator::lerp({0, 350, 0}, {0, 10, 0}, 0.5f).yaw, 0.0f, 1e-3);

    Rotator r = Rotator::fromDirection(Vec3{0, 0, -1});
    CHECK_NEAR(r.yaw, 0.0f, 1e-3);
    CHECK_NEAR(Rotator::fromDirection(Vec3{0, 1, 0}).pitch, 90.0f, 1e-3);
}

TEST(mat4_transform) {
    Mat4 t = Mat4::translation({1, 2, 3});
    CHECK_VEC(t.transformPoint(Vec3::Zero), (Vec3{1, 2, 3}), 1e-6);
    // A vector has no position, so translation must not touch it.
    CHECK_VEC(t.transformVector(Vec3{1, 0, 0}), (Vec3{1, 0, 0}), 1e-6);

    Mat4 m = Mat4::compose({5, -2, 1}, Quat::fromAxisAngle(Vec3::Up, 33.0f), {2, 2, 2});
    Mat4 inv = m.inverse();
    Vec3 p{1.5f, 2.5f, -3.0f};
    CHECK_VEC(inv.transformPoint(m.transformPoint(p)), p, 1e-4);

    Vec3 tr, sc; Quat rot;
    m.decompose(tr, rot, sc);
    CHECK_VEC(tr, (Vec3{5, -2, 1}), 1e-4);
    CHECK_VEC(sc, (Vec3{2, 2, 2}), 1e-4);
    CHECK_NEAR(Rotator::fromQuat(rot).yaw, 33.0f, 1e-3);
}

TEST(mat4_normals_under_scale) {
    // The classic non-uniform-scale trap: a normal transformed by the
    // matrix itself stops being perpendicular to its surface.
    Mat4 m = Mat4::compose(Vec3::Zero, Quat::Identity, {1, 4, 1});
    Vec3 tangent = m.transformVector(Vec3{1, 1, 0}.normalized());
    Vec3 n = m.transformNormal(Vec3{1, -1, 0}.normalized());
    CHECK_NEAR(dot(tangent.normalized(), n), 0.0f, 1e-4);
}

TEST(mat4_projection) {
    Mat4 p = Mat4::perspective(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    // A point on the near plane maps to NDC z = -1, the far plane to +1.
    Vec4 nearP = p * Vec4{0, 0, -0.1f, 1};
    CHECK_NEAR(nearP.z / nearP.w, -1.0f, 1e-3);
    Vec4 farP = p * Vec4{0, 0, -100.0f, 1};
    CHECK_NEAR(farP.z / farP.w, 1.0f, 1e-3);

    Mat4 v = Mat4::lookAt({0, 0, 5}, Vec3::Zero, Vec3::Up);
    // The camera sits 5 in front of the origin, so the origin lands at -5.
    CHECK_VEC(v.transformPoint(Vec3::Zero), (Vec3{0, 0, -5}), 1e-4);
    CHECK_VEC(v.inverseRigid().transformPoint(Vec3::Zero), (Vec3{0, 0, 5}), 1e-4);
}

TEST(transform_composition) {
    Transform parent;
    parent.position = {10, 0, 0};
    parent.rotation = Quat::fromAxisAngle(Vec3::Up, 90.0f);
    parent.scale = {2, 2, 2};

    Transform child;
    child.position = {0, 0, -1};   // one unit in front of the parent

    Transform world = parent * child;
    // Parent yawed 90, so its forward is -X; scaled by 2, that is 2 units.
    CHECK_VEC(world.position, (Vec3{8, 0, 0}), 1e-4);

    // Composition must agree with matrix multiplication.
    Mat4 byMatrix = parent.toMatrix() * child.toMatrix();
    CHECK_VEC(Transform::fromMatrix(byMatrix).position, world.position, 1e-4);

    Transform inv = world.inverse();
    CHECK_VEC(inv.transformPoint(world.transformPoint(Vec3{1, 2, 3})), (Vec3{1, 2, 3}), 1e-3);
}

TEST(box_and_frustum) {
    Box b{{-1, -1, -1}, {1, 1, 1}};
    CHECK(b.contains(Vec3::Zero));
    CHECK(!b.contains(Vec3{2, 0, 0}));
    CHECK_VEC(b.closestPoint(Vec3{5, 0, 0}), (Vec3{1, 0, 0}), 1e-6);
    CHECK_NEAR(b.volume(), 8.0f, 1e-4);

    Box rotated = b.transformed(Mat4::rotation(Quat::fromAxisAngle(Vec3::Up, 45.0f)));
    // A cube spun 45 degrees needs a wider box to hold it.
    CHECK_NEAR(rotated.max.x, std::sqrt(2.0f), 1e-4);
    CHECK_NEAR(rotated.max.y, 1.0f, 1e-4);

    Mat4 vp = Mat4::perspective(60.0f, 1.0f, 0.1f, 100.0f) * Mat4::lookAt({0, 0, 10}, Vec3::Zero, Vec3::Up);
    Frustum f = Frustum::fromMatrix(vp);
    CHECK(f.intersects(Box::fromCenterExtents(Vec3::Zero, Vec3{1, 1, 1})));
    CHECK(!f.intersects(Box::fromCenterExtents(Vec3{0, 0, 200}, Vec3{1, 1, 1})));
    CHECK(!f.intersects(Box::fromCenterExtents(Vec3{500, 0, 0}, Vec3{1, 1, 1})));
    CHECK(f.intersects(Sphere{Vec3::Zero, 1.0f}));
}

TEST(ray_intersections) {
    Ray r{{0, 0, 5}, {0, 0, -1}};
    float t; Vec3 n;
    CHECK(rayBox(r, Box{{-1, -1, -1}, {1, 1, 1}}, 100.0f, t, n));
    CHECK_NEAR(t, 4.0f, 1e-4);
    CHECK_VEC(n, (Vec3{0, 0, 1}), 1e-4);

    CHECK(raySphere(r, Vec3::Zero, 1.0f, 100.0f, t));
    CHECK_NEAR(t, 4.0f, 1e-4);
    CHECK(!raySphere(Ray{{0, 10, 5}, {0, 0, -1}}, Vec3::Zero, 1.0f, 100.0f, t));

    // Behind the ray is a miss, not a negative hit.
    CHECK(!rayBox(Ray{{0, 0, -5}, {0, 0, -1}}, Box{{-1, -1, -1}, {1, 1, 1}}, 100.0f, t, n));

    CHECK(rayTriangle(r, {-1, -1, 0}, {1, -1, 0}, {0, 1, 0}, 100.0f, t, n));
    CHECK_NEAR(t, 5.0f, 1e-4);
    CHECK_VEC(n, (Vec3{0, 0, 1}), 1e-4);

    CHECK_VEC(closestPointOnSegment({0, 0, 0}, {10, 0, 0}, {3, 5, 0}), (Vec3{3, 0, 0}), 1e-5);
    CHECK_VEC(closestPointOnSegment({0, 0, 0}, {10, 0, 0}, {-5, 5, 0}), (Vec3{0, 0, 0}), 1e-5);
}

TEST(color_space) {
    Color c = Color::fromHex("#ff0000");
    CHECK_NEAR(c.r, 1.0f, 1e-4);
    CHECK_NEAR(c.g, 0.0f, 1e-4);
    // Mid grey in sRGB is much darker in linear light — this is the
    // conversion that, when skipped, makes every engine look washed out.
    Color mid = Color::fromHex("#808080");
    CHECK(mid.r < 0.25f && mid.r > 0.2f);
    CHECK(Color::fromHex("#f0a").toHex() == "#ff00aa");
    CHECK(Color::fromHex("#ff0000").toHex() == "#ff0000");
    CHECK_NEAR(linearToSrgb(srgbToLinear(0.42f)), 0.42f, 1e-5);
}

TEST(noise_and_random) {
    // Noise must be deterministic and bounded.
    CHECK_NEAR(valueNoise(1.25f, 3.5f, 7), valueNoise(1.25f, 3.5f, 7), 1e-9);
    CHECK(valueNoise(1.25f, 3.5f, 7) != valueNoise(1.25f, 3.5f, 8));
    for (int i = 0; i < 200; ++i) {
        float v = fbm(i * 0.37f, i * 0.11f, 4);
        CHECK(v >= 0.0f && v <= 1.0f);
    }
    Random a(42), b(42);
    CHECK(a.nextUInt() == b.nextUInt());
    Random r(7);
    for (int i = 0; i < 100; ++i) {
        CHECK_NEAR(r.onSphere().length(), 1.0f, 1e-4);
        float u = r.unit();
        CHECK(u >= 0.0f && u < 1.0f);
        int n = r.rangeInt(3, 5);
        CHECK(n >= 3 && n <= 5);
    }
}

TEST(math_helpers) {
    CHECK_NEAR(clampf(5.0f, 0.0f, 1.0f), 1.0f, 1e-6);
    CHECK_NEAR(lerpf(0.0f, 10.0f, 0.25f), 2.5f, 1e-6);
    CHECK_NEAR(invLerp(10.0f, 20.0f, 15.0f), 0.5f, 1e-6);
    CHECK_NEAR(smoothStep(0.0f, 1.0f, 0.5f), 0.5f, 1e-6);
    CHECK_NEAR(approach(0.0f, 10.0f, 3.0f), 3.0f, 1e-6);
    CHECK_NEAR(approach(0.0f, 1.0f, 3.0f), 1.0f, 1e-6);   // must not overshoot
    // damp closes most of the gap in one second at speed 5, and the same
    // fraction regardless of how the second is subdivided.
    float oneStep = damp(0.0f, 1.0f, 5.0f, 1.0f);
    float many = 0.0f;
    for (int i = 0; i < 100; ++i) many = damp(many, 1.0f, 5.0f, 0.01f);
    CHECK_NEAR(oneStep, many, 1e-4);
}

int main() { return forge_test::runAll("math"); }
