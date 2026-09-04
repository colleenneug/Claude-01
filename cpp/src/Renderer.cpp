#include "Renderer.h"
#include <algorithm>

void Renderer::create(int width, int height) {
  width_ = width; height_ = height;

  depthShader_.load("shaders/depth.vert", "shaders/depth.frag");
  pbrShader_.load("shaders/pbr.vert", "shaders/pbr.frag");
  motesShader_.load("shaders/motes.vert", "shaders/motes.frag");
  compositeShader_.load("shaders/fullscreen.vert", "shaders/composite.frag");
  dofShader_.load("shaders/fullscreen.vert", "shaders/dof.frag");

  sceneHdr_.create(width_, height_, /*hdr=*/true, /*depthTexture=*/true);
  dofBuffer_.create(std::max(1, width_ / 2), std::max(1, height_ / 2), true, false);

  csm_.create({2048, 2048, 1024});
  bloom_.create(width_, height_);
  ibl_.build(128);

  glGenVertexArrays(1, &fsTriVao_);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
}

void Renderer::resize(int width, int height) {
  width_ = width; height_ = height;
  sceneHdr_.resize(width_, height_);
  dofBuffer_.resize(std::max(1, width_ / 2), std::max(1, height_ / 2));
  bloom_.resize(width_, height_);
}

void Renderer::destroy() {
  sceneHdr_.destroy();
  dofBuffer_.destroy();
  csm_.destroy();
  bloom_.destroy();
  ibl_.destroy();
  if (fsTriVao_) glDeleteVertexArrays(1, &fsTriVao_);
}

// ------------------------------------------------------------- shadow pass

void Renderer::renderShadowCascades(const Game&, const Camera&) {
  depthShader_.use();
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.5f, 4.0f);
  shadowDrawCalls = 0;

  for (int i = 0; i < CascadedShadowMap::CASCADES; i++) {
    csm_.beginCascade(i);
    depthShader_.set("uLightViewProj", csm_.viewProj(i));
    for (const auto& item : drawList_) {
      if (!item.castShadow) continue;
      depthShader_.set("uModel", item.model);
      item.mesh->draw();
      shadowDrawCalls++;
    }
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
}

// -------------------------------------------------------------- scene pass

void Renderer::renderSceneToHdr(const Game& scene, const Camera& camera) {
  sceneHdr_.bind();
  glClearColor(0.02f, 0.018f, 0.03f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glm::mat4 view = camera.view();
  glm::mat4 proj = glm::perspective(glm::radians(camera.fov()), (float)width_ / height_, 0.05f, 500.0f);

  pbrShader_.use();
  pbrShader_.set("uView", view);
  pbrShader_.set("uProj", proj);
  pbrShader_.set("uCamPos", camera.position);

  pbrShader_.set("uSunDir", scene.sunDirection);
  pbrShader_.set("uSunColour", scene.sunColour);
  pbrShader_.set("uSunIntensity", scene.sunIntensityLux);

  for (int i = 0; i < CascadedShadowMap::CASCADES; i++) {
    std::string p = "uCascade[" + std::to_string(i) + "].";
    pbrShader_.set(p + "viewProj", csm_.viewProj(i));
    pbrShader_.set(p + "range", csm_.range(i));
    glActiveTexture(GL_TEXTURE1 + i);
    glBindTexture(GL_TEXTURE_2D, csm_.depthTexture(i));
    pbrShader_.set("uCascadeMap" + std::to_string(i), 1 + i);
  }
  pbrShader_.set("uCascadeFade", csm_.fadeMetres());

  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_CUBE_MAP, ibl_.cubemap());
  pbrShader_.set("uIrradianceMap", 4);
  pbrShader_.set("uIblMaxMip", (float)ibl_.maxMipLevel());

  for (const auto& item : drawList_) {
    pbrShader_.set("uModel", item.model);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(item.model)));
    pbrShader_.set("uNormalMatrix", normalMat);
    pbrShader_.set("uMaterial", (int)item.material);
    pbrShader_.set("uTint", item.tint);
    pbrShader_.set("uMetallic", item.metallic);
    pbrShader_.set("uRoughness", item.roughness);
    pbrShader_.set("uWear", item.wear);
    pbrShader_.set("uEmissive", item.emissive);
    pbrShader_.set("uEmissiveIntensity", item.emissiveIntensity);
    pbrShader_.set("uAniso", item.anisoStrength);
    item.mesh->draw();
  }
  glCheck("Renderer::renderSceneToHdr");
}

// ------------------------------------------------------------------ motes

void Renderer::renderMotes(const Game& scene, const Camera& camera, float time) {
  sceneHdr_.bind();  // still bound from the scene pass, but explicit is cheap
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive
  glEnable(GL_PROGRAM_POINT_SIZE);

  motesShader_.use();
  motesShader_.set("uView", camera.view());
  motesShader_.set("uProj", glm::perspective(glm::radians(camera.fov()), (float)width_ / height_, 0.05f, 500.0f));
  motesShader_.set("uCamPos", camera.position);
  motesShader_.set("uBox", scene.moteBoxSize());
  motesShader_.set("uTime", time);
  motesShader_.set("uSize", 26.0f);
  motesShader_.set("uSunDir", scene.sunDirection);
  motesShader_.set("uColour", glm::vec3(0.75f, 0.85f, 0.95f));
  motesShader_.set("uOpacity", 0.22f);

  glBindVertexArray(scene.moteVao());
  glDrawArrays(GL_POINTS, 0, scene.moteCount());

  glDisable(GL_PROGRAM_POINT_SIZE);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
}

// -------------------------------------------------------------------- DoF

void Renderer::renderDof() {
  dofBuffer_.bind();
  dofShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.colorTexture());
  dofShader_.set("tScene", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  dofShader_.set("tDepth", 1);
  dofShader_.set("uTexel", glm::vec2(1.0f / dofBuffer_.width(), 1.0f / dofBuffer_.height()));
  dofShader_.set("uNear", 0.05f);
  dofShader_.set("uFar", 500.0f);
  dofShader_.set("uFocus", 16.0f);
  dofShader_.set("uRange", 60.0f);
  dofShader_.set("uMaxRadius", 4.0f);

  glBindVertexArray(fsTriVao_);
  glDisable(GL_DEPTH_TEST);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
}

// -------------------------------------------------------------- composite

void Renderer::renderComposite(const Camera& camera, const Game& scene, GLuint bloomTex, float time) {
  // bloomTex was produced by a separate chain of shader programs (Bloom
  // owns its own bright/downsample/upsample passes); the composite shader
  // is bound here, after all of that has finished, so every uniform set
  // below actually lands on the program that ends up drawing.
  Framebuffer::bindScreen(width_, height_);
  glDisable(GL_DEPTH_TEST);
  compositeShader_.use();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.colorTexture());
  compositeShader_.set("tScene", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  compositeShader_.set("tDepth", 1);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, bloomTex);
  compositeShader_.set("tBloom", 2);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, dofBuffer_.colorTexture());
  compositeShader_.set("tDof", 3);

  glm::mat4 view = camera.view();
  glm::mat4 proj = glm::perspective(glm::radians(camera.fov()), (float)width_ / height_, 0.05f, 500.0f);
  compositeShader_.set("uInvViewProj", glm::inverse(proj * view));
  compositeShader_.set("uCamPos", camera.position);
  compositeShader_.set("uNear", 0.05f);
  compositeShader_.set("uFar", 500.0f);

  compositeShader_.set("uAim", camera.aim);
  compositeShader_.set("uDofFocus", 16.0f);
  compositeShader_.set("uDofRange", 60.0f);

  // Aerial-perspective fog, not a corridor haze: thin per metre, adding up
  // to a visible fade over the scene's ~40m scale.
  compositeShader_.set("uFogDensity", 0.018f);
  compositeShader_.set("uFogFalloff", 0.05f);
  compositeShader_.set("uFogBase", -1.0f);
  compositeShader_.set("uFogColour", glm::vec3(0.42f, 0.30f, 0.34f));
  compositeShader_.set("uInscatter", 0.9f);

  compositeShader_.set("uSunDir", scene.sunDirection);
  compositeShader_.set("uSunColour", scene.sunColour);
  compositeShader_.set("uExposure", 1.15f);
  compositeShader_.set("uGrain", 0.03f);
  compositeShader_.set("uVignette", 0.65f);
  compositeShader_.set("uAberration", 1.0f);
  compositeShader_.set("uBloom", 0.32f);
  compositeShader_.set("uTime", time);

  glBindVertexArray(fsTriVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glCheck("Renderer::renderComposite");
}

// --------------------------------------------------------------- top level

void Renderer::renderFrame(const Game& scene, const Camera& camera, float time, float dt) {
  (void)dt;
  drawList_.clear();
  scene.collect(time, drawList_);

  glm::vec3 fwd = camera.forward();
  csm_.update(camera.position, fwd, glm::vec3(0, 1, 0), camera.fov(),
              (float)width_ / height_, 0.05f, scene.sunDirection);

  renderShadowCascades(scene, camera);

  Framebuffer::bindScreen(width_, height_);  // restore viewport after the depth passes
  glViewport(0, 0, width_, height_);
  renderSceneToHdr(scene, camera);
  renderMotes(scene, camera, time);

  // Bloom runs its own chain of shader programs and must finish — leaving
  // whatever state it wants — before the composite shader is bound; see the
  // comment at the top of renderComposite.
  GLuint bloomTex = bloom_.render(sceneHdr_.colorTexture(), fsTriVao_);

  if (camera.aim > 0.01f) renderDof();

  renderComposite(camera, scene, bloomTex, time);
}
