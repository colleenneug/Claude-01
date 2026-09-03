#include "Test.hpp"

#include "forge/assets/AssetLibrary.hpp"
#include "forge/core/Log.hpp"
#include "forge/gameplay/Actors.hpp"
#include "forge/gameplay/GameFramework.hpp"
#include "forge/render/SoftwareRenderer.hpp"
#include "forge/scene/World.hpp"

using namespace forge;

namespace {

RenderView lookFrom(const Vec3& eye, const Vec3& at, const Framebuffer& fb, float fov = 60.0f) {
    RenderView v;
    v.view = Mat4::lookAt(eye, at, Vec3::Up);
    v.projection = Mat4::perspective(fov, fb.aspect(), 0.1f, 300.0f);
    v.cameraPosition = eye;
    return v;
}

// A scene with one thing in it, which is what most of these tests want.
struct OneBox {
    AssetLibrary assets;
    World world{false};
    StaticMeshActor* box = nullptr;

    OneBox() {
        assets.createStarterContent();
        world.setAssets(&assets);
        box = world.spawn<StaticMeshActor>("Box", Vec3::Zero);
        box->rerunConstruction();
        world.settings().fogEnabled = false;
        world.settings().vignette = 0.0f;
    }
};

} // namespace

TEST(mesh_generators_are_well_formed) {
    ShapeParams p;
    for (const char* name : Mesh::allShapeNames()) {
        auto mesh = Mesh::makeShape(name, Mesh::shapeFromName(name), p);
        CHECK(mesh != nullptr);
        if (!mesh) continue;
        // Every generator must produce whole triangles, valid indices,
        // unit-length normals and real bounds.
        CHECK(!mesh->vertices.empty());
        CHECK(!mesh->indices.empty());
        CHECK(mesh->indices.size() % 3 == 0);
        for (uint32_t i : mesh->indices) CHECK(i < mesh->vertices.size());
        CHECK(mesh->bounds.valid());
        // Not volume: a plane and a ring are legitimately flat. What
        // matters is that the shape has real extent somewhere.
        CHECK(mesh->bounds.size().length() > 0.01f);
        for (const Vertex& v : mesh->vertices) CHECK_NEAR(v.normal.length(), 1.0f, 1e-3);
    }
}

TEST(mesh_bounds_match_parameters) {
    auto box = Mesh::makeBox({2, 4, 6});
    CHECK_VEC(box->bounds.min, (Vec3{-1, -2, -3}), 1e-4);
    CHECK_VEC(box->bounds.max, (Vec3{1, 2, 3}), 1e-4);

    auto sphere = Mesh::makeSphere(2.0f, 16, 12);
    CHECK_NEAR(sphere->bounds.max.x, 2.0f, 1e-3);
    CHECK_NEAR(sphere->bounds.min.y, -2.0f, 1e-3);

    // A capsule's total height includes its caps.
    auto capsule = Mesh::makeCapsule(0.5f, 3.0f, 12, 6);
    CHECK_NEAR(capsule->bounds.max.y - capsule->bounds.min.y, 3.0f, 1e-2);
}

TEST(mesh_raycast_against_triangles) {
    auto box = Mesh::makeBox({2, 2, 2});
    float t; Vec3 n;
    CHECK(box->raycastLocal(Ray{{0, 0, 5}, {0, 0, -1}}, 100.0f, t, n));
    CHECK_NEAR(t, 4.0f, 1e-3);
    CHECK(!box->raycastLocal(Ray{{0, 10, 5}, {0, 0, -1}}, 100.0f, t, n));
}

TEST(mesh_json_round_trip) {
    ShapeParams p;
    p.radius = 1.25f;
    p.segments = 20;
    auto original = Mesh::makeShape("Ball", ShapeKind::Sphere, p);
    std::string err;
    Json parsed = Json::parse(original->toJson().dump(), &err);
    CHECK(err.empty());
    auto back = Mesh::fromJson(parsed);
    CHECK(back->name == "Ball");
    CHECK(back->kind == ShapeKind::Sphere);
    CHECK_NEAR(back->params.radius, 1.25f, 1e-5);
    // Geometry is regenerated from the parameters rather than stored.
    CHECK(back->vertices.size() == original->vertices.size());
}

TEST(texture_generation) {
    Texture t;
    t.kind = TextureKind::Checker;
    t.width = t.height = 64;
    t.scale = 8.0f;
    t.colorA = Color::fromHex("#000000");
    t.colorB = Color::fromHex("#ffffff");
    t.rebuild();
    CHECK(t.pixels.size() == 64 * 64);
    // Adjacent cells must actually differ, or the pattern is not there.
    // At 64 pixels and 8 cells a cell is 8 pixels, so the samples have to
    // be at least that far apart to land in different squares.
    CHECK(t.texel(2, 2).r != t.texel(10, 2).r);
    CHECK(t.texel(2, 2).r == t.texel(18, 2).r);   // two cells over, same again
    // Sampling wraps rather than clamping, so tiling works.
    CHECK_NEAR(t.sample(0.1f, 0.1f).r, t.sample(1.1f, 1.1f).r, 1e-4);

    // A grid at a fine scale must still show lines rather than vanishing
    // into sub-pixel widths.
    Texture grid;
    grid.kind = TextureKind::Grid;
    grid.width = grid.height = 256;
    grid.scale = 16.0f;
    grid.colorA = Color::fromHex("#000000");
    grid.colorB = Color::fromHex("#ffffff");
    grid.rebuild();
    float brightest = 0.0f;
    for (const Color& c : grid.pixels) brightest = std::max(brightest, c.r);
    CHECK(brightest > 0.9f);
}

TEST(renders_something_rather_than_nothing) {
    OneBox s;
    Framebuffer fb(160, 120);
    RenderView view = lookFrom({0, 0, 5}, Vec3::Zero, fb);
    SoftwareRenderer r;
    r.render(fb, RenderScene::collect(s.world), view);

    CHECK(r.stats().trianglesSubmitted == 12);   // a cube
    CHECK(r.stats().trianglesDrawn > 0);
    CHECK(r.stats().pixelsShaded > 0);

    // The box is at the centre; the corners are sky.
    const Color centre = fb.pixel(80, 60);
    const Color corner = fb.pixel(2, 2);
    CHECK(centre.r != corner.r || centre.g != corner.g || centre.b != corner.b);
    CHECK(fb.averageLuminance() > 0.01f);
}

TEST(depth_buffer_orders_by_distance) {
    OneBox s;
    // A second box, nearer and unmistakably red, directly in front.
    auto* near = s.world.spawn<StaticMeshActor>("Near", {0, 0, 2});
    auto mat = std::make_unique<Material>();
    mat->name = "PureRed";
    mat->baseColor = Color::fromHex("#ff0000");
    mat->roughness = 1.0f;
    s.assets.addMaterial(std::move(mat));
    near->material = "PureRed";
    near->rerunConstruction();

    Framebuffer fb(160, 120);
    SoftwareRenderer r;
    r.render(fb, RenderScene::collect(s.world), lookFrom({0, 0, 8}, Vec3::Zero, fb));

    const Color centre = fb.pixel(80, 60);
    // The nearer box wins the depth test, so the centre reads red.
    CHECK(centre.r > centre.g * 1.5f);
    CHECK(centre.r > centre.b * 1.5f);

    // ...and the far box is still there when the near one is removed.
    near->destroy();
    r.render(fb, RenderScene::collect(s.world), lookFrom({0, 0, 8}, Vec3::Zero, fb));
    const Color without = fb.pixel(80, 60);
    CHECK(!(without.r > without.g * 1.5f));
}

TEST(frustum_culling_skips_offscreen_items) {
    OneBox s;
    for (int i = 0; i < 12; ++i) {
        auto* a = s.world.spawn<StaticMeshActor>("Far", {(float)i * 30.0f - 400.0f, 0, 0});
        a->rerunConstruction();
    }
    Framebuffer fb(160, 120);
    SoftwareRenderer r;
    r.render(fb, RenderScene::collect(s.world), lookFrom({0, 0, 5}, Vec3::Zero, fb, 40.0f));
    // Most of that row is well outside a 40-degree view.
    CHECK(r.stats().itemsCulled > 6);
}

TEST(shadows_darken_what_is_behind_a_caster) {
    AssetLibrary assets;
    assets.createStarterContent();
    World w(false);
    w.setAssets(&assets);
    w.settings().fogEnabled = false;
    w.settings().vignette = 0.0f;
    w.settings().ambientIntensity = 0.15f;   // so the sun dominates
    // Sun straight overhead, so a caster's shadow lands directly beneath.
    w.settings().sunPitch = 88.0f;
    w.settings().sunYaw = 0.0f;

    auto* floor = w.spawn<StaticMeshActor>("Floor", {0, -1, 0});
    floor->setScale({30, 0.5f, 30});
    floor->rerunConstruction();

    Framebuffer fb(200, 200);
    RenderView view = lookFrom({0, 12, 0.01f}, Vec3::Zero, fb);
    SoftwareRenderer r;

    r.render(fb, RenderScene::collect(w), view);
    const float litCentre = fb.pixel(100, 100).g;

    auto* caster = w.spawn<StaticMeshActor>("Caster", {0, 3, 0});
    caster->setScale({4, 0.4f, 4});
    caster->rerunConstruction();

    // Look past the caster from the side so the floor under it is visible.
    RenderView side = lookFrom({14, 6, 0.01f}, Vec3::Zero, fb);
    r.render(fb, RenderScene::collect(w), side);

    // Sample the floor beneath the caster and well away from it, and
    // compare: the shadowed patch must be measurably darker.
    Vec3 shadowed, open;
    bool gotShadowed = false, gotOpen = false;
    float shadowLuma = 0.0f, openLuma = 0.0f;
    for (int x = 0; x < fb.width(); ++x) {
        for (int y = fb.height() / 2; y < fb.height(); ++y) {
            Vec3 world;
            if (!r.worldPositionAt(x, y, side, world)) continue;
            if (world.y > -0.4f) continue;             // floor only
            const float d = std::sqrt(world.x * world.x + world.z * world.z);
            const Color c = fb.pixel(x, y);
            const float luma = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            if (d < 1.2f && !gotShadowed) { shadowed = world; shadowLuma = luma; gotShadowed = true; }
            if (d > 8.0f && d < 12.0f && !gotOpen) { open = world; openLuma = luma; gotOpen = true; }
        }
    }
    CHECK(gotShadowed);
    CHECK(gotOpen);
    (void)litCentre; (void)shadowed; (void)open;
    if (gotShadowed && gotOpen) CHECK(shadowLuma < openLuma * 0.75f);
}

TEST(metals_are_not_black) {
    AssetLibrary assets;
    assets.createStarterContent();
    World w(false);
    w.setAssets(&assets);
    w.settings().fogEnabled = false;
    w.settings().vignette = 0.0f;

    auto* ball = w.spawn<StaticMeshActor>("Ball", Vec3::Zero);
    ball->mesh = "Sphere";
    ball->material = "Gold";     // metallic 1.0, which has no diffuse at all
    ball->rerunConstruction();

    Framebuffer fb(160, 160);
    SoftwareRenderer r;
    r.render(fb, RenderScene::collect(w), lookFrom({0, 0, 4}, Vec3::Zero, fb));

    // A metal reflects its surroundings. Without an environment term it
    // renders black, which is the bug this pins down.
    const Color centre = fb.pixel(80, 80);
    const float luma = 0.2126f * centre.r + 0.7152f * centre.g + 0.0722f * centre.b;
    CHECK(luma > 0.05f);
}

TEST(picking_recovers_world_position) {
    OneBox s;
    Framebuffer fb(200, 200);
    RenderView view = lookFrom({0, 0, 6}, Vec3::Zero, fb);
    SoftwareRenderer r;
    r.render(fb, RenderScene::collect(s.world), view);

    Vec3 world;
    CHECK(r.worldPositionAt(100, 100, view, world));
    // The centre pixel is the near face of a unit cube at the origin.
    CHECK_NEAR(world.z, 0.5f, 0.05);
    CHECK_NEAR(world.x, 0.0f, 0.05);
    CHECK_NEAR(world.y, 0.0f, 0.05);

    // A pixel of empty sky has no world position.
    CHECK(!r.worldPositionAt(2, 2, view, world));
}

TEST(framebuffer_and_png) {
    Framebuffer fb(8, 4);
    fb.clearColor(Color::fromHex("#204060"));
    CHECK(fb.width() == 8);
    CHECK(fb.height() == 4);
    CHECK_NEAR(fb.aspect(), 2.0f, 1e-5);
    CHECK(fb.pixel(0, 0).b > fb.pixel(0, 0).r);
    // Out of bounds reads and writes are safe.
    CHECK(fb.pixel(-1, 0).a == 0.0f);
    fb.setPixel(100, 100, Color{1, 1, 1, 1});

    fb.blend(0, 0, Color{1, 1, 1, 1}, 0.5f);
    CHECK(fb.pixel(0, 0).r > 0.4f);

    const auto rgba = fb.toRGBA8();
    CHECK(rgba.size() == 8 * 4 * 4);

    const std::string path = "/tmp/forge_test_image.png";
    CHECK(fb.savePNG(path));
    FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        unsigned char header[8] = {};
        CHECK(std::fread(header, 1, 8, f) == 8);
        std::fclose(f);
        // A real PNG signature, so the file is not merely non-empty.
        CHECK(header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G');
        std::remove(path.c_str());
    }
}

TEST(scene_collection_respects_visibility) {
    OneBox s;
    CHECK(RenderScene::collect(s.world).items.size() == 1);

    // Hidden-in-game actors are editor aids: drawn in the editor, absent
    // from play.
    auto* start = s.world.spawn<PlayerStart>("Start");
    CHECK(RenderScene::collect(s.world, false).items.size() == 1);
    CHECK(RenderScene::collect(s.world, true).items.size() == 2);
    (void)start;

    // Hiding a component removes it from the frame entirely.
    s.box->meshComponent()->visible = false;
    CHECK(RenderScene::collect(s.world, true).items.size() == 1);
    s.box->meshComponent()->visible = true;

    // Lights are gathered separately from geometry.
    auto* lamp = s.world.spawn<PointLightActor>("Lamp", {0, 3, 0});
    lamp->rerunConstruction();
    RenderScene scene = RenderScene::collect(s.world, true);
    CHECK(scene.lights.size() == 1);
    CHECK(scene.lights[0].kind == LightKind::Point);
    CHECK(scene.hasSun);
}

int main() {
    Log::get().setEchoToConsole(false);
    return forge_test::runAll("render");
}
