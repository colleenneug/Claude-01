#include "CascadedShadowMap.h"
#include <algorithm>
#include <cmath>

void CascadedShadowMap::create(const std::array<int, CASCADES>& mapSizes) {
  mapSizes_ = mapSizes;
  for (int i = 0; i < CASCADES; i++) {
    glGenFramebuffers(1, &fbo_[i]);
    glGenTextures(1, &depth_[i]);
    glBindTexture(GL_TEXTURE_2D, depth_[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, mapSizes_[i], mapSizes_[i], 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // Outside the frustum reads as fully lit (depth 1 = the far plane),
    // rather than the map's edge smearing shadow across the horizon.
    float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_[i], 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      std::fprintf(stderr, "[CSM] cascade %d framebuffer incomplete: 0x%04x\n", i, status);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CascadedShadowMap::destroy() {
  for (int i = 0; i < CASCADES; i++) {
    if (depth_[i]) glDeleteTextures(1, &depth_[i]);
    if (fbo_[i]) glDeleteFramebuffers(1, &fbo_[i]);
  }
}

void CascadedShadowMap::beginCascade(int i) const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_[i]);
  glViewport(0, 0, mapSizes_[i], mapSizes_[i]);
  glClear(GL_DEPTH_BUFFER_BIT);
}

void CascadedShadowMap::update(const glm::vec3& camPos, const glm::vec3& camForward,
                                const glm::vec3& camUp, float fovYDeg, float aspect,
                                float nearClip, const glm::vec3& sunDirection) {
  const float farClip = 130.0f;  // beyond this the sun still lights, unshadowed

  // Practical split scheme: a blend of logarithmic (tight near texels) and
  // uniform (even far coverage) splits, same as the browser build.
  splits_[0] = nearClip;
  for (int i = 1; i < CASCADES; i++) {
    float p = (float)i / CASCADES;
    float logSplit = nearClip * std::pow(farClip / nearClip, p);
    float uniSplit = nearClip + (farClip - nearClip) * p;
    splits_[i] = lambda_ * logSplit + (1.0f - lambda_) * uniSplit;
  }
  splits_[CASCADES] = farClip;

  for (int i = 0; i < CASCADES; i++) {
    ranges_[i] = glm::vec2(i == 0 ? -1e4f : splits_[i], i == CASCADES - 1 ? 1e5f : splits_[i + 1]);
  }

  glm::vec3 dir = glm::normalize(sunDirection);
  glm::vec3 upHint = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  glm::vec3 camRight = glm::normalize(glm::cross(camForward, camUp));
  glm::vec3 realUp = glm::normalize(glm::cross(camRight, camForward));

  float tanHalfV = std::tan(glm::radians(fovYDeg) * 0.5f);
  float tanHalfH = tanHalfV * aspect;

  for (int i = 0; i < CASCADES; i++) {
    float nearD = splits_[i], farD = splits_[i + 1];
    glm::vec3 corners[8];
    int k = 0;
    for (float d : {nearD, farD}) {
      float h = tanHalfV * d, w = tanHalfH * d;
      for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
          corners[k++] = camPos + camForward * d + camRight * (float)sx * w + realUp * (float)sy * h;
        }
      }
    }

    // Fit a sphere rather than a box: it doesn't change size as the camera
    // rotates, so the map doesn't resize (and its texels don't shimmer).
    glm::vec3 centre(0.0f);
    for (auto& c : corners) centre += c;
    centre /= 8.0f;
    float radius = 0.0f;
    for (auto& c : corners) radius = std::max(radius, glm::length(c - centre));
    radius = std::ceil(radius * 8.0f) / 8.0f;  // quantise so it stops breathing

    // Snap the centre to this cascade's own texel grid, in light space.
    glm::mat4 lightRot = glm::lookAt(glm::vec3(0.0f), dir, upHint);
    float texelSize = (radius * 2.0f) / (float)mapSizes_[i];
    glm::vec3 centreLS = glm::vec3(lightRot * glm::vec4(centre, 1.0f));
    centreLS.x = std::floor(centreLS.x / texelSize) * texelSize;
    centreLS.y = std::floor(centreLS.y / texelSize) * texelSize;
    glm::mat4 invLightRot = glm::inverse(lightRot);
    centre = glm::vec3(invLightRot * glm::vec4(centreLS, 1.0f));

    glm::vec3 eye = centre - dir * (radius + backOff_);
    glm::mat4 view = glm::lookAt(eye, centre, upHint);
    glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 2.0f + backOff_ * 2.0f);
    viewProj_[i] = proj * view;
  }
}
