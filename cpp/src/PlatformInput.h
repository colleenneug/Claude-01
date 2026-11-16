#pragma once

#include "Input.h"

struct GLFWwindow;

// The one place GLFW key codes live. Fills `out` from the window's current
// key state, plus whatever text was typed since the last call.
void sampleInput(GLFWwindow* window, Input& out);
// Hooks up the text callback for Input::typed. Once, after the window exists.
void installTextCapture(GLFWwindow* window);
