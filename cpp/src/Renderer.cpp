#include "Renderer.h"
#include <algorithm>
#include <cstdio>

// Each tier gives up the least valuable thing left, in the same order the
// browser build's TIERS table does. Pixel-ratio supersampling from that
// table isn't ported — it would need an extra upscale-blit stage this
// renderer doesn't have — so this trades shadow/bloom/DOF/mote cost only.
const Renderer::QualityTier Renderer::kTiers[Renderer::kQualityTiers] = {
    {"high", {2048, 1024, 1024}, 5, true, 1.00f},
    {"medium", {1024, 1024, 512}, 4, true, 0.70f},
    {"low", {1024, 512, 512}, 3, false, 0.45f},
    {"minimal", {768, 512, 512}, 2, false, 0.20f},
};

void Renderer::create(int width, int height) {
  width_ = width; height_ = height;

  depthShader_.load("shaders/depth.vert", "shaders/depth.frag");
  pbrShader_.load("shaders/pbr.vert", "shaders/pbr.frag");
  motesShader_.load("shaders/motes.vert", "shaders/motes.frag");
  compositeShader_.load("shaders/fullscreen.vert", "shaders/composite.frag");
  dofShader_.load("shaders/fullscreen.vert", "shaders/dof.frag");

  sceneHdr_.create(width_, height_, /*hdr=*/true, /*depthTexture=*/true);
  dofBuffer_.create(std::max(1, width_ / 2), std::max(1, height_ / 2), true, false);

  applyQualityTier(0);
  bloom_.create(width_, height_);
  ibl_.build(128);

  glGenVertexArrays(1, &fsTriVao_);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
}

void Renderer::applyQualityTier(int i) {
  tier_ = std::max(0, std::min(kQualityTiers - 1, i));
  const QualityTier& q = kTiers[tier_];
  csm_.destroy();
  csm_.create({q.shadow[0], q.shadow[1], q.shadow[2]});
  bloom_.activeLevels = q.bloomLevels;
  dofAllowed_ = q.dofAllowed;
  motesFraction_ = q.motesFraction;
  std::fprintf(stderr, "[Renderer] quality tier -> %s (shadow %d/%d/%d, bloom x%d, dof=%d, motes=%.2f)\n",
               q.name, q.shadow[0], q.shadow[1], q.shadow[2], q.bloomLevels, q.dofAllowed, q.motesFraction);
}

void Renderer::trackFrameTime(float dtSeconds) {
  if (!autoQuality_) return;
  float ms = dtSeconds * 1000.0f;
  // A gap this long is a minimized window or a breakpoint, not a slow
  // frame — don't let it single-handedly force a downgrade.
  if (ms < 250.0f) emaMs_ += (ms - emaMs_) * 0.1f;

  if (emaMs_ > 26.0f) { slowFrames_++; fastFrames_ = 0; }
  else if (emaMs_ < 13.0f) { fastFrames_++; slowFrames_ = 0; }
  else { slowFrames_ = 0; fastFrames_ = 0; }

  if (slowFrames_ > 90 && tier_ < kQualityTiers - 1) {
    slowFrames_ = 0;
    downgrades_++;
    applyQualityTier(tier_ + 1);
  } else if (tier_ > 0 && fastFrames_ > 600 * (downgrades_ + 1)) {
    // Climbing back is deliberately much harder than falling, scaled by how
    // many times it has already fallen, so it can't sit oscillating
    // between two tiers.
    fastFrames_ = 0;
    applyQualityTier(tier_ - 1);
  }
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

void Renderer::renderShadowCascades(const SceneSource&, const Camera&) {
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

void Renderer::renderSceneToHdr(const SceneSource& scene, const Camera& camera) {
  sceneHdr_.bind();
  glm::vec3 clear = scene.clearColour();
  glClearColor(clear.r, clear.g, clear.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glm::mat4 view = camera.view();
  glm::mat4 proj = glm::perspective(glm::radians(camera.fov()), (float)width_ / height_, 0.05f, farPlane_);

  pbrShader_.use();
  pbrShader_.set("uView", view);
  pbrShader_.set("uProj", proj);
  pbrShader_.set("uCamPos", camera.position);

  pbrShader_.set("uSunDir", scene.sunDirection());
  pbrShader_.set("uSunColour", scene.sunColour());
  pbrShader_.set("uSunIntensity", scene.sunIntensity());

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
  pbrShader_.set("uIblIntensity", scene.iblIntensity());
  pbrShader_.set("uAmbientFill", scene.ambientFill());

  for (const auto& item : drawList_) {
    pbrShader_.set("uModel", item.model);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(item.model)));
    pbrShader_.set("uNormalMatrix", normalMat);
    pbrShader_.set("uMaterial", (int)item.material);
    pbrShader_.set("uTint", item.tint);
    pbrShader_.set("uMetallic", item.metallic);
    pbrShader_.set("uRoughness", item.roughness);
    pbrShader_.set("uWear", item.wear);
    pbrShader_.set("uTint2", item.tint2);
    pbrShader_.set("uCapExtent", item.capExtent);
    pbrShader_.set("uEmissive", item.emissive);
    pbrShader_.set("uEmissiveIntensity", item.emissiveIntensity);
    pbrShader_.set("uAniso", item.anisoStrength);
    item.mesh->draw();
  }
  glCheck("Renderer::renderSceneToHdr");
}

// ------------------------------------------------------------------ motes

void Renderer::renderMotes(const SceneSource& scene, const Camera& camera, float time) {
  sceneHdr_.bind();  // still bound from the scene pass, but explicit is cheap
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive
  glEnable(GL_PROGRAM_POINT_SIZE);

  motesShader_.use();
  motesShader_.set("uView", camera.view());
  motesShader_.set("uProj", glm::perspective(glm::radians(camera.fov()), (float)width_ / height_, 0.05f, farPlane_));
  motesShader_.set("uCamPos", camera.position);
  motesShader_.set("uBox", scene.moteBoxSize());
  motesShader_.set("uTime", time);
  motesShader_.set("uSize", scene.moteSize());
  motesShader_.set("uDistanceScaled", scene.moteDistanceScaled() ? 1.0f : 0.0f);
  motesShader_.set("uSunDir", scene.sunDirection());
  motesShader_.set("uColour", scene.moteColour());
  motesShader_.set("uOpacity", scene.moteOpacity());

  glBindVertexArray(scene.moteVao());
  int moteCount = std::max(0, (int)(scene.moteCount() * motesFraction_));
  glDrawArrays(GL_POINTS, 0, moteCount);

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
  dofShader_.set("uFar", farPlane_);
  dofShader_.set("uFocus", 16.0f);
  dofShader_.set("uRange", 60.0f);
  dofShader_.set("uMaxRadius", 4.0f);

  glBindVertexArray(fsTriVao_);
  glDisable(GL_DEPTH_TEST);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
}

// -------------------------------------------------------------- composite

void Renderer::renderComposite(const Camera& camera, const SceneSource& scene, GLuint bloomTex, float time) {
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
  glm::mat4 proj = glm::perspective(glm::radians(camera.fov()), (float)width_ / height_, 0.05f, farPlane_);
  // Kept so the HUD can label things in the world with the same matrices
  // the frame was actually drawn with.
  lastViewProj_ = proj * view;
  compositeShader_.set("uInvViewProj", glm::inverse(lastViewProj_));
  compositeShader_.set("uCamPos", camera.position);
  compositeShader_.set("uNear", 0.05f);
  compositeShader_.set("uFar", farPlane_);

  // If this tier doesn't allow the DoF pass to run, dofBuffer_ holds stale
  // (or never-rendered) contents — zeroing uAim here, rather than at the
  // call site, keeps camera.aim itself real for anything else that reads
  // it while skipping the one shader branch that would blend that stale
  // buffer in.
  compositeShader_.set("uAim", dofAllowed_ ? camera.aim : 0.0f);
  compositeShader_.set("uDofFocus", 16.0f);
  compositeShader_.set("uDofRange", 60.0f);

  // Aerial-perspective fog, not a corridor haze: thin per metre, adding up
  // to a visible fade over a mission's ~40m scale. Scene-supplied, because
  // the same numbers that read as depth in an arena saturate into pink soup
  // across the thousands of units open space spans.
  compositeShader_.set("uFogDensity", scene.fogDensity());
  compositeShader_.set("uFogFalloff", scene.fogFalloff());
  compositeShader_.set("uFogBase", scene.fogBase());
  compositeShader_.set("uFogColour", scene.fogColour());
  compositeShader_.set("uInscatter", scene.fogInscatter());

  compositeShader_.set("uSunDir", scene.sunDirection());
  compositeShader_.set("uSunColour", scene.sunColour());
  compositeShader_.set("uSkyZenith", scene.skyZenith());
  compositeShader_.set("uSkyHorizon", scene.skyHorizon());
  compositeShader_.set("uSkyIntensity", scene.skyIntensity());
  // 1.15 with the ACES curve on top of it put the dusty floor of a mission
  // arena around 0.85 in the final image, which left nothing above it for a
  // highlight and nothing below it for a shadow: the whole frame read as
  // one flat wash. Lower exposure puts the ground back in the mid tones and
  // gives the sun somewhere to go.
  compositeShader_.set("uExposure", 0.82f);
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

void Renderer::debugPrintCenterPixel() const {
  int cx = width_ / 2, cy = height_ / 2;

  glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
  sceneHdr_.bind();
  float hdr[4] = {0, 0, 0, 0};
  glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_FLOAT, hdr);

  Framebuffer::bindScreen(width_, height_);
  unsigned char screen[4] = {0, 0, 0, 0};
  glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, screen);

  std::fprintf(stderr,
               "[debug-pixel] centre=(%d,%d) hdrScene(linear)=(%.4f,%.4f,%.4f) "
               "finalScreen(0-255)=(%d,%d,%d)\n",
               cx, cy, hdr[0], hdr[1], hdr[2], screen[0], screen[1], screen[2]);
  glCheck("Renderer::debugPrintCenterPixel");
}

void Renderer::renderFrame(const SceneSource& scene, const Camera& camera, float time, float dt) {
  trackFrameTime(dt);
  farPlane_ = scene.viewDistance();

  drawList_.clear();
  scene.collect(time, drawList_);

  // Open space is a handful of convex bodies thousands of units apart with
  // nothing to receive their shadows; fitting three cascades around that
  // spends the whole shadow budget on emptiness and self-shadows the
  // planets into darkness. The scene says whether the pass is worth it.
  if (scene.wantsShadows()) {
    glm::vec3 fwd = camera.forward();
    csm_.update(camera.position, fwd, glm::vec3(0, 1, 0), camera.fov(),
                (float)width_ / height_, 0.05f, scene.sunDirection());
    renderShadowCascades(scene, camera);
  } else {
    // Still clear them, or the previous scene's depth lingers and paints
    // mission shadows across space.
    for (int i = 0; i < CascadedShadowMap::CASCADES; i++) csm_.beginCascade(i);
  }

  Framebuffer::bindScreen(width_, height_);  // restore viewport after the depth passes
  glViewport(0, 0, width_, height_);
  renderSceneToHdr(scene, camera);
  renderMotes(scene, camera, time);

  // Bloom runs its own chain of shader programs and must finish — leaving
  // whatever state it wants — before the composite shader is bound; see the
  // comment at the top of renderComposite.
  GLuint bloomTex = bloom_.render(sceneHdr_.colorTexture(), fsTriVao_);

  if (dofAllowed_ && camera.aim > 0.01f) renderDof();

  renderComposite(camera, scene, bloomTex, time);
}
