# Rendering

Every surface, light and post-process pass in the game, and where each one
lives. The whole renderer is classic `<script>` files against a vendored
three.js r147 UMD build — no bundler, no ES modules, no npm — so the page
still opens from `file://` and the single-file build in `dist/` is one HTML
file. Anything three ships only as an ES module (EffectComposer, CSM) is
hand-rolled here instead.

Load order matters in `index.html`:

```
materials.js  lights.js  shading.js  csm.js  atmos.js  engine.js  ...
```

`shading.js` owns the one shader-patch point; `csm.js` registers a patch with
it at load; `engine.js` installs it against the renderer at mission build.

---

## 1. Physically based materials

### Metal — `shading.js`, `materials.js`

The house values are one constant, `SF.shading.METAL`:

```js
const METAL = { metalness: 0.85, roughness: 0.25 };
```

`SF.shading.enforce(scene)` runs once per mission, after the world and its
occupants exist (`game.js`, in `start()`), and pins every material already
authored as metal to those values. A material carrying a roughness *map* has
its scalar divided by the map's average — recorded as `userData.roughMean`
when the map is generated — so the product still lands on 0.25 rather than on
whatever the map happens to average.

Hostile armour sets the same values at construction (`ai.js`), because
enemies spawn long after the sweep has run.

### Anisotropy

Two different things, both wanted:

**Anisotropic filtering** — every texture is set to the driver's maximum
(`renderer.capabilities.getMaxAnisotropy()`, 16 on the machines tested), in
`materials.js` when the texture is made and again in the `enforce()` sweep for
anything built elsewhere. Without it a floor seen at a grazing angle collapses
to grey mush a few metres out.

**Anisotropic reflections** — rolled and brushed metal has its microscopic
grooves all running one way, so a highlight smears along them and stretches as
you move past. three's standard material is isotropic and reflects a round
blob. The patch in `shading.js` bends the vector used to sample the
environment towards the surface grain before the lookup (Filament's method),
which stretches the reflection along the grain by construction:

```glsl
vec3 sfAnisoBend( vec3 n, vec3 v ) { ... }
```

It replaces exactly one line of three's `lights_fragment_maps` chunk, so only
the specular environment lookup is bent — diffuse irradiance keeps the true
normal, since bending that would tilt the surface rather than stretch its
highlight.

### Terrain — layered vertex blending

`materials.js` builds the maps (`stone()`, `detailNormal()`,
`layeredGround()`); `shading.js` does the blending in the shader;
`planets.js` paints the weights.

* Two full stone layers — a rough noise-textured base and a second, coarser
  one — each with albedo, a Sobel-derived normal map and a roughness map,
  metalness pinned to 0 because stone is a dielectric.
* A per-vertex `aBlend` attribute chooses between them. `planets.js` fills it
  with three octaves of value noise over a 96×96 grid, pushed towards its ends
  so the result reads as patches of different material rather than a smear
  between two colours.
* A third detail normal map is sampled at roughly one tile per 0.8 m and
  folded in with a whiteout blend (`normalize(vec3(a.xy + b.xy, a.z * b.z))`),
  which keeps both sets of bumps instead of averaging the weaker one away.
  That is the microscopic surface detail: it survives being stood on, whatever
  the layer maps are doing at that scale.

---

## 2. Lighting and atmosphere

### The sun — `csm.js`, `game.js`

One directional light at 20° elevation. Low is the point: a light overhead
lays a shadow directly under whatever casts it and the ground reads flat, while
20° throws shadows about three times the height of the caster and still leaves
upward-facing surfaces enough sun to show their own shape.

### Cascaded shadow maps — `csm.js`

A single shadow camera covering 190 m at 2048² spends about 9 cm per texel, so
a low sun gives staircase edges and the contact shadow under your own boots
disappears. `SF.csm.create()` instead runs three cascades:

| Cascade | Covers | Map | Texels per metre |
|---|---|---|---|
| 0 | 1 – 23 m | 2048² | ~23 |
| 1 | 23 – 61 m | 2048² | ~9 |
| 2 | 61 m + | 1024² | ~1.4 |

* Splits come from the practical scheme — a blend of logarithmic and uniform,
  λ = 0.7.
* Each cascade is refitted every frame to a **bounding sphere** around its
  slice of the view frustum. A sphere is the same size whichever way the
  camera faces, so the map does not resize (and its texels do not shimmer) as
  you turn. The centre is then snapped to that cascade's own texel grid.
* All three lights are added to the scene **once**, before anything else, so
  the scene's light count never changes — three keys its shader programs on
  that, and changing it recompiles every material in the scene (the same
  reason `lights.js` pools its point lights).
* A fragment patch masks each cascade light to its own depth band, with a soft
  overlap so the handover is invisible:

  ```glsl
  float sfCsmWeight( vec2 band ) {
    float d = - vViewPosition.z;
    float f = max( uCsmFade, 0.001 );
    return smoothstep( band.x - f, band.x + f, d ) *
           ( 1.0 - smoothstep( band.y - f, band.y + f, d ) );
  }
  ```

  Neighbouring bands use the same edges with opposite slopes, so the two
  weights always sum to one. The first band has no lower edge and the last
  none above, so the sun still lights whatever falls outside the shadowed
  range.

### Volumetric fog — `engine.js` composite, parameters in `atmos.js`

The analytic integral of an exponential height distribution along the view
ray, evaluated per pixel against the depth buffer:

```glsl
float span = ( abs( t ) < 1e-3 ) ? rayLen : rayLen * ( 1.0 - exp( -t ) ) / t;
float optical = uFogDensity * exp( -k * ( uCamPos.y - uFogBase ) ) * span;
float fogAmount = 1.0 - exp( -optical );
```

so haze pools in low ground and thins as you climb, and looking towards the
sun scatters its colour back at you (`uInscatter`, weighted by
`pow(dot(viewDir, -sunDir), 8.0)`). Each environment has its own density,
falloff and colour in `atmos.FOG` — a corridor's haze and a planet's aerial
perspective are different problems and want different numbers.

### Dust motes — `atmos.js`

A light beam you cannot see is just a bright patch on the floor. A few
thousand motes hang around the camera as one `THREE.Points` draw call and cost
nothing per frame on the CPU: their positions are fixed in the buffer and the
**vertex shader wraps them** into a box that follows the camera —

```glsl
vec3 rel = p - uCamPos;
rel = mod( rel + uBox * 0.5, uBox ) - uBox * 0.5;
```

— so walking through them gives real parallax without ever touching the
attribute array, and the same mote simply reappears behind you. Each drifts on
its own seeded figure. Brightness rises sharply when a mote sits between you
and the sun (`pow(dot(toEye, uSunDir), 12.0)`), which is why dust is invisible
until you turn into the beam. Additive, depth-tested but never depth-writing,
so geometry occludes dust and dust occludes nothing.

### Image-based lighting — `envmap.js`

A small room of emissive panels — sky above, ground below, and the two or
three coloured things big enough to show up in a reflection — run through
three's `PMREMGenerator` into a prefiltered cube map used as
`scene.environment`. One render at load, free thereafter, and it is the
difference between metal and grey plastic. Four presets: `ship`, `station`,
`desert`, `ice`.

---

## 3. Camera and post-processing

`engine.js`. The scene renders into a **linear half-float buffer with tone
mapping switched off**, so every pass downstream works on real radiance rather
than on display values. That ordering is the whole reason the chain behaves
like a camera:

```
scene (HDR, linear)
  -> bright pass, soft knee
  -> 5-level downsample / tent upsample bloom
  -> radial light shafts from the sun's screen position
  -> half-res circle-of-confusion blur
  -> composite: DoF, bloom, shafts, volumetric fog, aberration,
                ACES, sRGB, grain, vignette, damage
  -> screen
```

### ACES filmic tone mapping

Applied in the composite, not by the renderer, using three's own RRT/ODT fit so
the grade is unchanged from when the renderer did it. Doing it last is what
lets bloom and the shafts see highlights that a tone-mapped buffer has already
thrown away.

### Bloom

* Soft-knee bright pass: a hard cutoff makes bloom switch on and off as a
  surface crosses the threshold; the knee ramps it in over a band.
* Five levels down with the thirteen-tap filter from Call of Duty's *Next
  Generation Post Processing* — a box filter at these sizes flickers badly on
  small bright things.
* Back up with a 3×3 tent, blended additively, each level scaled by 0.62 so
  the five summed levels converge on about 2.5× the top level rather than 5×.
  That sum is what gives a wide smooth skirt instead of a visible halo.

### Light shafts

Radial blur of the bright buffer towards the sun's screen position, 28 taps at
quarter resolution. Bright pixels smear away from the sun and dark geometry
stays dark, so a silhouette in front of the sun carves a shadow out of the
beam. Faded out by how far the sun is from the frame, and skipped entirely
when it is behind the camera.

### Depth of field

Off at the hip — the pass does not even run. `game.js` feeds
`weapon.state.adsAmount` to `eng.setAim()` each frame, and the circle of
confusion opens from a focus distance of 14 m over the next 45 m, so aiming
down the sights softens the background and leaves the target sharp. Half
resolution, twelve taps in two rings.

### Lens and film

Chromatic aberration that grows towards the edge of the frame and much
further when you are hurt, film grain, vignette, and the red damage tint —
all after the tone map, because they are properties of the picture rather
than of the light.

---

## Verifying it

`SF.ui.launch(missionIndex)` drops straight into a mission without clicking
through the menus, and leaves the mission on `window.__m`. From there
`window.__m.engine`, `SF.csm.active` and `SF.shading` expose everything above,
which is how the pipeline is checked: drive the real game in a browser and
read measurements rather than judging by eye.

Two things a headless run cannot tell you: frame rate, because there is no GPU
in the test environment and SwiftShader's numbers mean nothing; and how any of
this actually looks on a display that is not a screenshot.
