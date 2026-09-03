#pragma once
#include "Gl.h"

// A free-fly FPS camera: WASD + mouse look, Shift to sprint, right mouse to
// aim (pulls the FOV in and hands the renderer a 0..1 blend it uses to drive
// depth of field, mirroring how the browser build's weapon ADS works).
class Camera {
public:
  glm::vec3 position{0.0f, 1.7f, 10.0f};
  float yaw = -90.0f;    // degrees, 0 looks down +X
  float pitch = -8.0f;
  float fovHip = 68.0f;
  float fovAds = 42.0f;
  float aim = 0.0f;      // smoothed 0..1, 1 = fully aimed

  void look(float dxPixels, float dyPixels, float sensitivity);
  void update(GLFWwindow* window, float dt, bool aiming);

  glm::vec3 forward() const;
  glm::vec3 right() const;
  glm::mat4 view() const;
  float fov() const { return glm::mix(fovHip, fovAds, aim); }
};
