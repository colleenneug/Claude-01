#version 460 core
// =============================================================================
//  pbr.frag — Cook-Torrance microfacet BRDF, clustered forward.
//
//  THE MODEL
//      f = f_diffuse + f_specular
//      f_diffuse  = (1 - F) * (1 - metallic) * albedo / PI          [Lambert]
//      f_specular = D * G * F / (4 * NdotL * NdotV)                 [Cook-Torrance]
//
//  with
//      D = GGX / Trowbridge-Reitz normal distribution
//      G = Smith height-correlated visibility
//      F = Schlick's Fresnel approximation
//
//  Three details that are usually what separates "PBR" from PBR:
//
//  1. ENERGY. The (1 - F) factor on the diffuse term is what conserves energy
//     between the two lobes. Without it, grazing angles gain energy and
//     everything looks subtly waxy.
//
//  2. HEIGHT-CORRELATED SMITH. The separable Smith G is the common shortcut;
//     the height-correlated form (Heitz 2014) is barely more expensive and is
//     noticeably better at high roughness, where the separable form loses
//     energy. Written below in its visibility form (V = G / (4 NdotL NdotV)),
//     which folds the denominator in and removes a division.
//
//  3. LINEAR SPACE, ALWAYS. Albedo textures are sRGB and must be decoded on
//     sample (use an _SRGB image format so the hardware does it free).
//     Lighting is linear. Tonemap and encode once, at the end of the frame,
//     never here. Every "why does my PBR look washed out" is one of these.
//
//  CLUSTERED LOOKUP
//  The froxel index is derived from gl_FragCoord and the view depth using the
//  exact same scale/bias the CPU used in ClusteredLighting.cpp. If these two
//  ever disagree, lights pop at froxel boundaries — keep them in a shared
//  header if your toolchain allows it.
// =============================================================================

const float PI = 3.14159265359;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormalWS;
layout(location = 2) in vec4 vTangentWS;
layout(location = 3) in vec2 vTexCoord;
layout(location = 4) in vec4 vCurrentClip;
layout(location = 5) in vec4 vPreviousClip;

layout(set = 0, binding = 0, std140) uniform FrameUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 previousViewProjection;
    vec4 cameraPositionWS;
    vec4 sunDirectionWS;
    vec4 sunColour;
    vec4 clusterParams;
    vec4 depthSliceParams;
    vec4 viewportSize;
} uFrame;

struct GpuLight {
    vec3  positionVS;
    float radius;
    vec3  colour;
    float intensity;
};

layout(set = 0, binding = 1, std430) readonly buffer LightBuffer   { GpuLight lights[]; };
layout(set = 0, binding = 2, std430) readonly buffer ClusterOffsets { uvec2 clusterOffsets[]; };
layout(set = 0, binding = 3, std430) readonly buffer ClusterIndices { uint clusterIndices[]; };

layout(set = 1, binding = 0) uniform sampler2D uAlbedo;
layout(set = 1, binding = 1) uniform sampler2D uNormalMap;
layout(set = 1, binding = 2) uniform sampler2D uMetallicRoughness;   // .b metal, .g rough (glTF)
layout(set = 1, binding = 3) uniform samplerCube uIrradiance;        // diffuse IBL
layout(set = 1, binding = 4) uniform samplerCube uPrefilteredEnv;    // specular IBL, roughness in mips
layout(set = 1, binding = 5) uniform sampler2D uBrdfLut;             // split-sum DFG term

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 previousModel;
    vec4 materialParams;
    vec4 baseColour;
} uDraw;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outMotion;   // for TAA / motion blur

// ---------------------------------------------------------------- BRDF terms

// GGX / Trowbridge-Reitz. The long tail is why it beats Blinn-Phong: real
// specular highlights have one, and it is most of what makes metal read as
// metal rather than as plastic.
float distributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;       // perceptual -> linear roughness
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

// Smith height-correlated visibility: G / (4 NdotL NdotV), already divided.
float visibilitySmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (1.0 - F0) * f;
}

// Roughness-aware Fresnel for the IBL term. Plain Schlick with a constant F90
// of 1 makes rough metal rim-light like a mirror.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    vec3 F90 = max(vec3(1.0 - roughness), F0);
    return F0 + (F90 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// One light's contribution. Shared by the sun and by every clustered point
// light so the two can never drift out of agreement.
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Dielectrics reflect ~4% at normal incidence; metals reflect their albedo
    // and have no diffuse lobe at all. That single lerp is the whole of what
    // "metallic workflow" means.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = distributionGGX(NdotH, roughness);
    float Vis = visibilitySmithGGXCorrelated(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(VdotH, F0);

    vec3 specular = D * Vis * F;
    // Energy conservation: whatever was reflected specularly is not available
    // to scatter diffusely.
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

// ---------------------------------------------------------------- clustering
uint clusterIndexForFragment(float viewDepth) {
    uint tilesX  = uint(uFrame.clusterParams.x);
    uint tilesY  = uint(uFrame.clusterParams.y);
    uint slicesZ = uint(uFrame.clusterParams.z);

    vec2 tileSize = uFrame.viewportSize.xy / vec2(tilesX, tilesY);
    uvec2 tile = uvec2(gl_FragCoord.xy / tileSize);
    tile = min(tile, uvec2(tilesX - 1u, tilesY - 1u));

    // Identical to ClusterGrid::sliceForDepth. If you change one, change both.
    float scale = uFrame.depthSliceParams.x;
    float bias  = uFrame.depthSliceParams.y;
    uint slice = uint(clamp(floor(log2(max(viewDepth, 1e-4)) * scale + bias),
                            0.0, float(slicesZ - 1u)));

    return (slice * tilesY + tile.y) * tilesX + tile.x;
}

// Inverse-square with a smooth window. The raw 1/d^2 never reaches zero, so a
// light's influence would extend past its culling radius and pop at the froxel
// boundary; the windowing term forces it to exactly zero at `radius`.
float distanceAttenuation(float distanceSq, float radius) {
    float factor = distanceSq / max(radius * radius, 1e-6);
    float smoothFactor = clamp(1.0 - factor * factor, 0.0, 1.0);
    return (smoothFactor * smoothFactor) / max(distanceSq, 1e-4);
}

void main() {
    // ------------------------------------------------------------- material
    vec4 albedoSample = texture(uAlbedo, vTexCoord) * uDraw.baseColour;
    vec3 albedo = albedoSample.rgb;

    vec2 mr = texture(uMetallicRoughness, vTexCoord).bg;
    float metallic  = mr.x * uDraw.materialParams.x;
    float roughness = clamp(mr.y * uDraw.materialParams.y, 0.045, 1.0);
    // Clamped away from zero: a perfectly smooth surface makes the GGX
    // denominator explode into specular aliasing that no amount of TAA hides.

    // ------------------------------------------------------------- normal
    vec3 N = normalize(vNormalWS);
    vec3 T = normalize(vTangentWS.xyz - N * dot(N, vTangentWS.xyz));   // Gram-Schmidt
    vec3 B = cross(N, T) * vTangentWS.w;
    vec3 tangentNormal = texture(uNormalMap, vTexCoord).xyz * 2.0 - 1.0;
    N = normalize(mat3(T, B, N) * tangentNormal);

    vec3 V = normalize(uFrame.cameraPositionWS.xyz - vWorldPos);

    vec3 Lo = vec3(0.0);

    // ------------------------------------------------------------- sun
    // TODO(shadows): sample the cascaded shadow map here and multiply the
    // radiance. Select the cascade by view depth with a blend band, or the
    // seam between cascades is a visible line across the ground.
    Lo += shade(N, V, normalize(-uFrame.sunDirectionWS.xyz),
                uFrame.sunColour.rgb * uFrame.sunDirectionWS.w,
                albedo, metallic, roughness);

    // ------------------------------------------------------------- clustered
    float viewDepth = -(uFrame.view * vec4(vWorldPos, 1.0)).z;
    uvec2 range = clusterOffsets[clusterIndexForFragment(viewDepth)];
    for (uint i = 0u; i < range.y; ++i) {
        GpuLight L = lights[clusterIndices[range.x + i]];

        // Lights are stored in view space for the binning pass; bring the
        // vector back to world space to shade in one consistent basis.
        vec3 lightWS = (inverse(uFrame.view) * vec4(L.positionVS, 1.0)).xyz;
        vec3 toLight = lightWS - vWorldPos;
        float distSq = dot(toLight, toLight);
        float atten = distanceAttenuation(distSq, L.radius);
        if (atten <= 0.0) continue;

        Lo += shade(N, V, toLight * inversesqrt(max(distSq, 1e-8)),
                    L.colour * L.intensity * atten, albedo, metallic, roughness);
    }

    // ------------------------------------------------------------- IBL
    // Split-sum approximation (Karis 2013): the irradiance cube carries the
    // diffuse integral, the prefiltered cube carries the specular one with
    // roughness in its mip chain, and the 2D LUT carries the scale/bias on F0.
    float NdotV = max(dot(N, V), 1e-4);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    vec3 irradiance = texture(uIrradiance, N).rgb;
    vec3 diffuseIBL = irradiance * albedo * kD;

    const float kMaxReflectionLod = 6.0;
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(uPrefilteredEnv, R, roughness * kMaxReflectionLod).rgb;
    vec2 dfg = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefiltered * (F * dfg.x + dfg.y);

    float ao = uDraw.materialParams.z;
    vec3 ambient = (diffuseIBL + specularIBL) * ao;

    vec3 emissive = albedo * uDraw.materialParams.w;

    // Linear HDR out. Tonemapping and the sRGB encode happen once, in the
    // composite pass, after bloom and volumetrics have been added — tonemapping
    // here would clamp the very energy the bloom pass is looking for.
    outColour = vec4(Lo + ambient + emissive, albedoSample.a);

    // ------------------------------------------------------------- motion
    vec2 currentNdc  = vCurrentClip.xy  / max(vCurrentClip.w, 1e-6);
    vec2 previousNdc = vPreviousClip.xy / max(vPreviousClip.w, 1e-6);
    outMotion = (currentNdc - previousNdc) * 0.5;
}
