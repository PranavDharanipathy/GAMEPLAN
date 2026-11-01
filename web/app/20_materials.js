// ================= materials: finish by part type, colour from the official STEP =================
const std = (o) => new THREE.MeshStandardMaterial(o);
const phys = (o) => new THREE.MeshPhysicalMaterial(o);
function noiseTex(size, base, amp, seed = 1) {   // fine foam / powder-coat texture
  const c = document.createElement("canvas"); c.width = c.height = size;
  const g = c.getContext("2d"), img = g.createImageData(size, size);
  let s = seed;
  for (let i = 0; i < size * size; i++) { s = (s * 16807) % 2147483647; const v = base + (s / 2147483647 - 0.5) * amp; img.data.set([v, v, v, 255], i * 4); }
  g.putImageData(img, 0, 0);
  const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping; return t;
}

// ---- clear sheet (polycarbonate): HIVE skins, perimeter panels, robot guards ----
// A thin clear sheet passes nearly all light straight through; what you see is the scene behind it
// (including other clear parts), the Fresnel reflections of its two surfaces (weak face-on, strong at
// grazing angles, which is what outlines a real sheet), a little absorption along the path and a
// faint haze from scuffs. Rendered as premultiplied "over": colour = reflected (+ haze) light,
// alpha = the share of the background that is blocked. No refraction pass: a 1.5-5 mm sheet
// shifts what is behind it by under a millimetre.
function clearSheet({ tint = 0xeef4f5, haze = 0.025, absorb = 0.01, roughness = 0.03 } = {}) {
  const m = phys({ color: tint, metalness: 0, roughness, ior: 1.585, specularIntensity: 1, envMapIntensity: 1.1,
    transparent: true, depthWrite: false, side: THREE.DoubleSide, fog: false,
    blending: THREE.CustomBlending, blendEquation: THREE.AddEquation, blendSrc: THREE.OneFactor, blendDst: THREE.OneMinusSrcAlphaFactor,
    blendSrcAlpha: THREE.OneFactor, blendDstAlpha: THREE.OneMinusSrcAlphaFactor });
  m.userData.noShadow = true; m.userData.sheet = true;
  m.onBeforeCompile = (sh) => {
    sh.uniforms.sheetHaze = { value: haze }; sh.uniforms.sheetAbsorb = { value: absorb };
    sh.fragmentShader = "uniform float sheetHaze;\nuniform float sheetAbsorb;\n" + sh.fragmentShader.replace("#include <opaque_fragment>", `
      float sheetNV = clamp( abs( dot( normal, geometryViewDir ) ), 0.03, 1.0 );
      float sheetF = material.specularColor.g + ( 1.0 - material.specularColor.g ) * pow( 1.0 - sheetNV, 5.0 );
      float sheetA = 1.0 - exp( - sheetAbsorb / sheetNV );
      gl_FragColor = vec4( totalSpecular + totalDiffuse * sheetHaze, clamp( sheetF + ( 1.0 - sheetF ) * ( sheetA + sheetHaze ), 0.0, 1.0 ) );`);
  };
  m.customProgramCacheKey = () => "gp-clear-sheet";
  return m;
}

// ---- shared finishes (one material per finish + colour, so static parts merge into few draw calls) ----
const finishCache = new Map();
function finish(kind, colour) {
  const c = colour instanceof THREE.Color ? colour : new THREE.Color(colour);
  const key = kind + ":" + c.getHexString();
  let m = finishCache.get(key);
  if (m) return m;
  switch (kind) {
    case "metal": m = std({ color: c, metalness: 0.9, roughness: 0.46, envMapIntensity: 1.6 }); break;   // bead-blasted / brushed aluminium
    case "anodized": m = std({ color: c, metalness: 0.7, roughness: 0.45, envMapIntensity: 1.3 }); break; // black anodized or powder coat
    case "fastener": m = std({ color: c, metalness: 1, roughness: 0.32, envMapIntensity: 1.4 }); m.userData.tiny = true; break;   // zinc / stainless hardware
    case "plastic": m = phys({ color: c, metalness: 0, roughness: 0.42, specularIntensity: 0.6 }); break;   // moulded plastic
    case "nylon": m = phys({ color: c, metalness: 0, roughness: 0.55, sheen: 0.2, specularIntensity: 0.5 }); break;
    case "tape": m = std({ color: c, roughness: 0.82 }); break;                                // gaffer tape: matte cloth
    case "acm": m = phys({ color: c, roughness: 0.32, clearcoat: 0.4, clearcoatRoughness: 0.25 }); break;   // painted aluminium composite
    case "vinyl": m = std({ color: c, roughness: 0.45 }); break;
    case "rubber": m = std({ color: c, roughness: 0.85 }); break;
    default: m = std({ color: c, roughness: 0.5 });
  }
  finishCache.set(key, m);
  return m;
}
const M = {
  sheet: clearSheet(),                                                     // perimeter and robot polycarbonate
  hiveSheet: clearSheet({ tint: 0xf3f7f7, haze: 0.02, absorb: 0.006 }),    // HIVE skins: thinner, cleaner
  steel: finish("fastener", 0x9da3a8), alu: finish("metal", 0xd0d4d8), aluDark: finish("anodized", 0x1d1f22),
  rubber: finish("rubber", 0x161616), blackPlastic: finish("plastic", 0x1b1c1e),
};
M.floor = std({ color: 0x1b1d20, roughness: 0.95, bumpMap: noiseTex(256, 128, 40, 3), bumpScale: 0.4 });
M.floor.bumpMap.repeat.set(30, 30);

// CAD part names arrive as a product path ("Shapes/am-5850 BIOBUZZ/.../am-5876: A-Frame Leg"); the
// loader keeps the original in userData.name (three.js sanitizes object names).
const partName = (path) => String(path || "").split("/").pop();
const cadColour = (path) => {
  const c = CADCOL[partName(path)];
  return c && c.length ? new THREE.Color().setRGB(c[0], c[1], c[2], THREE.SRGBColorSpace) : null;
};
// The STEP uses pure primaries for alliance parts; real moulded red/blue plastic and gaffer tape are deeper
function realColour(c, kind) {
  if (!c) return null;
  const hex = c.getHexString();
  if (hex === "ff0000") return new THREE.Color(kind === "tape" ? 0xb5201a : 0xc8261e);
  if (hex === "0000ff") return new THREE.Color(kind === "tape" ? 0x1a4cb8 : 0x1f56c4);
  return c;
}
function materialFor(path, group) {
  const p = partName(path).toLowerCase();
  const cad = cadColour(path), lum = cad ? cad.r * 0.2126 + cad.g * 0.7152 + cad.b * 0.0722 : 0.6;
  const dark = lum < 0.1;
  if (/cable tie/.test(p)) return finish("nylon", realColour(cad, "plastic") || new THREE.Color(0x222222));
  if (/skin/.test(p)) return M.hiveSheet;
  if (/glass|polycarb/.test(p)) return M.sheet;
  if (group === "tape" || /gaffer|tape/.test(p)) return finish("tape", realColour(cad, "tape") || new THREE.Color(0x333333));
  if (/acm panel/.test(p)) return finish("acm", cad || new THREE.Color(0xf0f0ee));
  if (/sticker/.test(p)) return finish("vinyl", cad || new THREE.Color(0xf4f4f2));
  if (/nylon/.test(p)) return finish("nylon", cad || new THREE.Color(0xe6e0cf));
  if (/rail with rivet holes/.test(p)) return finish("anodized", cad || new THREE.Color(0x303030));
  if (/screw|nut\b|washer|rivet|pin\b|bearing|rivnut|hinge|wing nut|^4in$|^4-20/.test(p)) return finish("fastener", cad || new THREE.Color(0x9da3a8));
  if (/rib|flower layer|backstop|peanut|hips|damper\b|plug|top corner|frame foot|axle holder|damper holder|tray/.test(p))
    return finish("plastic", realColour(cad, "plastic") || new THREE.Color(0x888888));
  if (/strap|leg|bar\b|tube|churro|bracket|link|spacer|foot bar|clip/.test(p)) return finish(dark ? "anodized" : "metal", cad || new THREE.Color(0xd0d4d8));
  if (cad) return finish(dark ? "plastic" : "metal", cad);
  return M.alu;
}
