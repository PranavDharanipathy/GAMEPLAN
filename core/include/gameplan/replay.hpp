// Viewer replays: a recorded match turned into the frames a renderer draws, for scrubbing and
// free-camera playback (the browser app opens these files; any other host can too).
//
// File format gameplan.replay/1 (the whole file is gzip-compressed):
//   bytes 0-9    "GPREPLAY1\n"
//   bytes 10-13  header length N (uint32, little-endian)
//   N bytes      header JSON (padded with spaces so the frames start on a 4-byte boundary)
//   rest         frames: float32 little-endian, frameLength floats each, in view-frame layout
//                (core/include/gameplan/view.hpp), sampled every 1/hz s of world time (hz 5, 10 or 20)
// Header: schema, gameplanVersion, season {id, packageVersion, rulesEngine, manual}, seed, mode,
// matchStart (world time AUTO began), timing {auto_s, transition_s, teleop_s}, hz, frameLength, frames,
// robots [view info + controller], events [{t, kind, robot, ...}], violations, result, reproduction, source.
#pragma once
#include <string>

namespace gp {

struct ReplayInfo {
    size_t frames = 0;
    double seconds = 0;
    std::string reproduction;   // "EXACT" when the re-execution reproduced the recorded final state
};

// Re-executes the match recorded in recordingDir (same seed and recorded inputs; team code runs again,
// exactly as for `gp-sim verify`) and writes a viewer replay to outPath.
bool exportReplay(const std::string& recordingDir, const std::string& outPath, double hz, ReplayInfo& info, std::string& err);

}  // namespace gp
