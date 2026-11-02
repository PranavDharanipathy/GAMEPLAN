// View frames: everything a viewer needs to draw one instant of a match - robot poses and mechanism
// angles, ball poses, spin and squish, CELL tag centres, HIVE angles and perimeter-wall flex - plus the
// static robot geometry to draw them with. One definition shared by the browser build (apps/web),
// replay export (gp-sim replay-export) and any other renderer, so the layout cannot drift between them.
//
// Frame layout (floats):
//   [0] world time  [1] phase (int Phase)  [2] score red  [3] score blue  [4] nRobots  [5] nElements  [6] nCells
//   robots, kViewRobot each: x y heading alliance held tipped length width height intakeAngle flyAngle
//         hoodAngle wheelAngle[4] (lf rf lb rb) pitch shooterStatus roll
//   elements, kViewElement each: x y z kind(0 pollen, 1 red nectar, 2 blue nectar) radius visible
//         qw qx qy qz squish nx ny nz
//   cells, kViewCell each: x y z up(1/0) alliance
//   hiveAngle[2] (rad about +x through the pivot, 0 = audience CELL up)
//   walls[4] (-x, +x, -y, +y): flex (m), flex speed (m/s)
// Replay files (gameplan.replay/1, written by exportReplay) store a run of these frames.
#pragma once
#include <string>
#include <vector>

#include "gameplan/json.hpp"
#include "gameplan/world.hpp"

namespace gp {

constexpr int kViewHeader = 7, kViewRobot = 19, kViewElement = 14, kViewCell = 5, kViewTail = 2 + 8;

// Appends one view frame of the world's current state. robotStatus[i] is the shooter-status LED of
// robot i (0 off, 1 spinning up / not aimed, 2 ready); missing entries are 0.
void appendViewFrame(const World& w, const std::vector<int>& robotStatus, std::vector<float>& out);
// Number of floats appendViewFrame writes for this world.
size_t viewFrameLength(const World& w);
// Static geometry of robot i for rendering: chassis, wheels (with mecanum roller handedness), intake
// roller and mouth, internal ball path, shooter and hood range. Body frame metres (x forward, y left, z up).
Json robotViewInfo(const World& w, int i);

}  // namespace gp
