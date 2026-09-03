// Erebus Cradle — native renderer
//
// A standalone C++/OpenGL rendering engine implementing the same
// Destiny-2-style cinematic pipeline as the browser build of this project:
// PBR armour with anisotropic reflections, layered vertex-blended terrain,
// three cascaded shadow maps off a low sun, volumetric fog, camera-relative
// dust motes, image-based lighting, ACES filmic tone mapping, bloom, and
// depth of field on aim. See ../docs/NATIVE_RENDERER.md for how each part
// maps to a rendering rule and where the code for it lives.
//
// This is a rendering-and-camera demo, not a port of the game's missions,
// AI, inventory, or netcode — see that doc for the honest scope line.
#include "Gl.h"
#include "Camera.h"
#include "Renderer.h"
#include "Scene.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct AppState {
  Camera camera;
  bool mouseCaptured = true;
  bool firstMouse = true;
  double lastX = 0.0, lastY = 0.0;
  bool aiming = false;
};

void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
  auto* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
  if (renderer && w > 0 && h > 0) renderer->resize(w, h);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc; (void)argv;

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
  glfwWindowHint(GLFW_SAMPLES, 0);  // we do our own post AA-adjacent work; MSAA would fight the HDR pipeline

  int width = 1280, height = 800;
  GLFWwindow* window = glfwCreateWindow(width, height, "Erebus Cradle — native renderer", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "glfwCreateWindow failed (no GL 4.1 core context available)\n");
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  glewExperimental = GL_TRUE;  // required for core-profile VAOs on some drivers
  GLenum glewStatus = glewInit();
  glGetError();  // glewInit() reliably leaves a spurious GL_INVALID_ENUM behind on core profiles
  if (glewStatus != GLEW_OK) {
    std::fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(glewStatus));
    return 1;
  }
  std::printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
  std::printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

  AppState app;
  Renderer renderer;
  Scene scene;

  glfwGetFramebufferSize(window, &width, &height);
  renderer.create(width, height);
  scene.build();

  glfwSetWindowUserPointer(window, &renderer);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwGetCursorPos(window, &app.lastX, &app.lastY);

  // ---------- headless verification ----------
  // Set EREBUS_DUMP_FRAME=<path.ppm> and EREBUS_MAX_FRAMES=<n> to render n
  // frames off-screen and dump the last one as a PPM, then exit — how this
  // was actually test-rendered under Xvfb + llvmpipe rather than taken on
  // faith. No dependency needed: PPM is trivial to write by hand.
  const char* dumpPath = std::getenv("EREBUS_DUMP_FRAME");
  int maxFrames = 0;
  if (const char* mf = std::getenv("EREBUS_MAX_FRAMES")) maxFrames = std::atoi(mf);

  double lastTime = glfwGetTime();
  int frame = 0;

  while (!glfwWindowShouldClose(window)) {
    double now = glfwGetTime();
    float dt = std::min(0.05, now - lastTime > 0 ? now - lastTime : 0.0);
    lastTime = now;

    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      if (app.mouseCaptured) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        app.mouseCaptured = false;
      }
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !app.mouseCaptured) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      app.mouseCaptured = true;
      app.firstMouse = true;
    }

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (app.mouseCaptured) {
      if (app.firstMouse) { app.lastX = mx; app.lastY = my; app.firstMouse = false; }
      app.camera.look((float)(mx - app.lastX), (float)(my - app.lastY), 0.09f);
    }
    app.lastX = mx; app.lastY = my;

    app.aiming = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    app.camera.update(window, dt, app.aiming);

    renderer.renderFrame(scene, app.camera, (float)now, dt);

    if (frame % 30 == 0) {
      char title[192];
      std::snprintf(title, sizeof(title),
                     "Erebus Cradle — native renderer | %.1f ms/frame | shadow draws/frame: %d | aim %.2f",
                     dt * 1000.0f, renderer.shadowDrawCalls, app.camera.aim);
      glfwSetWindowTitle(window, title);
    }
    frame++;

    if (dumpPath && maxFrames > 0 && frame >= maxFrames) {
      glfwGetFramebufferSize(window, &width, &height);
      std::vector<unsigned char> pixels(size_t(width) * height * 3);
      glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
      FILE* f = std::fopen(dumpPath, "wb");
      if (f) {
        std::fprintf(f, "P6\n%d %d\n255\n", width, height);
        // glReadPixels is bottom-up; PPM is top-down.
        for (int y = height - 1; y >= 0; y--) {
          std::fwrite(pixels.data() + size_t(y) * width * 3, 1, size_t(width) * 3, f);
        }
        std::fclose(f);
        std::printf("[dump] wrote %s (%dx%d) at frame %d\n", dumpPath, width, height, frame);
      } else {
        std::fprintf(stderr, "[dump] could not open %s for writing\n", dumpPath);
      }
      break;
    }

    glfwSwapBuffers(window);
  }

  scene.destroy();
  renderer.destroy();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
