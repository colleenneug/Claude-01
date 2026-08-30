/* ============================================================
   Renderer, tone mapping and post-processing.

   three's EffectComposer ships only as an ES module, and this project
   stays on classic scripts so it can run from file://. The bloom /
   grain / vignette chain below is therefore hand-rolled: render to a
   target, threshold the bright pixels, blur them separably at half
   resolution, then composite.
   ============================================================ */
(function (SF) {
  'use strict';

  const QUAD = new THREE.PlaneGeometry(2, 2);

  function fullscreenPass(fragment, uniforms) {
    const mat = new THREE.ShaderMaterial({
      uniforms: uniforms,
      vertexShader: `
        varying vec2 vUv;
        void main(){ vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }`,
      fragmentShader: fragment,
      depthTest: false, depthWrite: false
    });
    const scene = new THREE.Scene();
    scene.add(new THREE.Mesh(QUAD, mat));
    return { scene, mat };
  }

  const THRESHOLD_FS = `
    uniform sampler2D tDiffuse; uniform float uThreshold; varying vec2 vUv;
    void main(){
      vec3 c = texture2D(tDiffuse, vUv).rgb;
      float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
      float k = smoothstep(uThreshold, uThreshold + 0.45, l);
      gl_FragColor = vec4(c * k, 1.0);
    }`;

  const BLUR_FS = `
    uniform sampler2D tDiffuse; uniform vec2 uDir; varying vec2 vUv;
    void main(){
      vec4 sum = texture2D(tDiffuse, vUv) * 0.2270270270;
      sum += texture2D(tDiffuse, vUv + uDir * 1.3846153846) * 0.3162162162;
      sum += texture2D(tDiffuse, vUv - uDir * 1.3846153846) * 0.3162162162;
      sum += texture2D(tDiffuse, vUv + uDir * 3.2307692308) * 0.0702702703;
      sum += texture2D(tDiffuse, vUv - uDir * 3.2307692308) * 0.0702702703;
      gl_FragColor = sum;
    }`;

  /* Composite: bloom, chromatic aberration at the edges, film grain,
     vignette, and a red pulse when the player is hurt. */
  const COMPOSITE_FS = `
    uniform sampler2D tDiffuse; uniform sampler2D tBloom;
    uniform float uBloom; uniform float uTime; uniform float uGrain;
    uniform float uDamage; uniform float uVignette; uniform float uAberration;
    varying vec2 vUv;

    float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

    void main(){
      vec2 uv = vUv;
      vec2 fromCentre = uv - 0.5;
      float r2 = dot(fromCentre, fromCentre);

      float ab = uAberration * (0.0015 + uDamage * 0.004);
      vec3 col;
      col.r = texture2D(tDiffuse, uv + fromCentre * ab).r;
      col.g = texture2D(tDiffuse, uv).g;
      col.b = texture2D(tDiffuse, uv - fromCentre * ab).b;

      col += texture2D(tBloom, uv).rgb * uBloom;

      float grain = (hash(uv * vec2(1024.0, 768.0) + uTime) - 0.5) * uGrain;
      col += grain;

      float vig = 1.0 - uVignette * r2 * 1.9;
      col *= clamp(vig, 0.0, 1.0);

      col = mix(col, vec3(0.55, 0.03, 0.05), uDamage * 0.3);

      gl_FragColor = vec4(col, 1.0);
    }`;

  function create(canvas) {
    const renderer = new THREE.WebGLRenderer({
      canvas: canvas, antialias: true, powerPreference: 'high-performance'
    });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
    renderer.outputEncoding = THREE.sRGBEncoding;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.5;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x05070b);
    scene.fog = new THREE.FogExp2(0x0a1018, 0.019);

    const camera = new THREE.PerspectiveCamera(75, 1, 0.05, 3200);

    const rtOpts = { minFilter: THREE.LinearFilter, magFilter: THREE.LinearFilter,
                     format: THREE.RGBAFormat, encoding: THREE.sRGBEncoding };
    let sceneRT = new THREE.WebGLRenderTarget(1, 1, rtOpts);
    let bloomA = new THREE.WebGLRenderTarget(1, 1, rtOpts);
    let bloomB = new THREE.WebGLRenderTarget(1, 1, rtOpts);

    const threshold = fullscreenPass(THRESHOLD_FS, {
      tDiffuse: { value: null }, uThreshold: { value: 0.72 }
    });
    const blur = fullscreenPass(BLUR_FS, {
      tDiffuse: { value: null }, uDir: { value: new THREE.Vector2() }
    });
    const composite = fullscreenPass(COMPOSITE_FS, {
      tDiffuse: { value: null }, tBloom: { value: null },
      uBloom: { value: 0.85 }, uTime: { value: 0 }, uGrain: { value: 0.055 },
      uDamage: { value: 0 }, uVignette: { value: 0.7 }, uAberration: { value: 1 }
    });

    const quadCam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);

    function resize() {
      const w = canvas.clientWidth || window.innerWidth;
      const h = canvas.clientHeight || window.innerHeight;
      renderer.setSize(w, h, false);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();

      const pr = renderer.getPixelRatio();
      const fw = Math.max(1, Math.floor(w * pr)), fh = Math.max(1, Math.floor(h * pr));
      sceneRT.setSize(fw, fh);
      bloomA.setSize(Math.max(1, fw >> 1), Math.max(1, fh >> 1));
      bloomB.setSize(Math.max(1, fw >> 1), Math.max(1, fh >> 1));
    }
    window.addEventListener('resize', resize);

    function render(time, damage) {
      // scene -> target
      renderer.setRenderTarget(sceneRT);
      renderer.clear();
      renderer.render(scene, camera);

      // bright pass
      threshold.mat.uniforms.tDiffuse.value = sceneRT.texture;
      renderer.setRenderTarget(bloomA);
      renderer.render(threshold.scene, quadCam);

      // separable blur, twice
      const w = bloomA.width, h = bloomA.height;
      for (let i = 0; i < 2; i++) {
        blur.mat.uniforms.tDiffuse.value = bloomA.texture;
        blur.mat.uniforms.uDir.value.set(1 / w, 0);
        renderer.setRenderTarget(bloomB);
        renderer.render(blur.scene, quadCam);

        blur.mat.uniforms.tDiffuse.value = bloomB.texture;
        blur.mat.uniforms.uDir.value.set(0, 1 / h);
        renderer.setRenderTarget(bloomA);
        renderer.render(blur.scene, quadCam);
      }

      // composite to screen
      composite.mat.uniforms.tDiffuse.value = sceneRT.texture;
      composite.mat.uniforms.tBloom.value = bloomA.texture;
      composite.mat.uniforms.uTime.value = time;
      composite.mat.uniforms.uDamage.value = damage || 0;
      renderer.setRenderTarget(null);
      renderer.render(composite.scene, quadCam);
    }

    resize();
    return { renderer, scene, camera, render, resize, composite };
  }

  SF.engine = { create };
})(window.SF);
