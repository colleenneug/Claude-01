// Tests for image-based lighting and the sky: the environment bake, matte
// and mirror surfaces lit by it, the sky pass, tone mapping, and bad input.
//
// Most tests use a made-up HDR panorama (say, red above the horizon and blue
// below), so the colour each surface should take is easy to predict. A
// surface facing up sees only the upper half; a mirror facing up reflects
// straight up.
//
// They need a display and a Vulkan driver; without either the executable
// exits 77 (CTest: skipped). White-box: the renderer's private header.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "VulkanRenderer.h"  // render/src: private
#include "playground/core/Log.h"
#include "playground/engine/Application.h"
#include "playground/engine/Camera.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

using namespace playground;

namespace {

constexpr int kSkipExitCode = 77;

std::unique_ptr<engine::Application> makeApp(u64 maxFrames) {
  auto app = engine::Application::create({
      .window = {.title = "environment_tests", .width = 320, .height = 240},
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

// A panorama filled by colourAt(direction): the inverse of the bake's
// mapping (middle column looking along -z, top row straight up).
render::HdrImage panorama(u32 width, u32 height, const std::function<glm::vec3(glm::vec3)>& colourAt) {
  render::HdrImage image{.width = width, .height = height};
  image.rgba.reserve(usize{width} * height * 4);
  for (u32 y = 0; y < height; ++y) {
    const f32 theta = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height) * glm::pi<f32>();
    for (u32 x = 0; x < width; ++x) {
      const f32 phi = ((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width) - 0.5f) * 2.0f * glm::pi<f32>();
      const glm::vec3 d{std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi)};
      const glm::vec3 c = colourAt(d);
      image.rgba.insert(image.rgba.end(), {c.r, c.g, c.b, 1.0f});
    }
  }
  return image;
}

// Upper half `above`, lower half `below`.
render::HdrImage split(glm::vec3 above, glm::vec3 below) {
  return panorama(64, 32, [=](glm::vec3 d) { return d.y >= 0.0f ? above : below; });
}

// Good enough for these tests, and quick on a CPU driver.
constexpr render::EnvironmentQuality kTestQuality{.cubeSize = 64, .specularSize = 32, .irradianceSize = 8,
                                                  .samples = 64};

// A 1 x 1 square lying flat (facing +y), counter-clockwise from above.
render::MeshData floorTile() {
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

// Looking straight down at the origin from above, orthographic, 4 x 3 units visible.
engine::Camera lookingDown() {
  return {.projection = engine::Projection::Orthographic, .position = {0.0f, 10.0f, 0.0f},
          .target = {0.0f, 0.0f, 0.0f}, .up = {0.0f, 0.0f, -1.0f}, .orthoHalfExtent = {2.0f, 1.5f},
          .nearPlane = 0.1f, .farPlane = 100.0f};
}

// Environment light only: the sun off, no flat ambient.
void environmentOnly(render::FrameData& f, const engine::Camera& camera) {
  f.viewProjection = camera.viewProjection(f.viewportSize);
  f.cameraPosition = camera.position;
  f.sun.intensity = 0.0f;
  f.ambient = glm::vec3(0.0f);
  f.environmentIntensity = 1.0f;
}

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

render::MeshHandle createMesh(render::VulkanRenderer& renderer, const render::MeshData& data) {
  auto mesh = renderer.createMesh(data, "test mesh");
  REQUIRE_MESSAGE(mesh.has_value(), (mesh ? "" : mesh.error().message));
  return *mesh;
}

std::string describe(render::CapturedFrame::Pixel p) {
  return std::to_string(p.r) + "," + std::to_string(p.g) + "," + std::to_string(p.b);
}

}  // namespace

TEST_CASE("a renderer starts with a baked default environment: three cube maps of the sizes asked for") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(1);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    CHECK(renderer->environmentBakes() == 1);
    const render::EnvironmentMaps& maps = renderer->environment();
    CHECK(maps.environment.extent().width == render::kFastEnvironment.cubeSize);
    CHECK(maps.environment.layers() == 6);
    CHECK(maps.environment.mipLevels() == gpu::fullMipCount(render::kFastEnvironment.cubeSize, 1));
    CHECK(maps.irradiance.extent().width == render::kFastEnvironment.irradianceSize);
    CHECK(maps.specular.extent().width == render::kFastEnvironment.specularSize);
    CHECK(maps.specularMips() == 4);  // 32, 16, 8, 4: stops at 4x4
    REQUIRE(app->run().has_value());
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("a matte surface takes the colour of the sky it faces; a new environment changes it") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const render::Material matte{.baseColourFactor = glm::vec4(1.0f), .metallicFactor = 0.0f, .roughnessFactor = 1.0f};
    const auto draw = [&](render::FrameData& f) {
      environmentOnly(f, lookingDown());
      f.meshes.push_back({.mesh = tile, .material = matte, .transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f))});
    };

    REQUIRE(renderer->setEnvironment(split({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}), kTestQuality).has_value());
    const auto redSky = renderOnce(*app, *renderer, draw).at(160, 120);
    INFO("under a red sky: ", describe(redSky));
    CHECK(redSky.r > 200);  // facing up: lit by the red upper half only
    CHECK(redSky.b < 40);

    REQUIRE(renderer->setEnvironment(split({0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}), kTestQuality).has_value());
    const auto blueSky = renderOnce(*app, *renderer, draw).at(160, 120);
    INFO("under a blue sky: ", describe(blueSky));
    CHECK(blueSky.b > 200);
    CHECK(blueSky.r < 40);
    CHECK(renderer->environmentBakes() == 3);  // the default, then two
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("a mirror reflects what is straight above it; roughness blurs in the rest of the sky") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    // Green in a small cap round the zenith (within ~25 degrees), red elsewhere.
    REQUIRE(renderer
                ->setEnvironment(panorama(128, 64,
                                          [](glm::vec3 d) {
                                            return d.y > 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                                          }),
                                 kTestQuality)
                .has_value());
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const render::Material mirror{.metallicFactor = 1.0f, .roughnessFactor = 0.0f};
    const render::Material rough{.metallicFactor = 1.0f, .roughnessFactor = 1.0f};
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      environmentOnly(f, lookingDown());
      f.meshes.push_back({.mesh = tile, .material = mirror,
                          .transform = glm::translate(glm::mat4(1.0f), {-1.0f, 0.0f, 0.0f})});
      f.meshes.push_back({.mesh = tile, .material = rough,
                          .transform = glm::translate(glm::mat4(1.0f), {1.0f, 0.0f, 0.0f})});
    });
    const auto sharp = frame.at(80, 120);
    const auto blurred = frame.at(240, 120);
    INFO("mirror: ", describe(sharp), "; rough metal: ", describe(blurred));
    CHECK(sharp.g > 200);  // the zenith, exactly
    CHECK(sharp.r < 60);
    CHECK(blurred.r > blurred.g);  // a wide cone: mostly the red sky round the green cap
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("the sky fills what no mesh covers, shows the sun's disk, and is skipped when not asked for") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    REQUIRE(renderer->setEnvironment(split({0.8f, 0.1f, 0.1f}, {0.1f, 0.1f, 0.8f}), kTestQuality).has_value());
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    // Looking along -z from the origin; the sun straight ahead (light travelling +z).
    const engine::Camera camera{.projection = engine::Projection::Perspective, .position = {0.0f, 0.0f, 0.0f},
                                .target = {0.0f, 0.0f, -1.0f}, .fovY = glm::radians(60.0f), .nearPlane = 0.1f,
                                .farPlane = 100.0f};
    const auto frame = renderOnce(*app, *renderer, [&](render::FrameData& f) {
      f.viewProjection = camera.viewProjection(f.viewportSize);
      f.cameraPosition = camera.position;
      f.sun = {.direction = {0.0f, 0.0f, 1.0f}, .colour = {1.0f, 1.0f, 1.0f}, .intensity = 3.0f};
      f.drawSky = true;
      // A grey tile standing up in the lower right, facing the camera.
      f.meshes.push_back({.mesh = tile,
                          .material = {.baseColourFactor = {0.2f, 0.2f, 0.2f, 1.0f}, .metallicFactor = 0.0f},
                          .transform = glm::translate(glm::mat4(1.0f), {1.0f, -0.6f, -3.0f}) *
                                       glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), {1.0f, 0.0f, 0.0f})});
    });
    const auto above = frame.at(160, 20);
    const auto below = frame.at(60, 225);
    const auto sun = frame.at(160, 120);
    const auto tileCentre = frame.at(217, 174);
    INFO("above: ", describe(above), "; below: ", describe(below), "; sun: ", describe(sun), "; tile: ",
         describe(tileCentre));
    CHECK(above.r > above.b + 60);  // the red upper half
    CHECK(below.b > below.r + 60);  // the blue lower half
    CHECK((sun.r > 240 && sun.g > 240 && sun.b > 240));  // the disk, dead ahead
    CHECK(std::abs(int{tileCentre.r} - int{tileCentre.b}) < 60);  // the tile hides the sky: grey, not red or blue
    CHECK(renderer->lastDrawCalls() >= 2);

    // Without drawSky: the clear colour.
    app->setExtract([&](render::FrameData& f, f64) {
      f.viewProjection = camera.viewProjection(f.viewportSize);
      f.drawSky = false;  // presentation settings persist between frames: turn it off
    });
    renderer->requestCapture();
    REQUIRE(app->run().has_value());
    auto plain = renderer->takeCapture();
    REQUIRE(plain.has_value());
    const auto background = plain->at(160, 20);
    CHECK((background.r < 40 && background.g < 40 && background.b < 60));
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("tone mapping rolls bright light off below white instead of clipping; dim light passes nearly unchanged") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(2);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const render::MeshHandle tile = createMesh(*renderer, floorTile());
    const auto shade = [&](f32 exposure, bool toneMap) {
      return renderOnce(*app, *renderer, [&](render::FrameData& f) {
               environmentOnly(f, lookingDown());
               f.environmentIntensity = 0.0f;
               f.ambient = glm::vec3(1.0f);  // flat light: a non-metal shows its base colour x exposure
               f.exposure = exposure;
               f.toneMap = toneMap;
               f.meshes.push_back({.mesh = tile,
                                   .material = {.baseColourFactor = {0.5f, 0.5f, 0.5f, 1.0f}, .metallicFactor = 0.0f},
                                   .transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f))});
             })
          .at(160, 120);
    };
    const auto plain = shade(1.0f, false);
    const auto mapped = shade(1.0f, true);
    const auto bright = shade(4.0f, false);
    const auto brightMapped = shade(4.0f, true);
    INFO("0.5: ", describe(plain), "; tone mapped: ", describe(mapped), "; x4: ", describe(bright),
         "; x4 tone mapped: ", describe(brightMapped));
    CHECK(std::abs(int{plain.r} - 188) <= 3);       // linear 0.5 in sRGB
    CHECK(std::abs(int{mapped.r} - int{plain.r}) <= 10);  // dim: almost untouched
    CHECK(bright.r == 255);                          // 2.0 clips
    CHECK(brightMapped.r < 255);                     // rolled off...
    CHECK(brightMapped.r > 230);                     // ...but still near white
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("bad environment input is rejected and the current environment is kept") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(1);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    const auto notPowerOfTwo = renderer->setEnvironment(render::ProceduralSky{}, {.cubeSize = 100});
    REQUIRE_FALSE(notPowerOfTwo.has_value());
    CHECK(notPowerOfTwo.error().code == Errc::InvalidArgument);
    CHECK(notPowerOfTwo.error().message.find("cubeSize") != std::string::npos);

    render::HdrImage shortImage{.width = 8, .height = 4};
    shortImage.rgba.resize(10);
    const auto bad = renderer->setEnvironment(shortImage, kTestQuality);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == Errc::InvalidArgument);
    CHECK(renderer->environmentBakes() == 1);  // still the default
    REQUIRE(app->run().has_value());
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
      std::printf("skipping environment_tests: %s\n", probe.error().message.c_str());
      return kSkipExitCode;
    }
    if (!(*probe)->vulkanSupported()) {
      std::printf("skipping environment_tests: no Vulkan driver on this machine\n");
      return kSkipExitCode;
    }
  }
  doctest::Context context(argc, argv);
  return context.run();
}
