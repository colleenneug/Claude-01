#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// Each tier gives up the least valuable thing left. The order is by what it
// costs against what you would miss: temporal AA and the shafts go before
// the occlusion does, because a scene without contact shadows looks wrong
// and a scene without shafts merely looks plainer.
const Renderer::QualityTier Renderer::kTiers[Renderer::kQualityTiers] = {
    //  name       shadow maps         bloom dof    motes  soft   ao     shafts taa    lights
    {"high",    {2048, 2048, 1024}, 5,    true,  1.00f, true,  true,  true,  true,  16},
    {"medium",  {1024, 1024, 512},  4,    true,  0.70f, true,  true,  true,  true,  12},
    {"low",     {1024, 512, 512},   3,    false, 0.45f, false, true,  false, false, 8},
    {"minimal", {768, 512, 512},    2,    false, 0.20f, false, false, false, false, 4},
};

namespace {
const float kNear = 0.05f;

// The low-discrepancy sequence the sub-pixel jitter walks: successive points
// fill the pixel evenly rather than clumping, so eight frames cover it about
// as well as eight samples chosen by hand.
float halton(int index, int base) {
  float f = 1.0f, r = 0.0f;
  while (index > 0) {
    f /= (float)base;
    r += f * (float)(index % base);
    index /= base;
  }
  return r;
}

float luminance(const glm::vec3& c) { return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)); }
}  // namespace

void Renderer::create(int width, int height) {
  width_ = width; height_ = height;

  depthShader_.load("shaders/depth.vert", "shaders/depth.frag");
  prepassShader_.load("shaders/pbr.vert", "shaders/prepass.frag");
  pbrShader_.load("shaders/pbr.vert", "shaders/pbr.frag");
  skyShader_.load("shaders/sky.vert", "shaders/sky.frag");
  motesShader_.load("shaders/motes.vert", "shaders/motes.frag");
  ssaoShader_.load("shaders/fullscreen.vert", "shaders/ssao.frag");
  bilateralShader_.load("shaders/fullscreen.vert", "shaders/bilateral.frag");
  shaftShader_.load("shaders/fullscreen.vert", "shaders/volumetric.frag");
  taaShader_.load("shaders/fullscreen.vert", "shaders/taa.frag");
  luminanceShader_.load("shaders/fullscreen.vert", "shaders/luminance.frag");
  adaptShader_.load("shaders/fullscreen.vert", "shaders/adapt.frag");
  compositeShader_.load("shaders/fullscreen.vert", "shaders/composite.frag");
  dofShader_.load("shaders/fullscreen.vert", "shaders/dof.frag");

  const int hw = std::max(1, width_ / 2), hh = std::max(1, height_ / 2);
  sceneHdr_.create(width_, height_, /*hdr=*/true, /*depthTexture=*/true);
  aoRaw_.createColour(hw, hh, Framebuffer::Format::R8);
  ao_.createColour(width_, height_, Framebuffer::Format::R8);
  shaftRaw_.createColour(hw, hh, Framebuffer::Format::R8);
  shaft_.createColour(hw, hh, Framebuffer::Format::R8);
  for (auto& h : history_) h.createColour(width_, height_, Framebuffer::Format::Rgba16f);
  luminance_.createColour(64, 64, Framebuffer::Format::R16f, /*mipmapped=*/true);
  for (auto& a : adapt_) {
    a.createColour(1, 1, Framebuffer::Format::R16f);
    // Read back before it is first written (the meter blends toward its
    // previous value), so it must not start as whatever the driver left
    // there: one NaN in this texel is a black screen forever after.
    a.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  dofBuffer_.create(hw, hh, true, false);

  // One white texel: "fully open", "fully sunlit". A pass that is switched
  // off leaves the shader that reads its output reading this instead, so
  // the shader itself never has to know.
  glGenTextures(1, &whiteTex_);
  glBindTexture(GL_TEXTURE_2D, whiteTex_);
  const unsigned char white[4] = {255, 255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glGenVertexArrays(1, &fsTriVao_);

  applyQualityTier(0);
  bloom_.create(width_, height_);
  ibl_.create(128);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glCheck("Renderer::create");
}

void Renderer::applyQualityTier(int i) {
  tier_ = std::max(0, std::min(kQualityTiers - 1, i));
  const QualityTier& q = kTiers[tier_];
  csm_.destroy();
  csm_.create({q.shadow[0], q.shadow[1], q.shadow[2]});
  bloom_.activeLevels = q.bloomLevels;
  dofAllowed_ = q.dofAllowed;
  motesFraction_ = q.motesFraction;
  // The history buffer is a picture made by the old tier's pipeline.
  historyValid_ = false;
  std::fprintf(stderr,
               "[Renderer] quality tier -> %s (shadow %d/%d/%d %s, bloom x%d, dof=%d, motes=%.2f, "
               "ao=%d, shafts=%d, taa=%d, lights=%d)\n",
               q.name, q.shadow[0], q.shadow[1], q.shadow[2], q.softShadows ? "soft" : "pcf",
               q.bloomLevels, q.dofAllowed, q.motesFraction, q.ambientOcclusion, q.sunShafts,
               q.temporalAA, q.maxLights);
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
  const int hw = std::max(1, width_ / 2), hh = std::max(1, height_ / 2);
  sceneHdr_.resize(width_, height_);
  aoRaw_.resize(hw, hh);
  ao_.resize(width_, height_);
  shaftRaw_.resize(hw, hh);
  shaft_.resize(hw, hh);
  for (auto& h : history_) h.resize(width_, height_);
  dofBuffer_.resize(hw, hh);
  bloom_.resize(width_, height_);
  historyValid_ = false;
}

void Renderer::destroy() {
  sceneHdr_.destroy();
  aoRaw_.destroy();
  ao_.destroy();
  shaftRaw_.destroy();
  shaft_.destroy();
  for (auto& h : history_) h.destroy();
  luminance_.destroy();
  for (auto& a : adapt_) a.destroy();
  dofBuffer_.destroy();
  csm_.destroy();
  bloom_.destroy();
  ibl_.destroy();
  if (whiteTex_) glDeleteTextures(1, &whiteTex_);
  if (fsTriVao_) glDeleteVertexArrays(1, &fsTriVao_);
  whiteTex_ = fsTriVao_ = 0;
}

void Renderer::drawFullscreen() const {
  glBindVertexArray(fsTriVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

// --------------------------------------------------------------- the probe

void Renderer::bindProbe(const SceneSource& scene) {
  if (!scene.skyLitAmbient()) {
    ibl_.useRoom(fsTriVao_);
    return;
  }
  // The underside of the probe is the ground, lit the way the ground is:
  // by the sun at whatever angle it stands, and by the sky. Something lit
  // from below is lit by that.
  const glm::vec3 sunDir = glm::normalize(scene.sunDirection());
  const glm::vec3 skyMean = (scene.skyZenith() + scene.skyHorizon()) * 0.5f * scene.skyIntensity();
  const float sunOnGround = std::max(-sunDir.y, 0.0f) * 0.7f;   // some of it is in shadow
  IBL::Sky sky;
  sky.zenith = scene.skyZenith();
  sky.horizon = scene.skyHorizon();
  sky.sunDir = sunDir;
  sky.sunColour = scene.sunColour();
  sky.cloudCover = scene.cloudCover();
  sky.intensity = scene.skyIntensity();
  sky.ground = scene.groundAlbedo() *
               (scene.sunColour() * scene.sunIntensity() * sunOnGround / 3.14159265f + skyMean) /
               std::max(scene.skyIntensity(), 1e-3f);
  ibl_.useSky(sky, fsTriVao_);
}

// -------------------------------------------------------------- the lights

// Everything emissive in the draw list, turned into a light on what is near
// it. Until this existed a ceiling strip was a white rectangle that lit
// nothing — the station was carried entirely by a flat fill, and a muzzle
// flash was a bright shape in front of a wall that did not notice.
//
// A light's strength comes from the emitter itself: its radiance times the
// area it shows, which is what the light arriving from a small glowing
// surface actually is. So a bigger or brighter strip lights more, with no
// second set of numbers to keep in step with the first.
void Renderer::gatherLights(const Camera& camera, int maxLights) {
  lightCount_ = 0;
  maxLights = std::min(maxLights, kMaxLights);
  if (maxLights <= 0) return;

  struct Candidate { glm::vec4 pos, colour, axis; float score; };
  static std::vector<Candidate> found;
  found.clear();

  const float kGain = 2.2f;       // artistic: emitters here are dimmer than real lamps
  for (const DrawItem& item : drawList_) {
    if (item.material != MaterialType::Emissive) continue;
    if (item.lightScale <= 0.0f || item.emissiveIntensity <= 0.02f) continue;

    glm::vec3 half = item.mesh->boxHalf();
    if (glm::dot(half, half) <= 0.0f) half = glm::vec3(0.5f);
    const glm::vec3 ex = glm::vec3(item.model[0]) * half.x * 2.0f;
    const glm::vec3 ey = glm::vec3(item.model[1]) * half.y * 2.0f;
    const glm::vec3 ez = glm::vec3(item.model[2]) * half.z * 2.0f;
    const float sx = glm::length(ex), sy = glm::length(ey), sz = glm::length(ez);

    // The mean area a box shows, over every direction, is a quarter of its
    // surface. The floor is the glow round a small emitter being bigger
    // than the emitter.
    const float area = std::max(0.5f * (sx * sy + sy * sz + sx * sz), 0.012f);
    const glm::vec3 intensity = item.emissive * item.emissiveIntensity * area * kGain * item.lightScale;
    const float lum = luminance(intensity);
    if (lum < 0.004f) continue;

    // Long and thin: a line of light rather than a point of it.
    glm::vec3 axis(0.0f);
    const float longest = std::max(sx, std::max(sy, sz));
    const float mid = sx + sy + sz - longest - std::min(sx, std::min(sy, sz));
    if (longest > 2.2f * std::max(mid, 1e-4f)) {
      const glm::vec3 along = longest == sx ? ex : longest == sy ? ey : ez;
      axis = along * (0.5f * (longest - mid) / longest);
    }

    const glm::vec3 pos = glm::vec3(item.model[3]);
    // Where the light stops being worth the arithmetic.
    const float range = glm::clamp(std::sqrt(lum / 0.010f), 1.5f, 32.0f);

    // Importance: how much of it reaches the neighbourhood of the camera.
    glm::vec3 nearest = pos;
    const float al = glm::dot(axis, axis);
    if (al > 1e-6f) nearest += axis * glm::clamp(glm::dot(camera.position - pos, axis) / al, -1.0f, 1.0f);
    const glm::vec3 toCam = camera.position - nearest;
    const float d2 = glm::dot(toCam, toCam);

    Candidate c;
    c.pos = glm::vec4(pos, range);
    c.colour = glm::vec4(intensity, 1.0f);
    c.axis = glm::vec4(axis, 0.0f);
    c.score = lum / (d2 + 4.0f);
    found.push_back(c);
  }
  if (found.empty()) return;

  const size_t keep = std::min(found.size(), (size_t)maxLights);
  std::partial_sort(found.begin(), found.begin() + keep, found.end(),
                    [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

  // If there are more lights than slots, the least important one kept is
  // faded toward nothing as it approaches the cut — so walking down a
  // corridor of strips, the far one dims out as the next one comes in
  // rather than switching.
  float cut = 0.0f;
  if (found.size() > keep) {
    for (size_t i = keep; i < found.size(); i++) cut = std::max(cut, found[i].score);
  }
  for (size_t i = 0; i < keep; i++) {
    float fade = 1.0f;
    if (cut > 0.0f) fade = glm::clamp((found[i].score - cut) / (cut * 0.6f), 0.0f, 1.0f);
    lightPos_[i] = found[i].pos;
    lightColour_[i] = glm::vec4(glm::vec3(found[i].colour) * fade, 1.0f);
    lightAxis_[i] = found[i].axis;
  }
  lightCount_ = (int)keep;
}

// ------------------------------------------------------------- shadow pass

void Renderer::renderShadowCascades() {
  depthShader_.use();
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.5f, 4.0f);
  // Draw the faces that point *away* from the sun. For a closed solid that
  // is the same shadow, and it moves the depth being compared against from
  // the lit face — where the smallest error stripes a surface with its own
  // shadow — to the far side of the object, where there is a whole object's
  // thickness of margin.
  glCullFace(GL_FRONT);
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
  glCullFace(GL_BACK);
}

// ---------------------------------------------------------- depth pre-pass

void Renderer::renderDepthPrepass(const FrameView& fv) {
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  prepassShader_.use();
  prepassShader_.set("uView", fv.view);
  prepassShader_.set("uProj", fv.proj);
  for (const auto& item : drawList_) {
    prepassShader_.set("uModel", item.model);
    item.mesh->draw();
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glCheck("Renderer::renderDepthPrepass");
}

// ------------------------------------------------------- ambient occlusion

void Renderer::renderAmbientOcclusion(const FrameView& fv) {
  glDisable(GL_DEPTH_TEST);

  aoRaw_.bind();
  ssaoShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  ssaoShader_.set("tDepth", 0);
  ssaoShader_.set("uDepthTexel", glm::vec2(1.0f / width_, 1.0f / height_));
  ssaoShader_.set("uProjInfo", glm::vec4(1.0f / fv.proj[0][0], 1.0f / fv.proj[1][1], fv.proj[2][0], fv.proj[2][1]));
  ssaoShader_.set("uNear", kNear);
  ssaoShader_.set("uFar", farPlane_);
  ssaoShader_.set("uProjScale", 0.5f * (float)aoRaw_.height() * fv.proj[1][1]);
  ssaoShader_.set("uRadius", 0.75f);
  ssaoShader_.set("uFrame", taaOn_ ? (float)(frameIndex_ % 8) : 0.0f);
  drawFullscreen();

  ao_.bind();
  bilateralShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, aoRaw_.colorTexture());
  bilateralShader_.set("tSource", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  bilateralShader_.set("tDepth", 1);
  bilateralShader_.set("uSourceTexel", glm::vec2(1.0f / aoRaw_.width(), 1.0f / aoRaw_.height()));
  bilateralShader_.set("uNear", kNear);
  bilateralShader_.set("uFar", farPlane_);
  drawFullscreen();

  glEnable(GL_DEPTH_TEST);
  glCheck("Renderer::renderAmbientOcclusion");
}

// -------------------------------------------------------------- sun shafts

void Renderer::renderSunShafts(const SceneSource& scene, const Camera& camera, const FrameView& fv) {
  glDisable(GL_DEPTH_TEST);

  shaftRaw_.bind();
  shaftShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  shaftShader_.set("tDepth", 0);
  for (int i = 0; i < CascadedShadowMap::CASCADES; i++) {
    std::string p = "uCascade[" + std::to_string(i) + "].";
    shaftShader_.set(p + "viewProj", csm_.viewProj(i));
    shaftShader_.set(p + "range", csm_.range(i));
    glActiveTexture(GL_TEXTURE1 + i);
    glBindTexture(GL_TEXTURE_2D, csm_.depthTexture(i));
    shaftShader_.set("uCascadeMap" + std::to_string(i), 1 + i);
  }
  shaftShader_.set("uInvViewProj", fv.invViewProj);
  shaftShader_.set("uCamPos", camera.position);
  shaftShader_.set("uCamForward", camera.forward());
  shaftShader_.set("uFogDensity", scene.fogDensity());
  shaftShader_.set("uFogFalloff", scene.fogFalloff());
  shaftShader_.set("uFogBase", scene.fogBase());
  // No further than the shadow cascades reach: past that there is nothing
  // to say whether the air is lit, and the answer would be made up.
  shaftShader_.set("uMaxDistance", 110.0f);
  shaftShader_.set("uFrame", taaOn_ ? (float)(frameIndex_ % 8) : 0.0f);
  drawFullscreen();

  shaft_.bind();
  bilateralShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, shaftRaw_.colorTexture());
  bilateralShader_.set("tSource", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  bilateralShader_.set("tDepth", 1);
  bilateralShader_.set("uSourceTexel", glm::vec2(1.0f / shaftRaw_.width(), 1.0f / shaftRaw_.height()));
  bilateralShader_.set("uNear", kNear);
  bilateralShader_.set("uFar", farPlane_);
  drawFullscreen();

  glEnable(GL_DEPTH_TEST);
  glCheck("Renderer::renderSunShafts");
}

// -------------------------------------------------------------- scene pass

void Renderer::renderScene(const SceneSource& scene, const Camera& camera, const FrameView& fv) {
  pbrShader_.use();
  pbrShader_.set("uView", fv.view);
  pbrShader_.set("uProj", fv.proj);
  pbrShader_.set("uCamPos", camera.position);

  pbrShader_.set("uSunDir", scene.sunDirection());
  pbrShader_.set("uSunColour", scene.sunColour());
  pbrShader_.set("uSunIntensity", scene.sunIntensity());

  for (int i = 0; i < CascadedShadowMap::CASCADES; i++) {
    const std::string n = std::to_string(i);
    std::string p = "uCascade[" + n + "].";
    pbrShader_.set(p + "viewProj", csm_.viewProj(i));
    pbrShader_.set(p + "range", csm_.range(i));
    pbrShader_.set("uCascadeInfo[" + n + "]",
                   glm::vec3(1.0f / (float)csm_.mapSize(i), csm_.worldWidth(i), csm_.depthSpan(i)));
    glActiveTexture(GL_TEXTURE1 + i);
    glBindTexture(GL_TEXTURE_2D, csm_.depthTexture(i));
    pbrShader_.set("uCascadeMap" + n, 1 + i);
    // The same texture again on a second unit, read as plain depth.
    glActiveTexture(GL_TEXTURE8 + i);
    glBindTexture(GL_TEXTURE_2D, csm_.depthTexture(i));
    glBindSampler(8 + i, csm_.rawSampler());
    pbrShader_.set("uCascadeRaw" + n, 8 + i);
  }
  pbrShader_.set("uCascadeFade", csm_.fadeMetres());
  pbrShader_.set("uSoftShadows", softShadows_ ? 1 : 0);

  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_CUBE_MAP, ibl_.irradiance());
  pbrShader_.set("uIrradianceMap", 4);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_CUBE_MAP, ibl_.specular());
  pbrShader_.set("uSpecMap", 5);
  pbrShader_.set("uIblMaxMip", (float)ibl_.maxMipLevel());
  pbrShader_.set("uIblIntensity", scene.iblIntensity());
  pbrShader_.set("uAmbientFill", scene.ambientFill());

  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, aoOn_ ? ao_.colorTexture() : whiteTex_);
  pbrShader_.set("uAoMap", 6);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, shaftsOn_ ? shaft_.colorTexture() : whiteTex_);
  pbrShader_.set("uShaftMap", 7);
  pbrShader_.set("uInvScreen", glm::vec2(1.0f / width_, 1.0f / height_));
  pbrShader_.set("uFrame", taaOn_ ? (float)(frameIndex_ % 8) : 0.0f);

  pbrShader_.set("uLightCount", lightCount_);
  pbrShader_.setArray("uLightPos", lightPos_, lightCount_);
  pbrShader_.setArray("uLightColour", lightColour_, lightCount_);
  pbrShader_.setArray("uLightAxis", lightAxis_, lightCount_);

  // Aerial-perspective fog, not a corridor haze: thin per metre, adding up
  // to a visible fade over a mission's ~40m scale. Scene-supplied, because
  // the same numbers that read as depth in an arena saturate into pink soup
  // across the thousands of units open space spans.
  pbrShader_.set("uFogDensity", scene.fogDensity());
  pbrShader_.set("uFogFalloff", scene.fogFalloff());
  pbrShader_.set("uFogBase", scene.fogBase());
  pbrShader_.set("uFogColour", scene.fogColour());
  pbrShader_.set("uInscatter", scene.fogInscatter());

  pbrShader_.set("uWetness", scene.wetness());
  pbrShader_.set("uDustColour", scene.groundAlbedo());
  const float sceneGrime = scene.grime();

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

    // The object's own frame: every model matrix here is a translation, a
    // rotation and a scale, in that order, so the scale is the length of
    // each column and the rotation is what is left.
    glm::vec3 scale(glm::length(glm::vec3(item.model[0])), glm::length(glm::vec3(item.model[1])),
                    glm::length(glm::vec3(item.model[2])));
    scale = glm::max(scale, glm::vec3(1e-6f));
    glm::mat3 rot(glm::vec3(item.model[0]) / scale.x, glm::vec3(item.model[1]) / scale.y,
                  glm::vec3(item.model[2]) / scale.z);
    const glm::vec3 boxHalf = item.mesh->boxHalf();
    pbrShader_.set("uBoxHalf", boxHalf);
    pbrShader_.set("uObjScale", scale);
    pbrShader_.set("uObjRot", rot);
    pbrShader_.set("uLocalShading", item.objectSpace ? 1 : 0);
    // Weather is for things that stand in it: fixed to the world, box-shaped
    // and big enough to have a foot and a top worth telling apart.
    const bool weathered = !item.objectSpace && item.material == MaterialType::Armour &&
                           boxHalf.y * scale.y * 2.0f > 0.6f;
    pbrShader_.set("uGrime", weathered ? sceneGrime : 0.0f);

    item.mesh->draw();
  }

  for (int i = 0; i < CascadedShadowMap::CASCADES; i++) glBindSampler(8 + i, 0);
  glCheck("Renderer::renderScene");
}

// -------------------------------------------------------------------- sky

void Renderer::renderSky(const SceneSource& scene, const Camera& camera, const FrameView& fv, float time) {
  if (scene.skyIntensity() <= 0.001f) return;
  skyShader_.use();
  skyShader_.set("uInvViewProj", fv.invViewProj);
  skyShader_.set("uCamPos", camera.position);
  skyShader_.set("uSkyZenith", scene.skyZenith());
  skyShader_.set("uSkyHorizon", scene.skyHorizon());
  skyShader_.set("uSkySunDir", glm::normalize(scene.sunDirection()));
  skyShader_.set("uSkySunColour", scene.sunColour());
  // Below the horizon line, on screen, is far ground seen through all the
  // haze there is: mostly the fog's colour.
  skyShader_.set("uSkyGround", glm::mix(scene.skyHorizon() * 0.42f, scene.fogColour(), 0.5f));
  skyShader_.set("uCloudCover", scene.cloudCover());
  skyShader_.set("uSkyTime", time);
  skyShader_.set("uSkyIntensity", scene.skyIntensity());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, shaftsOn_ ? shaft_.colorTexture() : whiteTex_);
  skyShader_.set("tShafts", 0);
  skyShader_.set("uShaftStrength", shaftsOn_ ? 1.0f : 0.0f);
  drawFullscreen();
  glCheck("Renderer::renderSky");
}

// ------------------------------------------------------------------ motes

void Renderer::renderMotes(const SceneSource& scene, const Camera& camera, const FrameView& fv, float time) {
  // A scene with no motes (both stations) has no vertex array for them, and
  // a draw with none bound is GL_INVALID_OPERATION in a core profile even at
  // a count of zero — one per frame, every frame, for as long as you stood in
  // the Cradle.
  const int moteCount = std::max(0, (int)(scene.moteCount() * motesFraction_));
  if (scene.moteVao() == 0 || moteCount == 0) return;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive
  glEnable(GL_PROGRAM_POINT_SIZE);

  motesShader_.use();
  motesShader_.set("uView", fv.view);
  motesShader_.set("uProj", fv.proj);
  motesShader_.set("uCamPos", camera.position);
  motesShader_.set("uBox", scene.moteBoxSize());
  motesShader_.set("uTime", time);
  motesShader_.set("uSize", scene.moteSize());
  motesShader_.set("uDistanceScaled", scene.moteDistanceScaled() ? 1.0f : 0.0f);
  motesShader_.set("uSunDir", scene.sunDirection());
  motesShader_.set("uColour", scene.moteColour());
  motesShader_.set("uOpacity", scene.moteOpacity());

  glBindVertexArray(scene.moteVao());
  glDrawArrays(GL_POINTS, 0, moteCount);

  glDisable(GL_PROGRAM_POINT_SIZE);
  glDisable(GL_BLEND);
}

// ------------------------------------------------------------ temporal AA

GLuint Renderer::resolveTemporal(const FrameView& fv) {
  if (!taaOn_) {
    historyValid_ = false;
    return sceneHdr_.colorTexture();
  }
  const int write = historyIndex_, read = 1 - historyIndex_;
  glDisable(GL_DEPTH_TEST);
  history_[write].bind();
  taaShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.colorTexture());
  taaShader_.set("tCurrent", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, history_[read].colorTexture());
  taaShader_.set("tHistory", 1);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  taaShader_.set("tDepth", 2);
  taaShader_.set("uTexel", glm::vec2(1.0f / width_, 1.0f / height_));
  taaShader_.set("uInvViewProjJittered", fv.invViewProj);
  taaShader_.set("uViewProj", fv.cleanViewProj);
  taaShader_.set("uPrevViewProj", prevCleanViewProj_);
  taaShader_.set("uNear", kNear);
  taaShader_.set("uFar", farPlane_);
  taaShader_.set("uHistoryValid", historyValid_ ? 1.0f : 0.0f);
  drawFullscreen();
  glEnable(GL_DEPTH_TEST);

  historyValid_ = true;
  historyIndex_ = read;
  glCheck("Renderer::resolveTemporal");
  return history_[write].colorTexture();
}

// --------------------------------------------------------- exposure meter

void Renderer::meterExposure(GLuint sceneTex, float dt) {
  glDisable(GL_DEPTH_TEST);

  luminance_.bind();
  luminanceShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneTex);
  luminanceShader_.set("tScene", 0);
  luminanceShader_.set("uTexel", glm::vec2(1.0f / width_, 1.0f / height_));
  drawFullscreen();
  glBindTexture(GL_TEXTURE_2D, luminance_.colorTexture());
  glGenerateMipmap(GL_TEXTURE_2D);

  const int write = adaptIndex_, read = 1 - adaptIndex_;
  adapt_[write].bind();
  adaptShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, luminance_.colorTexture());
  adaptShader_.set("tLuminance", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, adapt_[read].colorTexture());
  adaptShader_.set("tPrevious", 1);
  adaptShader_.set("uTopMip", 6.0f);   // log2(64): the 1x1 level
  // An eye takes a second or two; the first frame anywhere takes none, or
  // every scene would open with a slow fade from whatever was on before it.
  const float rate = adaptValid_ ? 1.0f - std::exp(-std::max(dt, 0.0f) * 1.8f) : 1.0f;
  adaptShader_.set("uRate", rate);
  drawFullscreen();
  adaptValid_ = true;
  adaptIndex_ = read;

  glEnable(GL_DEPTH_TEST);
  glCheck("Renderer::meterExposure");
}

// -------------------------------------------------------------------- DoF

void Renderer::renderDof(GLuint sceneTex) {
  dofBuffer_.bind();
  dofShader_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneTex);
  dofShader_.set("tScene", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, sceneHdr_.depthTexture());
  dofShader_.set("tDepth", 1);
  dofShader_.set("uTexel", glm::vec2(1.0f / dofBuffer_.width(), 1.0f / dofBuffer_.height()));
  dofShader_.set("uNear", kNear);
  dofShader_.set("uFar", farPlane_);
  dofShader_.set("uFocus", 16.0f);
  dofShader_.set("uRange", 60.0f);
  dofShader_.set("uMaxRadius", 4.0f);

  glDisable(GL_DEPTH_TEST);
  drawFullscreen();
  glEnable(GL_DEPTH_TEST);
}

// -------------------------------------------------------------- composite

void Renderer::renderComposite(const Camera& camera, const SceneSource& scene, GLuint sceneTex,
                               GLuint bloomTex, float time) {
  // bloomTex was produced by a separate chain of shader programs (Bloom
  // owns its own bright/downsample/upsample passes); the composite shader
  // is bound here, after all of that has finished, so every uniform set
  // below actually lands on the program that ends up drawing.
  Framebuffer::bindScreen(width_, height_);
  glDisable(GL_DEPTH_TEST);
  compositeShader_.use();

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneTex);
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
  glActiveTexture(GL_TEXTURE4);
  // The meter wrote adapt_[1 - adaptIndex_] this frame (it flips after).
  glBindTexture(GL_TEXTURE_2D, adapt_[1 - adaptIndex_].colorTexture());
  compositeShader_.set("tAdapt", 4);

  compositeShader_.set("uTexel", glm::vec2(1.0f / width_, 1.0f / height_));
  compositeShader_.set("uNear", kNear);
  compositeShader_.set("uFar", farPlane_);

  // If this tier doesn't allow the DoF pass to run, dofBuffer_ holds stale
  // (or never-rendered) contents — zeroing uAim here, rather than at the
  // call site, keeps camera.aim itself real for anything else that reads
  // it while skipping the one shader branch that would blend that stale
  // buffer in.
  compositeShader_.set("uAim", dofAllowed_ ? camera.aim : 0.0f);
  compositeShader_.set("uDofFocus", 16.0f);
  compositeShader_.set("uDofRange", 60.0f);

  // 1.15 with the ACES curve on top of it put the dusty floor of a mission
  // arena around 0.85 in the final image, which left nothing above it for a
  // highlight and nothing below it for a shadow: the whole frame read as
  // one flat wash. Lower exposure puts the ground back in the mid tones and
  // gives the sun somewhere to go.
  compositeShader_.set("uExposure", 0.82f);
  // What the outdoor missions this was graded on meter at, before exposure
  // (0.15 to 0.18 across the campaign; the gun range in full sun is 0.41 and
  // the great cave 0.05). The adaptation pulls toward it, part way — see
  // composite.frag — so the typical scene is left exactly as it was lit.
  compositeShader_.set("uAdaptKey", 0.17f);
  compositeShader_.set("uAdaptStrength", scene.wantsAutoExposure() && adaptValid_ ? 0.45f : 0.0f);
  compositeShader_.set("uGrain", 0.03f);
  compositeShader_.set("uVignette", 0.65f);
  compositeShader_.set("uAberration", 1.0f);
  compositeShader_.set("uBloom", 0.32f);
  compositeShader_.set("uSharpen", taaOn_ ? 0.55f : 0.0f);
  compositeShader_.set("uTime", time);

  drawFullscreen();
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

  adapt_[1 - adaptIndex_].bind();
  float logLum[4] = {0, 0, 0, 0};
  glReadPixels(0, 0, 1, 1, GL_RED, GL_FLOAT, logLum);

  Framebuffer::bindScreen(width_, height_);
  unsigned char screen[4] = {0, 0, 0, 0};
  glReadPixels(cx, cy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, screen);

  std::fprintf(stderr,
               "[debug-pixel] centre=(%d,%d) hdrScene(linear)=(%.4f,%.4f,%.4f) "
               "finalScreen(0-255)=(%d,%d,%d) meteredLuminance=%.4f lights=%d\n",
               cx, cy, hdr[0], hdr[1], hdr[2], screen[0], screen[1], screen[2],
               std::exp(logLum[0]), lightCount_);
  glCheck("Renderer::debugPrintCenterPixel");
}

void Renderer::renderFrame(const SceneSource& scene, const Camera& camera, float time, float dt) {
  trackFrameTime(dt);
  farPlane_ = scene.viewDistance();
  const QualityTier& q = kTiers[tier_];

  drawList_.clear();
  scene.collect(time, drawList_);

  // A different scene is a different place: last frame's picture of the
  // other one is not history, and its exposure is not this one's.
  if (&scene != lastScene_) {
    historyValid_ = false;
    adaptValid_ = false;
    lastScene_ = &scene;
  }

  const bool shadows = scene.wantsShadows();
  softShadows_ = q.softShadows && shadows;
  aoOn_ = q.ambientOcclusion && scene.wantsAmbientOcclusion();
  shaftsOn_ = q.sunShafts && shadows && scene.fogDensity() > 0.0f && scene.sunIntensity() > 0.0f;
  taaOn_ = q.temporalAA && scene.wantsTemporalAA();

  bindProbe(scene);
  gatherLights(camera, q.maxLights);

  // ---- this frame's camera
  FrameView fv;
  fv.view = camera.view();
  const glm::mat4 cleanProj =
      glm::perspective(glm::radians(camera.fov()), (float)width_ / height_, kNear, farPlane_);
  fv.proj = cleanProj;
  if (taaOn_) {
    // Shift the whole image by a fraction of a pixel, a different fraction
    // each frame. In clip space x is divided by -z, so adding to the z
    // column's x and y moves every point by the same amount on screen.
    const int step = frameIndex_ % 8 + 1;
    fv.proj[2][0] += (halton(step, 2) - 0.5f) * 2.0f / (float)width_;
    fv.proj[2][1] += (halton(step, 3) - 0.5f) * 2.0f / (float)height_;
  }
  fv.viewProj = fv.proj * fv.view;
  fv.invViewProj = glm::inverse(fv.viewProj);
  fv.cleanViewProj = cleanProj * fv.view;
  // Kept so the HUD can label things in the world with the same matrices
  // the frame was actually drawn with.
  lastViewProj_ = fv.cleanViewProj;

  // Open space is a handful of convex bodies thousands of units apart with
  // nothing to receive their shadows; fitting three cascades around that
  // spends the whole shadow budget on emptiness and self-shadows the
  // planets into darkness. The scene says whether the pass is worth it.
  if (shadows) {
    glm::vec3 fwd = camera.forward();
    csm_.update(camera.position, fwd, glm::vec3(0, 1, 0), camera.fov(),
                (float)width_ / height_, kNear, scene.sunDirection());
    renderShadowCascades();
  } else {
    // Still clear them, or the previous scene's depth lingers and paints
    // mission shadows across space.
    for (int i = 0; i < CascadedShadowMap::CASCADES; i++) csm_.beginCascade(i);
  }

  // ---- depth first, then what can be worked out from depth alone
  sceneHdr_.bind();
  glm::vec3 clear = scene.clearColour();
  glClearColor(clear.r, clear.g, clear.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  renderDepthPrepass(fv);
  if (aoOn_) renderAmbientOcclusion(fv);
  if (shaftsOn_) renderSunShafts(scene, camera, fv);

  // ---- colour. The depth buffer is already final, so nothing here writes
  // it: each pixel is shaded exactly once, by whatever is in front.
  sceneHdr_.bind();
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  renderScene(scene, camera, fv);
  renderSky(scene, camera, fv, time);
  renderMotes(scene, camera, fv, time);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);

  // ---- the camera
  GLuint frame = resolveTemporal(fv);
  if (scene.wantsAutoExposure()) meterExposure(frame, dt);
  else adaptValid_ = false;

  // Bloom runs its own chain of shader programs and must finish — leaving
  // whatever state it wants — before the composite shader is bound; see the
  // comment at the top of renderComposite.
  GLuint bloomTex = bloom_.render(frame, fsTriVao_);

  if (dofAllowed_ && camera.aim > 0.01f) renderDof(frame);

  renderComposite(camera, scene, frame, bloomTex, time);

  prevCleanViewProj_ = fv.cleanViewProj;
  frameIndex_++;
}
