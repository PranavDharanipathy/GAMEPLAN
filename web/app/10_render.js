// ================= renderer, sky, arena lighting rig, post-processing =================
const canvas = $("view");
const renderer = new THREE.WebGLRenderer({ canvas, antialias: false, powerPreference: "high-performance" });
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
const scene = new THREE.Scene();

// The rig: a square truss RIG_H above the field carries 8 spot fixtures. They light the field, the
// drive team areas and a little beyond; the forest outside only gets faint moonlight.
const RIG_H = 6.4, RIG = 3.9, FIX_ANGLE = 0.5, SHADOW_NEAR = 2, SHADOW_FAR = 14, FIX_LENS = 0.35;

// ---- soft shadows: contact-hardening (PCSS) for the rig's spot lights ----
// Real fixtures are not points: a 35 cm lens 6 m up gives sharp contact shadows under a ball and
// soft, wide penumbrae under the tall HIVE. The blocker search finds the average occluder depth,
// the penumbra width follows from similar triangles, and a rotated Poisson disk filters it.
// Active when shadowMap.type is PCFShadowMap ("soft shadows" on); PCFSoftShadowMap stays stock.
{
  const disk = [[-0.94201624, -0.39906216], [0.94558609, -0.76890725], [-0.094184101, -0.92938870], [0.34495938, 0.29387760],
    [-0.91588581, 0.45771432], [-0.81544232, -0.87912464], [-0.38277543, 0.27676845], [0.97484398, 0.75648379],
    [0.44323325, -0.97511554], [0.53742981, -0.47373420], [-0.26496911, -0.41893023], [0.79197514, 0.19090188],
    [-0.24188840, 0.99706507], [-0.81409955, 0.91437590], [0.19984126, 0.78641367], [0.14383161, -0.14100790]];
  const f = (x) => x.toFixed(6);
  const pcss = `
	#define GP_N ${f(SHADOW_NEAR)}
	#define GP_F ${f(SHADOW_FAR)}
	#define GP_LIGHT_UV ${f(FIX_LENS / (2 * Math.tan(FIX_ANGLE)))}
	const vec2 gpDisk[ 16 ] = vec2[]( ${disk.map((d) => `vec2( ${f(d[0])}, ${f(d[1])} )`).join(", ")} );
	float gpLinZ( float d ) { return GP_N * GP_F / ( GP_F - d * ( GP_F - GP_N ) ); }
	float gpPCSS( sampler2D map, vec2 mapSize, vec2 uv, float zr ) {
		float zR = gpLinZ( zr );
		float zNear = max( GP_N, zR - 2.2 );                  // occluders stand at most ~2 m above what they shade
		float search = 0.5 * GP_LIGHT_UV * ( zR - zNear ) / ( zR * zNear );
		float a = 6.2831853 * fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );
		mat2 rot = mat2( cos( a ), sin( a ), - sin( a ), cos( a ) );
		float sum = 0.0, n = 0.0;
		for ( int i = 0; i < 16; i ++ ) {
			float d = unpackRGBAToDepth( texture2D( map, uv + rot * gpDisk[ i ] * search ) );
			if ( d < zr ) { sum += gpLinZ( d ); n += 1.0; }
		}
		if ( n < 0.5 ) return 1.0;
		float zB = sum / n;
		float rad = clamp( 0.5 * GP_LIGHT_UV * ( zR - zB ) / ( zB * zR ), 1.0 / mapSize.x, 28.0 / mapSize.x );
		float lit = 0.0;
		for ( int i = 0; i < 16; i ++ ) lit += texture2DCompare( map, uv + rot * gpDisk[ i ] * rad, zr );
		return lit * ( 1.0 / 16.0 );
	}
	vec2 texture2DDistribution(`;
  let ch = THREE.ShaderChunk.shadowmap_pars_fragment;
  const pcfBranch = /#if defined\( SHADOWMAP_TYPE_PCF \)\n[\s\S]*?(?=#elif defined\( SHADOWMAP_TYPE_PCF_SOFT \))/;
  if (ch.includes("vec2 texture2DDistribution(") && pcfBranch.test(ch)) {
    ch = ch.replace("vec2 texture2DDistribution(", pcss)
      .replace(pcfBranch, "#if defined( SHADOWMAP_TYPE_PCF )\n\t\t\tshadow = gpPCSS( shadowMap, shadowMapSize, shadowCoord.xy, shadowCoord.z );\n\t\t");
    THREE.ShaderChunk.shadowmap_pars_fragment = ch;
  } else console.warn("soft shadows unavailable: unexpected three.js shadow shader");
}

// ---- night sky (static) and fog depth for the forest ----
{
  const W = 2048, Hs = 1024, c = document.createElement("canvas"); c.width = W; c.height = Hs; const g = c.getContext("2d");
  const gr = g.createLinearGradient(0, 0, 0, Hs);
  gr.addColorStop(0, "#02040a"); gr.addColorStop(0.35, "#061022"); gr.addColorStop(0.47, "#0f1a2c"); gr.addColorStop(0.5, "#141a20");
  gr.addColorStop(0.53, "#07090a"); gr.addColorStop(1, "#030404");
  g.fillStyle = gr; g.fillRect(0, 0, W, Hs);
  let sd = 97; const rnd = () => (sd = (sd * 16807) % 2147483647) / 2147483647;
  for (let i = 0; i < 2600; i++) {   // stars, denser and brighter toward the zenith
    const y = Math.pow(rnd(), 1.6) * Hs * 0.47, x = rnd() * W, m = rnd();
    g.fillStyle = `rgba(${220 + 35 * rnd() | 0},${225 + 30 * rnd() | 0},255,${0.25 + 0.75 * m * (1 - y / (Hs * 0.5))})`;
    g.fillRect(x, y, m > 0.97 ? 2 : 1, m > 0.97 ? 2 : 1);
  }
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.mapping = THREE.EquirectangularReflectionMapping;
  scene.background = t;
}
scene.fog = new THREE.FogExp2(0x070b10, 0.03);
const camera = new THREE.PerspectiveCamera(75, 1, 0.03, 260);
// GamePlan field frame (x to blue wall, y away from audience, z up) inside the three.js world (y up)
const gp = new THREE.Group(); gp.rotation.x = -Math.PI / 2; scene.add(gp);
const T = (x, y, z) => new THREE.Vector3(x, z, -y);

const pmrem = new THREE.PMREMGenerator(renderer);
{   // environment for reflections = the same surroundings: dark woods, bright fixtures overhead, the lit field below
  const es = new THREE.Scene(); es.background = new THREE.Color(0x030507);
  const lamp = new THREE.MeshBasicMaterial({ color: new THREE.Color(1, 0.97, 0.9).multiplyScalar(18), side: THREE.DoubleSide });
  for (const [x, z] of [[-RIG, -RIG], [RIG, -RIG], [-RIG, RIG], [RIG, RIG], [0, -RIG], [0, RIG], [-RIG, 0], [RIG, 0]]) {
    const m = new THREE.Mesh(new THREE.CircleGeometry(0.28, 20), lamp); m.position.set(x, RIG_H, z); m.rotation.x = Math.PI / 2; es.add(m);
  }
  const truss = new THREE.MeshBasicMaterial({ color: 0x2a2d31 });
  for (const s of [-1, 1]) for (const horiz of [true, false]) {
    const b = new THREE.Mesh(new THREE.BoxGeometry(horiz ? 2 * RIG + 1.2 : 0.3, 0.3, horiz ? 0.3 : 2 * RIG + 1.2), truss);
    b.position.set(horiz ? 0 : s * (RIG + 0.6), RIG_H + 0.3, horiz ? s * (RIG + 0.6) : 0); es.add(b);
  }
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(60, 60), new THREE.MeshBasicMaterial({ color: 0x1d2024 })); ground.rotation.x = -Math.PI / 2; es.add(ground);
  const field = new THREE.Mesh(new THREE.PlaneGeometry(9.5, 7.5), new THREE.MeshBasicMaterial({ color: new THREE.Color(0.5, 0.51, 0.53) })); field.rotation.x = -Math.PI / 2; field.position.y = 0.01; es.add(field);
  const tiles = new THREE.Mesh(new THREE.PlaneGeometry(3.66, 3.66), new THREE.MeshBasicMaterial({ color: new THREE.Color(0.33, 0.34, 0.35) })); tiles.rotation.x = -Math.PI / 2; tiles.position.y = 0.02; es.add(tiles);
  const ring = new THREE.Mesh(new THREE.CylinderGeometry(14, 14, 10, 32, 1, true), new THREE.MeshBasicMaterial({ color: 0x0d120c, side: THREE.BackSide })); ring.position.y = 5; es.add(ring);
  // spill light around the field: drive stations, mats and the first trees catch some of the rig's light
  const spill = new THREE.Mesh(new THREE.CylinderGeometry(7, 7, 2.2, 32, 1, true), new THREE.MeshBasicMaterial({ color: new THREE.Color(0.16, 0.17, 0.17), side: THREE.BackSide }));
  spill.position.y = 1.1; es.add(spill);
  scene.environment = pmrem.fromScene(es, 0.02).texture;
}
RectAreaLightUniformsLib.init();
const fill = new THREE.RectAreaLight(0xfff3e2, 1.2, 4.2, 4.2);   // the diffuse glow of the whole rig
fill.position.set(0, RIG_H - 0.3, 0); fill.lookAt(0, 0, 0); scene.add(fill);
const fixtures = [];
for (const [x, z] of [[-RIG, -RIG], [RIG, -RIG], [-RIG, RIG], [RIG, RIG], [0, -RIG], [0, RIG], [-RIG, 0], [RIG, 0]]) {
  const s = new THREE.SpotLight(0xfff1df, 40, 0, FIX_ANGLE, 0.45, 2);
  s.position.set(x, RIG_H, z);
  s.target.position.set(-x * 0.18, 0, -z * 0.18);              // cross-lighting from all sides, like an event rig
  s.shadow.bias = -0.0002; s.shadow.normalBias = 0.015; s.shadow.radius = 2;
  s.shadow.camera.near = SHADOW_NEAR; s.shadow.camera.far = SHADOW_FAR;
  scene.add(s, s.target); fixtures.push(s);
}
const moon = new THREE.DirectionalLight(0x9db4dc, 0.45); moon.position.set(-30, 40, 20); scene.add(moon);
scene.add(new THREE.HemisphereLight(0x2c3f5e, 0x0c0e0a, 0.22));

// ---- post-processing: MSAA scene target, ambient occlusion, bloom, tone mapping, camera grain ----
// Film/sensor look: slight vignette and fine, animated grain strongest in the mid-tones.
const FilmShader = {
  uniforms: { tDiffuse: { value: null }, uTime: { value: 0 }, uGrain: { value: 0.03 }, uVignette: { value: 0.24 }, uAspect: { value: 1.7 }, uRes: { value: new THREE.Vector2(1, 1) } },
  vertexShader: "varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 ); }",
  fragmentShader: `uniform sampler2D tDiffuse; uniform float uTime, uGrain, uVignette, uAspect; uniform vec2 uRes; varying vec2 vUv;
    float h12( vec2 p ) { vec3 p3 = fract( vec3( p.xyx ) * 0.1031 ); p3 += dot( p3, p3.yzx + 33.33 ); return fract( ( p3.x + p3.y ) * p3.z ); }
    void main() {
      vec4 c = texture2D( tDiffuse, vUv );
      vec2 q = vUv - 0.5; q.x *= uAspect;
      float v = 1.0 - uVignette * smoothstep( 0.3, 0.95, length( q ) );
      float l = dot( c.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
      float g = ( h12( vUv * uRes + fract( uTime * 7.31 ) * 517.0 ) - 0.5 ) * uGrain * ( 1.0 - abs( l - 0.45 ) );
      gl_FragColor = vec4( c.rgb * v + g, c.a );
    }`,
};
let composer = null, renderPass = null, aoPass = null, bloomPass = null, outputPass = null, filmPass = null;
function buildComposer() {
  const rt = new THREE.WebGLRenderTarget(2, 2, { type: THREE.HalfFloatType, samples: 4 });
  composer = new EffectComposer(renderer, rt);
  renderPass = new RenderPass(scene, camera); composer.addPass(renderPass);
  aoPass = new GTAOPass(scene, camera, 2, 2);
  aoPass.updateGtaoMaterial({ radius: 0.32, distanceExponent: 1.4, thickness: 0.5, scale: 1.0 });
  // clear sheets and alpha-cut foliage cards don't occlude: leave them out of the AO depth/normal pass
  aoPass.overrideVisibility = function () {
    const cache = this._visibilityCache;
    this.scene.traverse((o) => {
      cache.set(o, o.visible);
      if (o.isPoints || o.isLine || o.userData.noAO || (o.material && o.material.userData && o.material.userData.sheet)) o.visible = false;
    });
  };
  composer.addPass(aoPass);
  bloomPass = new UnrealBloomPass(new THREE.Vector2(256, 256), 0.16, 0.4, 1.1);   // glare around the fixture lenses and LEDs only
  composer.addPass(bloomPass);
  outputPass = new OutputPass(); composer.addPass(outputPass);
  filmPass = new ShaderPass(FilmShader); composer.addPass(filmPass);
}
buildComposer();

// ---- apply the graphics settings (presets or custom) ----
const SHADOW_ORDER = [0, 3, 1, 2, 4, 5, 6, 7];   // opposite corners first, then the rest
let lastSize = "", vegetation = [];
function refreshMaterials() { scene.traverse((o) => { if (o.material) [].concat(o.material).forEach((m) => { m.needsUpdate = true; }); }); }
function applyGraphics() {
  const s = settings, dpr = window.devicePixelRatio || 1;
  renderer.setPixelRatio(s.res >= 2 ? Math.min(dpr, 2) : Math.min(s.res, dpr));
  renderer.toneMapping = s.toneMap === "aces" ? THREE.ACESFilmicToneMapping : THREE.AgXToneMapping;
  renderer.toneMappingExposure = s.exposure * (s.toneMap === "aces" ? 0.85 : 1.25);
  const nShadow = clamp(s.shadowLights | 0, 0, 8), soft = s.softShadows && nShadow > 0;
  const type = soft ? THREE.PCFShadowMap : THREE.PCFSoftShadowMap;
  if (renderer.shadowMap.type !== type || renderer.shadowMap.enabled !== nShadow > 0) {
    renderer.shadowMap.type = type; renderer.shadowMap.enabled = nShadow > 0; refreshMaterials();
  }
  // the casting fixtures carry more of the light so little of it leaks under robots unshadowed
  const casters = SHADOW_ORDER.slice(0, nShadow), total = 40 * 8;
  const wCast = nShadow === 0 || nShadow === 8 ? 1 : Math.min(1.6, 0.6 * 8 / nShadow);
  const wRest = nShadow === 0 || nShadow === 8 ? 1 : (8 - nShadow * wCast) / (8 - nShadow);
  fixtures.forEach((f, i) => {
    const c = casters.includes(i);
    f.intensity = total / 8 * (c ? wCast : wRest);
    if (f.castShadow !== c || f.shadow.mapSize.x !== s.shadowMap) {
      f.castShadow = c; f.shadow.mapSize.set(s.shadowMap, s.shadowMap);
      if (f.shadow.map) { f.shadow.map.dispose(); f.shadow.map = null; }
    }
  });
  aoPass.enabled = s.ao; bloomPass.enabled = s.bloom; filmPass.enabled = s.film;
  vegetation.forEach((o) => { o.visible = s.vegetation === "full" || !o.userData.optional; });
  lastSize = "";
}
onSettings((k) => { if (k === "*" || GFX_KEYS.includes(k) || ["preset", "toneMap", "exposure"].includes(k)) applyGraphics(); });
function resize() {
  const w = canvas.clientWidth, h = canvas.clientHeight, key = w + "x" + h + "@" + renderer.getPixelRatio();
  if (key === lastSize) return;
  lastSize = key;
  renderer.setSize(w, h, false); composer.setPixelRatio(renderer.getPixelRatio()); composer.setSize(w, h);
  camera.aspect = w / Math.max(1, h); camera.updateProjectionMatrix();
  filmPass.uniforms.uAspect.value = w / Math.max(1, h);
  filmPass.uniforms.uRes.value.set(w * renderer.getPixelRatio(), h * renderer.getPixelRatio());
}
