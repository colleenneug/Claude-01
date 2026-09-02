/* ============================================================
   Material layer: one shader-patch point, PBR enforcement, and the
   layered terrain blend.

   three compiles a program per material configuration, and the only
   supported way to inject GLSL into a built-in material is
   onBeforeCompile. Several features here want to inject at once
   (cascaded shadows, anisotropic reflections, layered blending), and
   an instance-level hook would let each one clobber the others. So a
   single hook is installed on MeshStandardMaterial.prototype and the
   patches register themselves with it; every patch reads flags from
   material.userData and contributes to the program cache key, so two
   materials that want different things get different programs.

   Load order: this file must come before any module that registers a
   patch (fps/csm.js).
   ============================================================ */
(function (SF) {
  'use strict';

  const patches = [];
  let installed = false;
  let maxAnisotropy = 8;

  /* A patch is { name, key(material) -> string, apply(shader, material) }.
     key() must return a different string whenever apply() would emit
     different GLSL, or three will hand back a cached program built for
     the other variant. */
  function register(patch) {
    patches.push(patch);
    return patch;
  }

  function install(renderer) {
    if (renderer) maxAnisotropy = renderer.capabilities.getMaxAnisotropy();
    if (installed) return;
    installed = true;

    const proto = THREE.MeshStandardMaterial.prototype;   // physical extends standard
    proto.onBeforeCompile = function (shader, r) {
      for (let i = 0; i < patches.length; i++) patches[i].apply(shader, this, r);
    };
    proto.customProgramCacheKey = function () {
      let k = 'sf';
      for (let i = 0; i < patches.length; i++) {
        k += '|' + (patches[i].key ? patches[i].key(this) : '');
      }
      return k;
    };
  }

  /* Insert declarations immediately above main(). Everything a patch
     needs — varyings, uniforms, chunk functions — is already declared by
     that point, whichever material is being compiled. */
  function declare(shader, glsl) {
    shader.fragmentShader = shader.fragmentShader.replace(
      'void main() {', glsl + '\nvoid main() {');
  }

  /* Replace a built-in chunk with a patched copy of itself. The include
     is still unresolved at onBeforeCompile time, so the chunk source has
     to be pulled from ShaderChunk and edited by hand. */
  function patchChunk(shader, chunkName, from, to) {
    const src = THREE.ShaderChunk[chunkName];
    if (!src || src.indexOf(from) < 0) {
      console.warn('[shading] chunk anchor missing in ' + chunkName);
      return false;
    }
    shader.fragmentShader = shader.fragmentShader.replace(
      '#include <' + chunkName + '>', src.split(from).join(to));
    return true;
  }

  /* ------------------------------------------------------------------
     Anisotropic reflections.

     A rolled or brushed metal panel does not reflect a point of light as
     a point: the microscopic grooves all run one way, so the highlight
     smears along them and stretches as you walk past. three's standard
     material is isotropic, so every metal in the game reflects a round
     blob and reads as polished plastic.

     The cheap, correct-looking fix (Filament's) is to bend the vector
     used to sample the environment towards the grain before the lookup,
     which stretches the reflection along the grain by construction.
     ------------------------------------------------------------------ */
  const ANISO_GLSL = `
uniform float sfAnisoStrength;
uniform vec3 sfAnisoAxis;

/* Bend the reflection normal towards the surface grain. n and v are in
   view space; the grain axis is authored in world space so a panel keeps
   its grain direction however the camera moves. */
vec3 sfAnisoBend( vec3 n, vec3 v ) {
  vec3 axis = ( viewMatrix * vec4( sfAnisoAxis, 0.0 ) ).xyz;
  vec3 t = axis - n * dot( axis, n );      // grain, projected onto the surface
  float tl = length( t );
  if ( tl < 0.08 ) return n;               // grain is edge-on: nothing to stretch
  t /= tl;
  vec3 b = normalize( cross( n, t ) );     // across the grain
  vec3 aT = cross( b, v );
  float al = length( aT );
  if ( al < 1e-4 ) return n;
  vec3 aN = normalize( cross( aT / al, b ) );
  return normalize( mix( n, aN, sfAnisoStrength ) );
}`;

  register({
    name: 'aniso',
    key(m) { return m.userData.aniso ? 'a' + (m.userData.anisoAxisKey || '') : ''; },
    apply(shader, m) {
      const strength = m.userData.aniso;
      if (!strength) return;
      shader.uniforms.sfAnisoStrength = { value: strength };
      shader.uniforms.sfAnisoAxis = { value: m.userData.anisoAxis || new THREE.Vector3(0, 1, 0) };
      declare(shader, ANISO_GLSL);
      /* Only the specular environment lookup is bent. Diffuse irradiance
         keeps the true normal — bending that would tilt the surface, not
         stretch its highlight. */
      patchChunk(shader, 'lights_fragment_maps',
        'radiance += getIBLRadiance( geometry.viewDir, geometry.normal, material.roughness );',
        'radiance += getIBLRadiance( geometry.viewDir, sfAnisoBend( geometry.normal, geometry.viewDir ), material.roughness );');
    }
  });

  /* Turn a material anisotropic. axis is the world-space direction the
     grain runs in; strength 0 is isotropic, 1 is a mirror smeared to a line. */
  function anisotropic(material, strength, axis) {
    material.userData.aniso = strength;
    if (axis) {
      material.userData.anisoAxis = axis.clone();
      material.userData.anisoAxisKey = axis.x + ',' + axis.y + ',' + axis.z;
    }
    material.needsUpdate = true;
    return material;
  }

  /* ------------------------------------------------------------------
     Specular anti-aliasing.

     Pinning metal to roughness 0.25 buys sharp reflections and, with it,
     a highlight narrower than a pixel. On a normal-mapped grating seen at
     a grazing angle that turns into crawling rainbow sparkle, because each
     pixel is averaging a normal map that swings wildly across its own
     footprint.

     The fix is Kaplanyan's: measure how fast the shading normal is
     changing across the pixel with screen-space derivatives, and widen the
     roughness by that variance. A surface that is geometrically noisy at
     pixel scale is, for that pixel, a rougher surface — which is what it
     physically is.
     ------------------------------------------------------------------ */
  const SPEC_AA_GLSL = `
uniform float sfSpecAA;
uniform float sfSpecAAMax;`;

  let specularAA = true;

  register({
    name: 'specaa',
    key(m) { return specularAA && m.userData.specAA !== false ? 's' : ''; },
    apply(shader, m) {
      if (!specularAA || m.userData.specAA === false) return;
      shader.uniforms.sfSpecAA = { value: 0.5 };
      shader.uniforms.sfSpecAAMax = { value: 0.18 };
      declare(shader, SPEC_AA_GLSL);
      /* roughnessFactor is set before the normal is, and read after it, so
         this sits between the two. */
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <normal_fragment_maps>',
        `#include <normal_fragment_maps>
  {
    vec3 sfDNx = dFdx( normal );
    vec3 sfDNy = dFdy( normal );
    float sfVariance = sfSpecAA * ( dot( sfDNx, sfDNx ) + dot( sfDNy, sfDNy ) );
    float sfWiden = min( 2.0 * sfVariance, sfSpecAAMax );
    roughnessFactor = sqrt( min( 1.0, roughnessFactor * roughnessFactor + sfWiden ) );
  }`);
    }
  });

  /* ------------------------------------------------------------------
     Layered vertex blending.

     Terrain painted with one repeating texture reads as wallpaper: the
     tile is visible from any height, and the ground is the same stuff
     everywhere. Two layers blended by a per-vertex weight breaks both —
     large slow patches of coarse stone over fine grit — and a third
     high-frequency normal map, sampled far tighter than either layer,
     puts back the microscopic detail that both layers lose to tiling.
     ------------------------------------------------------------------ */
  const LAYER_GLSL = `
uniform sampler2D sfLayerMap;
uniform sampler2D sfLayerNormal;
uniform sampler2D sfLayerRough;
uniform sampler2D sfDetailNormal;
uniform vec2 sfLayerRepeat;
uniform vec2 sfDetailRepeat;
uniform float sfDetailScale;
varying float sfBlend;

/* Whiteout blending: adding the two tangent-space normals and
   renormalising keeps both sets of bumps instead of flattening the
   weaker one, which is what a plain mix() does. */
vec3 sfBlendNormals( vec3 a, vec3 b ) {
  return normalize( vec3( a.xy + b.xy, a.z * b.z ) );
}`;

  register({
    name: 'layer',
    key(m) { return m.userData.layered ? 'L' : ''; },
    apply(shader, m) {
      const L = m.userData.layered;
      if (!L) return;

      Object.assign(shader.uniforms, {
        sfLayerMap: { value: L.map },
        sfLayerNormal: { value: L.normalMap },
        sfLayerRough: { value: L.roughnessMap },
        sfDetailNormal: { value: L.detailNormal },
        sfLayerRepeat: { value: L.repeat || new THREE.Vector2(1, 1) },
        sfDetailRepeat: { value: L.detailRepeat || new THREE.Vector2(64, 64) },
        sfDetailScale: { value: L.detailScale == null ? 0.8 : L.detailScale }
      });

      // the blend weight rides along as a vertex attribute
      shader.vertexShader = shader.vertexShader
        .replace('void main() {',
          'attribute float aBlend;\nvarying float sfBlend;\nvoid main() {')
        .replace('#include <begin_vertex>',
          '#include <begin_vertex>\n\tsfBlend = aBlend;');

      declare(shader, LAYER_GLSL);

      /* Albedo: mix the second layer in over the first. Both are already
         tinted by the material colour, so the mix is between the two
         finished colours rather than between the raw texels. */
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <map_fragment>',
        `#include <map_fragment>
  {
    vec3 sfLayerTexel = texture2D( sfLayerMap, vUv * sfLayerRepeat ).rgb;
    diffuseColor.rgb = mix( diffuseColor.rgb, diffuse * sfLayerTexel, sfBlend );
  }`);

      /* Roughness: the same blend, so the coarse layer also reads rougher. */
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <roughnessmap_fragment>',
        `#include <roughnessmap_fragment>
  roughnessFactor = mix( roughnessFactor,
    roughness * texture2D( sfLayerRough, vUv * sfLayerRepeat ).g, sfBlend );`);

      /* Normals: blend the two layers with a whiteout blend, then fold in
         the detail map at a much tighter repeat so the surface still has
         grain when you stand on it. Terrain carries no tangent attribute,
         so the frame comes from screen-space derivatives, exactly as the
         stock chunk does. */
      shader.fragmentShader = shader.fragmentShader.replace(
        '#include <normal_fragment_maps>',
        `#if defined( TANGENTSPACE_NORMALMAP ) && ! defined( USE_TANGENT )
  {
    vec3 sfBaseN = texture2D( normalMap, vUv ).xyz * 2.0 - 1.0;
    vec3 sfLayerN = texture2D( sfLayerNormal, vUv * sfLayerRepeat ).xyz * 2.0 - 1.0;
    vec3 sfDetN = texture2D( sfDetailNormal, vUv * sfDetailRepeat ).xyz * 2.0 - 1.0;
    sfDetN.xy *= sfDetailScale;
    vec3 sfMapN = sfBlendNormals( mix( sfBaseN, sfLayerN, sfBlend ), sfDetN );
    sfMapN.xy *= normalScale;
    normal = perturbNormal2Arb( - vViewPosition, normal, sfMapN, faceDirection );
  }
#else
  #include <normal_fragment_maps>
#endif`);
    }
  });

  /* ------------------------------------------------------------------
     PBR enforcement sweep.

     Rules the whole world has to obey, applied once after the level is
     built rather than trusted to every call site that ever made a
     material.
     ------------------------------------------------------------------ */
  const METAL = { metalness: 0.85, roughness: 0.25 };

  /* A roughness map multiplies the scalar, so a material carrying one
     needs its scalar divided by the map's average to land on the target.
     materials.js records that average as userData.roughMean. */
  function setRoughness(m, target) {
    const mean = m.userData.roughMean;
    m.roughness = mean ? THREE.MathUtils.clamp(target / mean, 0.02, 1) : target;
  }

  function enforce(root, opts) {
    const o = Object.assign({ metalCut: 0.6, aniso: 0.35,
                              axis: new THREE.Vector3(0, 1, 0) }, opts || {});
    const seen = new Set();
    let metals = 0, textures = 0;

    root.traverse((obj) => {
      const mats = obj.material ? (Array.isArray(obj.material) ? obj.material : [obj.material]) : null;
      if (!mats) return;
      for (const m of mats) {
        if (!m || seen.has(m)) continue;
        seen.add(m);

        /* Anisotropic *filtering*: without it every texture viewed at a
           glancing angle — which is most of the floor, most of the time —
           blurs to mush a few metres out. */
        for (const k of ['map', 'normalMap', 'roughnessMap', 'metalnessMap', 'aoMap', 'emissiveMap']) {
          const t = m[k];
          if (t && t.anisotropy !== maxAnisotropy) {
            t.anisotropy = maxAnisotropy;
            t.needsUpdate = true;
            textures++;
          }
        }
        if (m.userData.layered) {
          for (const t of [m.userData.layered.map, m.userData.layered.normalMap,
                           m.userData.layered.roughnessMap, m.userData.layered.detailNormal]) {
            if (t) { t.anisotropy = maxAnisotropy; t.needsUpdate = true; textures++; }
          }
        }

        if (!m.isMeshStandardMaterial) continue;
        if (m.userData.pbr === 'skip' || m.transparent) continue;

        /* Armour, hull plating, weapons, machinery: anything already
           authored as metal is pinned to the house values and given a
           grain so its reflections stretch. */
        if (m.userData.pbr === 'metal' || m.metalness >= o.metalCut) {
          m.metalness = METAL.metalness;
          setRoughness(m, METAL.roughness);
          if (!m.userData.aniso) anisotropic(m, o.aniso, o.axis);
          m.needsUpdate = true;
          metals++;
        }
      }
    });
    return { metals, textures, anisotropy: maxAnisotropy };
  }

  SF.shading = {
    install, register, declare, patchChunk,
    anisotropic, enforce, METAL,
    get specularAA() { return specularAA; },
    set specularAA(v) { specularAA = !!v; },
    get maxAnisotropy() { return maxAnisotropy; }
  };
})(window.SF);
