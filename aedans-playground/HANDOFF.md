# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** real-time sun shadows and HDR post-processing are in, on top
of PBR and image-based lighting (start of this milestone: commit
`e65e43e`).
- **HDR target:** meshes and the sky render into an offscreen
  `R16G16B16A16_SFLOAT` image with the depth buffer. A composite pass
  applies bloom, exposure and ACES (or PBR Neutral) tone mapping into the
  swapchain, encoding sRGB exactly once: in the shader only for `_UNORM`
  swapchains. Sprites draw after it, untouched.
- **Sun shadow map:** a 2048² `D32_SFLOAT` map from an orthographic light
  box. The box is fitted to a bounding sphere of the scene and
  texel-snapped.
  - Acne is handled with hardware depth bias plus a normal offset.
  - Shadows are sampled with 3×3 PCF through a `sampler2DShadow`, and fade
    out at the map's edges.
- **Bloom:** a half-size mip chain. The first downsample applies a
  soft-knee threshold and the Karis average; the glow is added to the
  scene.
- **Emissive materials** in the renderer and the glTF loader.
- **The demo:** a low sun casting shadows, two glowing lamps, and the
  user's favourite, **the Damaged Helmet** (kept by request) on a
  pedestal.
- Screenshot: `docs/shadows-bloom.png`.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- **Long-term goal (user):** Erebus Cradle (the game in `cpp/`) will be moved
  onto this engine eventually; we are still far from that. Roadmap Milestone 5
  covers the port.
- **The helmet's licence:** `app/assets/third_party/DamagedHelmet/` is
  CC BY 4.0 (ctxwing's glTF rebuild) on top of CC BY-NC 4.0
  (theblueturtle_'s original). It is credited in its `LICENSE.md` and in
  the README caption, and is **non-commercial only**: it must be removed
  before anything built on this engine is sold. Everything else in
  `app/assets/` is ours (`tools/make_demo_assets.py`).
- **Environment:** the cloud environment's setup script installs every
  package. Not yet proven in a fresh session; if configure cannot find
  `tiny_gltf.h`, install the packages listed in `README.md`.
- Standing guidelines:
  - modern C++20 (smart pointers, RAII, `std::span`, designated initialisers);
  - no over-engineering;
  - clean, commented, production-ready code;
  - strict error handling and logging for graphics initialisation.
- Decisions:
  - C++20 with `tl::expected`;
  - Windows and Linux only;
  - a `Renderer` interface, with no Vulkan types above render;
  - our own ECS;
  - storage buffers read by device address;
  - reverse-Z everywhere, **including the shadow map** (bias is negative;
    the compare op is GREATER_OR_EQUAL);
  - asset decode on workers, finalize on the main thread;
  - bindless textures;
  - one pipeline layout for scene pipelines;
  - generational handles with deferred freeing;
  - environment maps are set 1, bindings 1–4, and **the shadow map is
    binding 5**;
  - **meshes and the sky output linear HDR; tone mapping happens only in
    the composite pass**;
  - **sprites are composited after tone mapping, against the scene's
    depth**;
  - **sprites-only frames skip the HDR path**;
  - **bloom is added (`scene + glow × strength`), not mixed**;
  - **`FrameData` settings for shadows, bloom and tone mapping are off by
    default** (exact-colour tests stay exact), and **persist between
    frames**.
- The user's milestone numbers run ahead of `ROADMAP.md`'s.
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
core      Result, Log, Assert, FrameClock, FixedStep, Types, JobSystem
platform  Platform, Window, Input
gpu       Context, Buffer, Image (cube maps, views, half floats), Sampler, Swapchain (+ preferSrgb),
          DeviceHandle
render    Renderer, FrameData (+ ToneMap, BloomSettings, ShadowSettings), Resources (+ emissive,
          castsShadow), Environment, Shadow (new: Aabb, directionalShadowMatrix);
          VulkanRenderer (private) + Mesh (+ bounds), ResourcePool, IblBaker, BrdfLut,
          PostProcess (new), ShadowMap (new)
engine    Application, Camera, CameraController, AssetManager, Gltf (+ emissive; Bounds = render::Aabb),
          EnvironmentAsset, systems
app       ModelScene (shadows, bloom, ACES, lamps, helmet) / SpriteScene
```

**A frame** (`VulkanRenderer::recordCommands`):
- **When there are meshes or a sky:**
  1. **Shadow map**, if `shadows.enabled` and something casts:
     `ShadowMap::record`.
     - Layout DEPTH_STENCIL_READ_ONLY → DEPTH_ATTACHMENT; clear to 0;
       unflipped viewport.
     - `vkCmdSetDepthBias(−depthBias, 0, −slopeBias)`.
     - `shadow.vert` draws the casters (`firstInstance` = draw entry).
     - Back to DEPTH_STENCIL_READ_ONLY.
  2. **Scene:**
     - HDR target UNDEFINED → COLOR_ATTACHMENT; depth cleared, stored.
     - Meshes (culled, then double-sided pipeline), then the sky.
     - HDR → SHADER_READ_ONLY; a depth memory barrier for the sprites.
  3. **Bloom**, if enabled (`PostProcess::recordBloom`).
     - Down: levels 0..n−1, each UNDEFINED → COLOR_ATTACHMENT, a
       full-screen pass, → SHADER_READ_ONLY. Level 0 reads the HDR image
       with the threshold and Karis average.
     - Up: levels n−2..0, each SHADER_READ_ONLY → COLOR_ATTACHMENT, loaded
       and blended ONE + ONE with the level below.
  4. **Swapchain:**
     - UNDEFINED → COLOR_ATTACHMENT (load: DONT_CARE); depth loaded.
     - The composite (`tonemap.frag`, full-screen, reading pixels by
       `gl_FragCoord`).
     - Sprites, after rebinding the scene sets.
- **Sprites only:** step 4 alone, with the swapchain and depth cleared as
  before. The clear colour is sRGB-encoded on the CPU for `_UNORM`
  swapchains.

**`PostProcess`** (`render/src/PostProcess.*`):
- Owns:
  - a clamp-linear sampler;
  - a post set layout (binding 0: source, binding 1: bloom) and pipeline
    layout with a 32-byte `PostPush`;
  - bloom down and up pipelines (RGBA16F);
  - the composite pipeline for the swapchain format. Its
    `kEncodeSrgb` specialization constant is true for `_UNORM` formats,
    and it declares `kDepthFormat` because it shares a rendering with the
    sprites.
- `resize(extent)`:
  - destroys the old sets/pool, views and images, in that order;
  - creates the HDR image, the bloom image (half size, levels =
    clamp(bit_width(min side) − 2, 1, 6)) and a view per level;
  - creates the sets: `levels` down, `levels − 1` up, and one composite;
  - moves the bloom image to SHADER_READ_ONLY (the composite's descriptor
    must be valid even with bloom off).
- Called from `createSwapchain` after the depth buffer is rebuilt; the GPU
  has been waited on before that.

**`ShadowMap`** (`render/src/ShadowMap.*`):
- The image starts in DEPTH_STENCIL_READ_ONLY.
- Comparison sampler: compare op GREATER_OR_EQUAL; linear filtering if the
  format supports it (it does on llvmpipe), otherwise nearest; clamp to
  border, with a border depth of 0, which reads as lit.
- A depth-only pipeline: `shadow.vert` alone, position-only vertex input,
  no culling, depth bias enabled (dynamic), the scene's pipeline layout.

**Framing** (`render/Shadow.h`):
- `directionalShadowMatrix(direction, Aabb, mapSize)` returns
  `{viewProjection, texelWorldSize}`.
- A rotation-only `lookAtRH` from the origin along the light ("up" is
  switched when the light is near vertical).
- A sphere around the bounds (radius × 1.02); its centre's light-space x/y
  are snapped to texel multiples.
- `reverseZ() × orthoRH_ZO(…, −c.z − r, −c.z + r)`.
- The renderer fits to `shadows.bounds` or to the union of every drawn
  mesh's world bounds (`Mesh::bounds().transformed`).

**Shaders:**
- `frame.glsl` (288-byte `FrameUniforms`): + `lightViewProjection`,
  + `shadow` (on, normal offset in world units = `normalOffset ×
  texelWorldSize`, strength, one texel in uv); binding 5
  `sampler2DShadow`. No tone mapping any more.
- `pbr.frag`:
  - `sunVisibility()`: offset along the vertex normal (flipped for back
    faces), project, `uv = ndc × 0.5 + 0.5`, 9 compare taps, fade =
    (1 − smoothstep(0.9, 1, max|ndc.xy|)) × (depth in [0, 1]);
  - multiplies direct sunlight only;
  - adds emissive (`textures.w` × `emissive.rgb`).
- `MeshDrawGpu` is 192 bytes (+ emissive).
- `bloom.frag`: the 13-tap downsample with five groups (centre weight 0.5,
  corners 0.125; Karis-weighted on the first pass, then threshold), and a
  3×3 tent upsample.
- `tonemap.frag`: `scene + bloom × strength / levels`, × exposure, then
  ACES (Hill: input matrix, RRT+ODT fit, output matrix) or PBR Neutral,
  then sRGB encoding when `kEncodeSrgb`.
- `sprite.frag` also encodes when `kEncodeSrgb`.

**Demo** (`app/ModelScene.cpp`):
- Sun direction (0.55, −0.62, 0.55): low, from behind-left.
- Objects: floor, crate (spinning), small crate, spheres, pedestal crate,
  **helmet** (scale 0.38, at y 0.98), two `lamp.glb` (post + bulb with
  emissive strength 12).
- Shadows on, with bounds (−3.5, 0, −2.6)..(3.5, 2, 2.4); with `--model`,
  automatic bounds.
- Bloom: threshold 1.2, knee 0.6, strength 0.12. ACES, exposure 1.15.

**Verified** (GCC 13.3 debug and release, Clang 18.1, GCC + ASan + UBSan,
GCC + TSan; Mesa llvmpipe, Xvfb):

| Check | Result |
|---|---|
| Baseline at `e65e43e` | 17/17 suites passed |
| After | **19/19 suites** (new `shadow_tests`, `postfx_tests`) in debug, release, Clang and ASan; TSan 3/3, 3 runs, 0 race reports; 0 warnings |
| Validation | **0 messages of any kind**: every GPU test asserts 0 errors; no VUID appears in any GPU suite's output or the app's log; the app exits 0 (it would report warnings) |
| Test cases | core 22, job 10, asset 10, gltf 14, ibl 5, **shadow 6**, ecs 26, input 15, camera 16, app 14, gpu 17, render 12, mesh 11, environment 6, **postfx 9** |
| `shadow_tests` (CPU) | Aabb maths; every scene corner inside the map for 4 light directions (including straight down and from below); nearer the light = larger depth, centre within a texel of the middle; texel size = diameter × 1.02 / size; **snapping: a fixed point's position within its texel stays the same while the bounds drift over 20 steps**; zero direction / empty bounds give a usable matrix |
| `postfx_tests` (pixels) | box shadow: mid-shadow and **at the base** < 1/3 of lit (no peter-panning), beyond it lit (±6); shadows off: even (±6); **no acne**: a lit floor under a low sun varies ≤ 3 and matches shadows-off ≤ 3 at 560 points; **outside a small map: lit** like open floor (±6), not black; bloom: halo +30 over off, square clipped to white, **threshold above the light: unchanged (±3)**; **Karis**: 10× brighter (1000 vs 100) glows < 3× as much; **ACES** greys at 0.18 / 1 / 4 match a C++ reference (±3), ordered, 4× not clipped; **`B8G8R8A8_UNORM` vs `_SRGB` give the same mesh, sprite and background pixels (±2)**; **4 resizes**: HDR = swapchain size, bloom = half, live allocations unchanged; sprites-only frames skip HDR, a mesh or the sky does not |
| End-to-end | `camera_e2e` now waits for and checks N of N models (8 of 8); flew 3.13 units in 1 s at 3 units/s |
| Speed (release, llvmpipe, 1600×900) | 3D scene **~83 ms/frame** (was ~36 ms: + shadow map, 9-tap PCF, bloom, composite, the 46k-triangle helmet, all on the CPU); 2D scene ~36 ms (unchanged: fast path) |

**Not yet run on a real GPU or on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `render/include/playground/render/Shadow.h`, `render/src/Shadow.cpp` | `Aabb`, `ShadowProjection`, `directionalShadowMatrix` |
| `render/src/PostProcess.h`, `render/src/PostProcess.cpp` | `kHdrFormat`, `kMaxBloomLevels`, `PostProcess` (create, resize, buildComposite, recordBloom, recordComposite) |
| `render/src/ShadowMap.h`, `render/src/ShadowMap.cpp` | `ShadowMap` (create, buildPipeline, record) |
| `render/shaders/shadow.vert`, `fullscreen.vert`, `post_common.glsl`, `bloom.frag`, `tonemap.frag` | Shadow pass, post passes |
| `tests/shadow_tests.cpp`, `tests/postfx_tests.cpp` | 6 and 9 tests |
| `app/assets/models/lamp.glb` | Generated lamp (emissive bulb) |
| `app/assets/third_party/DamagedHelmet/DamagedHelmet.glb`, `LICENSE.md` | The helmet, with credit and licence terms |
| `docs/shadows-bloom.png` | Screenshot |

Renamed: `render/shaders/mesh.vert` → `pbr.vert`, `mesh.frag` → `pbr.frag`.

Changed:

| File | Change |
|---|---|
| `render/include/playground/render/FrameData.h` | `ToneMap` enum (replaces `bool toneMap`), `BloomSettings`, `ShadowSettings`, `shadows`, `bloom` |
| `render/include/playground/render/Resources.h` | `Material::emissiveFactor`, `Material::emissive`, `MeshInstance::castsShadow` |
| `render/include/playground/render/VulkanRenderer.h` | `VulkanRendererDesc::shadowMapSize`, `preferUnormSwapchain` |
| `render/src/VulkanRenderer.h/.cpp` | `createPostAndShadows`; HDR/shadow/bloom/composite recording; set 1 binding 5; scene pipelines target RGBA16F; sprite pipelines specialised for UNORM; 288-byte uniforms; 192-byte draws; scene bounds and casters per frame; `swapchainClear_`; diagnostics (`post()`, `shadowMap()`, `lastFrameUsedHdr()`, `lastShadowCasters()`, `lastLightViewProjection()`) |
| `render/src/Mesh.h/.cpp` | Model-space `bounds()` |
| `render/shaders/frame.glsl`, `mesh_common.glsl`, `pbr.frag`, `sky.frag`, `sprite.frag` | See section 1 |
| `gpu/include/playground/gpu/Swapchain.h`, `gpu/src/Swapchain.cpp` | `SwapchainDesc::preferSrgb`; UNORM formats are in the preference list |
| `engine/include/playground/engine/Gltf.h`, `engine/src/Gltf.cpp` | `Bounds` = `render::Aabb`; emissive factor, texture, `KHR_materials_emissive_strength` |
| `app/ModelScene.cpp` | Sun, lamps, pedestal, helmet, shadows with bounds, bloom, ACES, exposure |
| `tools/make_demo_assets.py` | `sphere_geometry`, `box_geometry`, `lamp()` |
| `tests/environment_tests.cpp` | Tone mapping uses `ToneMap::PbrNeutral` |
| `tests/e2e/camera.sh` | Waits for every model; checks N of N |
| `render/CMakeLists.txt`, `tests/CMakeLists.txt` | New sources, shaders, tests |
| `README.md` | Shadows and post-processing section, frame steps, helmet credit, layout, speed |

## 3. Code Snippet of the Core Loop/Active Component

`render/shaders/pbr.frag`, the sun's visibility (normal offset, 3×3 PCF, edge fade):

```glsl
float sunVisibility(vec3 worldPosition, vec3 surfaceNormal) {
  if (frame.shadow.x < 0.5) return 1.0;
  vec4 clip = frame.lightViewProjection * vec4(worldPosition + surfaceNormal * frame.shadow.y, 1.0);
  vec3 ndc = clip.xyz / clip.w;
  vec2 uv = ndc.xy * 0.5 + 0.5;  // the shadow pass's viewport is not flipped
  float edge = max(abs(ndc.x), abs(ndc.y));
  float inside = (1.0 - smoothstep(0.9, 1.0, edge)) * step(0.0, ndc.z) * step(ndc.z, 1.0);
  if (inside <= 0.0) return 1.0;
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      lit += texture(shadowMap, vec3(uv + vec2(x, y) * frame.shadow.w, ndc.z));  // hardware compare + filter
  return mix(1.0, lit / 9.0, inside * frame.shadow.z);
}
```

`render/src/Shadow.cpp`, framing with texel snapping:

```cpp
const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f), direction, up);   // rotation only
const f32 texel = 2.0f * radius / static_cast<f32>(size);
const glm::vec3 c = glm::vec3(view * glm::vec4(centre, 1.0f));
const f32 x = std::floor(c.x / texel) * texel;                          // the map slides in whole texels
const f32 y = std::floor(c.y / texel) * texel;
const glm::mat4 projection =
    reverseZ() * glm::orthoRH_ZO(x - radius, x + radius, y - radius, y + radius, -c.z - radius, -c.z + radius);
return {.viewProjection = projection * view, .texelWorldSize = texel};
```

`render/shaders/tonemap.frag`, the composite:

```glsl
vec3 colour = texelFetch(source, ivec2(gl_FragCoord.xy), 0).rgb;
if (push.a.w > 0.5) colour += textureLod(bloom, gl_FragCoord.xy / vec2(textureSize(source, 0)), 0.0).rgb
                              * push.b.x * push.a.z;      // glow / levels x strength: added, not mixed
colour *= push.a.x;                                         // exposure
if (push.a.y > 1.5) colour = toneMapPbrNeutral(colour); else if (push.a.y > 0.5) colour = toneMapAces(colour);
outColour = vec4(kEncodeSrgb ? encodeSrgb(colour) : colour, 1.0);  // sRGB once: here only for _UNORM swapchains
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Found and fixed this session**
- Bloom first used `mix(scene, glow, strength)`. With a threshold the glow
  is empty below it, so the mix darkened the whole image by
  `strength` (a test caught a halo pixel at 80 against 99 with bloom
  off). It is now additive.
- The bloom level count underflowed for windows under 4 pixels
  (unsigned `bit_width − 2`). It is now computed signed and clamped.
- `camera_e2e` expected exactly 4 models; it now counts.
- My own mistakes in tests and scripts: a UNORM test first used a
  top-down camera that sees sprites edge-on; a Python patch turned a
  string into a tuple (re-applied).
- **Flaky once:** `environment_tests` failed one run with "X11: Failed to
  open display :99" (Xvfb not ready). It passed 5 runs alone and every
  full run since. If it recurs, it is the virtual display, not the
  renderer.

**Not verified**
- A real GPU; Windows (vcpkg tinygltf/stb include paths, MSVC warnings).
- The environment setup script in a fresh session.

**Limitations, acceptable for now**
- **One shadow map for the whole scene** (no cascades). Fine for a
  demo-sized area; large worlds need cascaded shadow maps (split by
  distance from the camera).
- The shadow map is re-rendered every frame, even when nothing moved.
- PCF is a fixed 3×3 kernel (a fixed softness, not size-dependent
  penumbrae).
- Only the sun casts shadows; emissive lamps light nothing around them
  (no point lights yet).
- Bloom and the composite cost two full-screen-sized passes even on
  frames with bloom off (the composite always runs on the HDR path).
- On a `_UNORM` swapchain, translucent sprites blend in sRGB space (as
  most 2D UI does), not in linear light.
- Carried over: `setEnvironment` stalls; glTF limits (no skins,
  animation, alpha blend/mask); one GPU mesh per primitive; one draw per
  instance; no frustum culling; uploads `submitAndWait` per resource; the
  ECS doesn't hold 3D objects; fences, not timeline semaphores; no Vulkan
  debug names; no pipeline cache.

## 5. Immediate Next Steps for Next Weekend

1. **Point and spot lights:** a small light list in the uniforms or a
   storage buffer; lamps that actually light their surroundings.
   Clustered later (the roadmap's CPU clustered-light binner in `engine/`
   is ready to move over).
2. **3D in the ECS:** `Transform3D` and `MeshRenderer` components, an
   `extractModels` system; the demo's objects become entities.
3. **Cascaded shadow maps** for large outdoor areas (Erebus Cradle's open
   space), reusing `directionalShadowMatrix` per cascade.
4. **Faster frames on real hardware:** Vulkan debug names (RenderDoc), a
   pipeline cache, frustum culling, and batched uploads.
5. Smaller: alpha MASK/BLEND, matching the sun to an HDR panorama,
   auto-exposure.
6. Later: roadmap Milestone 4 (visual parity with `cpp/`) and Milestone 5
   (moving Erebus Cradle onto the engine).

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (if configure cannot find tiny_gltf.h, install the packages in
`README.md`; `xdotool` is needed for the e2e tests), then start step 1."
