#version 410 core

out vec4 fragColor;

uniform sampler2D tCurrent;
uniform sampler2D tHistory;
uniform sampler2D tDepth;

uniform mat4 uInvViewProj;
uniform mat4 uPrevViewProj;
uniform vec2 uTexelSize;
uniform vec2 uJitter;

vec3 rgbToYCoCg(vec3 c) {
    return vec3(
         0.25 * c.r + 0.50 * c.g + 0.25 * c.b,
         0.50 * c.r                - 0.50 * c.b,
        -0.25 * c.r + 0.50 * c.g - 0.25 * c.b
    );
}

vec3 yCoCgToRgb(vec3 c) {
    float t = c.x - c.z;
    return vec3(t + c.y, c.x + c.z, t - c.y);
}

vec4 sampleCatmullRom(sampler2D tex, vec2 uv, vec2 texSize) {
    vec2 pos    = uv * texSize - 0.5;
    vec2 f      = fract(pos);
    vec2 center = floor(pos) + 0.5;

    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    vec2 w12  = w1 + w2;
    vec2 tc0  = (center - 1.0) / texSize;
    vec2 tc12 = (center + w2 / w12) / texSize;
    vec2 tc3  = (center + 2.0) / texSize;

    vec4 result =
        texture(tex, vec2(tc0.x,  tc0.y))  * (w0.x  * w0.y)  +
        texture(tex, vec2(tc12.x, tc0.y))  * (w12.x * w0.y)  +
        texture(tex, vec2(tc3.x,  tc0.y))  * (w3.x  * w0.y)  +
        texture(tex, vec2(tc0.x,  tc12.y)) * (w0.x  * w12.y) +
        texture(tex, vec2(tc12.x, tc12.y)) * (w12.x * w12.y) +
        texture(tex, vec2(tc3.x,  tc12.y)) * (w3.x  * w12.y) +
        texture(tex, vec2(tc0.x,  tc3.y))  * (w0.x  * w3.y)  +
        texture(tex, vec2(tc12.x, tc3.y))  * (w12.x * w3.y)  +
        texture(tex, vec2(tc3.x,  tc3.y))  * (w3.x  * w3.y);

    return max(result, vec4(0.0));
}

vec3 clipAABB(vec3 aabbMin, vec3 aabbMax, vec3 history) {
    vec3 center = 0.5 * (aabbMax + aabbMin);
    vec3 extent = 0.5 * (aabbMax - aabbMin) + 1e-6;
    vec3 offset = history - center;
    vec3 ts     = abs(offset / extent);
    float t     = max(ts.x, max(ts.y, ts.z));
    return t > 1.0 ? center + offset / t : history;
}

void main() {
    vec2 screenSize = 1.0 / uTexelSize;
    vec2 uv = gl_FragCoord.xy * uTexelSize;

    // Unjitter current frame sample
    vec2 unjitteredUV = uv - uJitter * uTexelSize;
    vec3 current = texture(tCurrent, unjitteredUV).rgb;

    // 3x3 neighborhood in YCoCg for clamping
    vec3 nMin = vec3( 1e10);
    vec3 nMax = vec3(-1e10);
    vec3 m1   = vec3(0.0);
    vec3 m2   = vec3(0.0);

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 sampleUV = unjitteredUV + vec2(x, y) * uTexelSize;
            vec3 s = rgbToYCoCg(texture(tCurrent, sampleUV).rgb);
            nMin = min(nMin, s);
            nMax = max(nMax, s);
            m1  += s;
            m2  += s * s;
        }
    }

    // Variance-based AABB (tighter than pure min/max)
    vec3 mu    = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mu * mu, vec3(0.0)));
    vec3 boxMin = mu - 1.25 * sigma;
    vec3 boxMax = mu + 1.25 * sigma;
    // Intersect with hard min/max to stay safe
    boxMin = max(boxMin, nMin);
    boxMax = min(boxMax, nMax);

    // Reproject: reconstruct world position from depth
    float depth = texture(tDepth, uv).r;
    vec4 clip   = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world  = uInvViewProj * clip;
    world      /= world.w;

    vec4 prevClip = uPrevViewProj * world;
    vec2 historyUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // Off-screen rejection
    bool offScreen = any(lessThan(historyUV, vec2(0.0))) ||
                     any(greaterThan(historyUV, vec2(1.0)));

    // Sample history with Catmull-Rom for sharpness
    vec3 history = sampleCatmullRom(tHistory, historyUV, screenSize).rgb;

    // Clamp history in YCoCg
    vec3 histYCoCg = rgbToYCoCg(history);
    histYCoCg      = clipAABB(boxMin, boxMax, histYCoCg);
    history        = yCoCgToRgb(histYCoCg);

    // Blend factor: mostly keep history, increase for high-variance areas
    float varianceMag = length(sigma);
    float blendFactor = mix(0.05, 0.20, clamp(varianceMag * 4.0, 0.0, 1.0));

    if (offScreen) blendFactor = 1.0;

    fragColor = vec4(mix(history, current, blendFactor), 1.0);
}
