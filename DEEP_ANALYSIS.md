# DEEP_ANALYSIS.md — investigations, decisions, trade-offs, open problems

## D1. Environment reality (2026-11-01)
The development container has no Unreal Engine (it requires an Epic account and ~100 GB) and no GPU.
Decision: build the engine-independent simulation core and FTC runtime first (fully testable here),
and write the Unreal host against the core's public API, labelled **unbuilt**. The Unreal host is a
renderer/input layer by design, so this ordering does not compromise the architecture.

## D2. Custom deterministic solver instead of Chaos for the authoritative simulation
Chaos is not bit-deterministic across runs/platforms and is not available headless outside Unreal.
Exact replay, counterfactual branching with common random numbers, and identical headless/batch
physics require an engine-independent deterministic solver. Chaos may be used for purely cosmetic
effects only (never feeding back into World).

## D3. Runtime architecture (see ROBOT_CODE_COMPATIBILITY.md)
Hybrid real-SDK jars + shadows in a separate sandboxed JVM, lockstep over pipes. PoC result: real
SDK 12.0 classes load on a desktop JVM except Android-bound ones. Compiling the shadows against the
real jars catches every signature mismatch at build time.

## D4. Virtual time and loop cost
Robot code must not run "desktop fast". Every SDK call charges a modelled Control Hub cost; the host
never waits in wall-clock time. Result: a loop doing 4×(position, velocity, current, power) +
voltage without bulk caching runs 25.7 ms/loop vs 6.9 ms for a bulk-cached loop — this changes robot
behaviour in simulation exactly as on hardware. Absolute µs values are estimates (medium-low
confidence) until calibrated against HardwareMonitor sessions.
Deterministic compute model: user code costs a fixed 300 µs per loop (configurable) — bit-reproducible.
Measured model (host CPU × 3.2) exists but is not reproducible.

## D5. Counterfactual exactness
Java runtime state (threads, heap) cannot be checkpointed safely. Counterfactuals therefore
re-execute from match start with identical seed, recorded inputs injected at exact exchange times,
and verify the re-execution against the recorded per-second state hashes. Branches are labelled
EXACT up to the branch point only when every hash matched. Native-only (AI) matches additionally
support `World::snapshot/restore`.
Bugs found and fixed while validating this: floating time drift (`t += dt`) livelocked lockstep;
match-end detection depended on chunk size; replay inputs used match time instead of world time and
arrived one exchange late; the hash file was compared before being flushed.

## D6. BIOBUZZ field data provenance
Manual TU07 gives areas, dimensions, scoring, timing. The official STEP CAD (sha256 05b35961…) gives
positions: flowers at (±0.5942, ±1.7282)/(±1.7282, ±0.5942), loading zones on tiles A5/F2, gardens on
A1/F6 (which fixed the tile row convention: row 1 = audience), hive cells, NECTAR/POLLEN staging.
Hive states are 60° apart (rotating the up-cell tag pose reproduces the CAD down pose to <1 mm).
Tip threshold derived from the Event Field Setup Guide calibration table.

## D7. Open problems
- STEP B-rep tessellation needs a CAD kernel (OpenCascade) — licence and packaging analysis pending.
- REV electrical limits (servo pair/rail, H-bridge, brownout, reboot times) are recalled values; the
  REV spec page URLs returned 404 on 2026-11-01. Must be re-verified.
- Pedro `tuning.autotune` package is not present in the resolved `com.pedropathing:*` artifacts for the
  team project (likely requires an artifact/version not declared or published differently).
- Windows process/sandbox path and macOS sandbox unvalidated.
- Cross-platform bitwise determinism is not guaranteed (libm); detected via hash verification.
- AI perceives other robots' and elements' true positions (like a human watching the field) but no hidden
  parameters; a perception-noise model for opponents is future work.
