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
masses, restitution, CELL opening height. The AprilTag layout within a cluster is **official**: the
SDK 12.0.0 BIOBUZZ library (`AprilTagGameDatabase.getBioBuzzTagLibrary()`) gives four tags in a row
at x = −6.5, −2.75, 2.75, 6.5 in, y = 7.19 in, z = −5.62 in in the cluster frame (OpenCV object frame:
x right, y down facing the tags, z into the cell). The row centre comes from the CAD sticker placement;
the frame origin follows from those offsets and lands inside the CELL (≈0.5 in from the CAD-derived
cell centre), which is the remaining *derived* part.

## Virtual AprilTag clusters
Each camera frame, the host groups the tags it detected by HIVE cell and solves one pose for that
cell's cluster frame: range/bearing noise shrinks with √(members seen) and with the corner spread
(baseline) of the visible members; yaw noise shrinks with the spread; a hallucinated member corrupts
the solve (×3 noise) and a hallucination alone gives a garbage cluster pose. The runtime then applies
the team's library exactly like `AprilTagProcessorImpl`: tags in a library cluster are consumed by
that cluster (`percentClusterFound = round(seen/members·100)`), all others become single detections,
with metadata and `ftcPose` only when the library knows the tag. `pitch`/`roll` are 0 and `rawPose`
/`robotPose` are `null`.

## Scoring elements (balls.cpp)
Geometry is official CAD: POLLEN Ø71.1 mm and NECTAR Ø92.0 mm are hollow polyethylene shells (1.78 /
2.16 mm wall) with 26 Ø11 mm holes; mass = CAD material volume × 0.95 g/cm³ (material assumed);
thin-shell inertia I = 0.635 m R². Each ball is a rigid body (position, velocity, orientation,
spin). Constants with provenance are in `scoringElements.*.physics`.

| Effect | Model |
|---|---|
| Contacts (floor, walls, HIVE legs, FLOWERS, robot chassis, intake roller, ball-ball) | Hertz spring F = k·d^1.5, damping from restitution, Coulomb friction at the contact point (couples slide ↔ spin); 4 substeps per 1 ms step |
| Squish | contact compression d (a few mm on impacts, ~0.2 mm resting), exported per ball for rendering |
| Flight | gravity, drag (Cd 0.6, perforated ball), Magnus lift C_L = min(0.35, 0.8·rω/v), spin decay |
| Intake | compliant roller (friction 0.9) drags the ball through the mouth and spins it; no room → the ball stays at the roller |
| Inside the robot | 1-D bodies on the robot's ball path (mouth → shooter): belts are friction-limited (3 g) velocity sources, gravity along the path, robot acceleration (balls slosh on hard stops/hits), Hertz contact between neighbours, end stop at the gate; reversing spits balls out |
| Launch | exit speed = efficiency × wheel surface speed × grip(compression of *this* ball) × momentum exchange with the wheel; backspin ≈ v/r (rolling on the hood); flywheel loses the ball's translational + rotational energy |

Estimates (contact stiffness, friction, Cd, C_L, belt grip) are flagged in the season file; replace
them with drop / squeeze / spin measurements of real elements.

