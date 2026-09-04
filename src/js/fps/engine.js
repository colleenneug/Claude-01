/* ============================================================
   Renderer and the post-processing pipeline.

   The scene is rendered into a linear half-float buffer with tone
   mapping switched off, so everything downstream works on real
   radiance rather than on display values. That ordering is what makes
   the rest of the chain behave the way a camera does:

     scene (HDR, linear)
       -> bright pass with a soft knee
       -> five-level downsample / tent upsample bloom
       -> radial light shafts from the sun's screen position
       -> half-resolution circle-of-confusion blur for depth of field
       -> composite: depth of field, bloom, shafts, volumetric height
          fog with sun inscatter, lens aberration, ACES filmic tone
          mapping, sRGB, grain, vignette, damage
       -> screen

   three's EffectComposer ships only as an ES module and this project
   stays on classic scripts so it can run from file://, so the chain is
   hand-rolled. Every pass is a full-screen triangle-pair with one
   fragment program; the expensive ones run at half or quarter
   resolution.
   ============================================================ */
(function (SF) {
  'use strict';

  const QUAD = new THREE.PlaneGeometry(2, 2);

  const VERT = `
varying vec2 vUv;
void main(){ vUv = uv; gl_Position = vec4( position.xy, 0.0, 1.0 ); }`;

  function pass(fragment, uniforms, opts) {
    const mat = new THREE.ShaderMaterial(Object.assign({
      uniforms: uniforms,
      vertexShader: VERT,
      fragmentShader: fragment,
      depthTest: false, depthWrite: false
    }, opts || {}));
    const scene = new THREE.Scene();
    scene.add(new THREE.Mesh(QUAD, mat));
    return { scene, mat, u: uniforms };
  }

  /* ---------- shared GLSL ---------- */

  /* Window-space depth back to a positive distance along the view axis. */
  const DEPTH_GLSL = `
uniform sampler2D tDepth;
uniform float uNear;
uniform float uFar;
float sfViewDepth( vec2 uv ) {
  float d = texture2D( tDepth, uv ).x;
  float ndc = d * 2.0 - 1.0;
  return ( 2.0 * uNear * uFar ) / ( uFar + uNear - ndc * ( uFar - uNear ) );
}`;

  const LUMA = `float sfLuma( vec3 c ){ return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) ); }`;

  /* ---------- bright pass ----------
     A hard cutoff makes bloom pop on and off as a surface crosses the
     threshold. The soft knee ramps the contribution in over a band, so a
     lamp brightens smoothly instead of switching. */
  const BRIGHT_FS = `
uniform sampler2D tDiffuse;
uniform float uThreshold;
uniform float uKnee;
varying vec2 vUv;
${LUMA}
void main(){
  vec3 c = texture2D( tDiffuse, vUv ).rgb;
  float l = sfLuma( c );
  float knee = uThreshold * uKnee + 1e-5;
  float soft = clamp( l - uThreshold + knee, 0.0, 2.0 * knee );
  soft = soft * soft / ( 4.0 * knee );
  float contribution = max( soft, l - uThreshold ) / max( l, 1e-5 );
  gl_FragColor = vec4( c * contribution, 1.0 );
}`;

  /* ---------- bloom downsample ----------
     The thirteen-tap filter from Call of Duty's "Next Generation Post
     Processing". A plain box filter at these sizes flickers badly on
     small bright things; this one is stable. */
  const DOWN_FS = `
uniform sampler2D tDiffuse;
uniform vec2 uTexel;
varying vec2 vUv;
void main(){
  vec2 t = uTexel;
  vec3 a = texture2D( tDiffuse, vUv + t * vec2( -2.0,  2.0 ) ).rgb;
  vec3 b = texture2D( tDiffuse, vUv + t * vec2(  0.0,  2.0 ) ).rgb;
  vec3 c = texture2D( tDiffuse, vUv + t * vec2(  2.0,  2.0 ) ).rgb;
  vec3 d = texture2D( tDiffuse, vUv + t * vec2( -2.0,  0.0 ) ).rgb;
  vec3 e = texture2D( tDiffuse, vUv                          ).rgb;
  vec3 f = texture2D( tDiffuse, vUv + t * vec2(  2.0,  0.0 ) ).rgb;
  vec3 g = texture2D( tDiffuse, vUv + t * vec2( -2.0, -2.0 ) ).rgb;
  vec3 h = texture2D( tDiffuse, vUv + t * vec2(  0.0, -2.0 ) ).rgb;
  vec3 i = texture2D( tDiffuse, vUv + t * vec2(  2.0, -2.0 ) ).rgb;
  vec3 j = texture2D( tDiffuse, vUv + t * vec2( -1.0,  1.0 ) ).rgb;
  vec3 k = texture2D( tDiffuse, vUv + t * vec2(  1.0,  1.0 ) ).rgb;
  vec3 l = texture2D( tDiffuse, vUv + t * vec2( -1.0, -1.0 ) ).rgb;
  vec3 m = texture2D( tDiffuse, vUv + t * vec2(  1.0, -1.0 ) ).rgb;
  vec3 o = ( j + k + l + m ) * 0.5 * 0.25
         + ( a + b + d + e ) * 0.125 * 0.25
         + ( b + c + e + f ) * 0.125 * 0.25
         + ( d + e + g + h ) * 0.125 * 0.25
         + ( e + f + h + i ) * 0.125 * 0.25;
  gl_FragColor = vec4( o, 1.0 );
}`;

  /* ---------- bloom upsample ----------
     A 3x3 tent, blended additively into the next larger level. Summing
     the levels on the way up is what gives bloom a wide, smooth skirt
     instead of a visible halo. */
  const UP_FS = `
uniform sampler2D tDiffuse;
uniform vec2 uTexel;
uniform float uRadius;
uniform float uStrength;
varying vec2 vUv;
void main(){
  vec2 t = uTexel * uRadius;
  vec3 s = texture2D( tDiffuse, vUv + vec2( -t.x,  t.y ) ).rgb
         + texture2D( tDiffuse, vUv + vec2(  0.0,  t.y ) ).rgb * 2.0
         + texture2D( tDiffuse, vUv + vec2(  t.x,  t.y ) ).rgb
         + texture2D( tDiffuse, vUv + vec2( -t.x,  0.0 ) ).rgb * 2.0
         + texture2D( tDiffuse, vUv                     ).rgb * 4.0
         + texture2D( tDiffuse, vUv + vec2(  t.x,  0.0 ) ).rgb * 2.0
         + texture2D( tDiffuse, vUv + vec2( -t.x, -t.y ) ).rgb
         + texture2D( tDiffuse, vUv + vec2(  0.0, -t.y ) ).rgb * 2.0
         + texture2D( tDiffuse, vUv + vec2(  t.x, -t.y ) ).rgb;
  gl_FragColor = vec4( s * ( uStrength / 16.0 ), 1.0 );
}`;

  /* ---------- volumetric light shafts ----------
     Radial blur of the bright buffer towards the sun's screen position:
     bright pixels smear away from the sun and dark geometry stays dark,
     so a silhouette in front of the sun carves a shadow out of the beam.
     Cheap, and the only screen-space technique that reads as a real
     shaft rather than as a glow. */
  const RAYS_FS = `
uniform sampler2D tDiffuse;
uniform vec2 uSunUv;
uniform float uDensity;
uniform float uDecay;
uniform float uWeight;
varying vec2 vUv;
const int SAMPLES = 28;
void main(){
  vec2 uv = vUv;
  vec2 stride = ( uv - uSunUv ) * ( uDensity / float( SAMPLES ) );
  vec3 col = vec3( 0.0 );
  float illum = 1.0;
  for ( int i = 0; i < SAMPLES; i ++ ) {
    uv -= stride;
    col += texture2D( tDiffuse, uv ).rgb * illum * uWeight;
    illum *= uDecay;
  }
  gl_FragColor = vec4( col / float( SAMPLES ), 1.0 );
}`;

  /* ---------- depth of field ----------
     Half-resolution twelve-tap disc. The radius follows the circle of
     confusion, so near geometry stays sharp while the taps spread out
     over the background. */
  const DOF_FS = `
uniform sampler2D tDiffuse;
uniform vec2 uTexel;
uniform float uFocus;
uniform float uRange;
uniform float uMaxRadius;
varying vec2 vUv;
${DEPTH_GLSL}
void main(){
  float dist = sfViewDepth( vUv );
  float coc = smoothstep( uFocus, uFocus + uRange, dist );
  float r = coc * uMaxRadius;
  vec3 sum = texture2D( tDiffuse, vUv ).rgb;
  float w = 1.0;
  for ( int i = 0; i < 12; i ++ ) {
    float a = float( i ) * 0.5236;                 // 30 degrees apart
    float ring = ( i < 6 ) ? 0.55 : 1.0;           // two rings, not one
    vec2 o = vec2( cos( a ), sin( a ) ) * uTexel * r * ring;
    sum += texture2D( tDiffuse, vUv + o ).rgb;
    w += 1.0;
  }
  gl_FragColor = vec4( sum / w, 1.0 );
}`;

  /* ---------- composite ---------- */
  const COMPOSITE_FS = `
uniform sampler2D tDiffuse;
uniform sampler2D tBloom;
uniform sampler2D tRays;
uniform sampler2D tDof;

uniform float uBloom;
uniform float uRays;
uniform vec3 uSunColour;
uniform vec3 uSunDir;          // direction the light travels, world space
uniform vec2 uSunUv;

uniform float uExposure;
uniform float uTime;
uniform float uGrain;
uniform float uDamage;
uniform float uVignette;
uniform float uAberration;

uniform float uAim;            // 0 hip fire, 1 fully down the sights
uniform float uDofFocus;
uniform float uDofRange;

uniform float uFogDensity;
uniform float uFogFalloff;
uniform float uFogBase;
uniform vec3  uFogColour;
uniform float uInscatter;
uniform float uFogSky;

uniform vec2 uProjParams;      // tan(fov/2) * aspect, tan(fov/2)
uniform mat4 uInvView;
uniform vec3 uCamPos;
uniform float uHasDepth;

varying vec2 vUv;
${DEPTH_GLSL}
${LUMA}

/* three's ACES fit, kept identical so the scene grades the same as it
   did when the renderer was doing the tone mapping itself. */
vec3 sfRRTAndODTFit( vec3 v ) {
  vec3 a = v * ( v + 0.0245786 ) - 0.000090537;
  vec3 b = v * ( 0.983729 * v + 0.4329510 ) + 0.238081;
  return a / b;
}
vec3 sfACES( vec3 colour ) {
  const mat3 IN = mat3(
    vec3( 0.59719, 0.07600, 0.02840 ),
    vec3( 0.35458, 0.90834, 0.13383 ),
    vec3( 0.04823, 0.01566, 0.83777 ) );
  const mat3 OUT = mat3(
    vec3(  1.60475, -0.10208, -0.00327 ),
    vec3( -0.53108,  1.10813, -0.07276 ),
    vec3( -0.07367, -0.00605,  1.07602 ) );
  colour = IN * ( colour / 0.6 );
  colour = sfRRTAndODTFit( colour );
  return clamp( OUT * colour, 0.0, 1.0 );
}

vec3 sfLinearToSRGB( vec3 c ) {
  return mix( pow( c, vec3( 0.41666 ) ) * 1.055 - vec3( 0.055 ),
              c * 12.92, vec3( lessThanEqual( c, vec3( 0.0031308 ) ) ) );
}

float sfHash( vec2 p ){ return fract( sin( dot( p, vec2( 127.1, 311.7 ) ) ) * 43758.5453 ); }

void main(){
  vec2 uv = vUv;
  vec2 fromCentre = uv - 0.5;
  float r2 = dot( fromCentre, fromCentre );

  /* Lens aberration: the three channels focus at slightly different
     radii, more so at the edge of the frame and much more when hurt. */
  float ab = uAberration * ( 0.0012 + uDamage * 0.004 ) * ( 0.35 + r2 );
  vec3 col;
  col.r = texture2D( tDiffuse, uv + fromCentre * ab ).r;
  col.g = texture2D( tDiffuse, uv ).g;
  col.b = texture2D( tDiffuse, uv - fromCentre * ab ).b;

  float dist = uHasDepth > 0.5 ? sfViewDepth( uv ) : uFar;

  /* Depth of field, only while aiming. */
  if ( uAim > 0.001 && uHasDepth > 0.5 ) {
    float coc = smoothstep( uDofFocus, uDofFocus + uDofRange, dist ) * uAim;
    col = mix( col, texture2D( tDof, uv ).rgb, clamp( coc, 0.0, 1.0 ) );
  }

  /* Volumetric fog: the analytic integral of an exponential height
     distribution along the view ray, so haze pools in the low ground and
     thins as you climb, and looking towards the sun scatters its colour
     back at you. */
  vec3 viewRay = vec3( ( uv * 2.0 - 1.0 ) * uProjParams, -1.0 );
  vec3 worldDir = normalize( ( uInvView * vec4( viewRay, 0.0 ) ).xyz );
  float rayLen = dist * length( viewRay );
  if ( uHasDepth > 0.5 && dist >= uFar * 0.995 ) rayLen *= uFogSky;

  vec3 endPos = uCamPos + worldDir * rayLen;
  float k = uFogFalloff;
  float dY = endPos.y - uCamPos.y;
  float t = k * dY;
  float span = ( abs( t ) < 1e-3 ) ? rayLen : rayLen * ( 1.0 - exp( -t ) ) / t;
  float optical = uFogDensity * exp( -k * ( uCamPos.y - uFogBase ) ) * max( span, 0.0 );
  float fogAmount = 1.0 - exp( -optical );

  float towardsSun = max( dot( worldDir, -uSunDir ), 0.0 );
  vec3 fogColour = uFogColour + uSunColour * uInscatter * pow( towardsSun, 8.0 );
  col = mix( col, fogColour, clamp( fogAmount, 0.0, 1.0 ) );

  /* Bloom, then the shafts, which are tinted by the sun and faded out as
     it leaves the frame so they never appear from nowhere. */
  col += texture2D( tBloom, uv ).rgb * uBloom;

  float sunOnScreen = ( 1.0 - smoothstep( 0.35, 0.95, length( uSunUv - vec2( 0.5 ) ) ) );
  col += texture2D( tRays, uv ).rgb * uSunColour * uRays * sunOnScreen;

  /* Everything above is radiance. From here down it is a picture. */
  col = sfACES( col * uExposure );
  col = sfLinearToSRGB( col );

  col += ( sfHash( uv * vec2( 1024.0, 768.0 ) + uTime ) - 0.5 ) * uGrain;
  col *= clamp( 1.0 - uVignette * r2 * 1.9, 0.0, 1.0 );
  col = mix( col, vec3( 0.55, 0.03, 0.05 ), uDamage * 0.3 );

  gl_FragColor = vec4( col, 1.0 );
}`;

  /* ---------- rig ---------- */

  const BLOOM_LEVELS = 5;

  /* ---------- quality ----------
     Cascaded shadows and a ten-pass composite are not free: three shadow
     maps mean three extra passes over the whole scene every frame, before
     anything is drawn to the screen. On a machine that cannot afford that,
     a beautiful frame arriving twenty times a second is worse than a plain
     one arriving sixty times, so the rig measures itself and steps down
     until it fits. Each tier gives up the least valuable thing left. */
  const TIERS = [
    { name: 'high',    pixelRatio: 1.75, mips: 5, rays: true,  dof: true,
      shadow: [2048, 1024, 1024], motes: 1.00 },
    { name: 'medium',  pixelRatio: 1.25, mips: 4, rays: true,  dof: true,
      shadow: [1024, 1024, 512],  motes: 0.70 },
    { name: 'low',     pixelRatio: 1.00, mips: 3, rays: false, dof: true,
      shadow: [1024, 512, 512],   motes: 0.45 },
    { name: 'minimal', pixelRatio: 0.75, mips: 2, rays: false, dof: false,
      shadow: [768, 512, 512],    motes: 0.20 }
  ];


  function create(canvas) {
    const renderer = new THREE.WebGLRenderer({
      canvas: canvas, antialias: true, powerPreference: 'high-performance'
    });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
    renderer.outputEncoding = THREE.sRGBEncoding;
    /* Tone mapping happens in the composite, not per material: bloom and
       the light shafts have to see radiance, and a tone-mapped buffer has
       already thrown the highlights away. */
    renderer.toneMapping = THREE.NoToneMapping;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.shadowMap.autoUpdate = true;
    /* Every pass declares its own clear, because the bloom up-chain
       accumulates into targets it must not wipe first. */
    renderer.autoClear = false;

    const gl2 = renderer.capabilities.isWebGL2;
    const hdrType = (gl2 || renderer.extensions.has('OES_texture_half_float'))
      ? THREE.HalfFloatType : THREE.UnsignedByteType;
    const canDepth = gl2 || renderer.extensions.has('WEBGL_depth_texture');

    SF.shading.install(renderer);

    const scene = new THREE.Scene();
    /* Clear colour bypasses the material output path, so it has to be
       written as linear or the background sits at a different brightness
       from everything painted next to it. */
    scene.background = new THREE.Color(0x05070b).convertSRGBToLinear();
    scene.fog = new THREE.FogExp2(0x0a1018, 0.019);

    const camera = new THREE.PerspectiveCamera(75, 1, 0.05, 3200);

    const hdrOpts = { minFilter: THREE.LinearFilter, magFilter: THREE.LinearFilter,
                      format: THREE.RGBAFormat, type: hdrType,
                      encoding: THREE.LinearEncoding };

    const sceneRT = new THREE.WebGLRenderTarget(1, 1, hdrOpts);
    if (canDepth) {
      sceneRT.depthTexture = new THREE.DepthTexture(1, 1);
      sceneRT.depthTexture.minFilter = THREE.NearestFilter;
      sceneRT.depthTexture.magFilter = THREE.NearestFilter;
    }

    const mips = [];
    for (let i = 0; i < BLOOM_LEVELS; i++) mips.push(new THREE.WebGLRenderTarget(1, 1, hdrOpts));
    const raysRT = new THREE.WebGLRenderTarget(1, 1, hdrOpts);
    const dofRT = new THREE.WebGLRenderTarget(1, 1, hdrOpts);

    const brightPass = pass(BRIGHT_FS, {
      tDiffuse: { value: null }, uThreshold: { value: 1.45 }, uKnee: { value: 0.5 }
    });
    const downPass = pass(DOWN_FS, {
      tDiffuse: { value: null }, uTexel: { value: new THREE.Vector2() }
    });
    const upPass = pass(UP_FS, {
      tDiffuse: { value: null }, uTexel: { value: new THREE.Vector2() },
      uRadius: { value: 1.0 },
      /* Each level contributes less than the one below it, so the five
         summed levels converge on about two and a half times the top level
         rather than five times it. Without this the bloom is a haze over
         the whole frame. */
      uStrength: { value: 0.62 }
    }, { blending: THREE.AdditiveBlending, transparent: true });
    const raysPass = pass(RAYS_FS, {
      tDiffuse: { value: null }, uSunUv: { value: new THREE.Vector2(0.5, 0.5) },
      uDensity: { value: 0.85 }, uDecay: { value: 0.94 }, uWeight: { value: 0.9 }
    });
    const dofPass = pass(DOF_FS, {
      tDiffuse: { value: null }, tDepth: { value: null },
      uTexel: { value: new THREE.Vector2() },
      uNear: { value: 0.05 }, uFar: { value: 3200 },
      uFocus: { value: 16 }, uRange: { value: 60 }, uMaxRadius: { value: 4 }
    });
    const composite = pass(COMPOSITE_FS, {
      tDiffuse: { value: null }, tBloom: { value: null },
      tRays: { value: null }, tDof: { value: null }, tDepth: { value: null },
      uBloom: { value: 0.30 }, uRays: { value: 0.22 },
      uSunColour: { value: new THREE.Color(1, 0.94, 0.82) },
      uSunDir: { value: new THREE.Vector3(-0.42, -0.26, -0.34).normalize() },
      uSunUv: { value: new THREE.Vector2(0.5, 1.6) },
      uExposure: { value: 1.10 }, uTime: { value: 0 }, uGrain: { value: 0.035 },
      uDamage: { value: 0 }, uVignette: { value: 0.7 }, uAberration: { value: 1 },
      uAim: { value: 0 }, uDofFocus: { value: 16 }, uDofRange: { value: 60 },
      uFogDensity: { value: 0.02 }, uFogFalloff: { value: 0.06 },
      uFogBase: { value: -2 }, uFogColour: { value: new THREE.Color(0x14212f) },
      uInscatter: { value: 0.4 }, uFogSky: { value: 0.35 },
      uProjParams: { value: new THREE.Vector2(1, 1) },
      uInvView: { value: new THREE.Matrix4() },
      uCamPos: { value: new THREE.Vector3() },
      uNear: { value: 0.05 }, uFar: { value: 3200 },
      uHasDepth: { value: canDepth ? 1 : 0 }
    });

    const quadCam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);

    /* The sun as the world sees it: a direction light travels along. The
       shafts need where it lands on screen, the fog needs its colour. */
    const sunDir = new THREE.Vector3(-0.42, -0.26, -0.34).normalize();
    const _sunPoint = new THREE.Vector3();
    const _fwd = new THREE.Vector3();

    let width = 1, height = 1;

    /* ---------- quality state ---------- */
    const listeners = [];
    const maxPixelRatio = Math.min(window.devicePixelRatio || 1, 1.75);
    let tier = 0;
    let activeMips = TIERS[0].mips;
    let raysOn = TIERS[0].rays;
    let dofOn = TIERS[0].dof;
    let auto = true;
    let downgrades = 0;
    let emaMs = 16, slowFrames = 0, fastFrames = 0, lastFrameAt = 0;

    function applyTier(i) {
      tier = Math.max(0, Math.min(TIERS.length - 1, i));
      const q = TIERS[tier];
      activeMips = q.mips;
      raysOn = q.rays;
      dofOn = q.dof;
      if (!raysOn) composite.u.uRays.value = 0;
      renderer.setPixelRatio(Math.min(maxPixelRatio, q.pixelRatio));
      resize();
      for (const fn of listeners) { try { fn(q, tier); } catch (err) { void err; } }
    }

    /* Frame pacing. The rig watches a smoothed frame time rather than any
       single frame, because one slow frame is a garbage collection and a
       hundred of them is a machine that cannot keep up. */
    function trackFrame() {
      const t = performance.now();
      if (lastFrameAt) {
        const ms = t - lastFrameAt;
        // a gap this long is a backgrounded tab, not a slow frame
        if (ms < 250) emaMs += (ms - emaMs) * 0.1;
      }
      lastFrameAt = t;
      if (!auto) return;

      if (emaMs > 26) { slowFrames++; fastFrames = 0; }
      else if (emaMs < 13) { fastFrames++; slowFrames = 0; }
      else { slowFrames = 0; fastFrames = 0; }

      if (slowFrames > 90 && tier < TIERS.length - 1) {
        slowFrames = 0; downgrades++;
        applyTier(tier + 1);
      } else if (tier > 0 && fastFrames > 600 * (downgrades + 1)) {
        // climbing back is deliberately much harder than falling, so the
        // rig cannot sit oscillating between two tiers
        fastFrames = 0;
        applyTier(tier - 1);
      }
    }

    function resize() {
      const w = canvas.clientWidth || window.innerWidth;
      const h = canvas.clientHeight || window.innerHeight;
      renderer.setSize(w, h, false);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();

      const pr = renderer.getPixelRatio();
      width = Math.max(1, Math.floor(w * pr));
      height = Math.max(1, Math.floor(h * pr));

      sceneRT.setSize(width, height);
      if (sceneRT.depthTexture) {
        sceneRT.depthTexture.image.width = width;
        sceneRT.depthTexture.image.height = height;
      }

      const bw = Math.max(1, width >> 1), bh = Math.max(1, height >> 1);
      for (let i = 0; i < BLOOM_LEVELS; i++) {
        mips[i].setSize(Math.max(1, bw >> i), Math.max(1, bh >> i));
      }
      raysRT.setSize(Math.max(1, width >> 2), Math.max(1, height >> 2));
      dofRT.setSize(bw, bh);

      composite.u.uProjParams.value.set(
        Math.tan(THREE.MathUtils.degToRad(camera.fov * 0.5)) * camera.aspect,
        Math.tan(THREE.MathUtils.degToRad(camera.fov * 0.5)));
    }
    window.addEventListener('resize', resize);

    /* autoClear is off for the whole rig, so every pass says whether it
       replaces its target or accumulates into it. */
    function draw(p, target, clear) {
      renderer.setRenderTarget(target);
      if (clear !== false) renderer.clear();
      renderer.render(p.scene, quadCam);
    }

    /* Where the sun lands on screen, in UV. It sits at infinity along
       -sunDir, so a point a long way out in that direction projects to the
       same place and can just be run through the camera. */
    function sunScreen() {
      _sunPoint.copy(camera.position).addScaledVector(sunDir, -20000);
      _sunPoint.project(camera);
      const behind = camera.getWorldDirection(_fwd).dot(sunDir) > 0;
      if (behind) return composite.u.uSunUv.value.set(0.5, 3.0);   // off frame
      return composite.u.uSunUv.value.set(_sunPoint.x * 0.5 + 0.5, _sunPoint.y * 0.5 + 0.5);
    }

    function render(time, damage) {
      trackFrame();

      // ---- scene, in linear HDR
      renderer.setRenderTarget(sceneRT);
      renderer.clear();
      renderer.render(scene, camera);

      // ---- bright pass, straight into the top bloom level
      brightPass.u.tDiffuse.value = sceneRT.texture;
      draw(brightPass, mips[0], true);

      /* ---- bloom: down the chain, then back up, summing as it goes.
         The upsample blends additively, so those targets must not be
         cleared first — the sum is the whole point. */
      for (let i = 1; i < activeMips; i++) {
        downPass.u.tDiffuse.value = mips[i - 1].texture;
        downPass.u.uTexel.value.set(1 / mips[i - 1].width, 1 / mips[i - 1].height);
        draw(downPass, mips[i], true);
      }
      for (let i = activeMips - 1; i > 0; i--) {
        upPass.u.tDiffuse.value = mips[i].texture;
        upPass.u.uTexel.value.set(1 / mips[i].width, 1 / mips[i].height);
        draw(upPass, mips[i - 1], false);
      }

      // ---- light shafts, radial-blurred out of the same bright buffer
      const uv = sunScreen();
      if (raysOn && composite.u.uRays.value > 0.001 && uv.y < 1.5) {
        raysPass.u.tDiffuse.value = mips[1].texture;
        raysPass.u.uSunUv.value.copy(uv);
        draw(raysPass, raysRT, true);
      }

      // ---- depth of field, only worth the pass while aiming
      if (canDepth && dofOn && composite.u.uAim.value > 0.001) {
        dofPass.u.tDiffuse.value = sceneRT.texture;
        dofPass.u.tDepth.value = sceneRT.depthTexture;
        dofPass.u.uTexel.value.set(1 / dofRT.width, 1 / dofRT.height);
        draw(dofPass, dofRT, true);
      }

      // ---- composite to the screen
      const c = composite.u;
      c.tDiffuse.value = sceneRT.texture;
      c.tBloom.value = mips[0].texture;
      c.tRays.value = raysRT.texture;
      c.tDof.value = dofRT.texture;
      c.uAim.value = dofOn ? c.uAim.value : 0;
      c.tDepth.value = sceneRT.depthTexture || null;
      c.uTime.value = time;
      c.uDamage.value = damage || 0;
      c.uSunDir.value.copy(sunDir);
      c.uCamPos.value.copy(camera.position);
      c.uInvView.value.copy(camera.matrixWorld);
      c.uNear.value = camera.near;
      c.uFar.value = camera.far;
      c.uProjParams.value.set(
        Math.tan(THREE.MathUtils.degToRad(camera.fov * 0.5)) * camera.aspect,
        Math.tan(THREE.MathUtils.degToRad(camera.fov * 0.5)));
      dofPass.u.uNear.value = camera.near;
      dofPass.u.uFar.value = camera.far;
      draw(composite, null, true);
    }

    const api = {
      renderer, scene, camera, render, resize, composite,
      capabilities: { hdr: hdrType === THREE.HalfFloatType, depth: canDepth },

      /* The sun, as a direction the light travels in. Everything that
         needs it — cascades, shafts, fog inscatter, dust — is fed from
         this one call. */
      setSun(dir, colour, exposure) {
        sunDir.copy(dir).normalize();
        if (colour !== undefined) composite.u.uSunColour.value.set(colour);
        if (exposure !== undefined) composite.u.uExposure.value = exposure;
        return sunDir;
      },
      get sunDir() { return sunDir; },

      /* Fog parameters, straight from fps/atmos.js. */
      setFog(fog) {
        if (!fog) return;
        const c = composite.u;
        if (fog.density !== undefined) c.uFogDensity.value = fog.density;
        if (fog.falloff !== undefined) c.uFogFalloff.value = fog.falloff;
        if (fog.base !== undefined) c.uFogBase.value = fog.base;
        if (fog.colour !== undefined) c.uFogColour.value.copy(fog.colour);
        if (fog.inscatter !== undefined) c.uInscatter.value = fog.inscatter;
        if (fog.sky !== undefined) c.uFogSky.value = fog.sky;
      },

      /* 0 at the hip, 1 fully down the sights. */
      setAim(t) { composite.u.uAim.value = dofOn ? THREE.MathUtils.clamp(t, 0, 1) : 0; },

      /* Quality. Anything that costs frames per tier — the shadow map sizes,
         how much dust there is — registers here rather than polling. */
      quality: {
        TIERS,
        get tier() { return tier; },
        get name() { return TIERS[tier].name; },
        get frameMs() { return +emaMs.toFixed(2); },
        get auto() { return auto; },
        set auto(v) { auto = !!v; },
        set(i) { auto = false; applyTier(i); },
        onChange(fn) { listeners.push(fn); fn(TIERS[tier], tier); }
      },

      set(name, value) {
        const u = composite.u[name];
        if (!u) return;
        if (u.value && u.value.copy && value && value.isVector2) u.value.copy(value);
        else if (u.value && u.value.set && typeof value === 'number' && u.value.isColor) u.value.set(value);
        else u.value = value;
      },

      dispose() {
        sceneRT.dispose();
        for (const m of mips) m.dispose();
        raysRT.dispose();
        dofRT.dispose();
        window.removeEventListener('resize', resize);
      }
    };

    resize();
    return api;
  }

  SF.engine = { create };
})(window.SF);
