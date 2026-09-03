#include "forge/render/SoftwareRenderer.hpp"

#include <chrono>

#include "forge/assets/Material.hpp"
#include "forge/assets/Mesh.hpp"

namespace forge {

SoftwareRenderer::SoftwareRenderer() = default;
SoftwareRenderer::~SoftwareRenderer() = default;

// -----------------------------------------------------------------
//  Shading model
// -----------------------------------------------------------------

namespace {

// Trowbridge-Reitz (GGX) normal distribution: the specular lobe that
// makes a rough surface look rough and a smooth one look wet.
float distributionGGX(float nDotH, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / std::max(1e-6f, kPi * d * d);
}

// Smith geometry term with the Schlick-GGX approximation, using the
// direct-lighting remap of roughness.
float geometrySmith(float nDotV, float nDotL, float roughness) {
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    const float gv = nDotV / (nDotV * (1.0f - k) + k);
    const float gl = nDotL / (nDotL * (1.0f - k) + k);
    return gv * gl;
}

Vec3 fresnelSchlick(float cosTheta, const Vec3& f0) {
    const float f = std::pow(saturate(1.0f - cosTheta), 5.0f);
    return f0 + (Vec3{1, 1, 1} - f0) * f;
}

// The filmic curve everything is graded against. Without it, bright
// values clip to flat white and the image looks like a render rather
// than a photograph.
Vec3 acesToneMap(const Vec3& x) {
    constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    Vec3 num = x * (x * a + Vec3(b));
    Vec3 den = x * (x * c + Vec3(d)) + Vec3(e);
    return Vec3{saturate(num.x / std::max(1e-6f, den.x)),
                saturate(num.y / std::max(1e-6f, den.y)),
                saturate(num.z / std::max(1e-6f, den.z))};
}

} // namespace

float SoftwareRenderer::sunShadowFactor(const Vec3& worldPos, const Vec3& normal) const {
    if (!shadowValid_ || shadowResolution <= 0) return 1.0f;

    // Normal offset. A depth bias alone cannot fix acne on a surface lit
    // at a glancing angle: within one shadow texel the true depth varies
    // by more than any bias small enough to keep contact shadows. Moving
    // the sample point off the surface by about a texel sidesteps the
    // problem geometrically instead of fighting it in depth.
    const float texelWorld = (2.0f * shadowExtent) / (float)shadowResolution;
    const Vec3 samplePos = worldPos + normal * (texelWorld * 1.8f);

    const Vec4 lightSpace = shadowMatrix_ * Vec4(samplePos, 1.0f);
    if (lightSpace.w <= 0.0f) return 1.0f;
    const Vec3 ndc{lightSpace.x / lightSpace.w, lightSpace.y / lightSpace.w, lightSpace.z / lightSpace.w};
    // Outside the map is lit: better a missing shadow at the edge of the
    // cascade than a hard black band across the level.
    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z > 1.0f) return 1.0f;

    const float u = (ndc.x * 0.5f + 0.5f) * (float)shadowResolution;
    const float v = (ndc.y * 0.5f + 0.5f) * (float)shadowResolution;
    const float depth = ndc.z * 0.5f + 0.5f;

    // Slope-scaled bias: a surface nearly edge-on to the sun needs far
    // more offset than one facing it, or it shadows itself in stripes.
    // shadowBias is in world units, so divide by the range the depth
    // buffer covers to get the same distance in normalised depth.
    const float slope = 1.0f - saturate(std::fabs(normal.y));
    const float bias = (shadowBias * (1.0f + slope * 3.0f)) / std::max(1e-4f, shadowDepthRange_);

    // 3x3 percentage-closer filter, so edges are soft rather than jagged.
    int lit = 0, total = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int sx = (int)u + dx, sy = (int)v + dy;
            if (sx < 0 || sy < 0 || sx >= shadowResolution || sy >= shadowResolution) continue;
            ++total;
            if (depth - bias <= shadowDepth_[(size_t)sy * (size_t)shadowResolution + (size_t)sx]) ++lit;
        }
    }
    return total > 0 ? (float)lit / (float)total : 1.0f;
}

Color SoftwareRenderer::shade(const Vec3& worldPos, const Vec3& normalIn, const Vec2& uv,
                              const DrawItem& item, const RenderScene& scene,
                              const RenderView& view) const {
    const Material* mat = item.material;

    Vec3 albedo = item.tint.rgb();
    float metallic = 0.0f, roughness = 0.6f;
    Vec3 emissive{0, 0, 0};

    if (mat) {
        albedo = albedo * mat->baseColor.rgb();
        metallic = saturate(mat->metallic);
        roughness = clampf(mat->roughness, 0.03f, 1.0f);
        emissive = mat->emissive.rgb() * mat->emissiveStrength;
    }
    if (item.texture) {
        const Vec2 scaled = mat ? Vec2{uv.x * mat->textureScale.x, uv.y * mat->textureScale.y} : uv;
        albedo = albedo * item.texture->sample(scaled.x, scaled.y).rgb();
    }
    if (mat && mat->unlit) return Color{albedo.x, albedo.y, albedo.z, 1.0f};

    Vec3 n = normalIn.normalized();
    const Vec3 v = (view.cameraPosition - worldPos).normalized();
    // Two-sided surfaces and back faces seen through a thin wall should
    // still light, not go black.
    if (dot(n, v) < 0.0f && mat && mat->twoSided) n = -n;
    const float nDotV = std::max(1e-4f, dot(n, v));

    const Vec3 f0 = lerp(Vec3(0.04f), albedo, metallic);
    const Vec3 diffuseAlbedo = albedo * (1.0f - metallic);
    Vec3 lit{0, 0, 0};

    auto accumulate = [&](const Vec3& toLight, const Vec3& radiance) {
        const float nDotL = dot(n, toLight);
        if (nDotL <= 0.0f) return;
        const Vec3 h = (toLight + v).normalized();
        const float nDotH = std::max(0.0f, dot(n, h));
        const float vDotH = std::max(0.0f, dot(v, h));

        const float ndf = distributionGGX(nDotH, roughness);
        const float g = geometrySmith(nDotV, nDotL, roughness);
        const Vec3 f = fresnelSchlick(vDotH, f0);

        const Vec3 specular = f * (ndf * g / std::max(1e-5f, 4.0f * nDotV * nDotL));
        const Vec3 kd = (Vec3{1, 1, 1} - f) * (1.0f - metallic);
        lit += (diffuseAlbedo * kd * (1.0f / kPi) + specular) * radiance * nDotL;
    };

    if (scene.hasSun && scene.sun.intensity > 0.0f) {
        const Vec3 toSun = -scene.sun.direction.normalized();
        const float shadow = scene.environment.shadows ? sunShadowFactor(worldPos, n) : 1.0f;
        if (shadow > 0.0f)
            accumulate(toSun, scene.sun.color.rgb() * (scene.sun.intensity * shadow));
    }

    for (const LightItem& light : scene.lights) {
        if (light.intensity <= 0.0f) continue;
        if (light.kind == LightKind::Directional) {
            accumulate(-light.direction.normalized(), light.color.rgb() * light.intensity);
            continue;
        }
        Vec3 toLight = light.position - worldPos;
        const float dist = toLight.length();
        if (dist > light.radius || dist < 1e-5f) continue;
        toLight = toLight / dist;

        // Inverse-square falloff, windowed so it reaches exactly zero at
        // the radius instead of being cut off mid-gradient.
        const float t = saturate(dist / light.radius);
        const float window = sqr(1.0f - t * t * t * t);
        float attenuation = window / (1.0f + dist * dist);

        if (light.kind == LightKind::Spot) {
            const float cosAngle = dot(-toLight, light.direction.normalized());
            const float denom = std::max(1e-4f, light.innerCos - light.outerCos);
            attenuation *= saturate((cosAngle - light.outerCos) / denom);
            if (attenuation <= 0.0f) continue;
        }
        accumulate(toLight, light.color.rgb() * (light.intensity * attenuation));
    }

    // Hemisphere ambient: sky above, bounce from the ground below. Cheap,
    // and far better than a flat constant at suggesting where light is.
    const Vec3 skyRgb = scene.environment.skyColor.rgb();
    const Vec3 groundRgb = scene.environment.groundColor.rgb();
    const float up = n.y * 0.5f + 0.5f;
    const Vec3 ambient = lerp(groundRgb, skyRgb, up) * scene.environment.ambientIntensity;
    lit += diffuseAlbedo * ambient;

    // Ambient specular. Without it a metal is black: metalness removes
    // the diffuse term entirely, so a metal shows only what it reflects,
    // and analytic lights alone are a handful of highlights on nothing.
    // Sampling the same hemisphere along the reflection vector stands in
    // for an environment probe.
    const Vec3 refl = reflect(-v, n);
    const Vec3 envColor = lerp(groundRgb, skyRgb, refl.y * 0.5f + 0.5f) *
                          scene.environment.ambientIntensity;
    // A rough surface scatters its reflection away; a smooth one keeps it.
    lit += envColor * fresnelSchlick(nDotV, f0) * (1.0f - roughness * 0.75f);

    lit += emissive;

    // Height-independent exponential fog, applied in linear light so it
    // blends with the sky rather than washing over it.
    if (scene.environment.fogEnabled && scene.environment.fogDensity > 0.0f) {
        const float dist = distance(worldPos, view.cameraPosition);
        const float f = 1.0f - std::exp(-dist * scene.environment.fogDensity);
        lit = lerp(lit, scene.environment.fogColor.rgb(), saturate(f));
    }

    return Color{lit.x, lit.y, lit.z, 1.0f};
}

// -----------------------------------------------------------------
//  Rasterisation
// -----------------------------------------------------------------

namespace {

// Clip a polygon against the near plane in clip space. Everything else
// is handled by the scissor in screen space; the near plane is the one
// that cannot be, because w flips sign through it.
template <typename V, typename Lerp>
int clipNear(const V* in, int count, V* out, float nearW, Lerp mix) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const V& a = in[i];
        const V& b = in[(i + 1) % count];
        const bool aIn = a.clip.w > nearW;
        const bool bIn = b.clip.w > nearW;
        if (aIn) out[n++] = a;
        if (aIn != bIn) {
            const float t = (nearW - a.clip.w) / (b.clip.w - a.clip.w);
            out[n++] = mix(a, b, t);
        }
    }
    return n;
}

} // namespace

void SoftwareRenderer::renderShadowMap(const RenderScene& scene, const RenderView& view) {
    shadowValid_ = false;
    if (shadowResolution <= 0 || !scene.hasSun || !scene.environment.shadows) return;

    const int res = shadowResolution;
    shadowDepth_.assign((size_t)res * (size_t)res, 1.0f);

    // Fit the map around the camera rather than the whole level: a level
    // a kilometre across would otherwise get a metre per texel.
    const Vec3 focus = view.cameraPosition;
    const Vec3 sunDir = scene.sun.direction.normalized();
    const Vec3 eye = focus - sunDir * (shadowExtent * 2.0f);
    Vec3 up = std::fabs(sunDir.y) > 0.95f ? Vec3{0, 0, 1} : Vec3::Up;

    const float zNear = 0.1f;
    const float zFar = shadowExtent * 6.0f;
    const Mat4 lightView = Mat4::lookAt(eye, focus, up);
    const Mat4 lightProj = Mat4::orthographic(-shadowExtent, shadowExtent, -shadowExtent,
                                              shadowExtent, zNear, zFar);
    shadowMatrix_ = lightProj * lightView;
    shadowDepthRange_ = zFar - zNear;

    for (const DrawItem& item : scene.items) {
        if (!item.castShadow || !item.mesh) continue;
        const Mesh& mesh = *item.mesh;
        const Mat4 mvp = shadowMatrix_ * item.transform;

        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            Vec4 clip[3];
            bool behind = false;
            for (int k = 0; k < 3; ++k) {
                clip[k] = mvp * Vec4(mesh.vertices[mesh.indices[i + (size_t)k]].position, 1.0f);
                if (clip[k].w <= 1e-5f) behind = true;
            }
            if (behind) continue;

            Vec3 s[3];
            for (int k = 0; k < 3; ++k) {
                s[k] = {(clip[k].x / clip[k].w * 0.5f + 0.5f) * (float)res,
                        (clip[k].y / clip[k].w * 0.5f + 0.5f) * (float)res,
                        clip[k].z / clip[k].w * 0.5f + 0.5f};
            }

            const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) -
                               (s[2].x - s[0].x) * (s[1].y - s[0].y);
            if (std::fabs(area) < 1e-8f) continue;

            const int minX = std::max(0, (int)std::floor(minComponent(Vec3{s[0].x, s[1].x, s[2].x})));
            const int maxX = std::min(res - 1, (int)std::ceil(maxComponent(Vec3{s[0].x, s[1].x, s[2].x})));
            const int minY = std::max(0, (int)std::floor(minComponent(Vec3{s[0].y, s[1].y, s[2].y})));
            const int maxY = std::min(res - 1, (int)std::ceil(maxComponent(Vec3{s[0].y, s[1].y, s[2].y})));

            const float invArea = 1.0f / area;
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    const float px = (float)x + 0.5f, py = (float)y + 0.5f;
                    float w0 = ((s[1].x - px) * (s[2].y - py) - (s[2].x - px) * (s[1].y - py)) * invArea;
                    float w1 = ((s[2].x - px) * (s[0].y - py) - (s[0].x - px) * (s[2].y - py)) * invArea;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                    const float z = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
                    float& dst = shadowDepth_[(size_t)y * (size_t)res + (size_t)x];
                    if (z < dst) dst = z;
                }
            }
        }
    }
    shadowValid_ = true;
}

void SoftwareRenderer::rasterTriangle(Framebuffer& target, const ShadedVertex& a,
                                      const ShadedVertex& b, const ShadedVertex& c,
                                      const DrawItem& item, const RenderScene& scene,
                                      const RenderView& view) {
    const ShadedVertex* vs[3] = {&a, &b, &c};
    Vec3 screen[3];
    float invW[3];

    for (int k = 0; k < 3; ++k) {
        const Vec4& cl = vs[k]->clip;
        invW[k] = 1.0f / cl.w;
        screen[k] = {(cl.x * invW[k] * 0.5f + 0.5f) * (float)width_,
                     (1.0f - (cl.y * invW[k] * 0.5f + 0.5f)) * (float)height_,
                     cl.z * invW[k] * 0.5f + 0.5f};
    }

    const float area = (screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) -
                       (screen[2].x - screen[0].x) * (screen[1].y - screen[0].y);
    if (std::fabs(area) < 1e-9f) return;
    // Back-face cull, unless the material asked to be drawn both ways.
    const bool twoSided = item.material && item.material->twoSided;
    if (area > 0.0f && !twoSided) return;

    ++stats_.trianglesDrawn;

    const int minX = std::max(0, (int)std::floor(std::min({screen[0].x, screen[1].x, screen[2].x})));
    const int maxX = std::min(width_ - 1, (int)std::ceil(std::max({screen[0].x, screen[1].x, screen[2].x})));
    const int minY = std::max(0, (int)std::floor(std::min({screen[0].y, screen[1].y, screen[2].y})));
    const int maxY = std::min(height_ - 1, (int)std::ceil(std::max({screen[0].y, screen[1].y, screen[2].y})));
    if (minX > maxX || minY > maxY) return;

    const float invArea = 1.0f / area;
    const float opacity = item.material ? saturate(item.material->opacity) : 1.0f;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((screen[1].x - px) * (screen[2].y - py) -
                        (screen[2].x - px) * (screen[1].y - py)) * invArea;
            float w1 = ((screen[2].x - px) * (screen[0].y - py) -
                        (screen[0].x - px) * (screen[2].y - py)) * invArea;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;

            const float z = w0 * screen[0].z + w1 * screen[1].z + w2 * screen[2].z;
            if (z < 0.0f || z > 1.0f) continue;
            float& dst = depth_[(size_t)y * (size_t)width_ + (size_t)x];
            if (z >= dst) continue;

            // Perspective-correct interpolation: barycentrics are linear
            // in screen space, but attributes are linear in 1/w.
            const float iw = w0 * invW[0] + w1 * invW[1] + w2 * invW[2];
            const float p0 = w0 * invW[0] / iw, p1 = w1 * invW[1] / iw, p2 = w2 * invW[2] / iw;

            const Vec3 worldPos = vs[0]->world * p0 + vs[1]->world * p1 + vs[2]->world * p2;
            const Vec3 normal = vs[0]->normal * p0 + vs[1]->normal * p1 + vs[2]->normal * p2;
            const Vec2 uv{vs[0]->uv.x * p0 + vs[1]->uv.x * p1 + vs[2]->uv.x * p2,
                          vs[0]->uv.y * p0 + vs[1]->uv.y * p1 + vs[2]->uv.y * p2};

            Color lit = shade(worldPos, normal, uv, item, scene, view);
            ++stats_.pixelsShaded;

            if (opacity >= 1.0f) {
                dst = z;
                target.setPixel(x, y, lit);
            } else {
                // Translucent surfaces blend and do not write depth, so
                // what is behind them stays visible.
                target.blend(x, y, lit, opacity);
            }
        }
    }
}

void SoftwareRenderer::drawLine3D(Framebuffer& target, const Mat4& viewProj, const Vec3& a,
                                  const Vec3& b, const Color& color) {
    Vec4 ca = viewProj * Vec4(a, 1.0f);
    Vec4 cb = viewProj * Vec4(b, 1.0f);
    // Both ends must be in front of the camera; clipping a debug line is
    // not worth the code.
    if (ca.w <= 1e-4f || cb.w <= 1e-4f) return;

    const float x0 = (ca.x / ca.w * 0.5f + 0.5f) * (float)width_;
    const float y0 = (1.0f - (ca.y / ca.w * 0.5f + 0.5f)) * (float)height_;
    const float x1 = (cb.x / cb.w * 0.5f + 0.5f) * (float)width_;
    const float y1 = (1.0f - (cb.y / cb.w * 0.5f + 0.5f)) * (float)height_;

    const int steps = (int)std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)) + 1;
    if (steps > 4000) return;
    for (int i = 0; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        target.blend((int)std::lround(lerpf(x0, x1, t)), (int)std::lround(lerpf(y0, y1, t)), color, 1.0f);
    }
}

void SoftwareRenderer::drawItem(Framebuffer& target, const DrawItem& item, const RenderScene& scene,
                                const RenderView& view, const Mat4& viewProj) {
    const Mesh& mesh = *item.mesh;
    const Mat4 mvp = viewProj * item.transform;

    ShadedVertex poly[8], clipped[8];
    auto mix = [](const ShadedVertex& p, const ShadedVertex& q, float t) {
        ShadedVertex r;
        r.clip = p.clip + (q.clip - p.clip) * t;
        r.world = lerp(p.world, q.world, t);
        r.normal = lerp(p.normal, q.normal, t);
        r.uv = {lerpf(p.uv.x, q.uv.x, t), lerpf(p.uv.y, q.uv.y, t)};
        return r;
    };

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        ++stats_.trianglesSubmitted;
        for (int k = 0; k < 3; ++k) {
            const Vertex& src = mesh.vertices[mesh.indices[i + (size_t)k]];
            poly[k].clip = mvp * Vec4(src.position, 1.0f);
            poly[k].world = item.transform.transformPoint(src.position);
            poly[k].normal = item.transform.transformNormal(src.normal);
            poly[k].uv = src.uv;
        }

        const int n = clipNear(poly, 3, clipped, view.nearClip, mix);
        // Clipping a triangle against one plane yields a triangle or a
        // quad; fan-triangulate whatever comes out.
        for (int k = 2; k < n; ++k)
            rasterTriangle(target, clipped[0], clipped[k - 1], clipped[k], item, scene, view);
    }
}

// -----------------------------------------------------------------
//  Frame
// -----------------------------------------------------------------

void SoftwareRenderer::render(Framebuffer& target, const RenderScene& scene, const RenderView& view) {
    const auto start = std::chrono::steady_clock::now();
    stats_ = RenderStats{};

    if (!target.valid()) return;
    target_ = &target;
    width_ = target.width();
    height_ = target.height();
    depth_.assign((size_t)width_ * (size_t)height_, 1.0f);

    // Sky: a vertical gradient from the horizon colour to the zenith,
    // which reads as a sky without costing a cubemap.
    for (int y = 0; y < height_; ++y) {
        const float t = (float)y / (float)std::max(1, height_ - 1);
        const Vec3 sky = lerp(scene.environment.skyColor.rgb(),
                              scene.environment.background.rgb(), smoothStep(0.0f, 0.85f, t));
        Color* row = target.colorRow(y);
        for (int x = 0; x < width_; ++x) row[x] = Color{sky.x, sky.y, sky.z, 1.0f};
    }

    renderShadowMap(scene, view);

    const Mat4 viewProj = view.projection * view.view;
    const Frustum frustum = Frustum::fromMatrix(viewProj);

    for (const DrawItem& item : scene.items) {
        if (!item.mesh || item.mesh->indices.empty()) continue;
        // Cull before transforming anything: the cheapest triangle is the
        // one never submitted.
        if (item.worldBounds.valid() && !frustum.intersects(item.worldBounds)) {
            ++stats_.itemsCulled;
            continue;
        }
        drawItem(target, item, scene, view, viewProj);
    }

    // Editor overlays, drawn after the scene so they are never occluded.
    if (showBounds || wireframe) {
        for (const DrawItem& item : scene.items) {
            if (!item.worldBounds.valid()) continue;
            if (!showBounds && !item.selected) continue;
            const Box& b = item.worldBounds;
            const Color c = item.selected ? Color::fromHex("#ffb454") : Color::fromHex("#5fa8d3");
            const Vec3 corners[8] = {
                {b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z},
                {b.max.x, b.min.y, b.max.z}, {b.min.x, b.min.y, b.max.z},
                {b.min.x, b.max.y, b.min.z}, {b.max.x, b.max.y, b.min.z},
                {b.max.x, b.max.y, b.max.z}, {b.min.x, b.max.y, b.max.z}};
            const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
                                      {6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (const auto& e : edges) drawLine3D(target, viewProj, corners[e[0]], corners[e[1]], c);
        }
    } else {
        for (const DrawItem& item : scene.items) {
            if (!item.selected || !item.worldBounds.valid()) continue;
            const Box& b = item.worldBounds;
            const Color c = Color::fromHex("#ffb454");
            const Vec3 corners[8] = {
                {b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z},
                {b.max.x, b.min.y, b.max.z}, {b.min.x, b.min.y, b.max.z},
                {b.min.x, b.max.y, b.min.z}, {b.max.x, b.max.y, b.min.z},
                {b.max.x, b.max.y, b.max.z}, {b.min.x, b.max.y, b.max.z}};
            const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
                                      {6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (const auto& e : edges) drawLine3D(target, viewProj, corners[e[0]], corners[e[1]], c);
        }
    }

    // Resolve: exposure, tone map, vignette. Done as a pass rather than
    // per-pixel in shade() so overlays are graded with the scene.
    const float cx = (float)width_ * 0.5f, cy = (float)height_ * 0.5f;
    const float maxR = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < height_; ++y) {
        Color* row = target.colorRow(y);
        for (int x = 0; x < width_; ++x) {
            Vec3 c = row[x].rgb() * scene.environment.exposure;
            c = acesToneMap(c);
            if (scene.environment.vignette > 0.0f) {
                const float dx = ((float)x - cx) / maxR, dy = ((float)y - cy) / maxR;
                const float r = std::sqrt(dx * dx + dy * dy);
                c = c * (1.0f - scene.environment.vignette * smoothStep(0.45f, 1.0f, r));
            }
            row[x] = Color{c.x, c.y, c.z, 1.0f};
        }
    }

    stats_.milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

float SoftwareRenderer::depthAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 1.0f;
    return depth_[(size_t)y * (size_t)width_ + (size_t)x];
}

bool SoftwareRenderer::worldPositionAt(int x, int y, const RenderView& view, Vec3& out) const {
    const float z = depthAt(x, y);
    if (z >= 1.0f) return false;   // nothing was drawn there

    // Screen back to world: undo the viewport, then the projection.
    const float ndcX = ((float)x + 0.5f) / (float)width_ * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((float)y + 0.5f) / (float)height_ * 2.0f;
    const float ndcZ = z * 2.0f - 1.0f;

    const Mat4 inv = (view.projection * view.view).inverse();
    const Vec4 p = inv * Vec4{ndcX, ndcY, ndcZ, 1.0f};
    if (std::fabs(p.w) < 1e-8f) return false;
    out = Vec3{p.x / p.w, p.y / p.w, p.z / p.w};
    return true;
}

} // namespace forge
