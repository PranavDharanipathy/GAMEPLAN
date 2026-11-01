# ELECTRICAL_MODEL.md

## Topology (per robot, from the robot description)
Battery (OCV(SoC,T) for 10 NiMH cells, R0(T, age, SoC), one RC polarisation branch, thermal mass,
20 A fuse I²t) → main path (switch + fuse + leads + XT30, thermal resistor) → hub feeds (each its own
wire/connector resistance and temperature) → hub 12 V buses (Control Hub parent of Expansion Hub, as
wired) → H-bridges → motor leads → motors; hub 5 V servo regulators with per-pair (ports 0-1, 2-3,
4-5) and total current limits and voltage fold-back; external servo power modules supported.

## Solve (every physics step)
Motor branch current `I = (d·V_bus − k_e·ω)/R(T)`, supply current `d·I`. Each hub contributes
`I = G·V − H` (motors linear in V, logic/servo loads as sinks); subtrees reduce analytically up to the
battery node; terminal voltage `V = OCV − V_RC − R0·I`. H-bridge current limit and servo rail fold-back
are handled by fixed-point re-solves. Torque `k_t·I − friction(ω)` drives the joints in the same step.
This is the causal chain: load → current → sag → available voltage → torque → motion. Nothing slows a
motor except physics (no ad-hoc penalties) — tested by `intake_load_reduces_drive_voltage_and_acceleration`.

## Thermal / time constants
Wires, connectors, main path: resistance follows each element's own temperature (I²R heating with a
thermal capacity) — never jumps with current (`wire_resistance_changes_with_thermal_time_constant`).
Motors: two-node (winding/case) thermal model, copper α = 0.00393/K. Battery: temperature-dependent R0.

## Motors and servos
Motor constants derived from datasheet free/stall points (goBILDA 5203 and REV HD Hex verified from
manufacturer pages on 2026-11-01). Per-unit variation (R, k_t, friction) sampled. Servos: torque/speed
scale with supply voltage, internal P controller, current ∝ delivered torque (holding current rises
with load), stall limited; different servo models carry different specs.

## Protection and faults
Brownout per hub (threshold 7.0 V, recovery 7.5 V — recalled values, flagged medium confidence):
Control Hub brownout ⇒ Robot Controller restarts (robot dead for the match); Expansion Hub reset ⇒
outputs off, then SDK re-init (modes reset, encoders zeroed). Fuse blow ⇒ power loss.
ESD: frame charge from rolling/slip (humidity-dependent leakage, anti-static treatment), discharge on
contact with the HIVE frame or a robot at a different potential; discharge energy × per-robot
susceptibility ⇒ fault draw: Expansion Hub comm loss, hub reset, DS disconnect (OpMode stopped; TELEOP
restarted after reconnection), or RC crash. Ordinary sensor corruption never causes device failure.

## Uncertain values (must be verified)
REV servo pair/rail limits, H-bridge limit, brownout thresholds, reboot times, NiMH OCV curve, R0,
contact resistances, all thermal constants. See `data/hardware/components.json` provenance.

## Post-match
Report: battery/OCV/hub voltages, battery and motor currents, servo current estimates, winding
temperatures, per-device energy/peaks/current-limit time, and a numerical attribution of drivetrain
slowdowns (SoC decline, polarisation, I·R0 split by who drew the current, wiring drop, heating).
`gp-sim stress` runs automated stress scenarios across battery and wiring conditions.
