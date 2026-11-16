#include "PlatformInput.h"

#include "Gl.h"

namespace {

std::string g_typed;

void onChar(GLFWwindow*, unsigned int codepoint) {
  if (codepoint < 128 && g_typed.size() < 64) g_typed += (char)codepoint;
}

// Key -> GLFW key code, in Key's order.
constexpr int kGlfw[(size_t)Key::Count] = {
    GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D,
    GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
    GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL,
    GLFW_KEY_C, GLFW_KEY_E, GLFW_KEY_F, GLFW_KEY_G, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_O,
    GLFW_KEY_Q, GLFW_KEY_R, GLFW_KEY_X, GLFW_KEY_Y, GLFW_KEY_N,
    GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3,
    GLFW_KEY_TAB, GLFW_KEY_ENTER, GLFW_KEY_KP_ENTER, GLFW_KEY_ESCAPE, GLFW_KEY_BACKSPACE,
};

}  // namespace

void installTextCapture(GLFWwindow* window) { glfwSetCharCallback(window, onChar); }

void sampleInput(GLFWwindow* window, Input& out) {
  for (size_t k = 0; k < (size_t)Key::Count; k++) {
    out.down[k] = window && glfwGetKey(window, kGlfw[k]) == GLFW_PRESS;
  }
  out.typed.swap(g_typed);
  g_typed.clear();
  out.time = glfwGetTime();
}
