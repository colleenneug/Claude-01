// Tests for 3D meshes and textures in the Vulkan renderer: texture sampling,
// back-face culling, depth against sprites, lighting, normal maps, deferred
// destruction and input checks. Pixel tests read the frame back from the GPU.
//
// They need a display and a Vulkan driver; without either the executable
// exits 77 (CTest: skipped). In the cloud container they run under
// `xvfb-run -a` on Mesa's software driver. White-box: they include the
// renderer's private header.
//
// Most tests light the scene with ambient light only (sun off, ambient 1):
// a non-metal then shows exactly its base colour, so expected pixels are
// simple. The lighting tests turn the sun on and compare brightness.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "VulkanRenderer.h"  // render/src: private
#include "playground/core/JobSystem.h"
#include "playground/core/Log.h"
#include "playground/engine/Application.h"
#include "playground/engine/Assets.h"
#include "playground/engine/Camera.h"
#include "playground/engine/Gltf.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

using namespace playground;

namespace {

constexpr int kSkipExitCode = 77;

std::unique_ptr<engine::Application> makeApp(u64 maxFrames) {
  auto app = engine::Application::create({
      .window = {.title = "mesh_tests", .width = 320, .height = 240},
      .maxFrames = maxFrames,
  });
  REQUIRE_MESSAGE(app.has_value(), (app ? "" : app.error().message));
  return std::move(*app);
}

render::VulkanRenderer* attachRenderer(engine::Application& app) {
  auto renderer = render::VulkanRenderer::create(app.platform(), app.window(), {.enableValidation = true});
  REQUIRE_MESSAGE(renderer.has_value(), (renderer ? "" : renderer.error().message));
  render::VulkanRenderer* raw = renderer->get();
  app.setRenderer(std::move(*renderer));
  return raw;
}

// A square facing +z (the viewer), `half` units from centre to edge, at depth
// z. Counter-clockwise seen from the front; `flipped` reverses the winding
// and the normals, so it faces away (-z) and the viewer sees its back.
// uv (0, 0) is the top-left corner, like an image.
render::MeshData quad(f32 half, f32 z = 0.0f, bool flipped = false) {
  render::MeshData mesh;
  const glm::vec3 n{0.0f, 0.0f, flipped ? -1.0f : 1.0f};
  const glm::vec4 t{1.0f, 0.0f, 0.0f, 1.0f};
  mesh.vertices = {
      {.position = {-half, -half, z}, .normal = n, .tangent = t, .uv = {0.0f, 1.0f}},  // bottom left
      {.position = {half, -half, z}, .normal = n, .tangent = t, .uv = {1.0f, 1.0f}},   // bottom right
      {.position = {half, half, z}, .normal = n, .tangent = t, .uv = {1.0f, 0.0f}},    // top right
      {.position = {-half, half, z}, .normal = n, .tangent = t, .uv = {0.0f, 0.0f}},   // top left
  };
  mesh.indices = flipped ? std::vector<u32>{0, 2, 1, 0, 3, 2} : std::vector<u32>{0, 1, 2, 0, 2, 3};
  return mesh;
}

render::ImageData image(u32 width, u32 height, const std::vector<u8>& rgba) {
  render::ImageData data{.width = width, .height = height};
  for (const u8 byte : rgba) data.rgba.push_back(std::byte{byte});
  return data;
}

// Ambient light only: a non-metal shows exactly its base colour.
void flatLighting(render::FrameData& frame) {
  frame.sun.intensity = 0.0f;
  frame.ambient = {1.0f, 1.0f, 1.0f};
}

// Default viewProjection: one world unit per pixel, (0, 0) at the centre of
// the 320x240 window, so world (x, y) is pixel (160 + x, 120 - y).
render::CapturedFrame renderOnce(engine::Application& app, render::VulkanRenderer& renderer,
                                 std::function<void(render::FrameData&)> fill) {
  app.setExtract([fill = std::move(fill)](render::FrameData& frame, f64) { fill(frame); });
  renderer.requestCapture();
  const Status status = app.run();
  REQUIRE_MESSAGE(status.has_value(), (status ? "" : status.error().message));
  auto frame = renderer.takeCapture();
  REQUIRE_MESSAGE(frame.has_value(), (frame ? "" : frame.error().message));
  return std::move(*frame);
}

bool near(u8 actual, u8 expected, int tolerance = 3) { return std::abs(int{actual} - int{expected}) <= tolerance; }

void checkColour(const render::CapturedFrame& frame, u32 x, u32 y, u8 r, u8 g, u8 b) {
  const auto p = frame.at(x, y);
  INFO("pixel (", x, ", ", y, ") is ", int(p.r), ",", int(p.g), ",", int(p.b), "; expected ", int(r), ",", int(g),
       ",", int(b));
  CHECK(near(p.r, r));
  CHECK(near(p.g, g));
  CHECK(near(p.b, b));
}

bool isBackground(const render::CapturedFrame& frame, u32 x, u32 y) {
  const auto p = frame.at(x, y);
  return p.r < 40 && p.g < 40 && p.b < 60;  // the default clear colour, a very dark blue
}

int brightness(const render::CapturedFrame& frame, u32 x, u32 y) {
  const auto p = frame.at(x, y);
  return int{p.r} + int{p.g} + int{p.b};
}

render::MeshHandle createMesh(render::VulkanRenderer& renderer, const render::MeshData& data) {
  auto mesh = renderer.createMesh(data, "test mesh");
  REQUIRE_MESSAGE(mesh.has_value(), (mesh ? "" : mesh.error().message));
  return *mesh;
}

render::TextureHandle createTexture(render::VulkanRenderer& renderer, const render::ImageData& data,
                                    render::TextureUsage usage) {
  auto texture = renderer.createTexture(data, usage, "test texture");
  REQUIRE_MESSAGE(texture.has_value(), (texture ? "" : texture.error().message));
  return *texture;
}

// A non-metal: the default metallic factor is 1 (glTF's), which would make
// ambient light show the reflected colour instead of the base colour.
render::Material plastic(glm::vec4 colour = glm::vec4(1.0f)) {
  return {.baseColourFactor = colour, .metallicFactor = 0.0f, .roughnessFactor = 1.0f};
}

}  // namespace

TEST_CASE("a textured mesh samples its texture: each quadrant shows its own texel") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle mesh = createMesh(*renderer, quad(80.0f));
    // 4x4 in four 2x2 blocks, top row first: red, green / blue, light grey.
    // Blocks, not single texels: bilinear filtering at a pixel centre then
    // only mixes texels of one colour. As Colour (sRGB), so the stored bytes
    // come back out of the sRGB swapchain unchanged.
    std::vector<u8> quadrants;
    for (u32 y = 0; y < 4; ++y) {
      for (u32 x = 0; x < 4; ++x) {
        const bool right = x >= 2;
        const bool bottom = y >= 2;
        const std::vector<u8> colour = !bottom ? (right ? std::vector<u8>{40, 200, 40} : std::vector<u8>{200, 40, 40})
                                               : (right ? std::vector<u8>{220, 220, 220} : std::vector<u8>{40, 40, 200});
        quadrants.insert(quadrants.end(), colour.begin(), colour.end());
        quadrants.push_back(255);
      }
    }
    const render::TextureHandle texture =
        createTexture(*renderer, image(4, 4, quadrants), render::TextureUsage::Colour);
    render::Material material = plastic();
    material.baseColour = texture;

    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      flatLighting(f);
      f.meshes.push_back({.mesh = mesh, .material = material});
    });
    CHECK(renderer->lastMeshDrawCount() == 1);
    // The quad covers pixels 80..240 x 40..200; quadrant centres sit on texel centres.
    checkColour(frame, 120, 80, 200, 40, 40);    // top left
    checkColour(frame, 200, 80, 40, 200, 40);    // top right
    checkColour(frame, 120, 160, 40, 40, 200);   // bottom left
    checkColour(frame, 200, 160, 220, 220, 220); // bottom right
    CHECK(isBackground(frame, 20, 20));
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("base colour factor tints the texture; a mesh with no texture shows the factor") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle mesh = createMesh(*renderer, quad(40.0f));
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      flatLighting(f);
      // Linear 0.5 is sRGB 188; no texture means the white default.
      f.meshes.push_back({.mesh = mesh, .material = plastic({0.5f, 0.0f, 1.0f, 1.0f}),
                          .transform = glm::translate(glm::mat4(1.0f), {-80.0f, 0.0f, 0.0f})});
      f.meshes.push_back({.mesh = mesh, .material = plastic({0.0f, 1.0f, 0.0f, 1.0f}),
                          .transform = glm::translate(glm::mat4(1.0f), {80.0f, 0.0f, 0.0f})});
    });
    checkColour(frame, 80, 120, 188, 0, 255);
    checkColour(frame, 240, 120, 0, 255, 0);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("back faces are culled: a triangle wound clockwise towards the viewer is not drawn") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle front = createMesh(*renderer, quad(40.0f));
    const render::MeshHandle back = createMesh(*renderer, quad(40.0f, 0.0f, /*flipped=*/true));
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      flatLighting(f);
      f.meshes.push_back({.mesh = front, .material = plastic(),
                          .transform = glm::translate(glm::mat4(1.0f), {-80.0f, 0.0f, 0.0f})});
      f.meshes.push_back({.mesh = back, .material = plastic(),
                          .transform = glm::translate(glm::mat4(1.0f), {80.0f, 0.0f, 0.0f})});
    });
    checkColour(frame, 80, 120, 255, 255, 255);  // counter-clockwise: the front
    CHECK(isBackground(frame, 240, 120));         // clockwise: its back, culled
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("a double-sided material shows its back face, lit as facing the viewer") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle back = createMesh(*renderer, quad(40.0f, 0.0f, /*flipped=*/true));
    render::Material twoSided = plastic({0.5f, 0.5f, 0.5f, 1.0f});
    twoSided.doubleSided = true;
    render::Material oneSided = plastic({0.5f, 0.5f, 0.5f, 1.0f});
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      f.sun = {.direction = {0.0f, 0.0f, -1.0f}, .colour = {1.0f, 1.0f, 1.0f}, .intensity = 2.0f};
      f.ambient = glm::vec3(0.0f);
      // Given double-sided first and single-sided last: the renderer regroups them.
      f.meshes.push_back({.mesh = back, .material = twoSided,
                          .transform = glm::translate(glm::mat4(1.0f), {-80.0f, 0.0f, 0.0f})});
      f.meshes.push_back({.mesh = back, .material = oneSided,
                          .transform = glm::translate(glm::mat4(1.0f), {80.0f, 0.0f, 0.0f})});
    });
    // Its back faces the viewer and the sun, and its normal is flipped to
    // match: lit, not black.
    CHECK(brightness(frame, 80, 120) > 150);
    CHECK(isBackground(frame, 240, 120));
    CHECK(renderer->lastMeshDrawCount() == 2);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("meshes and sprites share the depth buffer: the nearer one is in front whatever the order") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle mesh = createMesh(*renderer, quad(40.0f));
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      flatLighting(f);
      // Left: a red mesh at z 10 hides a green sprite at z 0 (sprites draw after meshes).
      f.meshes.push_back({.mesh = mesh, .material = plastic({1, 0, 0, 1}),
                          .transform = glm::translate(glm::mat4(1.0f), {-80.0f, 0.0f, 10.0f})});
      f.sprites.push_back({.position = {-80.0f, 0.0f}, .size = {80.0f, 80.0f}, .colour = {0, 1, 0, 1}, .depth = 0.0f});
      // Right: a green sprite at z 20 covers a red mesh at z 10.
      f.meshes.push_back({.mesh = mesh, .material = plastic({1, 0, 0, 1}),
                          .transform = glm::translate(glm::mat4(1.0f), {80.0f, 0.0f, 10.0f})});
      f.sprites.push_back({.position = {80.0f, 0.0f}, .size = {80.0f, 80.0f}, .colour = {0, 1, 0, 1}, .depth = 20.0f});
      // Middle: two meshes, the farther (blue, z 5) given after the nearer (white, z 15).
      f.meshes.push_back({.mesh = mesh, .material = plastic(),
                          .transform = glm::translate(glm::mat4(1.0f), {0.0f, 80.0f, 15.0f}) *
                                       glm::scale(glm::mat4(1.0f), {0.5f, 0.5f, 1.0f})});
      f.meshes.push_back({.mesh = mesh, .material = plastic({0, 0, 1, 1}),
                          .transform = glm::translate(glm::mat4(1.0f), {0.0f, 80.0f, 5.0f})});
    });
    checkColour(frame, 80, 120, 255, 0, 0);
    checkColour(frame, 240, 120, 0, 255, 0);
    checkColour(frame, 160, 40, 255, 255, 255);  // centre of the near, smaller white quad
    checkColour(frame, 130, 40, 0, 0, 255);      // outside it: the far blue one
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("the sun lights a surface turned towards it more than one turned away") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle mesh = createMesh(*renderer, quad(40.0f));
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      f.sun = {.direction = {0.0f, 0.0f, -1.0f}, .colour = {1.0f, 1.0f, 1.0f}, .intensity = 2.0f};  // shining into the screen
      f.ambient = glm::vec3(0.0f);
      f.meshes.push_back({.mesh = mesh, .material = plastic({0.5f, 0.5f, 0.5f, 1.0f}),
                          .transform = glm::translate(glm::mat4(1.0f), {-80.0f, 0.0f, 0.0f})});
      // Turned 70 degrees about y: the light hits it at a glancing angle.
      f.meshes.push_back({.mesh = mesh, .material = plastic({0.5f, 0.5f, 0.5f, 1.0f}),
                          .transform = glm::translate(glm::mat4(1.0f), {80.0f, 0.0f, 0.0f}) *
                                       glm::rotate(glm::mat4(1.0f), glm::radians(70.0f), {0.0f, 1.0f, 0.0f})});
    });
    const int facing = brightness(frame, 80, 120);
    const int glancing = brightness(frame, 240, 120);
    INFO("facing the sun: ", facing, "; at 70 degrees: ", glancing);
    CHECK(facing > 150);
    CHECK(glancing > 0);
    CHECK(facing > glancing + 100);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("a normal map bends the lighting; a flat one changes nothing") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle mesh = createMesh(*renderer, quad(40.0f));
    // Data textures: the bytes are directions, not colours.
    const render::TextureHandle flat =
        createTexture(*renderer, image(1, 1, {128, 128, 255, 255}), render::TextureUsage::Data);
    // About 60 degrees off the surface normal, along the tangent (+x).
    const render::TextureHandle tilted =
        createTexture(*renderer, image(1, 1, {238, 128, 191, 255}), render::TextureUsage::Data);
    render::Material withFlat = plastic({0.5f, 0.5f, 0.5f, 1.0f});
    withFlat.normal = flat;
    render::Material withTilted = withFlat;
    withTilted.normal = tilted;

    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      f.sun = {.direction = {0.0f, 0.0f, -1.0f}, .colour = {1.0f, 1.0f, 1.0f}, .intensity = 2.0f};
      f.ambient = glm::vec3(0.0f);
      f.meshes.push_back({.mesh = mesh, .material = plastic({0.5f, 0.5f, 0.5f, 1.0f}),
                          .transform = glm::translate(glm::mat4(1.0f), {-100.0f, 0.0f, 0.0f})});
      f.meshes.push_back({.mesh = mesh, .material = withFlat});
      f.meshes.push_back({.mesh = mesh, .material = withTilted,
                          .transform = glm::translate(glm::mat4(1.0f), {100.0f, 0.0f, 0.0f})});
    });
    const int none = brightness(frame, 60, 120);
    const int flatMap = brightness(frame, 160, 120);
    const int tiltedMap = brightness(frame, 260, 120);
    INFO("no normal map: ", none, "; flat map: ", flatMap, "; tilted map: ", tiltedMap);
    CHECK(std::abs(none - flatMap) <= 6);
    CHECK(tiltedMap + 60 < flatMap);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("destroyed meshes and textures go stale at once and are freed after the frames using them") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(4);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle mesh = createMesh(*renderer, quad(40.0f));
    const render::MeshHandle doomed = createMesh(*renderer, quad(40.0f));
    const render::TextureHandle red =
        createTexture(*renderer, image(1, 1, {255, 0, 0, 255}), render::TextureUsage::Colour);
    CHECK(renderer->meshCount() == 2);
    CHECK(renderer->textureCount() == 1);

    render::Material material = plastic();
    material.baseColour = red;
    u64 frameNumber = 0;
    app->setExtract([&](render::FrameData& f, f64) {
      flatLighting(f);
      f.meshes.push_back({.mesh = mesh, .material = material,
                          .transform = glm::translate(glm::mat4(1.0f), {-80.0f, 0.0f, 0.0f})});
      f.meshes.push_back({.mesh = doomed, .material = plastic(),
                          .transform = glm::translate(glm::mat4(1.0f), {80.0f, 0.0f, 0.0f})});
      if (frameNumber == 1) {
        // Destroyed while frame 0 may still be on the GPU, and still listed in this frame.
        renderer->destroyMesh(doomed);
        renderer->destroyTexture(red);
        CHECK(renderer->meshCount() == 1);
        CHECK(renderer->retiredResourceCount() == 2);  // waiting, not freed
        renderer->requestCapture();
      }
      ++frameNumber;
    });
    REQUIRE(app->run().has_value());
    CHECK(renderer->retiredResourceCount() == 0);  // freed once the frames that used them finished
    CHECK(renderer->textureCount() == 0);

    auto frame = renderer->takeCapture();
    REQUIRE(frame.has_value());
    checkColour(*frame, 80, 120, 255, 255, 255);  // the stale texture reads as the white default
    CHECK(isBackground(*frame, 240, 120));        // the stale mesh is not drawn
    CHECK(renderer->lastMeshDrawCount() == 1);

    renderer->destroyMesh(doomed);  // again: a stale handle is ignored
    renderer->destroyTexture({});   // an invalid one too
    renderer->destroyTexture({.index = render::kWhiteTexture, .generation = 0});  // the defaults stay
    CHECK(renderer->retiredResourceCount() == 0);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("bad mesh data is rejected with a reason, before it reaches the GPU") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(1);
    render::VulkanRenderer* renderer = attachRenderer(*app);

    render::MeshData outOfRange = quad(10.0f);
    outOfRange.indices[4] = 9;
    auto bad = renderer->createMesh(outOfRange, "out of range");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == Errc::InvalidArgument);
    CHECK(bad.error().message.find("index 9 points past its last vertex (3)") != std::string::npos);

    render::MeshData partial = quad(10.0f);
    partial.indices.pop_back();
    CHECK_FALSE(renderer->createMesh(partial, "partial").has_value());
    CHECK_FALSE(renderer->createMesh({}, "empty").has_value());

    auto badImage = renderer->createTexture(image(2, 2, {1, 2, 3, 4}), render::TextureUsage::Colour, "short");
    REQUIRE_FALSE(badImage.has_value());
    CHECK(badImage.error().code == Errc::InvalidArgument);
    CHECK(renderer->meshCount() == 0);
    CHECK(renderer->textureCount() == 0);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("a thousand mesh draws in one frame grow the draw list, with no validation errors") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle a = createMesh(*renderer, quad(2.0f));
    const render::MeshHandle b = createMesh(*renderer, quad(3.0f));
    app->setExtract([&](render::FrameData& f, f64) {
      for (int i = 0; i < 1000; ++i) {
        const glm::vec3 at{static_cast<f32>(i % 40) * 8.0f - 156.0f, static_cast<f32>(i / 40) * 8.0f - 100.0f, 0.0f};
        f.meshes.push_back({.mesh = i % 2 == 0 ? a : b, .material = plastic(),
                            .transform = glm::translate(glm::mat4(1.0f), at)});
      }
    });
    REQUIRE(app->run().has_value());
    CHECK(renderer->lastMeshDrawCount() == 1000);
    CHECK(renderer->lastDrawCalls() == 1000);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("a glTF model goes file -> worker decode -> GPU upload -> pixels, and clear() frees it") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    JobSystem jobs(2);
    engine::AssetManager assets(jobs, PLAYGROUND_DEMO_ASSETS);
    assets.registerType<engine::Model>(engine::gltfLoader(*renderer));

    const auto crate = assets.load<engine::Model>("models/crate.gltf");
    while (!assets.idle()) {
      (void)assets.update();
      std::this_thread::yield();
    }
    REQUIRE_MESSAGE(assets.state(crate) == engine::AssetState::Ready,
                    (assets.error(crate) ? assets.error(crate)->message : std::string("not ready")));
    const engine::Model* model = assets.get(crate);
    REQUIRE(model != nullptr);
    CHECK(renderer->meshCount() == 1);
    CHECK(renderer->textureCount() == 3);  // base colour, normal, metallic-roughness

    // Look straight at the crate's front face (+z), close enough to fill the middle.
    engine::Camera camera{.projection = engine::Projection::Perspective, .position = {0.0f, 0.0f, 2.2f},
                          .target = {0.0f, 0.0f, 0.0f}, .nearPlane = 0.05f, .farPlane = 100.0f};
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      flatLighting(f);
      f.viewProjection = camera.viewProjection(f.viewportSize);
      f.cameraPosition = camera.position;
      model->extract(glm::mat4(1.0f), f);
    });
    // The middle of the face is wooden plank: reddish brown, r > g > b.
    const auto wood = frame.at(160, 130);
    INFO("centre pixel ", int(wood.r), ",", int(wood.g), ",", int(wood.b));
    CHECK(wood.r > wood.g);
    CHECK(wood.g > wood.b);
    CHECK(wood.r > 80);
    CHECK(isBackground(frame, 5, 5));  // the corner is outside the crate
    CHECK(renderer->lastMeshDrawCount() == 1);

    assets.clear();  // before the renderer goes, as the app does
    CHECK(renderer->meshCount() == 0);
    CHECK(renderer->textureCount() == 0);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

int main(int argc, char** argv) {
  auto session = log::init({.filePath = {}, .level = log::Level::Warn});
  if (!session) {
    std::fprintf(stderr, "could not start logging: %s\n", session.error().message.c_str());
    return 1;
  }
  {
    auto probe = platform::Platform::create();
    if (!probe) {
      std::printf("skipping mesh_tests: %s\n", probe.error().message.c_str());
      return kSkipExitCode;
    }
    if (!(*probe)->vulkanSupported()) {
      std::printf("skipping mesh_tests: no Vulkan driver on this machine\n");
      return kSkipExitCode;
    }
  }
  doctest::Context context(argc, argv);
  return context.run();
}
