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
#include "Space.h"
#include "Station.h"
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

// An on/off environment hook. Empty counts as unset, not as set: the
// verification suite turns a flag off for one check by assigning it an empty
// string over the runner's default, and a plain getenv != nullptr test reads
// that as "still on" — which is exactly how the ground-site check ended up
// asserting against a run that had skipped the ground site.
static bool envFlag(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0';
}

enum class AppState { SlotSelect, CreateRecord, Space, Station, Hub, Mission };

// Where a new record starts: Earth, on Recovery Division's ground site.
// Named here rather than inferred from MissionDef::tutorial, because the
// question at record creation is "which mission do I launch", and a scan
// for the first tutorial-flagged file would pick an arbitrary one the day
// a second gets added.
constexpr const char* kTutorialMission = "tutorial_earth";

int main(int argc, char** argv) {
  // --mission <id> both names the mission EREBUS_SKIP_HUB boots straight
  // into and preselects it in the hub. The fallback is only for the
  // skip-hub case: with no argument the hub opens on the furthest sector of
  // the campaign you have actually reached (Hub::init), which is where you
  // left off, and preselecting a side patrol over that would be wrong.
  std::string missionId = "patrol_dust_shelf";
  bool missionIdGiven = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--mission") == 0 && i + 1 < argc) {
      missionId = argv[++i];
      missionIdGiven = true;
    }
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
  Space space;

  Station station;
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

  // Save slots. Three records, each its own file next to the executable;
  // the slot select screen runs before the hub and picks which one this
  // session is playing. EREBUS_SAVE_PATH still points at one explicit file
  // and skips slot selection entirely, which is what every headless test
  // uses so none of them have to drive a menu; EREBUS_SLOT=<1-3> picks a
  // slot headlessly instead.
  const char* savePathEnv = std::getenv("EREBUS_SAVE_PATH");
  const char* slotEnv = std::getenv("EREBUS_SLOT");
  std::string slotPaths[3] = {"save1.dat", "save2.dat", "save3.dat"};
  int slotIndex = 0;
  bool slotChosen = false;

  std::string savePath;
  if (savePathEnv) {
    savePath = savePathEnv;
    slotChosen = true;
  } else if (slotEnv) {
    slotIndex = std::clamp(std::atoi(slotEnv) - 1, 0, 2);
    savePath = slotPaths[slotIndex];
    slotChosen = true;
  } else {
    savePath = slotPaths[0];
  }
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
  if (missionIdGiven) hub.preselectMission(missionId);

  bool spaceReady = space.init(hubContent);
  // The Cradle you walk around in. Built once: its geometry never changes,
  // and rebuilding it every time you dock would throw away and re-upload
  // several hundred boxes for nothing.
  station.init();

  // EREBUS_SKIP_HUB=1 boots straight into --mission with whatever's
  // currently equipped, bypassing the Hub entirely — kept for every
  // headless verification flow that predates the Hub and still expects to
  // land in a mission on frame 0. EREBUS_SKIP_SPACE=1 goes to the hub menu
  // instead of open space, for the hub-script tests that predate flight.
  bool skipHub = envFlag("EREBUS_SKIP_HUB");
  bool skipSpace = envFlag("EREBUS_SKIP_SPACE") || !spaceReady;
  // EREBUS_SKIP_TUTORIAL=1 sends a brand-new record straight up instead of
  // to the ground site. Every headless check starts from a fresh save, so
  // without this every one of them would begin in the tutorial.
  bool skipTutorial = envFlag("EREBUS_SKIP_TUTORIAL");
  AppState afterSlot = skipSpace ? AppState::Hub : AppState::Space;
  AppState state = skipHub      ? AppState::Mission
                   : slotChosen ? afterSlot
                                : AppState::SlotSelect;

  // The doctrines, for the record-creation screen. Sorted (Content::classIds)
  // so the three columns are in the same order every run.
  std::vector<std::string> classIds = hubContent.classIds();
  int classIndex = 0;

  // EREBUS_CLASS=<id> settles the doctrine without the creation screen, the
  // same idea as EREBUS_SLOT for the record itself: a headless run has no way
  // to press a key on a screen whose whole job is to ask a question.
  const char* classEnv = std::getenv("EREBUS_CLASS");
  if (classEnv && *classEnv) {
    if (const ClassDef* c = hubContent.playerClass(classEnv)) {
      profile.classId = c->id;
      profile.ensureStarterGear(c->weaponId);
      profile.equippedWeapon = c->weaponId;
    } else {
      std::fprintf(stderr, "[main] EREBUS_CLASS='%s' is not in content/classes, ignored\n",
                   classEnv);
    }
  }

  // A record with no doctrine has not actually been created yet, however it
  // was reached — so even a scripted EREBUS_SLOT run stops here and asks,
  // rather than starting a campaign with no weapon, no ability and no perk.
  if (slotChosen && profile.classId.empty() && !classIds.empty() && !skipHub) {
    state = AppState::CreateRecord;
  }

  // Which body the ship is parked at, so a finished mission returns you to
  // the world you launched from rather than to the origin.
  std::string lastBodyId;
  bool gameEverStarted = false;
  // Whether the current mission was launched from the Cradle's flight deck
  // rather than by landing on a world, so ending it returns you to the one
  // you actually left from.
  bool launchedFromStation = false;
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
  bool forceForward = envFlag("EREBUS_FORCE_FORWARD");
  bool forceFire = envFlag("EREBUS_FORCE_FIRE");
  bool debugAutoaim = envFlag("EREBUS_DEBUG_AUTOAIM");
  // EREBUS_TUTORIAL_AUTO=1 walks the ground site's steps by feeding each
  // one the input it is asking for. Verification aid only.
  bool tutorialAuto = envFlag("EREBUS_TUTORIAL_AUTO");
  // Prints the screen-centre pixel, before and after tone mapping, every
  // 60 frames — see Renderer::debugPrintCenterPixel for why: it turns "the
  // screen looks dark/black" from a description into a number, so hardware
  // this project was never tested on doesn't have to be debugged by guessing.
  bool debugPixel = envFlag("EREBUS_DEBUG_PIXEL");
  // Flight aids, the space-mode counterparts of EREBUS_DEBUG_AUTOAIM:
  // EREBUS_SPACE_AUTOPILOT=<planet id> steers the ship at that body every
  // frame, and EREBUS_FORCE_ENGAGE=1 presses E the moment it's in range, so
  // a headless run can prove the whole fly-there-and-land path end to end.
  const char* spaceAutopilot = std::getenv("EREBUS_SPACE_AUTOPILOT");
  // EREBUS_STATION_AT="x,y,z" and EREBUS_STATION_YAW=<degrees> drop the
  // player at a spot inside the Cradle on arrival, so a headless run can
  // stand at the foot of a stair flight or in the cupola rather than only
  // walking the spine in a straight line.
  const char* stationAt = std::getenv("EREBUS_STATION_AT");
  float stationYaw = 90.0f;
  if (const char* sy = std::getenv("EREBUS_STATION_YAW")) stationYaw = (float)std::atof(sy);
  bool forceEngage = envFlag("EREBUS_FORCE_ENGAGE");
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

  // Slot select screen state: which slot the caret is on, whether a delete
  // is awaiting confirmation, the per-slot summaries shown on each row, and
  // the key-edge flags that keep a held key from firing every frame.
  int deletePending = -1;
  bool slotsDirty = true;
  Hud::SlotSummary slotSummaries[3];
  bool p1 = false, p2 = false, p3 = false, pUp = false, pDown = false;
  bool pEnter = false, pSpace = false, pDel = false, pY = false, pN = false;
  bool prevEngageKey = false, prevUndockKey = false, prevAbilityKey = false;

  // Everything that has to happen once a record is settled on, whether it
  // came out of an existing slot or was just created. Lives here rather than
  // being written twice, because the two paths diverging is exactly how a
  // freshly created record would end up in space with no gear.
  auto enterAfterSlot = [&]() {
    hub.init(hubContent, profile);
    if (missionIdGiven) hub.preselectMission(missionId);

    // A record that has never finished anything starts on Earth, on the
    // ground site, and is walked through its kit. The test is "has cleared
    // nothing at all" rather than "has not cleared the ground site", so a
    // save made before the tutorial existed is not sent back to school.
    if (!skipSpace && !skipHub && !skipTutorial && profile.completedMissions.empty() &&
        hubContent.mission(kTutorialMission) &&
        game.init(contentDir, kTutorialMission, profile)) {
      gameEverStarted = true;
      launchedFromStation = false;
      camera.position = game.player().eyePosition();
      state = AppState::Mission;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      mouseCaptured = true;
      firstMouse = true;
      return;
    }

    state = afterSlot;
    if (state == AppState::Space) {
      space.placeNear("");   // start docked off the Cradle
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      mouseCaptured = true;
      firstMouse = true;
    }
  };

  // Startup may already have settled on a record (EREBUS_SLOT, or
  // EREBUS_SAVE_PATH). Run it through the same entry path the slot screen
  // uses rather than a second copy that would quietly skip the ground site.
  if (state == afterSlot && !skipHub) enterAfterSlot();

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

    if (state == AppState::SlotSelect) {
      auto edge = [&](int key, bool& prev) {
        bool down = glfwGetKey(window, key) == GLFW_PRESS;
        bool fired = down && !prev;
        prev = down;
        return fired;
      };

      if (deletePending >= 0) {
        // A destructive action gets its own confirm step — one stray key
        // press should never wipe a record someone has been playing.
        if (edge(GLFW_KEY_Y, pY)) {
          ProfileStore::erase(slotPaths[deletePending]);
          deletePending = -1;
          slotsDirty = true;
        } else if (edge(GLFW_KEY_N, pN) || edge(GLFW_KEY_ESCAPE, pDel)) {
          deletePending = -1;
        }
      } else {
        if (edge(GLFW_KEY_1, p1)) slotIndex = 0;
        if (edge(GLFW_KEY_2, p2)) slotIndex = 1;
        if (edge(GLFW_KEY_3, p3)) slotIndex = 2;
        if (edge(GLFW_KEY_UP, pUp)) slotIndex = (slotIndex + 2) % 3;
        if (edge(GLFW_KEY_DOWN, pDown)) slotIndex = (slotIndex + 1) % 3;
        if (edge(GLFW_KEY_D, pDel)) deletePending = slotIndex;
        if (edge(GLFW_KEY_ENTER, pEnter) || edge(GLFW_KEY_SPACE, pSpace)) {
          savePath = slotPaths[slotIndex];
          bool fresh = !ProfileStore::exists(savePath);
          profile = ProfileStore::load(savePath);
          // A record with no doctrine is a record that has not been created
          // yet — an empty slot, or a save written before doctrines existed
          // and now owed the choice it never got.
          if ((fresh || profile.classId.empty()) && !classIds.empty()) {
            classIndex = 0;
            state = AppState::CreateRecord;
          } else {
            enterAfterSlot();
          }
        }
      }

      if (slotsDirty) {
        for (int i = 0; i < 3; i++) {
          Hud::SlotSummary& sum = slotSummaries[i];
          sum.used = ProfileStore::exists(slotPaths[i]);
          if (sum.used) {
            Profile p = ProfileStore::load(slotPaths[i]);
            sum.chits = p.chits;
            sum.missionsCleared = (int)p.completedMissions.size();
            const WeaponDef* wd = hubContent.weapon(p.equippedWeapon);
            sum.weaponName = wd ? wd->name : p.equippedWeapon;
          } else {
            sum = Hud::SlotSummary{};
          }
        }
        slotsDirty = false;
      }

      glClearColor(0.03f, 0.035f, 0.05f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glfwGetFramebufferSize(window, &width, &height);
      hud.drawSlotSelect(width, height, slotSummaries, slotIndex, deletePending);

      if (frame % 30 == 0) {
        glfwSetWindowTitle(window, "Erebus Cradle | Select a record");
      }
    } else if (state == AppState::CreateRecord) {
      auto edge = [&](int key, bool& prev) {
        bool down = glfwGetKey(window, key) == GLFW_PRESS;
        bool fired = down && !prev;
        prev = down;
        return fired;
      };
      const int n = (int)classIds.size();
      if (edge(GLFW_KEY_1, p1) && n > 0) classIndex = 0;
      if (edge(GLFW_KEY_2, p2) && n > 1) classIndex = 1;
      if (edge(GLFW_KEY_3, p3) && n > 2) classIndex = 2;
      if (edge(GLFW_KEY_LEFT, pUp) && n > 0) classIndex = (classIndex + n - 1) % n;
      if (edge(GLFW_KEY_RIGHT, pDown) && n > 0) classIndex = (classIndex + 1) % n;
      if (edge(GLFW_KEY_ESCAPE, pDel)) {
        state = AppState::SlotSelect;
        slotsDirty = true;
      } else if ((edge(GLFW_KEY_ENTER, pEnter) || edge(GLFW_KEY_SPACE, pSpace)) && n > 0) {
        profile.classId = classIds[classIndex];
        const ClassDef* chosen = hubContent.playerClass(profile.classId);
        // The doctrine's weapon is issued with the record, not bought.
        profile.ensureStarterGear(chosen ? chosen->weaponId : std::string());
        if (chosen) profile.equippedWeapon = chosen->weaponId;
        ProfileStore::save(profile, savePath);
        enterAfterSlot();
      }

      glClearColor(0.03f, 0.035f, 0.05f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glfwGetFramebufferSize(window, &width, &height);
      hud.drawCreate(width, height, hubContent, classIds, classIndex);

      if (frame % 30 == 0) {
        glfwSetWindowTitle(window, "Erebus Cradle | Select a doctrine");
      }

      if (logStatePath && frame + 1 == maxFrames && maxFrames > 0) {
        FILE* f = std::fopen(logStatePath, "w");
        if (f) {
          std::fprintf(f, "{\"appState\":\"create\",\"frame\":%d,\"selectedClass\":\"%s\"}\n",
                       frame + 1,
                       classIds.empty() ? "" : classIds[classIndex].c_str());
          std::fclose(f);
        }
      }
    } else if (state == AppState::Space) {
      // Headless flight aids, same idea as EREBUS_DEBUG_AUTOAIM for combat:
      // with no mouse there is no way to steer, so a run can't otherwise
      // prove that flying to a world and landing on it works at all.
      if (spaceAutopilot) {
        for (const Space::Body& b : space.bodies()) {
          if (b.id != spaceAutopilot) continue;
          glm::vec3 d = b.pos - space.shipPosition();
          if (glm::length(d) > 1e-3f) {
            d = glm::normalize(d);
            camera.yaw = glm::degrees(std::atan2(d.z, d.x));
            camera.pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
          }
          break;
        }
      }

      bool boost = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
      space.update(window, camera, dt, boost, forceForward);

      // E engages whatever you're close to: a world drops you into its
      // mission, the Cradle opens the hub.
      // The scripted press waits for the autopilot's own target: the ship
      // starts parked inside the Cradle's dock range, so "engage whatever
      // is nearest" would dock again on frame one and never fly anywhere.
      const Space::Body* engageable = space.engageTarget();
      bool scriptedEngage = forceEngage && engageable &&
                            (!spaceAutopilot || engageable->id == spaceAutopilot);
      bool eDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS || scriptedEngage;
      bool ePressed = eDown && !prevEngageKey;
      prevEngageKey = eDown;

      if (ePressed && engageable) {
        const Space::Body* target = engageable;
        if (target->isStation) {
          // Docking puts you inside the Cradle on foot. The hub's screens are
          // still there — they are what the terminals in it open.
          if (skipSpace) {
            state = AppState::Hub;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            mouseCaptured = false;
          } else {
            station.enter(camera);
            if (stationAt) {
              float ax = 0.0f, ay = 0.0f, az = 0.0f;
              if (std::sscanf(stationAt, "%f,%f,%f", &ax, &ay, &az) == 3) {
                station.placeAt(camera, glm::vec3(ax, ay, az), stationYaw);
              }
            }
            state = AppState::Station;
            // The contextual-action edge does not carry across a mode switch:
            // docking set it, and leaving it set meant the E that got you in
            // here was still "down" inside, so the first press at a terminal
            // never registered as a press at all. Re-read the real key so a
            // player still holding E does not immediately use something.
            prevEngageKey = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouseCaptured = true;
            firstMouse = true;
          }
        } else if (!target->missionId.empty()) {
          if (game.init(contentDir, target->missionId, profile)) {
            gameEverStarted = true;
            launchedFromStation = false;   // you dropped from orbit, not off the flight deck
            lastBodyId = target->id;
            camera.position = game.player().eyePosition();
            state = AppState::Mission;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouseCaptured = true;
            firstMouse = true;
          }
        }
      }

      renderer.renderFrame(space, camera, (float)now, dt);

      glfwGetFramebufferSize(window, &width, &height);
      Hud::SpaceState ss;
      if (const Space::Body* near = space.nearestBody() >= 0
                                        ? &space.bodies()[space.nearestBody()]
                                        : nullptr) {
        ss.nearestName = near->name;
        ss.isStation = near->isStation;
        ss.missionCleared = !near->missionId.empty() && profile.hasCompleted(near->missionId);
      }
      ss.nearestDistance = space.nearestDistance();
      ss.inRange = space.inEngageRange();
      ss.speed = space.speed();
      ss.maxSpeed = space.maxSpeed();
      if (const CosmeticDef* cd = hubContent.cosmetic(profile.equippedCosmetic)) ss.accent = cd->accent;
      hud.drawSpace(width, height, ss);

      if (frame % 30 == 0) {
        char title[224];
        std::snprintf(title, sizeof(title), "Erebus Cradle | Open space | %.0f u/s | %s",
                      space.speed(), ss.nearestName.c_str());
        glfwSetWindowTitle(window, title);
      }

      if (logStatePath && frame + 1 == maxFrames && maxFrames > 0) {
        FILE* f = std::fopen(logStatePath, "w");
        if (f) {
          glm::vec3 p = space.shipPosition();
          std::fprintf(f,
                       "{\"appState\":\"space\",\"frame\":%d,\"shipPos\":[%.1f,%.1f,%.1f],"
                       "\"speed\":%.1f,\"nearest\":\"%s\",\"nearestDistance\":%.1f,\"inRange\":%s}\n",
                       frame + 1, p.x, p.y, p.z, space.speed(), ss.nearestName.c_str(),
                       ss.nearestDistance, ss.inRange ? "true" : "false");
          std::fclose(f);
        }
      }
    } else if (state == AppState::Station) {
      ScriptedInput stationInput;
      stationInput.forward = forceForward;
      station.update(window, camera, dt, stationInput);
      renderer.renderFrame(station, camera, (float)now, dt);

      const Station::Terminal* term = station.nearestTerminal();
      // E uses whatever is in reach. The scripted press (EREBUS_FORCE_ENGAGE,
      // shared with docking) lets a headless run walk the concourse and open
      // the flight deck without a keyboard.
      bool eDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS || (forceEngage && term);
      bool ePressed = eDown && !prevEngageKey;
      prevEngageKey = eDown;
      bool qDown = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
      bool qPressed = qDown && !prevUndockKey;
      prevUndockKey = qDown;

      if ((ePressed && term && term->id == "airlock") || qPressed) {
        ProfileStore::save(profile, savePath);
        space.placeNear("");
        state = AppState::Space;
        prevEngageKey = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
      } else if (ePressed && term) {
        // The armoury and the flight deck both open the hub screen: this
        // build has one, listing gear and the route together. Splitting it in
        // two to match the browser's separate screens would be two screens
        // saying what one says.
        hub.init(hubContent, profile);
        if (missionIdGiven) hub.preselectMission(missionId);
        launchedFromStation = true;
        state = AppState::Hub;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        mouseCaptured = false;
      }

      glfwGetFramebufferSize(window, &width, &height);
      Hud::StationState ss;
      float py = station.player().position.y;
      ss.deck = py > 10.5f   ? "DECK C - UPPER RING AND THE CUPOLA"
                : py > 3.5f  ? "DECK B - GALLERY, COLUMBUS AND KIBO"
                             : "DECK A - CONCOURSE, ARRIVALS AND THE AIRLOCK";
      if (term) {
        ss.terminalName = term->name;
        ss.terminalLine = term->line;
        ss.terminalColour = term->colour;
      }
      hud.drawStation(width, height, ss);

      if (frame % 30 == 0) {
        glfwSetWindowTitle(window, "Erebus Cradle | The Cradle");
      }

      if (logStatePath && frame + 1 == maxFrames && maxFrames > 0) {
        FILE* f = std::fopen(logStatePath, "w");
        if (f) {
          glm::vec3 p = station.player().position;
          std::fprintf(f,
                       "{\"appState\":\"station\",\"frame\":%d,\"pos\":[%.1f,%.1f,%.1f],"
                       "\"terminal\":\"%s\"}\n",
                       frame + 1, p.x, p.y, p.z, term ? term->id.c_str() : "");
          std::fclose(f);
        }
      }
    } else if (state == AppState::Hub) {
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

      // Undock: back out to the ship without launching anything. Saves
      // first, since the hub is where gear gets bought.
      if (!skipSpace) {
        bool undockDown = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
        if (undockDown && !prevUndockKey) {
          ProfileStore::save(profile, savePath);
          // Back to the terminal you were standing at, not straight out to
          // the ship — you walked in here, so you walk out.
          state = AppState::Station;
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          mouseCaptured = true;
          firstMouse = true;
        }
        prevUndockKey = undockDown;
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
        // Verification aid: point the camera at a live hostile so firing can
        // be exercised without a real mouse. Prefers the nearest one it can
        // actually see: now that arenas carry pillars and barricades, aiming
        // at the nearest hostile regardless of what is in front of it means a
        // whole run can be spent shooting a wall — which reads as the mission
        // being unwinnable when it is only the aid being blind.
        glm::vec3 eye = camera.position;
        float bestVisible = 1e9f, bestAny = 1e9f;
        glm::vec3 dirVisible(0, 0, -1), dirAny(0, 0, -1);
        for (const Hostile& h : game.hostiles()) {
          if (!h.blocksShots()) continue;
          glm::vec3 target = h.headCentre();
          float d = glm::length(target - eye);
          if (d < bestAny) { bestAny = d; dirAny = glm::normalize(target - eye); }
          if (d < bestVisible && game.level().lineOfSight(eye, target)) {
            bestVisible = d;
            dirVisible = glm::normalize(target - eye);
          }
        }
        glm::vec3 bestDir = bestVisible < 1e8f ? dirVisible : dirAny;
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
      // The tutorial driver: a headless run has no keyboard, so on each step
      // it substitutes exactly the input that step is asking for. The same
      // idea as EREBUS_DEBUG_AUTOAIM — it exists so the sequence can be
      // proved end to end, and it is never on by default.
      ScriptedInput playerInput;
      playerInput.forward = forceForward || tutorialAuto;
      bool autoAbility = false;
      bool autoReload = false;
      if (tutorialAuto) {
        switch (game.tutorialStep()) {
          case Game::TutorialStep::Sprint: playerInput.sprint = true; break;
          case Game::TutorialStep::Jump:   playerInput.jump = true; break;
          case Game::TutorialStep::Slide:
            // A slide needs speed first, so hold sprint *and* crouch: crouch
            // outranks sprint in Player::update, but the speed you arrive
            // with is what the slide kicks off.
            playerInput.sprint = true;
            playerInput.crouch = game.player().planarSpeed() > 6.0f;
            break;
          case Game::TutorialStep::Reload:  autoReload = true; break;
          case Game::TutorialStep::Ability: autoAbility = true; break;
          default: break;
        }
      }
      if (autoReload) reloadHeld = true;
      game.update(window, camera, dt, firePressed, reloadHeld, playerInput);

      // The field ability is on Q or E, the same two keys the browser build
      // accepts (src/js/fps/game.js). On the press, not the hold.
      bool abilityDown = autoAbility ||
                         glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
      if (abilityDown && !prevAbilityKey) game.useAbility();
      prevAbilityKey = abilityDown;

      renderer.renderFrame(game, camera, (float)now, dt);
      if (debugPixel && frame % 60 == 0) renderer.debugPrintCenterPixel();

      glfwGetFramebufferSize(window, &width, &height);
      const Weapon& w = game.weapon();
      Hud::State hs;
      hs.hp = game.player().hp;
      hs.abilityReady = game.abilityReady();
      hs.weaponName = game.weaponName();
      hs.tutorialPrompt = game.tutorialPrompt();
      hs.tutorialHint = game.tutorialHint();
      hs.tutorialProgress = game.tutorialProgress();
      hs.overshield = game.player().overshield;
      hs.overshieldMax = game.player().overshieldMax;
      if (const ClassDef* pc = game.playerClass()) {
        hs.abilityName = pc->abilityName;
        hs.className = pc->name + " - " + pc->role;
      }
      hs.abilityFrac = game.abilityCooldownMax() > 0.0f
                     ? 1.0f - game.abilityCooldown() / game.abilityCooldownMax()
                     : 1.0f;
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
            "\"maxHp\":%.2f,\"class\":\"%s\",\"weapon\":\"%s\",\"magSize\":%d,"
            "\"playerPos\":[%.2f,%.2f,%.2f],\"ammoInMag\":%d,\"reserveAmmo\":%d,\"waveProgress\":%.3f,"
            "\"bossAlive\":%s,\"chits\":%d}\n",
            frame + 1,
            game.missionState() == MissionState::Complete ? "complete"
              : game.missionState() == MissionState::Failed ? "failed" : "in_progress",
            game.player().hp, game.player().maxHp,
            game.playerClass() ? game.playerClass()->id.c_str() : "",
            game.weaponName().c_str(), w.magSize,
            game.player().position.x, game.player().position.y, game.player().position.z,
            w.ammoInMag, w.reserveAmmo, game.waveProgress(), game.bossAlive() ? "true" : "false", profile.chits);
          std::fclose(f);
        }
      }

      // A mission that's ended (won or lost) waits here for the player to
      // commit to going back rather than snapping to the Hub the instant
      // the last hostile dies — same reason the old build left the
      // complete/fail HUD tint on screen instead of quitting outright.
      if (game.missionState() != MissionState::InProgress) {
        // forceEngage is the scripted "press the contextual action key", so
        // it covers this confirm too — otherwise a headless run can prove
        // you can fly out and land but never that you get back to the ship.
        bool wantReturn = forceEngage ||
                          glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
                          glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (wantReturn && !prevReturnKey) {
          bool justFinishedTutorial = game.isTutorial() &&
                                      game.missionState() == MissionState::Complete;
          ProfileStore::save(profile, savePath);
          game.destroy();
          hub.init(hubContent, profile);   // refresh: reward chits / new completion just landed
          if (skipSpace) {
            state = AppState::Hub;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            mouseCaptured = false;
          } else if (launchedFromStation || justFinishedTutorial) {
            // You launched off the flight deck, so you come back to it — and
            // finishing the ground site ships you up, which is the same
            // arrival.
            station.enter(camera);
            state = AppState::Station;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouseCaptured = true;
            firstMouse = true;
          } else {
            // Back to the ship, parked at the world you dropped from, so
            // leaving a mission puts you where you were rather than at the
            // origin with no idea which way you came.
            space.placeNear(lastBodyId);
            state = AppState::Space;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouseCaptured = true;
            firstMouse = true;
          }
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
