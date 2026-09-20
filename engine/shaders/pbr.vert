#version 460 core
// =============================================================================
//  pbr.vert — forward PBR vertex stage.
//
//  Outputs world-space position and a TBN basis. World space rather than view
//  space for the lighting basis because IBL, shadow cascades and volumetrics
//  all want world space, and converting three times per pixel to save one
//  matrix multiply per vertex is the wrong trade.
//
//  PREVIOUS-FRAME CLIP POSITION is carried through for motion vectors. TAA and
//  motion blur both need it, and retrofitting it later means touching every
//  vertex shader and every pipeline layout in the project — so it is here from
//  the start even though the first renderer will not read it.
// =============================================================================

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;    // .w is the bitangent handedness
layout(location = 3) in vec2 aTexCoord;

layout(set = 0, binding = 0, std140) uniform FrameUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 previousViewProjection;
    vec4 cameraPositionWS;   // .w = time in seconds
    vec4 sunDirectionWS;     // .w = sun intensity
    vec4 sunColour;
    vec4 clusterParams;      // x,y = tiles, z = slices, w unused
    vec4 depthSliceParams;   // x = scale, y = bias, z = near, w = far
    vec4 viewportSize;       // xy = pixels, zw = 1/pixels
} uFrame;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 previousModel;
    vec4 materialParams;     // x = metallic, y = roughness, z = ao, w = emissive
    vec4 baseColour;
} uDraw;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormalWS;
layout(location = 2) out vec4 vTangentWS;
layout(location = 3) out vec2 vTexCoord;
layout(location = 4) out vec4 vCurrentClip;
layout(location = 5) out vec4 vPreviousClip;

void main() {
    vec4 worldPos = uDraw.model * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;

    // The inverse-transpose is required for non-uniform scale; skipping it is
    // why normals go wrong on squashed geometry. Computed per vertex here for
    // clarity — ship it as a precomputed normal matrix in the push constant.
    mat3 normalMatrix = transpose(inverse(mat3(uDraw.model)));
    vNormalWS  = normalize(normalMatrix * aNormal);
    vTangentWS = vec4(normalize(mat3(uDraw.model) * aTangent.xyz), aTangent.w);

    vTexCoord = aTexCoord;

    vCurrentClip  = uFrame.viewProjection * worldPos;
    vPreviousClip = uFrame.previousViewProjection * (uDraw.previousModel * vec4(aPosition, 1.0));

    gl_Position = vCurrentClip;
}
