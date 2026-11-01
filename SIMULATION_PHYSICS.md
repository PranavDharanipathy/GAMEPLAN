# SIMULATION_PHYSICS.md

## Chassis
Planar rigid body (x, y, yaw) integrated semi-implicitly at `dt`, plus pitch/roll tip dynamics about
the support edge when inertial/contact moments exceed gravity's restoring moment (a robot whose CoM
passes the edge is `tipped`). Wheel normal loads: static distribution from CoG offset, longitudinal/
lateral load transfer (`m·a·h/L`), and a diagonal imbalance term representing frame/tile flex.
**Fidelity boundary:** vertical dynamics are reduced-order (no suspension, no 6-DOF airborne motion).

## Mecanum contact
Each wheel transmits force only perpendicular to its roller axis (`n`); slip `s = v_c·n − ωR(d·n)`;
force `−μN tanh(s/s0)`; roller rolling resistance along the roller axis. The reaction torque goes to
the wheel joint and its derivative is used for semi-implicit integration (stable at 1 ms).

## Drivetrain joints and imperfections
`GearedJoint`: motor rotor and load are separate bodies coupled through a gear ratio, efficiency
(motoring vs back-driven) and a **backlash gap** modelled as a unilateral contact (momentum-conserving
re-engagement). Encoders read the motor side, so backlash is visible to robot code. Sampled per robot:
gear efficiency, backlash, wheel μ, wheel misalignment, CoG offset, diagonal load imbalance, slip
scale. Disabled level removes them.

## Contacts
Perimeter walls, HIVE A-frame legs (capsules) and FLOWER bodies are spring–damper contacts
(k = 4·10⁴ N/m, c = 600 N·s/m — barrier compliance emerges from k), robot–robot OBB contacts with
friction. Collisions and ESD are separate models (a contact can trigger a discharge only if charge
accumulated).

## Scoring elements
Spheres with gravity, quadratic drag, floor restitution/rolling resistance, ball–ball collisions,
leg/flower bounces, perimeter (over-the-wall → out of field; POLLEN re-entered by field staff, NECTAR
returned to its drive team). FLOWER: entry only through the top opening, stacked, POLLEN retrievable
from the bottom (NECTAR does not fit the retrieval opening). CELL: capture through the up-facing
opening while moving downward.

## HIVE tip
Bistable: tip weight sum (POLLEN 1.0, NECTAR 1.667, plus impact bonus per m/s of entry speed) vs a
per-match sampled threshold (7.5 ± 0.25). Weights solved from the Event Field Setup Guide 12.3
calibration table (8 P tips, 3 N + 3 P tips, 7 P and 3 N + 2 P do not). Tip takes 1.2 s; contents
spill at 35 %; the cell/tag poses rotate ±60° about the pivot (CAD-verified to <1 mm).

## Fidelity modes
Realtime: dt = 1 ms. Engineering: dt = 0.25 ms. Both use identical models and rules; switching mode
changes only numerical resolution and is recorded in the match header.

## Field provenance
Geometry from the official STEP CAD (sha256 recorded) and TU07 manual. Not officially specified and
therefore **estimates with uncertainty**: tile μ (0.95, lognormal 7 %), wall stiffness, element
masses, restitution, CELL opening height, AprilTag layout within a cluster.
