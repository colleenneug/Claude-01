// =============================================================================
//  Aedan's Playground — entry point.
//
//  Start-up order: logging -> job system -> application (platform, window)
//  -> renderer -> assets -> loop. Shutdown is the reverse. Logging comes
//  first and goes last so that every other step, including a failed
//  start-up, is recorded.
//
//  Usage: playground_app [--scene models|sprites] [--model PATH] [--environment PATH] [--frames N]
//                        [--sprites N] [--verbose] [--log-file PATH]
//                        [--server [--port N]] [--connect HOST[:PORT] [--headless]]
//                        [--net-latency MS] [--net-loss PERCENT]
//                        [--validation | --no-validation]
//
//  Demo scenes (app/Scene.h): the 3D one (glTF models, orbit and fly
//  camera; ModelScene.h) by default, the 2D one (SpriteScene.h) with
//  --scene sprites, and the network client (NetScene.h) with --connect.
//  --server, and --connect with --headless, open no window (Server.h).
//
//  Exit codes: 0 clean exit, 1 start-up or runtime failure (including any
//  Vulkan validation error), 2 bad arguments.
// =============================================================================

#include "ModelScene.h"
#include "NetScene.h"
#include "Scene.h"
#include "Server.h"
#include "SpriteScene.h"

#include "playground/audio/AudioEngine.h"
#include "playground/core/JobSystem.h"
#include "playground/core/Log.h"
#include "playground/engine/Application.h"
#include "playground/engine/Assets.h"
#include "playground/engine/DebugUi.h"
#include "playground/engine/EnvironmentAsset.h"
#include "playground/engine/Gltf.h"
#include "playground/render/VulkanRenderer.h"

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace playground;
using log::Category;

enum class SceneChoice : u8 { Models, Sprites };

struct Options {
  u64 frames = 0;  // 0 = until the window is closed
  bool verbose = false;
  std::string logFile = "playground.log";
  bool validation = render::VulkanRendererDesc{}.enableValidation;  // on in debug builds
  SceneChoice scene = SceneChoice::Models;
  std::optional<std::filesystem::path> model;  // --model: show this glTF file
  std::optional<std::filesystem::path> environment;  // --environment: light the 3D scene with this .hdr
  bool server = false;                      // --server: headless dedicated server
  std::optional<std::string> connect;       // --connect HOST[:PORT]: play on a server
  bool headless = false;                    // --headless: with --connect, the scripted bot, no window
  u16 port = game::protocol::kDefaultPort;  // --port N
  net::NetworkConditions network;           // --net-latency, --net-loss
  u32 sprites = 25'000;  // how many bouncing squares the sprite scene spawns
  bool editor = false;                        // --editor: start with the developer UI shown
  std::filesystem::path sceneFile = "scene.json";  // --scene-file: what Ctrl+S / Ctrl+O save and load
};

void printUsage() {
  std::puts(
      "usage: playground_app [--scene models|sprites] [--model PATH] [--environment PATH] [--frames N]\n"
      "                      [--sprites N] [--verbose] [--log-file PATH] [--validation | --no-validation]\n"
      "  --scene models   3D glTF models with an orbit/fly camera (the default)\n"
      "  --scene sprites  the 2D demo: bouncing squares and a player to steer\n"
      "  --model PATH     show this glTF 2.0 file (.gltf or .glb) in the 3D scene\n"
      "  --environment PATH  light the 3D scene with this HDR panorama (.hdr) and show it as the sky\n"
      "  --server         run a dedicated server: no window, no GPU, 120 Hz simulation (Ctrl+C stops it)\n"
      "  --port N         the server's UDP port (default 27015)\n"
      "  --connect HOST[:PORT]  play on a server: a window showing its world, WASD moves your player\n"
      "  --headless       with --connect: no window, a bot walks for --frames ticks and reports\n"
      "  --net-latency MS     testing: delay everything this end sends by MS (plus up to MS/5 jitter)\n"
      "  --net-loss PERCENT   testing: lose this share of the unreliable packets this end sends\n"
      "  --frames N       run N frames, then exit (used by the automated tests)\n"
      "  --sprites N      how many bouncing squares to spawn (default 25000)\n"
      "  --editor         start with the developer UI shown (F1 shows and hides it)\n"
      "  --scene-file PATH  the scene file the editor saves (Ctrl+S) and loads (Ctrl+O); default scene.json\n"
      "  --verbose        log at debug level, including frame-time statistics\n"
      "  --log-file PATH  where to write the log (default: playground.log)\n"
      "  --validation     turn Vulkan validation on (default in debug builds)\n"
      "  --no-validation  turn Vulkan validation off (default in release builds)");
}

Result<Options> parseOptions(std::span<char*> args) {
  Options options;
  for (usize i = 1; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    auto nextValue = [&]() -> Result<std::string_view> {
      if (i + 1 >= args.size()) return fail(Errc::InvalidArgument, "{} needs a value", arg);
      return std::string_view{args[++i]};
    };

    if (arg == "--frames") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), options.frames);
      if (ec != std::errc{} || end != value->data() + value->size()) {
        return fail(Errc::InvalidArgument, "--frames expects a whole number, got \"{}\"", *value);
      }
    } else if (arg == "--sprites") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), options.sprites);
      if (ec != std::errc{} || end != value->data() + value->size()) {
        return fail(Errc::InvalidArgument, "--sprites expects a whole number, got \"{}\"", *value);
      }
      options.scene = SceneChoice::Sprites;  // asking for sprites means the sprite scene
    } else if (arg == "--scene") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      if (*value == "models") {
        options.scene = SceneChoice::Models;
      } else if (*value == "sprites") {
        options.scene = SceneChoice::Sprites;
      } else {
        return fail(Errc::InvalidArgument, "--scene expects models or sprites, got \"{}\"", *value);
      }
    } else if (arg == "--server") {
      options.server = true;
    } else if (arg == "--port") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), options.port);
      if (ec != std::errc{} || end != value->data() + value->size()) {
        return fail(Errc::InvalidArgument, "--port expects a number from 0 to 65535, got \"{}\"", *value);
      }
    } else if (arg == "--connect") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      options.connect = std::string(*value);
    } else if (arg == "--editor") {
      options.editor = true;
    } else if (arg == "--scene-file") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      options.sceneFile = std::filesystem::path(*value);
    } else if (arg == "--headless") {
      options.headless = true;
    } else if (arg == "--net-latency") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), options.network.latencyMs);
      if (ec != std::errc{} || end != value->data() + value->size() || options.network.latencyMs > 10'000) {
        return fail(Errc::InvalidArgument, "--net-latency expects milliseconds from 0 to 10000, got \"{}\"", *value);
      }
      options.network.jitterMs = options.network.latencyMs / 5;
    } else if (arg == "--net-loss") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), options.network.lossPercent);
      if (ec != std::errc{} || end != value->data() + value->size() || !(options.network.lossPercent >= 0.0f) ||
          options.network.lossPercent > 100.0f) {
        return fail(Errc::InvalidArgument, "--net-loss expects a percentage from 0 to 100, got \"{}\"", *value);
      }
    } else if (arg == "--environment") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      options.environment = std::filesystem::path(*value);
      options.scene = SceneChoice::Models;
    } else if (arg == "--model") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      options.model = std::filesystem::path(*value);
      options.scene = SceneChoice::Models;
    } else if (arg == "--validation") {
      options.validation = true;
    } else if (arg == "--no-validation") {
      options.validation = false;
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "--log-file") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      options.logFile = std::string(*value);
    } else {
      return fail(Errc::InvalidArgument, "unknown argument \"{}\"", arg);
    }
  }
  return options;
}

// A stand-in for gameplay: shows the frame rate in the title bar twice a
// second, which proves update() receives correct delta times.
class TitleFpsCounter {
 public:
  explicit TitleFpsCounter(platform::Window& window) : window_(window) {}

  void update(const FrameTime& time) {
    ++frames_;
    seconds_ += time.rawDeltaSeconds;
    if (seconds_ < 0.5) return;
    const f64 fps = static_cast<f64>(frames_) / seconds_;
    window_.setTitle(std::format("Aedan's Playground — {:.0f} fps ({:.2f} ms)", fps, 1000.0 / fps));
    frames_ = 0;
    seconds_ = 0.0;
  }

 private:
  platform::Window& window_;
  u64 frames_ = 0;
  f64 seconds_ = 0.0;
};

}  // namespace

int main(int argc, char** argv) {
  auto options = parseOptions(std::span<char*>(argv, static_cast<usize>(argc)));
  if (!options) {
    std::fprintf(stderr, "error: %s\n", options.error().message.c_str());
    printUsage();
    return 2;
  }

  auto logSession = log::init({
      .filePath = options->logFile,
      .level = options->verbose ? log::Level::Debug : log::Level::Info,
  });
  if (!logSession) {
    std::fprintf(stderr, "fatal: %s\n", logSession.error().message.c_str());
    return 1;
  }

  // The headless modes: no window, no GPU, nothing of GLFW or Vulkan started.
  if (options->server) {
    return app::runServer({.port = options->port, .maxTicks = options->frames, .network = options->network});
  }
  app::ClientOptions client{.port = options->port, .network = options->network};
  if (options->connect) {
    if (Status s = app::parseAddress(*options->connect, client); !s) {
      std::fprintf(stderr, "error: %s\n", s.error().message.c_str());
      return 2;
    }
    if (options->frames > 0) client.ticks = options->frames;
    if (options->headless) return app::runClient(client);
  } else if (options->headless) {
    std::fprintf(stderr, "error: --headless only goes with --connect\n");
    return 2;
  }

  // Worker threads for background work (file loading today). Created before
  // everything that submits jobs, so it is destroyed after them.
  JobSystem jobs;

  auto app = engine::Application::create({
      .window = {.title = "Aedan's Playground"},
      .maxFrames = options->frames,
  });
  if (!app) {
    log::critical(Category::Engine, "start-up failed ({}): {}", describe(app.error().code), app.error().message);
    return 1;
  }
  engine::Application& application = **app;

  // The Vulkan renderer: brings up the GPU (each step logged, a failure
  // names the step), then the swapchain and the sprite pipeline. Once
  // attached, the Application owns it and destroys it before the window.
  auto rendererResult = render::createVulkanRenderer(application.platform(), application.window(),
                                                     {.enableValidation = options->validation});
  if (!rendererResult) {
    log::critical(Category::Render, "renderer start-up failed ({}): {}", describe(rendererResult.error().code),
                  rendererResult.error().message);
    return 1;
  }

  render::Renderer& renderer = **rendererResult;  // still ours to call: the Application only owns it
  application.setRenderer(std::move(*rendererResult));

  // Game files, loaded on the workers and finished on this thread in
  // update(). They live in assets/ next to the executable (app/CMakeLists.txt).
  // glTF models are uploaded through the renderer, so every model must be
  // unloaded before the renderer goes: see assets.clear() below.
  engine::AssetManager assets(jobs, std::filesystem::path(argv[0]).parent_path() / "assets");
  assets.registerType<engine::TextAsset>(engine::textAssetLoader());
  assets.registerType<engine::Model>(engine::gltfLoader(renderer));
  assets.registerType<engine::EnvironmentAsset>(engine::environmentLoader(renderer));
  const auto welcome = assets.load<engine::TextAsset>("welcome.txt");
  bool welcomeShown = false;

  // Sound: the sound card, or miniaudio's null device where there is none
  // (never stops the game). Declared before the scene, which holds its clips.
  std::unique_ptr<audio::AudioEngine> sound;
  if (auto created = audio::AudioEngine::create()) {
    sound = std::move(*created);
  } else {
    log::warn(Category::Engine, "no sound: {}", created.error().message);
  }

  // The scene. Declared after the Application, so it is destroyed first;
  // shutdown() also drops the callbacks below that refer to it.
  std::unique_ptr<app::Scene> scene;
  if (options->connect) {
    auto netScene = app::NetScene::create(application, client, options->sceneFile);
    if (!netScene) {
      log::critical(Category::Net, "network client could not start: {}", netScene.error().message);
      return 1;
    }
    scene = std::move(*netScene);
  } else if (options->scene == SceneChoice::Sprites) {
    scene = std::make_unique<app::SpriteScene>(application, options->sprites, options->sceneFile, sound.get());
  } else {
    auto modelScene = app::ModelScene::create(assets, renderer, jobs, options->model, options->environment,
                                              options->sceneFile, sound.get());
    if (!modelScene) {
      log::critical(Category::Engine, "3D scene could not start ({}): {}", describe(modelScene.error().code),
                    modelScene.error().message);
      return 1;
    }
    scene = std::move(*modelScene);
  }

  // The simulation: fixed 1/120 s steps, 0..n per frame (core/FixedStep.h).
  application.setFixedUpdate(
      [&](const FixedStep& step, const platform::InputState& input) { scene->fixedUpdate(step, input); });

  // Once per frame, at the display's rate: loading, Escape, the FPS in the title, the scene.
  TitleFpsCounter fpsCounter(application.window());
  application.setUpdate([&](const FrameTime& time, const platform::InputState& input) {
    (void)assets.update();  // finish a few loaded files; never waits for one
    if (!welcomeShown && assets.state(welcome) != engine::AssetState::Loading) {
      welcomeShown = true;
      if (const engine::TextAsset* text = assets.get(welcome)) {
        log::info(Category::Game, "asset welcome.txt: {}", text->text.substr(0, text->text.find('\n')));
      } else if (const Error* error = assets.error(welcome)) {
        log::warn(Category::Game, "asset welcome.txt not loaded: {}", error->message);  // the game still runs
      }
    }
    if (input.pressed(platform::Key::Escape)) application.requestQuit();
    fpsCounter.update(time);
    scene->update(time, input);
  });

  // Once per rendered frame: what the scene shows.
  application.setExtract([&](render::FrameData& frame, f64 alpha) { scene->extract(frame, alpha); });

  // The developer UI (F1): the editor bar, hierarchy, inspector, profiler.
  // Without it (a renderer that cannot draw it) the game runs as before.
  std::unique_ptr<engine::DebugUi> debugUi;
  if (auto ui = engine::DebugUi::create(application.window(), renderer, options->editor)) {
    debugUi = std::move(*ui);
    debugUi->addPanel([&] {
      scene->drawDebugUi({.frame = application.lastFrameProfile(), .render = renderer.lastFrameStats()});
    });
    application.setDebugUi(debugUi.get());
  } else {
    log::warn(Category::Engine, "no developer UI: {}", ui.error().message);
  }

  const Status status = application.run();

  scene->logSummary();  // the end-to-end tests read it
  const int sceneExitCode = scene->exitCode();
  application.setDebugUi(nullptr);
  debugUi.reset();      // ImGui's Vulkan side goes while the renderer still exists
  assets.clear();       // every GPU asset goes back to the renderer while it still exists
  application.shutdown();  // renderer, then window, then GLFW

  if (!status) {
    log::critical(Category::Engine, "stopped on error ({}): {}", describe(status.error().code),
                  status.error().message);
    return 1;
  }
  // Validation errors also arrive during teardown (leaked objects are
  // reported when the instance is destroyed), so this is checked last.
  if (const render::GpuValidationCounts counts = render::gpuValidationCounts(); counts.errors > 0) {
    log::error(Category::Gpu, "{} Vulkan validation error(s) and {} warning(s) this run; see the log above",
               counts.errors, counts.warnings);
    return 1;
  } else if (counts.warnings > 0) {
    log::warn(Category::Gpu, "{} Vulkan validation warning(s) this run; see the log above", counts.warnings);
  }
  if (sceneExitCode != 0) return sceneExitCode;
  log::info(Category::Engine, "clean exit");
  return 0;
}
