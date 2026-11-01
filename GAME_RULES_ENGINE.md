# GAME_RULES_ENGINE.md — BIOBUZZ (`biobuzz-rules/1.0.0`, manual TU07 2026-10-29)

## Match flow
PRE_MATCH (AUTO OpMode INIT at t=0, match starts at t=3 s) → AUTO 30 s → TRANSITION 8 s (TELEOP OpMode
INIT 1 s in) → TELEOP 120 s → POST (assessment after elements and robots come to rest, ≤ 6 s) → FINAL.
The FIELD timer (2:30→2:00, 0:08→0:01, 2:00→0:00) is authoritative; audio cues (Table 9-1) are logged
at their timer values and are informational only.
Competition mode: no driver input during AUTO; gamepads delivered during TRANSITION but any powered
motion is a G403 call. Practice mode: driver control allowed in AUTO; results labelled practice.

## Scoring (Table 10-2), as assessed
| Achievement | When | Points |
|---|---|---|
| LEAVE (not contacting perimeter) | end of AUTO | 3 |
| PARK (partially in own LOADING ZONE) | end of AUTO / end of match | 5 / 5 |
| HIVE TIP | when complete; before TELEOP start ⇒ AUTO | 20 |
| Elements in up-facing CELL | final | 2 each |
| Bottom NECTAR bonus (per FLOWER) | final | 5 |
| Elements in an owned FLOWER (owner = top-most NECTAR in scoring volume) | final | 2 each |
| Elements partially in GARDEN (alliance of the GARDEN) | final | 1 each |
| RP: SWARM ≥16 LEAVE+PARK, POLLINATOR 1 ≥4 TIPS, POLLINATOR 2 ≥7 TIPS, WIN 3, TIE 1 | final | — |
MINOR foul credits 5, MAJOR 20 to the opponent. Each TIP unlocks one NECTAR entry for that alliance's
human player; all NECTAR unlocks with 60 s remaining (G426).

## Violations implemented
| Rule | Detector | Kind |
|---|---|---|
| G402 | AUTO contact with an opponent robot while on the opponent's side | objective-assisted, MAJOR/match |
| G403 | powered output in TRANSITION (0.25 s grace for the DS stop) | judgment (STRATEGIC escalates) |
| G404 | powered output after TELEOP end | judgment |
| G405 | element leaves the FIELD ≤3 s after robot contact/launch; penalised only if perceived deliberate | judgment |
| G407 | robot holds > 4 elements | judgment |
| G408 | robot holds opponent NECTAR | judgment |
| G409 | robot catches/deflects an element released by a TIP before it touched anything else | judgment |
| G410 | NECTAR enters FLOWER scoring volume with > 60 s left | objective, MAJOR per NECTAR |
| G417 | HIVE frame contact above the ramming speed | judgment, MAJOR+YELLOW once per match if STRATEGIC |
| G421 | PIN > 3 s (+ MAJOR per further 3 s), G421.A/B pause rules approximated | objective-assisted |
| G426 | human NECTAR entry before unlock | objective, MINOR per NECTAR |

## Referee judgment model
Explicit rules are deterministic. "STRATEGIC"/"deliberate" are never inferred from one contact:
`RefereeModel` escalates when occurrences reach the REPEATED threshold (default 3) or when a controller
declares intent; an optional stochastic profile (seeded, logged) models referee variance and misses.
Every violation stores rule id, penalty, points and a rationale string with the inputs and decision.

## Not yet executable (documented gaps)
G401 (human interaction during AUTO — not applicable to simulated drivers), G406, G411 (hoarding),
G412–G416 (expansion volume needs mechanism extents), G418 (FLOWER meddling beyond the physical
constraints already modelled), G419/G420 (damage/entanglement), G422–G425, G427/G428 (human-player
handling beyond the human model). Cards are recorded but tournament-phase carry-over is not modelled.
