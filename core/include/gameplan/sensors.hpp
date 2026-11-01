// Robot-observable sensor models. Ground truth lives in World/Robot; this layer turns it into
// what the robot program can see: quantisation, noise, bias, drift, latency, sample rates,
// stale data and occasional corrupt readings (outliers / garbage). Ordinary corruption NEVER
// produces a device failure; disconnects and resets are a separate fault system (World).
#pragma once
#include <deque>
#include <string>
#include <vector>

#include "gameplan/math.hpp"
#include "gameplan/rng.hpp"
#include "gameplan/uncertainty.hpp"

namespace gp {

class World;
class Robot;

struct SensorValue {
    uint16_t device;
    uint8_t field;
    double value;
};

struct AprilTagDetection {
    int id = 0;
    double frameTime = 0;       // exposure mid-time (sim seconds)
    double availableTime = 0;   // when the processed result becomes visible to the robot program
    // ftcPose convention (SDK AprilTagPoseFtc): x right, y forward, z up (metres), yaw/pitch/roll (rad),
    // range/bearing/elevation derived.
    double x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0, range = 0, bearing = 0, elevation = 0;
    double centerPx[2] = {0, 0};
    double corners[8] = {0};
    double decisionMargin = 0;
    bool falsePositive = false;
    double truthRange = 0;      // ground truth for engineering comparison (never sent to the runtime)
    // Cluster result (id <= -100: cell index = -100 - id, hive*2 + cell): pose of the cluster frame
    // origin solved from all detected members of that cell in this frame (SDK 12 cluster solve).
    std::vector<int> clusterIds;
};

struct CorruptionModel {
    double outlierProb = 0.0;   // per read
    double outlierScale = 0.0;  // relative magnitude of outliers
    double garbageProb = 0.0;   // per read: nonsensical value (sentinel / NaN-free nonsense)
    double staleProb = 0.0;     // per read: returns previous sample
};

class SensorSuite {
public:
    SensorSuite(Robot& r, UncertaintySampler& us, uint64_t seed, const std::string& prefix);
    void step(const World& w, double dt);
    // Values for the FTC runtime at time t (device index base supplied by the link layout).
    void fill(const World& w, int robotIndex, double t, uint16_t motorBase, uint16_t sensorBase, uint16_t hubBase,
              std::vector<SensorValue>& out);
    std::vector<AprilTagDetection> pollVision(const World& w, int robotIndex, int sensorIdx, double t);
    void captureFrames(const World& w, int robotIndex);   // called every step by World
    bool visionActive = false;                              // set when a consumer (runtime/engineering view) is attached
    // engineering access (truth vs observed) for recording
    double imuYawObserved() const { return imuYaw_; }
    double pinpointError() const;
    Vec3 pinpointPose() const { return {ppX_, ppY_, ppH_}; }
    CorruptionModel corruption;

private:
    double corrupt(double v, double prev, double scale, Rng& r);
    std::vector<AprilTagDetection> renderVisionFrame(const World& w, int robotIndex, int sensorIdx, double frameT);
    void renderFalsePositive(std::vector<AprilTagDetection>& out, double frameT, double latencyS);
    Robot& robot_;
    Rng rng_, rngVision_;
    // IMU
    double imuYaw_ = 0, imuBias_ = 0, imuScale_ = 1, gyroNoise_ = 0.0012, biasWalk_ = 2e-5;
    double imuLastHeading_ = 0, imuSampleT_ = -1, imuYawSampled_ = 0, imuRateSampled_ = 0;
    // Pinpoint odometry computer
    double ppX_ = 0, ppY_ = 0, ppH_ = 0, ppScaleX_ = 1, ppScaleY_ = 1, ppSlipSigma_ = 0.002;
    Vec2 ppLastPos_;
    double ppLastHeading_ = 0, ppEncX_ = 0, ppEncY_ = 0;
    bool ppInit_ = false;
    // voltage
    double voltNoise_ = 0.02;
    // vision
    struct CamState { double lastFrame = -1; std::deque<AprilTagDetection> pending; std::vector<AprilTagDetection> latest; double latencyS = 0.045; double calibFx = 1.0; };
    std::vector<CamState> cams_;
    std::vector<double> lastValues_;
    double encoderGlitchProb_ = 0;
    ULevel sensorLevel_ = ULevel::Nominal, visionLevel_ = ULevel::Nominal;
};

}  // namespace gp
