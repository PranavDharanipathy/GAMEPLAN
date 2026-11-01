#include "gameplan/link.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "gameplan/world.hpp"

namespace gp {

GamepadState GamepadState::fromVec(const std::vector<double>& v, size_t o) {
    GamepadState g;
    if (v.size() < o + 7) return g;
    g.lx = float(v[o]); g.ly = float(v[o + 1]); g.rx = float(v[o + 2]); g.ry = float(v[o + 3]);
    g.lt = float(v[o + 4]); g.rt = float(v[o + 5]); g.buttons = uint32_t(v[o + 6]);
    return g;
}

// ------------------------------------------------------------------ ChildProcess
#ifdef _WIN32
ChildProcess::~ChildProcess() { kill(); }
bool ChildProcess::spawn(const std::vector<std::string>& argv, const std::string& workdir, std::string& err) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inR, inW, outR, outW, errR, errW;
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0)) { err = "CreatePipe failed"; return false; }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);
    std::string cmd;
    for (auto& a : argv) { cmd += "\"" + a + "\" "; }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = errW;
    PROCESS_INFORMATION pi{};
    // Job object: the runtime and anything it spawns die with the simulator.
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    li.BasicLimitInformation.ActiveProcessLimit = 1;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                        workdir.empty() ? nullptr : workdir.c_str(), &si, &pi)) { err = "CreateProcess failed"; return false; }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(inR); CloseHandle(outW); CloseHandle(errW);
    proc_ = pi.hProcess; inW_ = inW; outR_ = outR; errR_ = errR;
    return true;
}
bool ChildProcess::writeAll(const void* d, size_t n) {
    const char* p = (const char*)d;
    while (n) { DWORD w = 0; if (!WriteFile((HANDLE)inW_, p, DWORD(n), &w, nullptr)) return false; p += w; n -= w; }
    return true;
}
bool ChildProcess::readAll(void* d, size_t n, double timeoutS) {
    char* p = (char*)d;
    DWORD start = GetTickCount();
    while (n) {
        DWORD avail = 0;
        if (!PeekNamedPipe((HANDLE)outR_, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (!avail) { if ((GetTickCount() - start) / 1000.0 > timeoutS) return false; Sleep(0); continue; }
        DWORD r = 0;
        if (!ReadFile((HANDLE)outR_, p, DWORD(std::min<size_t>(n, avail)), &r, nullptr) || r == 0) return false;
        p += r; n -= r;
    }
    return true;
}
bool ChildProcess::alive() { DWORD c; return proc_ && GetExitCodeProcess((HANDLE)proc_, &c) && c == STILL_ACTIVE; }
void ChildProcess::kill() { if (proc_) { TerminateProcess((HANDLE)proc_, 9); CloseHandle((HANDLE)proc_); proc_ = nullptr; } }
std::string ChildProcess::drainStderr() {
    std::string s;
    DWORD avail = 0;
    while (errR_ && PeekNamedPipe((HANDLE)errR_, nullptr, 0, nullptr, &avail, nullptr) && avail) {
        char b[4096]; DWORD r = 0;
        if (!ReadFile((HANDLE)errR_, b, std::min<DWORD>(avail, sizeof b), &r, nullptr) || !r) break;
        s.append(b, r);
    }
    return s;
}
#else
ChildProcess::~ChildProcess() { kill(); }
bool ChildProcess::spawn(const std::vector<std::string>& argv, const std::string& workdir, std::string& err) {
    int in[2], out[2], er[2];
    if (pipe(in) || pipe(out) || pipe(er)) { err = "pipe() failed"; return false; }
    pid_t pid = fork();
    if (pid < 0) { err = "fork() failed"; return false; }
    if (pid == 0) {
        dup2(in[0], 0); dup2(out[1], 1); dup2(er[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]); close(er[0]); close(er[1]);
        if (!workdir.empty() && chdir(workdir.c_str()) != 0) _exit(126);
        setpgid(0, 0);  // own process group: stop kills every thread/child of the runtime
        std::vector<char*> a;
        for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    close(in[0]); close(out[1]); close(er[1]);
    pid_ = pid; inFd_ = in[1]; outFd_ = out[0]; errFd_ = er[0];
    fcntl(errFd_, F_SETFL, fcntl(errFd_, F_GETFL) | O_NONBLOCK);
    signal(SIGPIPE, SIG_IGN);
    return true;
}
bool ChildProcess::writeAll(const void* d, size_t n) {
    const char* p = (const char*)d;
    while (n) {
        ssize_t w = write(inFd_, p, n);
        if (w <= 0) return false;
        p += w; n -= size_t(w);
    }
    return true;
}
bool ChildProcess::readAll(void* d, size_t n, double timeoutS) {
    char* p = (char*)d;
    while (n) {
        struct pollfd pf{outFd_, POLLIN, 0};
        int r = poll(&pf, 1, int(timeoutS * 1000));
        if (r <= 0) return false;
        ssize_t got = read(outFd_, p, n);
        if (got <= 0) return false;
        p += got; n -= size_t(got);
    }
    return true;
}
bool ChildProcess::alive() {
    if (pid_ <= 0) return false;
    int st;
    pid_t r = waitpid(pid_, &st, WNOHANG);
    if (r == pid_) { exitCode_ = WIFEXITED(st) ? WEXITSTATUS(st) : -1; pid_ = -1; return false; }
    return true;
}
void ChildProcess::kill() {
    if (pid_ > 0) {
        ::kill(-pid_, SIGKILL);
        ::kill(pid_, SIGKILL);
        int st;
        waitpid(pid_, &st, 0);
        pid_ = -1;
    }
    if (inFd_ >= 0) close(inFd_);
    if (outFd_ >= 0) close(outFd_);
    if (errFd_ >= 0) close(errFd_);
    inFd_ = outFd_ = errFd_ = -1;
}
std::string ChildProcess::drainStderr() {
    std::string s;
    char b[4096];
    ssize_t r;
    while (errFd_ >= 0 && (r = read(errFd_, b, sizeof b)) > 0) s.append(b, size_t(r));
    return s;
}
#endif

// ------------------------------------------------------------------ binary helpers
namespace {
struct Out {
    std::string b;
    template <class T> void put(T v) { b.append(reinterpret_cast<const char*>(&v), sizeof(T)); }
    void str16(const std::string& s) { put<uint16_t>(uint16_t(s.size())); b += s; }
    void str32(const std::string& s) { put<uint32_t>(uint32_t(s.size())); b += s; }
};
struct In {
    const std::string& b;
    size_t i = 0;
    explicit In(const std::string& s) : b(s) {}
    template <class T> T get() {
        if (i + sizeof(T) > b.size()) throw std::runtime_error("protocol: short frame");
        T v;
        std::memcpy(&v, b.data() + i, sizeof(T));
        i += sizeof(T);
        return v;
    }
};
}  // namespace

RuntimeLink::RuntimeLink(World& w, int robot, const RuntimeConfig& cfg, Recorder* rec) : w_(w), robot_(robot), cfg_(cfg), rec_(rec) {
    const Robot& r = *w_.robots()[robot_].robot;
    motorBase_ = 0;
    servoBase_ = uint16_t(r.ports.size());
    sensorBase_ = uint16_t(servoBase_ + r.elec.servos.size());
    hubBase_ = uint16_t(sensorBase_ + r.sensors.size());
}
RuntimeLink::~RuntimeLink() { shutdown(); }

Json RuntimeLink::deviceTable() const {
    const Robot& r = *w_.robots()[robot_].robot;
    Json a = Json::array();
    for (size_t m = 0; m < r.ports.size(); ++m) {
        const auto& mu = r.elec.motors[m];
        Json d = Json::object();
        d.set("index", int(motorBase_ + m)); d.set("name", mu.name); d.set("kind", "motor");
        d.set("hub", r.elec.hubs[r.elec.motorHub[m]].id); d.set("port", r.cfg.motorDefs[m]["port"].integer());
        d.set("ticksPerRev", r.ports[m].ticksPerRev);
        d.set("maxRpm", mu.spec.freeRpm / mu.gear.ratio);
        d.set("component", mu.spec.id); d.set("gearbox", mu.gear.id);
        a.push(d);
    }
    for (size_t s = 0; s < r.elec.servos.size(); ++s) {
        const auto& sv = r.elec.servos[s];
        Json d = Json::object();
        d.set("index", int(servoBase_ + s)); d.set("name", sv.name); d.set("kind", sv.spec.continuous ? "crservo" : "servo");
        d.set("hub", sv.hubId); d.set("port", r.cfg.servoDefs[s]["port"].integer()); d.set("component", sv.spec.id);
        a.push(d);
    }
    for (size_t s = 0; s < r.sensors.size(); ++s) {
        const auto& sd = r.sensors[s];
        static const char* kinds[] = {"imu", "pinpoint", "voltage", "distance", "color", "touch", "webcam", "analog_encoder", "digital", "analog"};
        Json d = Json::object();
        d.set("index", int(sensorBase_ + s)); d.set("name", sd.name); d.set("kind", kinds[int(sd.type)]); d.set("hub", sd.hub);
        if (sd.type == SensorType::Webcam) {
            d.set("resX", sd.resX); d.set("resY", sd.resY); d.set("fps", sd.fps); d.set("hfov", sd.hfov);
        }
        a.push(d);
    }
    for (size_t h = 0; h < r.elec.hubs.size(); ++h) {
        Json d = Json::object();
        const auto& hub = r.elec.hubs[h];
        d.set("index", int(hubBase_ + h));
        d.set("name", hub.spec.role == "robot-controller" ? "Control Hub" : "Expansion Hub " + std::to_string(h + 1));
        d.set("id", hub.id); d.set("kind", "hub"); d.set("role", hub.spec.role); d.set("component", hub.spec.id);
        a.push(d);
    }
    return a;
}

bool RuntimeLink::sendFrame(uint8_t type, const std::string& payload) {
    uint32_t len = uint32_t(payload.size() + 1);
    std::string f;
    f.append(reinterpret_cast<const char*>(&len), 4);
    f += char(type);
    f += payload;
    return proc_.writeAll(f.data(), f.size());
}

bool RuntimeLink::readFrame(uint8_t& type, std::string& payload, std::string& err) {
    uint32_t len = 0;
    if (!proc_.readAll(&len, 4, cfg_.watchdogS)) {
        err = proc_.alive() ? "runtime watchdog: no response within " + std::to_string(cfg_.watchdogS) + " s (robot code stuck outside SDK calls?)"
                            : "runtime process exited";
        std::string se = proc_.drainStderr();
        if (!se.empty()) err += "\n--- runtime stderr ---\n" + se.substr(se.size() > 4000 ? se.size() - 4000 : 0);
        return false;
    }
    if (len == 0 || len > (64u << 20)) { err = "protocol: bad frame length"; return false; }
    std::string buf(len, '\0');
    if (!proc_.readAll(&buf[0], len, cfg_.watchdogS)) { err = "protocol: truncated frame"; return false; }
    type = uint8_t(buf[0]);
    payload = buf.substr(1);
    return true;
}

bool RuntimeLink::start(std::string& err) {
    std::vector<std::string> argv;
#ifdef __linux__
    if (cfg_.sandbox && access("/usr/bin/unshare", X_OK) == 0) {
        // no network namespace access for robot code; user namespace so no privilege is needed
        argv = {"/usr/bin/unshare", "--user", "--map-root-user", "--net", "--"};
    }
#endif
    argv.push_back(cfg_.java);
    argv.push_back("-Xss4m");
    argv.push_back("-Xmx768m");
    argv.push_back("-XX:+UseSerialGC");
    argv.push_back("-Djava.awt.headless=true");
    argv.push_back("-cp");
    argv.push_back(cfg_.runtimeJar);
    argv.push_back("org.gameplan.runtime.RuntimeMain");
    argv.push_back("--package");
    argv.push_back(cfg_.packageDir);
    if (!proc_.spawn(argv, cfg_.packageDir, err)) return false;
    alive_ = true;
    Json init = Json::object();
    init.set("protocol", 1);
    init.set("devices", deviceTable());
    init.set("timingProfile", cfg_.timingProfile);
    init.set("deterministicCompute", cfg_.deterministicCompute);
    init.set("seed", double(w_.config().seed + uint64_t(robot_) * 7919));
    init.set("robot", w_.robots()[robot_].robot->label);
    init.set("sandbox", cfg_.sandbox);
    if (!sendFrame(1, init.dump())) { err = "failed to write INIT"; return false; }
    uint8_t type;
    std::string payload;
    while (true) {
        if (!readFrame(type, payload, err)) { alive_ = false; return false; }
        if (type == 10) { ready_ = Json::parse(payload); break; }
        if (type == 14 || type == 15) statusLog_.push_back(Json::parse(payload));
    }
    if (ready_["error"].isString()) { err = ready_["error"].str(); return false; }
    for (size_t s = 0; s < w_.robots()[robot_].robot->sensors.size(); ++s)
        if (w_.robots()[robot_].robot->sensors[s].type == SensorType::Webcam) w_.robots()[robot_].sensors->visionActive = true;
    return true;
}

bool RuntimeLink::exchange(double t, std::string& err) {
    if (!alive_) { err = "runtime not running"; return false; }
    Robot& r = *w_.robots()[robot_].robot;
    Out o;
    o.put<double>(t);
    o.put<uint8_t>(uint8_t(pendingControl_));
    o.str16(pendingControl_ == 1 ? pendingName_ : std::string());
    uint8_t flags = 0;
    if (r.rcAlive) flags |= 1;
    if (r.disabled) flags |= 2;
    if (inputAllowed_) flags |= 4;
    o.put<uint8_t>(flags);
    GamepadState z;
    for (const GamepadState* g : {&gp1_, &gp2_}) {
        const GamepadState& gg = inputAllowed_ ? *g : z;
        for (float f : {gg.lx, gg.ly, gg.rx, gg.ry, gg.lt, gg.rt}) o.put<float>(f);
        o.put<uint32_t>(gg.buttons);
    }
    std::vector<SensorValue> vals;
    w_.robots()[robot_].sensors->fill(w_, robot_, t, motorBase_, sensorBase_, hubBase_, vals);
    o.put<uint16_t>(uint16_t(vals.size()));
    for (auto& v : vals) { o.put<uint16_t>(v.device); o.put<uint8_t>(v.field); o.put<double>(v.value); }
    // vision results (only when a new processed frame became available)
    Json vis = Json::array();
    std::string key;
    for (size_t s = 0; s < r.sensors.size(); ++s) {
        if (r.sensors[s].type != SensorType::Webcam) continue;
        auto dets = w_.robots()[robot_].sensors->pollVision(w_, robot_, int(s), t);
        Json cam = Json::object();
        cam.set("device", int(sensorBase_ + s));
        double ft = dets.empty() ? -1 : dets[0].frameTime;
        cam.set("frameTime", ft);
        Json da = Json::array();
        for (auto& d : dets) {
            Json dj = Json::object();
            dj.set("id", d.id); dj.set("x", d.x); dj.set("y", d.y); dj.set("z", d.z); dj.set("yaw", d.yaw); dj.set("pitch", d.pitch);
            dj.set("roll", d.roll); dj.set("range", d.range); dj.set("bearing", d.bearing); dj.set("elevation", d.elevation);
            dj.set("cx", d.centerPx[0]); dj.set("cy", d.centerPx[1]); dj.set("margin", d.decisionMargin);
            Json c = Json::array(); for (double v : d.corners) c.push(v); dj.set("corners", c);
            da.push(dj);
        }
        cam.set("detections", da);
        key += std::to_string(s) + ":" + std::to_string(ft) + ";";
        vis.push(cam);
    }
    std::string visStr = (key != lastVisionKey_ && vis.size()) ? vis.dump() : std::string();
    lastVisionKey_ = key;
    o.str32(visStr);
    if (!sendFrame(2, o.b)) { alive_ = false; err = "runtime pipe closed"; return false; }
    pendingControl_ = 0;
    if (rec_) {  // record on change only (re-execution replays the last change <= t)
        std::vector<double> a = inputAllowed_ ? gp1_.toVec() : GamepadState().toVec();
        std::vector<double> b = inputAllowed_ ? gp2_.toVec() : GamepadState().toVec();
        std::vector<double> both = a;
        both.insert(both.end(), b.begin(), b.end());
        if (both != lastInput_) { rec_->input(robot_, t, a, b); lastInput_ = both; }
    }
    // read until SYNC
    while (true) {
        uint8_t type;
        std::string payload;
        if (!readFrame(type, payload, err)) { alive_ = false; return false; }
        if (type == 11) {
            In in(payload);
            target_ = in.get<double>();
            uint16_t n = in.get<uint16_t>();
            for (uint16_t k = 0; k < n; ++k) {
                Cmd c;
                c.dev = in.get<uint16_t>();
                c.field = in.get<uint8_t>();
                c.value = in.get<double>();
                c.t = in.get<double>();
                cmds_.push_back(c);
            }
            if (target_ < t) target_ = t;
            return true;
        } else if (type == 12) {
            Json j = Json::parse(payload);
            TelemetryFrame tf;
            tf.t = j["t"].num();
            for (auto& l : j["lines"].items()) tf.lines.push_back(l.str());
            telemetry_.push_back(tf);
            if (telemetry_.size() > 2000) telemetry_.erase(telemetry_.begin(), telemetry_.begin() + 1000);
        } else if (type == 13) {
            In in(payload);
            LoopRecord lr;
            lr.tStart = in.get<double>(); lr.tEnd = in.get<double>();
            lr.transactions = int(in.get<uint32_t>());
            lr.hwUs = in.get<double>(); lr.i2cUs = in.get<double>(); lr.computeUs = in.get<double>();
            lr.overheadUs = in.get<double>(); lr.visionUs = in.get<double>(); lr.slowestUs = in.get<double>();
            double dur = lr.tEnd - lr.tStart;
            if (dur > 0) lastLoopHz_ = 1.0 / dur;
            if (rec_) rec_->loop(robot_, lr);
        } else if (type == 14 || type == 15) {
            Json j = Json::parse(payload);
            statusLog_.push_back(j);
            if (type == 15) {
                status_ = j;
                if (j["opModeState"].isString()) opModeState_ = j["opModeState"].str();
                if (j["deterministic"].isBool()) deterministic_ = j["deterministic"].boolean();
            }
            if (rec_) rec_->event(t, type == 15 ? "runtime_status" : "runtime_log", robot_, j);
        } else if (type == 16) {
            hwStats_ = Json::parse(payload);
        }
    }
}

void RuntimeLink::applyDueCommands(double t) {
    Robot& r = *w_.robots()[robot_].robot;
    while (!cmds_.empty() && cmds_.front().t <= t + 1e-9) {
        Cmd c = cmds_.front();
        cmds_.pop_front();
        if (c.dev < servoBase_) {
            auto& p = r.ports[c.dev - motorBase_];
            switch (c.field) {
                case 1: p.power = c.value; p.velocityCommand = false; break;
                case 2: { RunMode nm = RunMode(int(c.value)); if (nm != p.mode) { p.integ = 0; } p.mode = nm; break; }
                case 3: p.targetPosition = int(c.value); break;
                case 4: p.targetVelocity = c.value; p.velocityCommand = true; break;
                case 5: p.brake = c.value != 0; break;
                case 6: p.motorEnabled = c.value != 0; break;
                case 7: p.kP = c.value; break;
                case 8: p.kI = c.value; break;
                case 9: p.kD = c.value; break;
                case 10: p.kF = c.value; break;
                case 11: p.tolerance = int(c.value); break;
                case 12: p.posP = c.value; break;
                default: break;
            }
        } else if (c.dev < sensorBase_) {
            auto& s = r.elec.servos[c.dev - servoBase_];
            if (c.field == 1) s.pulseUs = c.value;
            if (c.field == 2) s.pwmEnabled = c.value != 0;
        }
    }
}

void RuntimeLink::shutdown() {
    if (alive_) {
        sendFrame(9, "");
        uint8_t type;
        std::string payload, err;
        double saved = cfg_.watchdogS;
        cfg_.watchdogS = 3.0;
        while (readFrame(type, payload, err)) {
            if (type == 16) hwStats_ = Json::parse(payload);
            if (type == 17) break;
        }
        cfg_.watchdogS = saved;
    }
    alive_ = false;
    proc_.kill();
}

}  // namespace gp
