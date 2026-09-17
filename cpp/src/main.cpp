// Erebus Cradle — native game
//
// A standalone C++/OpenGL implementation combining the cinematic PBR
// renderer (see docs/NATIVE_RENDERER.md) with an actual mission loop: a
// physical player, a hitscan weapon, hostiles with real AI, missions
// loaded from plain data files under content/ rather than compiled in (so
// a new monthly mission or boss is a text file, not a code change), and a
// persistent Profile — chits, owned/equipped gear, completed missions —
// picked in a keyboard-driven Hub between missions and saved to disk.
#include "Gl.h"
#include "Camera.h"
#include "Renderer.h"
#include "Game.h"
#include "Hud.h"
#include "Hub.h"
#include "Profile.h"
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

enum class AppState { Hub, Mission };

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
  Hub hub;

  glfwGetFramebufferSize(window, &width, &height);
  renderer.create(width, height);
  hud.create();

  // EREBUS_QUALITY_TIER=<0-3> forces high/medium/low/minimal and disables
  // auto-adjustment; EREBUS_QUALITY_AUTO=0 disables auto-adjustment without
  // forcing a tier (stays on whatever the forced/default tier is). Useful
  // for reproducible testing, and as a diagnostic on hardware behaving
  // unexpectedly: if a simpler tier renders correctly where "high" doesn't,
  // that narrows down which pass is actually the problem there.
  if (const char* qt = std::getenv("EREBUS_QUALITY_TIER")) {
    renderer.setAutoQuality(false);
    renderer.setQualityTier(std::atoi(qt));
  } else if (const char* qa = std::getenv("EREBUS_QUALITY_AUTO")) {
    if (std::atoi(qa) == 0) renderer.setAutoQuality(false);
  }

  const char* contentDirEnv = std::getenv("EREBUS_CONTENT_DIR");
  std::string contentDir = contentDirEnv ? contentDirEnv : "content";

  const char* savePathEnv = std::getenv("EREBUS_SAVE_PATH");
  std::string savePath = savePathEnv ? savePathEnv : "save.dat";
  Profile profile = ProfileStore::load(savePath);

  // The Hub needs its own Content (ids, costs, mission list) before any
  // mission — and thus any Game — exists; Game loads its own copy again
  // when a mission actually starts. Content is small text files scanned
  // once, so loading it twice is cheap and keeps Hub and Game decoupled.
  Content hubContent;
  if (!hubContent.loadAll(contentDir)) {
    std::fprintf(stderr, "Failed to load content directory '%s'\n", contentDir.c_str());
    return 1;
  }
  hub.init(hubContent, profile);
  hub.preselectMission(missionId);

  // EREBUS_SKIP_HUB=1 boots straight into --mission with whatever's
  // currently equipped, bypassing the Hub entirely — kept for every
  // headless verification flow that predates the Hub and still expects to
  // land in a mission on frame 0.
  bool skipHub = std::getenv("EREBUS_SKIP_HUB") != nullptr;
  AppState state = skipHub ? AppState::Mission : AppState::Hub;
  bool gameEverStarted = false;
  if (state == AppState::Mission) {
    if (!game.init(contentDir, missionId, profile)) {
      std::fprintf(stderr, "Failed to load mission '%s' — check content/missions/%s.cfg exists\n",
                   missionId.c_str(), missionId.c_str());
      return 1;
    }
    gameEverStarted = true;
    camera.position = game.player().eyePosition();
  }

  glfwSetWindowUserPointer(window, &renderer);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  bool mouseCaptured = true;
  bool firstMouse = true;
  double lastX = 0.0, lastY = 0.0;
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwGetCursorPos(window, &lastX, &lastY);
  bool prevReturnKey = false;

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
  // Prints the screen-centre pixel, before and after tone mapping, every
  // 60 frames — see Renderer::debugPrintCenterPixel for why: it turns "the
  // screen looks dark/black" from a description into a number, so hardware
  // this project was never tested on doesn't have to be debugged by guessing.
  bool debugPixel = std::getenv("EREBUS_DEBUG_PIXEL") != nullptr;
  const char* dumpPath = std::getenv("EREBUS_DUMP_FRAME");
  int maxFrames = 0;
  if (const char* mf = std::getenv("EREBUS_MAX_FRAMES")) maxFrames = std::atoi(mf);
  const char* logStatePath = std::getenv("EREBUS_LOG_STATE");

  // EREBUS_HUB_SCRIPT="1,1,3,launch" drives the Hub deterministically for
  // headless tests, the same idea as EREBUS_FORCE_FORWARD but for menu
  // input rather than movement — one scripted action consumed per frame
  // while state is Hub, then ignored once a mission starts.
  std::vector<std::string> hubScript;
  if (const char* hs = std::getenv("EREBUS_HUB_SCRIPT")) {
    std::string s = hs;
    size_t start = 0;
    while (start <= s.size()) {
      size_t comma = s.find(',', start);
      std::string tok = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      if (!tok.empty()) hubScript.push_back(tok);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }
  size_t hubScriptPos = 0;

  // EREBUS_FIXED_DT=<seconds> advances the simulation by exactly that much
  // per frame instead of by real elapsed time. Without it, a headless
  // gameplay test's outcome depends on how fast the host happens to be
  // running: a loaded machine produces a larger clamped dt, so the same
  // frame budget covers several times as much game time, and a knife-edge
  // fight flips between won and lost run to run. Fixing dt makes those
  // tests reproducible and comparable.
  float fixedDt = 0.0f;
  if (const char* fd = std::getenv("EREBUS_FIXED_DT")) fixedDt = (float)std::atof(fd);

  double lastTime = glfwGetTime();
  int frame = 0;

  while (!glfwWindowShouldClose(window)) {
    double now = glfwGetTime();
    float dt = fixedDt > 0.0f
                   ? fixedDt
                   : (float)std::min(0.05, now - lastTime > 0 ? now - lastTime : 0.0);
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

    if (state == AppState::Hub) {
      std::string scriptedStorage;
      const char* scripted = nullptr;
      if (hubScriptPos < hubScript.size()) {
        scriptedStorage = hubScript[hubScriptPos++];
        scripted = scriptedStorage.c_str();
      }
      bool launch = hub.update(window, scripted);
      if (launch) {
        if (game.init(contentDir, hub.selectedMission(), profile)) {
          gameEverStarted = true;
          camera.position = game.player().eyePosition();
          state = AppState::Mission;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          mouseCaptured = true;
          firstMouse = true;
        }
      }

      glClearColor(0.03f, 0.035f, 0.05f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glfwGetFramebufferSize(window, &width, &height);
      hud.drawHub(width, height, hubContent, hub, profile);

      if (frame % 30 == 0) {
        glfwSetWindowTitle(window, "Erebus Cradle | Hub — 1/2/3 gear, Tab mission, Enter/Space launch");
      }

      if (logStatePath && frame + 1 == maxFrames && maxFrames > 0) {
        FILE* f = std::fopen(logStatePath, "w");
        if (f) {
          std::fprintf(f, "{\"appState\":\"hub\",\"frame\":%d,\"chits\":%d,\"equippedWeapon\":\"%s\","
                          "\"equippedArmor\":\"%s\",\"equippedCosmetic\":\"%s\",\"selectedMission\":\"%s\"}\n",
                       frame + 1, profile.chits, profile.equippedWeapon.c_str(), profile.equippedArmor.c_str(),
                       profile.equippedCosmetic.c_str(), hub.selectedMission().c_str());
          std::fclose(f);
        }
      }
    } else {
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
      // forceFire used to hold the reload key too, which kept the weapon
      // permanently mid-reload and let it fire only a handful of rounds over
      // a whole run — fine for the one-hostile fixture it was written
      // against, useless for measuring whether a real wave is survivable.
      // Reload only when the magazine is actually empty.
      bool reloadHeld = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS ||
                        (forceFire && game.weapon().ammoInMag == 0);
      game.update(window, camera, dt, firePressed, reloadHeld, forceForward);

      renderer.renderFrame(game, camera, (float)now, dt);
      if (debugPixel && frame % 60 == 0) renderer.debugPrintCenterPixel();

      glfwGetFramebufferSize(window, &width, &height);
      const Weapon& w = game.weapon();
      Hud::State hs;
      hs.hp = game.player().hp;
      hs.maxHp = game.player().maxHp;
      hs.ammoInMag = w.ammoInMag;
      hs.magSize = w.magSize;
      hs.reserveAmmo = w.reserveAmmo;
      hs.reloading = w.reloading;
      hs.reloadFrac = w.reloading ? 1.0f - (w.reloadT / w.reloadTime) : 0.0f;
      hs.hitMarkerT = game.hitMarkerT;
      hs.damageFlashT = game.damageFlashT;
      hs.missionName = game.missionName();
      hs.waveFrac = game.waveProgress();
      hs.bossAlive = game.bossAlive();
      hs.bossHpFrac = game.bossHpFraction();
      hs.bossName = game.bossName();
      hs.missionComplete = game.missionState() == MissionState::Complete;
      hs.missionFailed = game.missionState() == MissionState::Failed;
      hs.commsSpeaker = game.commsSpeaker();
      hs.commsLine = game.commsLine();
      hs.commsAlpha = game.commsAlpha();
      hs.pickupNote = game.pickupNote();
      hs.pickupAlpha = game.pickupNoteAlpha();
      hs.accent = game.hudAccent();
      hud.draw(width, height, hs);

      if (debugPixel && frame % 60 == 0) {
        // The health bar's fill always occupies this pixel while HP > 0 —
        // if the HUD is drawing at all, this should read close to the
        // health colour, not the 3D scene behind it.
        // glReadPixels is bottom-left-origin, but Hud's bar coordinates are
        // top-left-origin (by = screenH - 54) — the bar sits near the
        // bottom of the screen either way, so this is just y=45 from the
        // bottom, not height-45 from the bottom.
        unsigned char hudPixel[4] = {0, 0, 0, 0};
        glReadPixels(40, 45, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hudPixel);
        std::fprintf(stderr, "[debug-pixel] healthBarPixel(0-255)=(%d,%d,%d)\n",
                     hudPixel[0], hudPixel[1], hudPixel[2]);
      }

      if (frame % 30 == 0) {
        const char* stateStr = game.missionState() == MissionState::Complete ? "COMPLETE"
                              : game.missionState() == MissionState::Failed ? "FAILED" : "active";
        char title[224];
        std::snprintf(title, sizeof(title),
                       "Erebus Cradle | %s | %.1fms | hp %.0f | ammo %d/%d | wave %.0f%% | %s | gfx:%s",
                       game.missionName().c_str(), dt * 1000.0f, game.player().hp,
                       w.ammoInMag, w.reserveAmmo, game.waveProgress() * 100.0f, stateStr,
                       renderer.qualityTierName());
        glfwSetWindowTitle(window, title);
      }

      if (logStatePath && frame + 1 == maxFrames && maxFrames > 0) {
        FILE* f = std::fopen(logStatePath, "w");
        if (f) {
          std::fprintf(f,
            "{\"appState\":\"mission\",\"frame\":%d,\"missionState\":\"%s\",\"playerHp\":%.2f,"
            "\"playerPos\":[%.2f,%.2f,%.2f],\"ammoInMag\":%d,\"reserveAmmo\":%d,\"waveProgress\":%.3f,"
            "\"bossAlive\":%s,\"chits\":%d}\n",
            frame + 1,
            game.missionState() == MissionState::Complete ? "complete"
              : game.missionState() == MissionState::Failed ? "failed" : "in_progress",
            game.player().hp, game.player().position.x, game.player().position.y, game.player().position.z,
            w.ammoInMag, w.reserveAmmo, game.waveProgress(), game.bossAlive() ? "true" : "false", profile.chits);
          std::fclose(f);
        }
      }

      // A mission that's ended (won or lost) waits here for the player to
      // commit to going back rather than snapping to the Hub the instant
      // the last hostile dies — same reason the old build left the
      // complete/fail HUD tint on screen instead of quitting outright.
      if (game.missionState() != MissionState::InProgress) {
        bool wantReturn = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
                           glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (wantReturn && !prevReturnKey) {
          ProfileStore::save(profile, savePath);
          game.destroy();
          hub.init(hubContent, profile);   // refresh: reward chits / new completion just landed
          state = AppState::Hub;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          mouseCaptured = false;
        }
        prevReturnKey = wantReturn;
      } else {
        prevReturnKey = false;
      }
    }
    frame++;

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

  ProfileStore::save(profile, savePath);
  if (gameEverStarted) game.destroy();
  hud.destroy();
  renderer.destroy();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
