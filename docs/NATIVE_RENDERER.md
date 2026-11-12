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
roughness to 0.25, both passed down from `Level.cpp`/`Hostile.cpp` as `DrawItem::metallic
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
sun sits at 20° elevation (`Game::init`), and three shadow cascades —
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

**Dust motes** (`motes.vert/frag`, buffer built in `Game::init`): a fixed
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
- ~~**The IBL prefilter is a box-filtered mip chain**~~ — replaced with a
  GGX-prefiltered specular chain and a cosine-convolved irradiance cube; see
  *The realism pass* below.
- ~~**No light shafts / god rays.**~~ — added, as a march through the shadow
  cascades rather than a radial blur; see *The realism pass* below.
- **This is a rendering demo, not the game.** Missions, inventory, AI,
  economy, and netcode from the browser build were not ported. See
  `cpp/README.md`'s scope note.


## Update: this is now a game, not just a demo scene

Everything above still describes the rendering pipeline exactly — nothing
about the shaders, the cascades, the fog integral, or the post chain
changed. What changed is what feeds `Renderer::renderFrame()`: the
free-fly camera and static demo scene this document was originally written
against were replaced by an actual mission loop (`Game`, `Level`,
`Player`, `Weapon`, `Hostile`, `Content`) — see `cpp/README.md` for what
that adds and its own honest scope line. `Scene.{h,cpp}` no longer exists;
`Game::collect()` is what `Renderer` now calls where `Scene::collect()`
used to be, and it draws the level plus every live hostile instead of a
fixed demo layout.

One rendering-adjacent fix came out of that transition worth recording
here: the armour bump-mapping fade documented above (footprint-based,
`bumpedNormal()`) still aliased at oblique angles once tested against an
80m arena wall rather than the small demo props it was originally tuned
against. Rather than keep tuning a fade coefficient against a moving
target, armour shipped flat-normal (see the comment at
`shadeArmour`'s `s.bumpN = n;` in `shaders/pbr.frag`) — the seam/chip/drift
colour detail is already footprint-faded and alias-free on its own, and a
flat-shaded panel that reads cleanly beats a detailed one that shimmers.
Revisiting that bump with a proper analytic derivative, rather than a
finite-difference one, is the natural follow-up.

A second update, unrelated to rendering: the game now also has gear
(weapons/armour/cosmetics), a chits currency, a persistent save profile,
and a keyboard-driven Hub between missions — see `cpp/README.md`. None of
it touches `Renderer`; `Game::hudAccent()` is the one new signal that
reaches the HUD rather than the 3D pass, since a cosmetic only recolours
2D overlay rectangles.

## The viewport bug, and why nothing caught it for so long

`IBL::build()` sets a 128x128 viewport to capture the environment cube's
faces, and used to leave it set. Every 3D frame reset the viewport on its
way through `renderFrame()`, so the mission view was always correct and
this looked like nothing. Then a Hub screen arrived that only clears and
draws the 2D HUD — no 3D pass, no viewport reset — and inherited a 128x128
viewport on a 1280x800 window. The entire hub UI rendered into a small box
in the bottom-left corner, on an otherwise black screen (`glClear` isn't
bounded by the viewport, so the *clear* still covered the window).

It reached a player before it was caught, and what came back was "it's
just a dark screen, no UI at all" — which was exactly, literally true. The
lesson worth recording is not "restore the viewport" but that **a pass
that leaves global GL state changed is a bug even while every existing
caller happens to paper over it.** The fix is both halves: `IBL::build()`
now saves and restores the viewport, and `Hud::begin()` sets the viewport
it needs rather than trusting whatever ran before it, so neither a new
pass nor a reordering can resurrect this.

Also worth recording: the headless capture harness could have caught it
years earlier than a human did. Once a hub screenshot was actually dumped
and looked at, the bug was obvious in one frame. Dumping a frame is cheap;
assuming a pass is fine because a different pass looks fine is not.


## The realism pass

Everything above this heading describes the renderer as first written.
This section describes a second pass over it, asked for as "make it look
real". Sections 1 to 3 above are still accurate about *what* each technique
is; where this pass replaced one, it says so here.

Read this first, because it frames the rest: **no amount of rendering makes
a box into a character.** The games this was asked to look like sit under
thousands of hand-built meshes, scanned materials, motion capture and
lighting artists. None of that is in this repository and none of it can be
written in a shader, and the specific look of someone else's game is theirs
besides. What a renderer *can* supply is light that behaves like light:
shadows that soften with distance, corners that hold shade, air you can see
the sun through, surfaces that are not perfect. That is what was added.

### The frame, now

```
light probe      refreshed only when the scene's sky changes
shadow cascades  3 depth passes from the sun
depth pre-pass   everything opaque, depth only
  ambient occlusion   half-res from depth, bilateral blur up to full res
  sun shafts          half-res march through the cascades, bilateral blur
colour pass      HDR; reads the two maps above; fog applied here
sky              one triangle at the far plane
dust motes       additive
temporal AA      resolve against last frame
exposure meter   log-average luminance, adapted over time
bloom, DoF       as before
composite        sharpen, DoF blend, bloom, exposure, ACES, grade, sRGB,
                 aberration, grain, vignette
```

The rule the order follows: what is part of the **world** — sky, air,
light — goes in before the temporal resolve, so it is anti-aliased, bloomed
and exposed along with everything else. What is part of the **camera** goes
in after. Before this pass the sky and the fog were both painted in the
final composite, which is why the sun was the one bright thing in an outdoor
frame that never bloomed.

### Every solid was inside out

This was found by accident and it is the largest single change in the pass,
so it goes first.

Local lights had just been added, and a glowing pickup half a metre off the
ground lit the gun in your hand and did not light the ground under it. The
light list was right. The ground's albedo was right. Visualising one term at
a time (the method the *What broke* section above describes, and the reason
it is worth describing) got to: on the ground directly below the light,
`N·L` is zero. Then: the normal there is `(0, -1, 0)`. Then, from
`gl_FrontFacing` and the local position: the fragment being shaded is a
*front-facing* fragment of the *bottom* face of the floor box, seen from
above.

`Mesh::box`, `Mesh::cylinder` and `Mesh::sphere` all wound their triangles
clockwise as seen from outside. OpenGL's front face is counter-clockwise,
and back-face culling was on, so every solid in the game had its near faces
discarded and its far faces drawn from behind — lit by normals pointing
away from the viewer. Only the terrain plane, written separately, was wound
correctly.

It survived because a box is nearly its own mirror image. Drawn inside out
it is still box-shaped and still roughly box-coloured. What was actually
wrong, once you know to look:

- A crate's "top" was the inside of its bottom. A station deck was drawn
  40cm below the surface the player collides with.
- Faces were lit when the sun was *behind* them. Pillars looked as though
  they leaned, because the perspective of a far face is not that of a near
  one.
- The sun could never light a floor made of a box, since the face on screen
  pointed down. The stations were carried by a flat fill for exactly this
  reason, and the comment explaining the fill blamed the absence of local
  lights.

The fix is three index orders in `Mesh.cpp`. Two things then had to be put
right because they had been tuned against the mirror image:

- **The shadow pass now culls front faces.** With the old winding and
  back-face culling it had been drawing each solid's far side into the
  shadow map — by accident, the standard trick for avoiding self-shadowing
  on lit faces. Culling front faces with the corrected winding draws the
  same thing on purpose.
- **Open space's sun direction is negated.** It had been aimed to light the
  far hemisphere of each planet, since that was the hemisphere on screen.
  Unchanged, every world you flew toward showed you its night side.

The lesson is the same one as the viewport bug above: assuming a thing is
right because the picture looks plausible is not the same as checking it,
and one frame with the normal drawn as a colour would have shown this on
the first day.

### The light probe is the sky

`IBL` used to capture one fixed room of coloured panels and light every
scene with it; missions turned it down to 35% and added a flat fill derived
from their sky colours, because lighting a salt pan with a station interior
was visibly wrong. Now a scene that is under a sky gets a probe that *is*
its sky: `shaders/common/sky.glsl` is one function, `#include`d by the sky
pass and by the probe capture (`Shader` learned `#include` for this), so
the fill in a shadow is the colour of the sky above it and the two cannot
drift apart. The underside of the probe is the ground's own albedo lit by
that sun and sky. Roofed places — the stations, and missions whose file sets
`ambient` — keep the room.

Either capture is then filtered properly, replacing the box-filtered mip
chain called out under *Honest limits*:

- **Specular**: each mip is the capture seen through a GGX lobe of one
  roughness, 48 importance samples per texel, each read at the source mip
  matching its own solid angle (that last part is what stops a probe with
  one bright strip in it from prefiltering into fireflies).
- **Diffuse**: a 16px irradiance cube, cosine-weighted. The old stand-in —
  a near-top mip of the specular chain — is about the right blur with the
  wrong weighting, and leaks light from behind the surface.

### The sky

Still painted, not simulated: a mission says what colour its zenith and
horizon are, and a scattering integral would overrule that. What is
physical is the shape. Haze is a band at the horizon (you look through
forty times as much air sideways as up); the glow round the sun is
Henyey-Greenstein forward scattering; the disc has a darkened limb.

Clouds are a 2D fbm deck projected overhead, lit by asking how much cloud
lies between each point and the sun — step toward the sun, sample again —
so they are dark through their thickness and bright along the edge the sun
is behind. Because they are in the sky function they are in the probe:
an overcast mission is lit by an overcast sky. `clouds = 0..1` in a mission
file.

### Shadows that widen

A fixed 3x3 filter gives every shadow edge the same width, which is wrong
in both directions at once: a boot heel's shadow should be sharp and a
pillar's, thirty metres out, should not. `sampleCascade()` now does
percentage-closer soft shadows: eight taps to find what is nearer the sun
than the shaded point and by how much, then twelve Poisson taps over a disc
sized by that gap. The disc is turned by a different angle at every pixel,
so undersampling shows as grain rather than as twelve overlapping copies of
the edge, and the temporal resolve averages the grain away.

The cascade textures are bound twice for this — once through their own
comparison sampler, once through a sampler object that returns raw depth —
because finding a blocker needs the number and filtering needs the
comparison.

Two biases keep the ground from finding itself to be its own blocker: the
lookup is lifted off the surface along its normal by about a texel, and
both the search and the filter allow for how steeply the surface runs away
from the light. When the search finds nothing clearly in the way, the
filter falls back to its tightest size rather than to "fully lit", which is
what keeps the last few centimetres of a contact shadow.

### Ambient occlusion

The probe lights every point as though nothing were near it. `ssao.frag`
estimates how much of the hemisphere above each pixel is blocked (Scalable
Ambient Obscurance, McGuire 2012) at half resolution from the depth
pre-pass, with normals reconstructed from depth by differencing toward
whichever neighbour is on the same surface. `bilateral.frag` then blurs and
upsamples it without crossing depth edges.

It is applied to the fill light, not on top of the image, and it is applied
the way Jimenez et al. (2016) describe: light that gets into a crease
bounces before it gets out, so on a pale surface occlusion is much weaker
than the raw number. Applying it raw is why SSAO so often looks like soot.
Specular gets its own, sharper term (Lagarde), plus a horizon test so a
bumped normal cannot reflect the sky out of the ground.

### Sun shafts

The fog is an analytic integral, which is exact and nearly free and assumes
the sun reaches all the air along the ray. `volumetric.frag` walks each ray
through the shadow cascades and reports the fraction of the *visible* haze
that is actually lit. The fog multiplies its sun term by that. Nothing is
drawn over the picture: it corrects the fog's one wrong assumption, and the
edge between lit air and unlit air is what a shaft is.

### Everything that glows is a light

`Renderer::gatherLights` turns every emissive draw item into a light on what
is near it — up to sixteen, chosen by how much of each reaches the
neighbourhood of the camera, with the least important one faded toward
zero so that walking down a corridor of strips the far one dims out rather
than switching off.

Strength comes from the emitter: radiance times the area it shows. There is
no second set of numbers to keep in step. A long thin emitter is a line,
not a point: it lights from the nearest point *on* it, with the fall-off of
a line (inverse distance up close, inverse square far away). Treating a
thirty-metre ceiling strip as a point at its nearest end burned a white
hole in the ceiling above it, which is how that got found.

These lights cast no shadows. That is the largest physical shortcut left in
the pipeline.

### Materials

Still no textures; still one program. What changed:

- **Bump mapping uses an exact derivative.** The noise now returns its own
  slope from the same four corner values, so there is no finite-difference
  step to get wrong — the bug the *What broke* section is about cannot
  recur — and armour has its normal detail back, which had been removed
  because the finite-difference version would not stop shimmering.
- **Detail fades by the pixel's short side.** A pixel on ground seen at a
  shallow angle is a sliver, millimetres across and tens of centimetres
  deep. Fading noise by the long side — the first thing this pass did —
  turned the ground in front of you to smears pointing at the horizon. The
  footprint is now the geometric mean of the two sides, with the sliver
  never treated as thinner than a quarter of its length: the compromise an
  anisotropic texture filter makes.
- **Boxes have edges.** A fragment on a box knows how far it is from each
  pair of faces (`uBoxHalf`, `vLocalPos`), so within a bevel's width of an
  edge its normal turns toward the neighbouring face — a rounded corner,
  lit like one, with no extra triangles — and paint chips there first.
- **Things that stand outdoors weather.** Dirt at the foot of a wall,
  streaks down from its top, dust on whatever faces up — the dust in the
  colour of that mission's ground.
- **Things that move carry their detail with them.** Surface detail was
  worked out from world position for everything, so a hostile walked
  *through* its own scratches and the gun's seams swam as you turned.
  `DrawItem::objectSpace` shades those in the object's frame.
- **The ground is three scales** — the lie of the land, lumps, grit — and
  holds water. `wetness = 0..1` in a mission file fills the hollows those
  same height fields describe, so a puddle has the hollow's shape, and
  being a mirror under a sky probe it shows the clouds.
- **Expedition floors are ground.** They were plated metal like every other
  box; open-air areas now shade as terrain.

### Temporal anti-aliasing

The scene is drawn offset by a different sub-pixel amount each frame (an
eight-point Halton sequence) and blended with the previous result,
reprojected through the depth buffer and clipped to the range of colours in
the current frame's 3x3 neighbourhood. Besides edges, this is what averages
away the grain the shadow, occlusion and shaft passes deliberately produce.
History is read with a Catmull-Rom filter, because bilinear applied to its
own output thirty times a second is a blur. The composite puts a little
edge back with a sharpen clamped to the local range.

Open space opts out: a field of one-pixel stars is exactly what jitter and
history handle worst.

Known weakness: there are no per-object motion vectors. A moving hostile is
handled by the neighbourhood clip alone, and whatever is within arm's
reach is assumed to be the weapon and to move with the camera.

### Exposure

`luminance.frag` and `adapt.frag` meter the log-average brightness of the
frame and move the exposure toward a mid-grey average over a second or two.
It is deliberately partial (the correction is raised to the power 0.45) and
clamped to about half a stop either way: a place lit to be dark should stay
dark, just not unreadable. The target is what the outdoor missions meter at
(0.15 to 0.18), so a typical scene is left exactly as it was lit.

### Quality tiers

Soft shadows, occlusion, shafts and temporal AA are each tied to the
adaptive-quality tiers (`Renderer::kTiers`; table in `cpp/README.md`). The
`minimal` tier costs about what the whole pipeline did before this pass.

### Honest limits, second edition

- **Still never run on a physical GPU.** Everything here was rendered under
  Xvfb on llvmpipe and looked at. On that rasteriser `high` costs roughly
  twice what the old pipeline did per frame; what it costs on real hardware
  is unmeasured.
- **Local lights cast no shadows**, so a strip light shines through a wall.
- **No screen-space reflections.** A puddle reflects the sky probe, not the
  crate standing next to it.
- **No motion vectors**, as above.
- **The geometry is the geometry.** Enemies, crew and weapons are still
  assemblies of boxes, cylinders and spheres. They are better lit boxes.
