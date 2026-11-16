// The renderer side's include point: the GL loader and windowing, plus the
// math. Only rendering code (Renderer, Mesh.cpp, Hud.cpp, Shader, the
// post-processing passes) and the platform layer include this; gameplay
// includes Math.h.
// GLEW must be included before GLFW's own GL header pulls in gl.h, or the
// symbols collide.
#pragma once

#define GLEW_NO_GLU
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Math.h"

#include <cstdio>

// Call after any sequence of GL calls you want to be sure did not fail.
// Cheap enough to leave compiled in; this project favors catching a broken
// pipeline over shaving a few branches.
inline bool glCheck(const char* where) {
  GLenum err;
  bool ok = true;
  while ((err = glGetError()) != GL_NO_ERROR) {
    ok = false;
    std::fprintf(stderr, "[GL ERROR] 0x%04x at %s\n", err, where);
  }
  return ok;
}
