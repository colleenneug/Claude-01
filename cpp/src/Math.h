#pragma once

// The math library, and nothing else. Gameplay includes this; only the
// renderer side (Gl.h) sees OpenGL. Moving to another engine keeps this file.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
