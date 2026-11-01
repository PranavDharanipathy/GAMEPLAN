# ROADMAP.md (dependency order)

1. **Unreal validation**: build the plugin on Win/Linux/Mac, Drive screen with live gamepads, field from
   official CAD via Datasmith (STEP), robot visuals from CAD, replay scrubbing UI, post-match widget.
2. **OS sandboxes** on all platforms (Landlock/bubblewrap, sandbox-exec, AppContainer); Windows runtime path validation.
3. **Vision**: `robotPose` from camera pose, `ftcPose` pitch/roll, Unreal-rendered camera feeds for inspection. (Cluster detections: done.)
4. **CAD digital twin**: tessellate STEP (OpenCascade, LGPL-compatible dynamic link) → mass properties per part, wheel/motor recognition, annotation UI.
5. **Rules completeness**: expansion volume (G416) from mechanism extents, G418/G419/G420 detectors, card carry-over.
6. **Calibration**: fit friction/motor/battery/timing parameters from HardwareMonitor sessions and video tracks; versioned calibration sets.
7. **Video ingestion**: field homography, robot tracking with uncertainty.
8. **FTCScout team imitation**: GraphQL client, OPR/cycle statistics → AI personality with provenance.
9. **Multiplayer** (LAN/online; host-authoritative simulation, clients send inputs only), spectator cameras.
10. **Workspaces & versioning UI**, robot variants, backup/restore, robot library with consent records.
11. **Installer/updater** per platform with bundled JDK and verified dependency manifests.
