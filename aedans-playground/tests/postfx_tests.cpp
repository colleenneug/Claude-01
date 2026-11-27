// Tests for the sun's shadows and the HDR post-processing: shadow casting,
// contact (no gap under objects), no acne on lit floors, fading outside the
// map; bloom and its threshold, the Karis average against fireflies; ACES
// tone mapping against a reference; _UNORM vs _SRGB swapchains giving the
// same pixels (sRGB encoded exactly once); resizing without leaks; and the
// sprites-only fast path.
//
// Most scenes are looked at from straight above (orthographic, 4 x 3 world
// units on 320 x 240 pixels: 80 pixels per unit, x right, z down the
// screen), so world (x, 0, z) is pixel (160 + 80x, 120 + 80z).
//
// They need a display and a Vulkan driver; without either the executable
// exits 77 (CTest: skipped). White-box: the renderer's private header.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "VulkanRenderer.h"  // render/src: private
#include "playground/core/Log.h"
#include "playground/engine/Application.h"
#include "playground/engine/Camera.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/geometric.hpp>

using namespace playground;

namespace {

constexpr int kSkipExitCode = 77;

std::unique_ptr<engine::Application> makeApp(u64 maxFrames) {
  auto app = engine::Application::create({
      .window = {.title = "postfx_tests", .width = 320, .height = 240},
      .maxFrames = maxFrames,
  });
  REQUIRE_MESSAGE(app.has_value(), (app ? "" : app.error().message));
  return std::move(*app);
}

render::VulkanRenderer* attachRenderer(engine::Application& app, render::VulkanRendererDesc desc = {}) {
  desc.enableValidation = true;
  auto renderer = render::VulkanRenderer::create(app.platform(), app.window(), desc);
  REQUIRE_MESSAGE(renderer.has_value(), (renderer ? "" : renderer.error().message));
  render::VulkanRenderer* raw = renderer->get();
  app.setRenderer(std::move(*renderer));
  return raw;
}

render::MeshData floorTile() {  // 1 x 1, facing +y
  render::MeshData mesh;
  const glm::vec3 n{0.0f, 1.0f, 0.0f};
  const glm::vec4 t{1.0f, 0.0f, 0.0f, 1.0f};
  mesh.vertices = {{.position = {-0.5f, 0.0f, 0.5f}, .normal = n, .tangent = t, .uv = {0, 1}},
                   {.position = {0.5f, 0.0f, 0.5f}, .normal = n, .tangent = t, .uv = {1, 1}},
                   {.position = {0.5f, 0.0f, -0.5f}, .normal = n, .tangent = t, .uv = {1, 0}},
                   {.position = {-0.5f, 0.0f, -0.5f}, .normal = n, .tangent = t, .uv = {0, 0}}};
  mesh.indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

render::MeshData cube() {  // 1 x 1 x 1, centred on the origin
  render::MeshData mesh;
  const std::array<glm::vec3, 6> normals{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
  for (const glm::vec3 n : normals) {
    const glm::vec3 u = std::fabs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 v = glm::cross(n, u);
    const u32 base = static_cast<u32>(mesh.vertices.size());
    for (const glm::vec2 c : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)}) {
      mesh.vertices.push_back({.position = 0.5f * n + 0.5f * c.x * u + 0.5f * c.y * v, .normal = n,
                               .tangent = glm::vec4(u, 1.0f), .uv = (c + 1.0f) * 0.5f});
    }
    // Counter-clockwise seen from outside: (u, v, n) is right-handed.
    for (const u32 i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
  }
  return mesh;
}

render::MeshHandle createMesh(render::VulkanRenderer& renderer, const render::MeshData& data) {
  auto mesh = renderer.createMesh(data, "test mesh");
  REQUIRE_MESSAGE(mesh.has_value(), (mesh ? "" : mesh.error().message));
  return *mesh;
}

engine::Camera lookingDown() {
  return {.projection = engine::Projection::Orthographic, .position = {0.0f, 10.0f, 0.0f},
          .target = {0.0f, 0.0f, 0.0f}, .up = {0.0f, 0.0f, -1.0f}, .orthoHalfExtent = {2.0f, 1.5f},
          .nearPlane = 0.1f, .farPlane = 100.0f};
}

// The sun only (no environment, a little flat fill so shadows are not black).
void sunLit(render::FrameData& f, glm::vec3 sunDirection) {
  const engine::Camera camera = lookingDown();
  f.viewProjection = camera.viewProjection(f.viewportSize);
  f.cameraPosition = camera.position;
  f.sun = {.direction = sunDirection, .colour = {1.0f, 1.0f, 1.0f}, .intensity = 3.0f};
  f.environmentIntensity = 0.0f;
  f.ambient = glm::vec3(0.05f);
}

const render::Material kGrey{.baseColourFactor = {0.5f, 0.5f, 0.5f, 1.0f}, .metallicFactor = 0.0f,
                             .roughnessFactor = 1.0f};

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

int brightness(const render::CapturedFrame& frame, u32 x, u32 y) {
  const auto p = frame.at(x, y);
  return int{p.r} + int{p.g} + int{p.b};
}

u32 pixelX(f32 worldX) { return static_cast<u32>(160.0f + 80.0f * worldX); }
u32 pixelY(f32 worldZ) { return static_cast<u32>(120.0f + 80.0f * worldZ); }

}  // namespace

// --- Shadows ----------------------------------------------------------------------------

TEST_CASE("a box in the sun casts a shadow onto the floor, from its base outwards; turning shadows off removes it") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const render::MeshHandle box = createMesh(*renderer, cube());
    // The sun comes in at 45 degrees from the -x side: the box's shadow falls
    // towards +x, from its base at x = 0.25 to x = 0.75.
    const glm::vec3 sun = glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f));
    const auto scene = [&](bool shadows) {
      return [&, shadows](render::FrameData& f) {
        sunLit(f, sun);
        f.shadows.enabled = shadows;
        f.meshes.push_back({.mesh = tile, .material = kGrey, .transform = glm::scale(glm::mat4(1.0f), glm::vec3(4.0f))});
        f.meshes.push_back({.mesh = box, .material = kGrey,
                            .transform = glm::translate(glm::mat4(1.0f), {0.0f, 0.25f, 0.0f}) *
                                         glm::scale(glm::mat4(1.0f), glm::vec3(0.5f))});
      };
    };
    const auto withShadows = renderOnce(*app, *renderer, scene(true));
    CHECK(renderer->lastShadowCasters() == 2);
    const int lit = brightness(withShadows, pixelX(-1.0f), pixelY(0.0f));
    const int contact = brightness(withShadows, pixelX(0.32f), pixelY(0.0f));  // just past the base
    const int middle = brightness(withShadows, pixelX(0.55f), pixelY(0.0f));
    const int beyond = brightness(withShadows, pixelX(1.0f), pixelY(0.0f));
    INFO("lit ", lit, "; at the base ", contact, "; mid-shadow ", middle, "; beyond ", beyond);
    CHECK(middle < lit / 3);   // the sun's light is gone; the fill remains
    CHECK(contact < lit / 3);  // no gap between the box and its shadow (no peter-panning)
    CHECK(std::abs(beyond - lit) <= 6);

    const auto without = renderOnce(*app, *renderer, scene(false));
    const int noShadow = brightness(without, pixelX(0.55f), pixelY(0.0f));
    CHECK(std::abs(noShadow - brightness(without, pixelX(-1.0f), pixelY(0.0f))) <= 6);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("no shadow acne: a lit floor with nothing above it is evenly lit with shadows on, at a slant") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const glm::vec3 sun = glm::normalize(glm::vec3(1.0f, -0.6f, 0.4f));  // a low sun: the hard case for acne
    const auto floor = [&](bool shadows) {
      return renderOnce(*app, *renderer, [&, shadows](render::FrameData& f) {
        sunLit(f, sun);
        f.shadows.enabled = shadows;
        f.meshes.push_back({.mesh = tile, .material = kGrey, .transform = glm::scale(glm::mat4(1.0f), glm::vec3(4.0f))});
      });
    };
    const auto on = floor(true);
    const auto off = floor(false);
    int lowest = 1000;
    int highest = 0;
    for (u32 y = 20; y < 220; y += 10) {
      for (u32 x = 20; x < 300; x += 10) {
        const int b = brightness(on, x, y);
        lowest = std::min(lowest, b);
        highest = std::max(highest, b);
        CHECK(std::abs(b - brightness(off, x, y)) <= 3);
      }
    }
    INFO("brightness from ", lowest, " to ", highest);
    CHECK(highest - lowest <= 3);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("what the shadow map does not cover is lit, not black; the matrix frames the given bounds") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const render::MeshHandle box = createMesh(*renderer, cube());
    render::Aabb nearBox;
    nearBox.add({-0.6f, 0.0f, -0.6f});
    nearBox.add({0.6f, 1.0f, 0.6f});
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      sunLit(f, {0.0f, -1.0f, 0.0f});
      f.shadows.enabled = true;
      f.shadows.bounds = nearBox;  // a small map round the box only
      f.meshes.push_back({.mesh = tile, .material = kGrey, .transform = glm::scale(glm::mat4(1.0f), glm::vec3(4.0f))});
      f.meshes.push_back({.mesh = box, .material = kGrey,
                          .transform = glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, 0.0f}) *
                                       glm::scale(glm::mat4(1.0f), glm::vec3(0.4f))});
    });
    // Far outside the map's square: lit like any open floor.
    const int outside = brightness(frame, pixelX(-1.8f), pixelY(-1.3f));
    const int open = brightness(frame, pixelX(-0.5f), pixelY(-0.5f));  // inside the map, nothing above
    INFO("outside the map ", outside, "; open floor inside it ", open);
    CHECK(outside > 100);
    CHECK(std::abs(outside - open) <= 6);
    // The box's centre lands in the middle of the map, inside its depth range.
    const glm::vec4 clip = renderer->lastLightViewProjection() * glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
    CHECK(std::fabs(clip.x / clip.w) < 0.05f);
    CHECK(clip.z / clip.w > 0.0f);
    CHECK(clip.z / clip.w < 1.0f);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

// --- Bloom and tone mapping ------------------------------------------------------------------

namespace {

// A small glowing square in the dark, seen from above; returns the frame.
render::CapturedFrame glowingSquare(engine::Application& app, render::VulkanRenderer& renderer,
                                    render::MeshHandle tile, f32 emission, render::BloomSettings bloom) {
  return renderOnce(app, renderer, [&](render::FrameData& f) {
    sunLit(f, {0.0f, -1.0f, 0.0f});
    f.sun.intensity = 0.0f;
    f.ambient = glm::vec3(0.0f);
    f.bloom = bloom;
    f.meshes.push_back({.mesh = tile,
                        .material = {.baseColourFactor = {0.0f, 0.0f, 0.0f, 1.0f}, .metallicFactor = 0.0f,
                                     .emissiveFactor = glm::vec3(emission)},
                        .transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.25f))});
  });
}

}  // namespace

TEST_CASE("bloom: light above the threshold glows past its edges; below it, or with bloom off, nothing spills") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const u32 haloX = 160 + 10 + 14;  // 14 pixels outside the 20-pixel square's right edge
    const auto off = glowingSquare(*app, *renderer, tile, 20.0f, {.enabled = false});
    const auto on = glowingSquare(*app, *renderer, tile, 20.0f, {.enabled = true, .threshold = 1.0f, .strength = 0.3f});
    const auto above = glowingSquare(*app, *renderer, tile, 20.0f,
                                     {.enabled = true, .threshold = 40.0f, .knee = 1.0f, .strength = 0.3f});
    INFO("halo: bloom off ", brightness(off, haloX, 120), ", on ", brightness(on, haloX, 120),
         ", threshold above the light ", brightness(above, haloX, 120));
    CHECK(brightness(on, 160, 120) == 255 * 3);          // the square itself: far over 1, clipped (no tone map)
    CHECK(brightness(on, haloX, 120) > brightness(off, haloX, 120) + 30);
    CHECK(std::abs(brightness(above, haloX, 120) - brightness(off, haloX, 120)) <= 3);
    CHECK(renderer->post().bloomLevels() >= 3);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("bloom's Karis average: ten times brighter light does not glow anywhere near ten times wider") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const render::BloomSettings bloom{.enabled = true, .threshold = 1.0f, .strength = 0.3f};
    const u32 haloX = 160 + 10 + 20;
    const auto dark = glowingSquare(*app, *renderer, tile, 0.0f, {.enabled = false});
    const auto bright = glowingSquare(*app, *renderer, tile, 100.0f, bloom);
    const auto firefly = glowingSquare(*app, *renderer, tile, 1000.0f, bloom);
    const int base = brightness(dark, haloX, 120);
    const int glow100 = brightness(bright, haloX, 120) - base;
    const int glow1000 = brightness(firefly, haloX, 120) - base;
    INFO("halo above the background: x100 ", glow100, "; x1000 ", glow1000);
    CHECK(glow100 > 0);
    CHECK(glow1000 < glow100 * 3);  // without the Karis average, about ten times
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

namespace {

// Stephen Hill's ACES fit, as tonemap.frag computes it, and sRGB encoding.
f32 acesGrey(f32 v) {
  // A grey stays grey through the matrices up to rounding: each row of the
  // input matrix sums to about 1; the output matrix's rows too.
  const f32 in = v * (0.59719f + 0.35458f + 0.04823f);
  const f32 a = in * (in + 0.0245786f) - 0.000090537f;
  const f32 b = in * (0.983729f * in + 0.4329510f) + 0.238081f;
  return std::clamp((a / b) * (1.60475f - 0.53108f - 0.07367f), 0.0f, 1.0f);
}
int srgbByte(f32 linear) {
  const f32 e = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
  return static_cast<int>(std::lround(e * 255.0f));
}

}  // namespace

TEST_CASE("ACES: greys match the reference curve; bright light never clips; order is kept") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    std::vector<int> outputs;
    for (const f32 level : {0.18f, 1.0f, 4.0f}) {
      const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
        sunLit(f, {0.0f, -1.0f, 0.0f});
        f.sun.intensity = 0.0f;
        f.ambient = glm::vec3(1.0f);  // a non-metal shows its base colour x the fill
        f.exposure = level;
        f.toneMap = render::ToneMap::Aces;
        f.meshes.push_back({.mesh = tile, .material = {.baseColourFactor = glm::vec4(1.0f), .metallicFactor = 0.0f},
                            .transform = glm::scale(glm::mat4(1.0f), glm::vec3(4.0f))});
      });
      const auto p = frame.at(160, 120);
      INFO("level ", level, ": pixel ", int{p.r}, ", reference ", srgbByte(acesGrey(level)));
      CHECK(std::abs(int{p.r} - srgbByte(acesGrey(level))) <= 3);
      CHECK(std::abs(int{p.r} - int{p.g}) <= 2);  // grey in, grey out
      outputs.push_back(p.r);
    }
    CHECK(outputs[0] < outputs[1]);
    CHECK(outputs[1] < outputs[2]);
    CHECK(outputs[2] < 255);  // 4x over white, and still not clipped
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

// --- Swapchain formats, resizing, the fast path ---------------------------------------------

TEST_CASE("a _UNORM swapchain shows exactly what an _SRGB one does: sRGB is encoded once, never twice") {
  gpu::resetValidationCounts();
  // The default pixel view (one unit per pixel, looking along -z): a grey
  // tile turned to face the viewer, a red sprite in front of part of it.
  const auto draw = [](render::MeshHandle tile) {
    return [tile](render::FrameData& f) {
      f.sun.intensity = 0.0f;
      f.environmentIntensity = 0.0f;
      f.ambient = glm::vec3(1.0f);
      f.meshes.push_back({.mesh = tile, .material = kGrey,  // linear 0.5 -> sRGB 188
                          .transform = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), {1.0f, 0.0f, 0.0f}) *
                                       glm::scale(glm::mat4(1.0f), glm::vec3(160.0f))});
      f.sprites.push_back({.position = {60.0f, 0.0f}, .size = {30.0f, 30.0f}, .colour = {0.5f, 0.0f, 0.0f, 1.0f},
                           .depth = 50.0f});
    };
  };
  std::vector<render::CapturedFrame::Pixel> pixels[2];
  VkFormat formats[2]{};
  for (const bool unorm : {false, true}) {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app, {.preferUnormSwapchain = unorm});
    formats[unorm] = renderer->swapchain().format();
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const auto frame = renderOnce(*app, *renderer, draw(tile));
    // Mesh (through the composite), sprite (straight onto the swapchain), background.
    pixels[unorm] = {frame.at(120, 120), frame.at(220, 120), frame.at(5, 5)};
  }
  MESSAGE("swapchain formats: ", gpu::describe(formats[0]), " and ", gpu::describe(formats[1]));
  if (formats[1] != VK_FORMAT_B8G8R8A8_UNORM && formats[1] != VK_FORMAT_R8G8B8A8_UNORM) {
    MESSAGE("this surface offers no _UNORM format; only the _SRGB path was checked");
  }
  CHECK(std::abs(int{pixels[0][0].r} - 188) <= 2);
  for (usize i = 0; i < 3; ++i) {
    INFO("pixel ", i, ": ", int{pixels[0][i].r}, ",", int{pixels[0][i].g}, ",", int{pixels[0][i].b}, " vs ",
         int{pixels[1][i].r}, ",", int{pixels[1][i].g}, ",", int{pixels[1][i].b});
    CHECK(std::abs(int{pixels[0][i].r} - int{pixels[1][i].r}) <= 2);
    CHECK(std::abs(int{pixels[0][i].g} - int{pixels[1][i].g}) <= 2);
    CHECK(std::abs(int{pixels[0][i].b} - int{pixels[1][i].b}) <= 2);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("resizing rebuilds the HDR target and bloom chain at the new size, and leaks nothing") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(/*maxFrames=*/900);  // a safety net
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    engine::Application* raw = app.get();
    const std::array<std::array<u32, 2>, 4> sizes{{{480, 360}, {200, 150}, {640, 200}, {320, 240}}};
    usize step = 0;
    u64 frameAtStep = 0;
    std::vector<u64> allocations;
    bool sizesMatched = true;
    app->setExtract([&](render::FrameData& f, f64) {
      sunLit(f, {0.0f, -1.0f, 0.0f});
      f.bloom.enabled = true;
      f.shadows.enabled = true;
      f.meshes.push_back({.mesh = tile, .material = kGrey});
    });
    app->setUpdate([&](const FrameTime& time, const platform::InputState&) {
      if (step < sizes.size() && time.frameIndex == frameAtStep + 3) {
        raw->window().setSize(sizes[step][0], sizes[step][1]);
      }
      if (step < sizes.size()) {
        const VkExtent2D extent = renderer->swapchain().extent();
        if (extent.width == sizes[step][0] && extent.height == sizes[step][1]) {
          const VkExtent2D hdr = renderer->post().hdr().extent();
          const VkExtent2D bloom = renderer->post().bloom().extent();
          sizesMatched = sizesMatched && hdr.width == extent.width && hdr.height == extent.height &&
                         bloom.width == extent.width / 2 && bloom.height == extent.height / 2;
          allocations.push_back(renderer->context().allocator().stats().allocationCount);
          ++step;
          frameAtStep = time.frameIndex;
        }
      } else {
        raw->requestQuit();
      }
    });
    const Status status = app->run();
    REQUIRE_MESSAGE(status.has_value(), (status ? "" : status.error().message));
    REQUIRE(allocations.size() == sizes.size());
    CHECK(sizesMatched);
    // Same number of live GPU allocations after every resize: nothing left behind.
    for (const u64 count : allocations) CHECK(count == allocations.front());
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("sprites-only frames go straight to the swapchain; a mesh or the sky takes the HDR path") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(1);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    (void)renderOnce(*app, *renderer, [](render::FrameData& f) {
      f.sprites.push_back({.size = {20.0f, 20.0f}});
    });
    CHECK_FALSE(renderer->lastFrameUsedHdr());
    (void)renderOnce(*app, *renderer, [&](render::FrameData& f) { f.meshes.push_back({.mesh = tile}); });
    CHECK(renderer->lastFrameUsedHdr());
    (void)renderOnce(*app, *renderer, [](render::FrameData& f) { f.drawSky = true; });
    CHECK(renderer->lastFrameUsedHdr());
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
      std::printf("skipping postfx_tests: %s\n", probe.error().message.c_str());
      return kSkipExitCode;
    }
    if (!(*probe)->vulkanSupported()) {
      std::printf("skipping postfx_tests: no Vulkan driver on this machine\n");
      return kSkipExitCode;
    }
  }
  doctest::Context context(argc, argv);
  return context.run();
}
