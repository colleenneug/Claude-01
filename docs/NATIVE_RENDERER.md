# Native renderer — rule by rule

`cpp/` is a from-scratch C++/OpenGL implementation of the same rendering
brief given for the browser build (`docs/RENDERING.md`), built as a
standalone desktop application rather than a page. Nothing here reuses
three.js or the JS shader source directly — every technique is re-derived
against raw OpenGL 4.1 core — but the physical constructions (the fog
integral, the cascade split scheme, the bloom filter kernels, the ACES fit)
are deliberately kept mathematically identical to the browser build's, so
the two renderers agree on what "correct" looks like.

No texture assets ship with either renderer. The C++ version goes one step
further than the browser build's canvas-baked textures: every material
shades **procedurally from world position and normal** (triplanar-projected
noise), computed live in the fragment shader rather than baked to a bitmap
at load time. `cpp/shaders/pbr.frag` is the one material program every
opaque object in the scene uses.

## 1 — Physically based materials

**Armour** (`shadeArmour()` in `pbr.frag`): metalness pinned to 0.85,
roughness to 0.25, both passed down from `Scene.cpp` as `DrawItem::metallic
/ roughness`. A panel grid is projected triplanar (three axis-aligned 2D
projections blended by how much the surface normal faces each axis), with
seams, per-panel colour drift, and chip splotches that push local metalness
and roughness away from the two base values — paint reads as the dielectric
it is, and a chip punches through to bare metal — without ever moving the
*average* off the requested 0.85 / 0.25.

**Anisotropic reflections** (`anisoBendReflection()`): the vector used to
sample the environment cubemap is bent towards a grain axis before the
lookup — Kaplanyan's construction, the same one the browser build's
`shading.js` uses — so a highlight on brushed/rolled metal stretches along
the grain instead of reflecting as an isotropic round blob. Driven by
`DrawItem::anisoStrength`, applied to every armour surface.

**Layered terrain** (`shadeTerrain()`): two procedural "stone" layers —
different tint, different noise frequency — blended by a per-vertex weight
(`Mesh::terrainPlane`, painted with the same fbm construction as the browser
build's `planets.js`), plus a third, tighter-frequency detail layer folded
in with a whiteout normal blend. Metalness 0, roughness ~0.9 — a dielectric,
matching the brief's "low reflectivity" requirement — with the bump strength
carrying essentially all of the surface's character.

**High bump-mapping** (`bumpedNormal()`): rather than sampling a baked
normal map, the world-space gradient of the procedural height field is
computed by finite differences and subtracted from the geometric normal
(Blinn's method). See *What broke and how it was actually found* below for
why the finite-difference step size turned out to matter far more than
expected.

## 2 — Cinematic lighting and atmosphere

**Low-angle sun with cascaded shadows** (`CascadedShadowMap.{h,cpp}`): the
sun sits at 20° elevation (`Scene::build`), and three shadow cascades —
2048/2048/1024 — are refit every frame to a bounding sphere around each
slice of the view frustum, snapped to that cascade's own texel grid so
edges don't crawl as the camera moves. `pbr.frag`'s `shadowFactor()`
cross-fades between cascades by a smoothstep band around each split
distance, then runs a 3×3 PCF tap through a hardware comparison sampler
(`sampler2DShadow`, `GL_COMPARE_REF_TO_TEXTURE`) for the soft edge itself.

This is architecturally simpler than the browser build's CSM: the WebGL
version duplicates the sun as three separate `THREE.DirectionalLight`s
because three.js recompiles every material's shader when the scene's light
count changes, so it fakes cascades with three physical lights masked by
depth band. Nothing here has that constraint, so the C++ version does the
textbook version instead — one light, one shadow factor that blends across
three depth maps.

**Volumetric fog** (`composite.frag`): the analytic integral of an
exponential height-fog distribution along the view ray — the same closed
form as the browser build's `engine.js`, so haze pools in low ground and
thins with altitude, and looking towards the sun scatters its colour back
(`uInscatter`, weighted by `pow(dot(viewDir, -sunDir), 8.0)`). The ray is
reconstructed from the depth buffer through the inverse view-projection
matrix rather than carried as separate tan(fov) parameters, since the full
matrices are already on hand in a desktop renderer.

**Dust motes** (`motes.vert/frag`, buffer built in `Scene::build`): a fixed
buffer of a few thousand points, wrapped into a box around the camera in the
vertex shader with `mod()` — ported line-for-line from the browser build's
`atmos.js` — so walking through them gives real parallax at zero CPU cost,
and the same mote just reappears behind you as you pass it. Point size is
hard-clamped (see below); brightness rises sharply when a mote sits between
the camera and the sun (forward scattering), which is why dust stays
invisible until you turn into a beam.

**Image-based lighting** (`IBL.{h,cpp}`): a small room of flat-lit panels —
sky, ground, a bright key light, two coloured accents, the same "station"
preset as the browser build's `envmap.js` — captured into a cubemap by
rendering six 90° views from the origin, then `glGenerateMipmap`'d. The
specular term samples that mip chain at a roughness-driven LOD and the
diffuse term samples a near-max mip as a cheap irradiance approximation.
**This is a deliberate simplification**, called out where it happens in
`IBL.h`: a full split-sum prefilter would importance-sample a GGX lobe per
mip rather than box-filter one; for a scene this low-frequency (a handful of
large flat panels, no sharp reflected detail to lose) the box filter reads
as correct, but it is not the same algorithm three.js's `PMREMGenerator`
runs for the browser build.

## 3 — Camera and post-processing pipeline

`Renderer.cpp` renders into a linear HDR half-float buffer with no tone
mapping applied until the very last pass — the same ordering rule as the
browser build, and for the same reason: bloom and the fog's sun inscatter
need to see radiance a tone-mapped buffer has already thrown away.

**ACES filmic tone mapping** (`composite.frag`, `acesFilm()`): three's own
RRT/ODT fit, so the grade matches the browser build's.

**Bloom** (`Bloom.{h,cpp}`): a soft-knee bright pass, five levels of the
thirteen-tap "Next Generation Post Processing" downsample filter, and a 3×3
tent upsample blended additively back up the chain — identical construction
to the browser build's `engine.js`, including the per-level strength
(`uStrength = 0.62`) that keeps five summed levels from reading as a haze
over the whole frame.

**Depth of field** (`dof.frag` + the `uAim` blend in `composite.frag`): off
at the hip — the half-resolution blur pass doesn't even run
(`Renderer::renderFrame` only calls `renderDof()` when `camera.aim > 0.01`).
Holding the right mouse button pulls `Camera::aim` towards 1 over ~100ms,
which both narrows the FOV and opens a circle-of-confusion blur on anything
past the focus distance, exactly mirroring the browser build's weapon-ADS
behaviour.

## What broke, and how it was actually found

The first render was genuinely broken: the ground and every armour surface
were covered in dense black-and-white static, not a subtle texture issue but
something that made the scene nearly unreadable. It was tempting to guess
("probably needs more samples" or "probably a shadow bias problem") and
tune blindly. Instead each hypothesis was isolated by actually rendering a
variant and diffing the screenshot:

1. **Is it shadows?** Forced `shadowFactor()` to return `1.0` (no shadows
   at all) and re-rendered. The static didn't change at all. Ruled out.
2. **Is it the terrain material?** Bypassed `shadeTerrain()` entirely and
   rendered flat albedo with the geometric normal. The ground went
   completely smooth. Confirmed: the terrain shading function was the
   cause, and specifically —
3. **Albedo or bump?** Restored the procedural albedo but kept the
   geometric normal (no `bumpedNormal()` call). Still smooth. That
   isolated it to the bump-mapping code specifically.

With the cause narrowed to one function, the actual bug was visible on
inspection: `bumpedNormal()` estimated a gradient by finite differences with
a **fixed 4cm step**, but the noise it was differentiating has a highest
octave with a period as short as ~3.6cm at the frequencies some bump calls
used. Once the step is comparable to or larger than the signal's own
wavelength, a finite difference isn't measuring a slope any more — it's the
difference between two nearly uncorrelated points, i.e. numerical noise,
which then got amplified by the bump strength multiplier into a normal that
flipped almost randomly from one pixel to the next. This is why an earlier
attempt to fix it by fading the effect out with screen-space derivatives
(the textbook fix for *aliasing*) did nothing — the problem wasn't that a
real signal was under-sampled at a distance, it was that the "signal" being
measured up close was already wrong. The actual fix was making the
finite-difference step scale with each bump call's own frequency
(`eps = 0.12 / (scale * 5.1)`), plus — now that the derivative was
measuring something real — lowering the highest bump frequencies to
wavelengths a single point sample per pixel can actually resolve without a
mip chain to fall back on, which analytic procedural noise doesn't have.
The derivative-based fade was kept in on top of that fix; it's still the
correct behaviour for genuine distant-aliasing, just wasn't the bug.

Every image in this document's edit history was produced by rendering the
project under Xvfb with Mesa's llvmpipe software rasterizer
(`LIBGL_ALWAYS_SOFTWARE=1`, no physical GPU in the build environment) and
reading the framebuffer back — see the `EREBUS_DUMP_FRAME` mode documented
in `cpp/README.md`. The final build compiles with zero warnings under
`-Wall -Wextra` and produces zero `glGetError()` reports across a 60-frame
run covering the full shadow → scene → bloom → DoF → composite pipeline.

## Honest limits

- **No physical GPU was available to render on.** Everything above was
  verified for correctness (compiles, links, produces the expected geometry
  and radiance, zero GL errors) on a software rasterizer, which says
  nothing about frame rate on real hardware. The scene is small (a few
  hundred draw calls, three 2048²-or-smaller shadow maps, a 128px IBL
  cubemap) specifically so it has a good chance of running comfortably on
  real hardware, but that hasn't been measured.
- **The IBL prefilter is a box-filtered mip chain**, not full split-sum GGX
  importance sampling — see the note under rule 2.
- **No light shafts / god rays.** The browser build has a radial-blur shaft
  pass; it wasn't part of the eight rules asked for here, so it was left
  out to keep scope honest rather than silently added back in.
- **This is a rendering demo, not the game.** Missions, inventory, AI,
  economy, and netcode from the browser build were not ported. See
  `cpp/README.md`'s scope note.
