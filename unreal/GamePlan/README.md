# GamePlan Unreal host — visuals

The simulation (physics, electrical, rules, AI, robot code) is the C++ core; Unreal only renders it
and reads the driver's gamepads. This page covers making that rendering photoreal.
**Status: written against UE 5.4+ APIs but not yet compiled or run** (no Unreal toolchain in the
environment this was built in). See `IMPLEMENTATION_LEDGER.md`.

## 1. Field and elements from the official CAD (full resolution)

```bash
pip install gmsh
python tools/cad_to_glb.py <official field>.step out/cad
```

This tessellates every solid of the official STEP (exact B-rep, OpenCASCADE) into one GLB per
assembly (`pollen`, `nectar_red`, `nectar_blue`, `hive_red`, `hive_blue`, `frame`, `flowers`, `tiles`,
`perimeter`, `tape`, `trays`) in metres, GamePlan field frame. `out/cad/cad_assets.json` records the
STEP sha256 and the measured element geometry (hollow shells with 26 holes).

Import each GLB with **Interchange** (drag into `Content/GamePlan/Field/`):
- *Convert scene* off for the elements; on for field groups (one static mesh per CAD solid keeps part
  names for material assignment).
- **Nanite on** for every field mesh. Nanite renders the undecimated CAD (≈25 M triangles total) in
  real time; don't use the browser's decimated `assets/field/*.glb` here.
- Scale 100 (metres → centimetres). The field frame maps to Unreal with y mirrored
  (`GamePlanCoords.h`); import with *Convert to left-handed* or set scale Y = −100.
- Part colours are in the STEP (`assets/field/cad_colors.json` lists them per part name).

HIVES: place `hive_red` / `hive_blue` under a scene component whose origin is the HIVE axle
(`season.json` → `field.hives[i].pivot`) and add those components to the match actor's
`HiveBodies`. The CAD pose has the red AUDIENCE and blue SCORING cells up; the actor rotates each body
by the simulated tip angle relative to that pose.

## 2. Ball material (`M_GamePlanBall`)

Each ball instance carries 4 custom floats: squish direction (x, y, z, Unreal frame) and
compression / radius. Use them in World Position Offset so the hollow shell flattens against
whatever it touches and bulges sideways (volume ≈ conserved):

```hlsl
// Custom node, inputs: LocalPos (Local Position relative to the instance pivot), N (float3 from
// PerInstanceCustomData 0..2), S (PerInstanceCustomData 3). Output: float3 world offset.
float d = dot(LocalPos, N);
float3 axial = -N * d * S;                 // squash along the contact normal
float3 lateral = (LocalPos - N * d) * (0.5 * S);
return axial + lateral;
```

Surface: polyethylene — base colour from the CAD (POLLEN yellow, NECTAR red/blue), roughness ≈ 0.4,
specular 0.5, two-sided (the inner wall is visible through the holes), subsurface off.

## 3. Lighting and rendering

`Config/DefaultEngine.ini` enables Lumen GI/reflections, virtual shadow maps, Nanite, TSR, hardware
ray tracing and the path tracer. For a venue look: one rectangular area light per truss bank above
the field (5–6 m, 4000–5000 K), a dim HDRI of an indoor hall for reflections, auto-exposure on.
Use the path tracer (`r.PathTracing`) for reference stills or replays; live play uses Lumen.
