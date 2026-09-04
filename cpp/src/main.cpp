// Erebus Cradle — native game
//
// A standalone C++/OpenGL implementation combining the cinematic PBR
// renderer (see docs/NATIVE_RENDERER.md) with an actual mission loop: a
// physical player, a hitscan weapon, hostiles with real AI, and missions
// loaded from plain data files under content/ rather than compiled in —
// so a new monthly mission or boss is a text file, not a code change.
//
// This is Phase 1 of that: one weapon, one arena shape, three enemy
// archetypes, text-file missions. Gear, currencies, a hub, and a save
// system are Phase 2/3 — see cpp/README.md's roadmap section.
#include "Gl.h"
#include "Camera.h"
#include "Renderer.h"
#include "Game.h"
#include "Hud.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
  auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
  if (renderer && w > 0 && h > 0) renderer->resize(w, h);
}

}  // namespace

int main(int argc, char** argv) {
  std::string missionId = "patrol_dust_shelf";
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--mission") == 0 && i + 1 < argc) missionId = argv[++i];
  }

  if (!glfwInit()) {
    std::fprintf(stderr, "glfwInit failed\n");
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_SAMPLES, 0);

  int width = 1280, height = 800;
  GLFWwindow* window = glfwCreateWindow(width, height, "Erebus Cradle", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "glfwCreateWindow failed (no GL 4.1 core context available)\n");
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  glewExperimental = GL_TRUE;
  GLenum glewStatus = glewInit();
  glGetError();  // glewInit() reliably leaves a spurious GL_INVALID_ENUM behind on core profiles
  if (glewStatus != GLEW_OK) {
    std::fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(glewStatus));
    return 1;
  }
  std::printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
  std::printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

  Camera camera;
  Renderer renderer;
  Game game;
  Hud hud;

  glfwGetFramebufferSize(window, &width, &height);
  renderer.create(width, height);
  hud.create();

  const char* contentDir = std::getenv("EREBUS_CONTENT_DIR");
  if (!game.init(contentDir ? contentDir : "content", missionId)) {
    std::fprintf(stderr, "Failed to load mission '%s' — check content/missions/%s.cfg exists\n",
                 missionId.c_str(), missionId.c_str());
    return 1;
  }
  camera.position = game.player().eyePosition();

  glfwSetWindowUserPointer(window, &renderer);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  bool mouseCaptured = true;
  bool firstMouse = true;
  double lastX = 0.0, lastY = 0.0;
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwGetCursorPos(window, &lastX, &lastY);

  // ---------- headless / scripted-input verification ----------
  // No physical GPU or display was available while building this, so
  // gameplay logic was verified the same way the renderer was: run
  // headlessly and read real state back, not eyeball a screenshot.
  // EREBUS_FORCE_FORWARD=1 holds W the whole run (verifies player movement
  // and level collision). EREBUS_DEBUG_AUTOAIM=1 snaps the camera onto the
  // nearest live hostile every frame — a verification aid only, never on
  // by default, so firing can be tested without simulating real mouse
  // input. EREBUS_FORCE_FIRE=1 holds the trigger the whole run.
  bool forceForward = std::getenv("EREBUS_FORCE_FORWARD") != nullptr;
  bool forceFire = std::getenv("EREBUS_FORCE_FIRE") != nullptr;
  bool debugAutoaim = std::getenv("EREBUS_DEBUG_AUTOAIM") != nullptr;
  const char* dumpPath = std::getenv("EREBUS_DUMP_FRAME");
  int maxFrames = 0;
  if (const char* mf = std::getenv("EREBUS_MAX_FRAMES")) maxFrames = std::atoi(mf);
  const char* logStatePath = std::getenv("EREBUS_LOG_STATE");

  double lastTime = glfwGetTime();
  int frame = 0;

  while (!glfwWindowShouldClose(window)) {
    double now = glfwGetTime();
    float dt = std::min(0.05, now - lastTime > 0 ? now - lastTime : 0.0);
    lastTime = now;

    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && mouseCaptured) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      mouseCaptured = false;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !mouseCaptured) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      mouseCaptured = true;
      firstMouse = true;
    }

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (mouseCaptured) {
      if (firstMouse) { lastX = mx; lastY = my; firstMouse = false; }
      camera.look((float)(mx - lastX), (float)(my - lastY), 0.09f);
    }
    lastX = mx; lastY = my;

    if (debugAutoaim) {
      // Verification aid: point the camera at the nearest live hostile so
      // firing can be exercised without a real mouse. See Game::collect /
      // Hostile for where headCentre() comes from.
      glm::vec3 eye = camera.position;
      float best = 1e9f;
      glm::vec3 bestDir(0, 0, -1);
      // Game doesn't expose hostiles directly (Renderer-facing interface
      // only); this reaches in via the same draw-collection path so the
      // aid never needs its own privileged access.
      std::vector<DrawItem> probe;
      game.collect(0.0f, probe);
      for (auto& it : probe) {
        if (it.material != MaterialType::Emissive) continue;
        glm::vec3 p = glm::vec3(it.model[3]);
        float d = glm::length(p - eye);
        if (d < best) { best = d; bestDir = glm::normalize(p - eye); }
      }
      camera.yaw = glm::degrees(std::atan2(bestDir.z, bestDir.x));
      camera.pitch = glm::degrees(std::asin(std::clamp(bestDir.y, -1.0f, 1.0f)));
    }

    bool aiming = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    float targetAim = aiming ? 1.0f : 0.0f;
    camera.aim += (targetAim - camera.aim) * std::min(1.0f, dt * 10.0f);

    bool firePressed = forceFire || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool reloadHeld = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS || forceFire;
    game.update(window, camera, dt, firePressed, reloadHeld, forceForward);

    renderer.renderFrame(game, camera, (float)now, dt);

    glfwGetFramebufferSize(window, &width, &height);
    const Weapon& w = game.weapon();
    float ammoFrac = w.magSize > 0 ? (float)w.ammoInMag / w.magSize : 0.0f;
    float reloadFrac = w.reloading ? 1.0f - (w.reloadT / w.reloadTime) : 0.0f;
    hud.draw(width, height, game.player().hp / game.player().maxHp, ammoFrac, w.ammoInMag, w.magSize,
             w.reloading, reloadFrac, game.hitMarkerT, game.damageFlashT, game.waveProgress(),
             game.bossAlive(), game.bossHpFraction(), game.missionState() == MissionState::Complete,
             game.missionState() == MissionState::Failed);

    if (frame % 30 == 0) {
      const char* stateStr = game.missionState() == MissionState::Complete ? "COMPLETE"
                            : game.missionState() == MissionState::Failed ? "FAILED" : "active";
      char title[224];
      std::snprintf(title, sizeof(title),
                     "Erebus Cradle | %s | %.1fms | hp %.0f | ammo %d/%d | wave %.0f%% | %s",
                     game.missionName().c_str(), dt * 1000.0f, game.player().hp,
                     w.ammoInMag, w.reserveAmmo, game.waveProgress() * 100.0f, stateStr);
      glfwSetWindowTitle(window, title);
    }
    frame++;

    if (logStatePath && frame == maxFrames && maxFrames > 0) {
      FILE* f = std::fopen(logStatePath, "w");
      if (f) {
        std::fprintf(f,
          "{\"frame\":%d,\"missionState\":\"%s\",\"playerHp\":%.2f,\"playerPos\":[%.2f,%.2f,%.2f],"
          "\"ammoInMag\":%d,\"reserveAmmo\":%d,\"waveProgress\":%.3f,\"bossAlive\":%s}\n",
          frame,
          game.missionState() == MissionState::Complete ? "complete"
            : game.missionState() == MissionState::Failed ? "failed" : "in_progress",
          game.player().hp, game.player().position.x, game.player().position.y, game.player().position.z,
          w.ammoInMag, w.reserveAmmo, game.waveProgress(), game.bossAlive() ? "true" : "false");
        std::fclose(f);
      }
    }

    if (dumpPath && maxFrames > 0 && frame >= maxFrames) {
      glfwGetFramebufferSize(window, &width, &height);
      std::vector<unsigned char> pixels(size_t(width) * height * 3);
      glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      FILE* f = std::fopen(dumpPath, "wb");
      if (f) {
        std::fprintf(f, "P6\n%d %d\n255\n", width, height);
        for (int y = height - 1; y >= 0; y--) {
          std::fwrite(pixels.data() + size_t(y) * width * 3, 1, size_t(width) * 3, f);
        }
        std::fclose(f);
        std::printf("[dump] wrote %s (%dx%d) at frame %d\n", dumpPath, width, height, frame);
      }
      break;
    }
    if (maxFrames > 0 && frame >= maxFrames && !dumpPath) break;

    glfwSwapBuffers(window);
  }

  game.destroy();
  hud.destroy();
  renderer.destroy();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
