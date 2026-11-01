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
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
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
int landlockAbi() { return 0; }
ChildProcess::~ChildProcess() { kill(); }
bool ChildProcess::spawn(const std::vector<std::string>& argv, const std::string& workdir, std::string& err, const FsPolicy*) {
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
#ifdef __linux__
// Landlock (Linux >= 5.13). Constants are defined here because distribution headers often lag the
// kernel (e.g. no scopes); values are the stable kernel UAPI.
namespace {
struct LlRulesetAttr { uint64_t fs, net, scoped; };
struct __attribute__((packed)) LlPathBeneath { uint64_t allowed; int32_t parentFd; };
struct LlNetPort { uint64_t allowed, port; };
constexpr uint32_t LL_CREATE_VERSION = 1u << 0;
constexpr int LL_RULE_PATH_BENEATH = 1;
constexpr uint64_t FS_EXECUTE = 1ull << 0, FS_WRITE_FILE = 1ull << 1, FS_READ_FILE = 1ull << 2, FS_READ_DIR = 1ull << 3,
                   FS_REMOVE_DIR = 1ull << 4, FS_REMOVE_FILE = 1ull << 5, FS_MAKE_CHAR = 1ull << 6, FS_MAKE_DIR = 1ull << 7,
                   FS_MAKE_REG = 1ull << 8, FS_MAKE_SOCK = 1ull << 9, FS_MAKE_FIFO = 1ull << 10, FS_MAKE_BLOCK = 1ull << 11,
                   FS_MAKE_SYM = 1ull << 12, FS_REFER = 1ull << 13, FS_TRUNCATE = 1ull << 14, FS_IOCTL_DEV = 1ull << 15;
constexpr uint64_t NET_BIND_TCP = 1ull << 0, NET_CONNECT_TCP = 1ull << 1;
constexpr uint64_t SCOPE_ABSTRACT_UNIX = 1ull << 0, SCOPE_SIGNAL = 1ull << 1;
#ifndef SYS_landlock_create_ruleset
#define SYS_landlock_create_ruleset 444
#define SYS_landlock_add_rule 445
#define SYS_landlock_restrict_self 446
#endif
uint64_t fsAllFor(int abi) {
    uint64_t a = (1ull << 13) - 1;  // ABI 1: EXECUTE..MAKE_SYM
    if (abi >= 2) a |= FS_REFER;
    if (abi >= 3) a |= FS_TRUNCATE;
    if (abi >= 5) a |= FS_IOCTL_DEV;
    return a;
}
// Builds the ruleset in the parent (no effect until a process restricts itself with it).
int buildRuleset(const FsPolicy& p, int abi, std::string& err) {
    LlRulesetAttr attr{fsAllFor(abi), 0, 0};
    size_t sz = sizeof(uint64_t);
    if (abi >= 4 && p.denyTcp) { attr.net = NET_BIND_TCP | NET_CONNECT_TCP; sz = 2 * sizeof(uint64_t); }
    if (abi >= 6) { attr.scoped = SCOPE_ABSTRACT_UNIX | (p.scopeSignals ? SCOPE_SIGNAL : 0); sz = sizeof(attr); }
    if (abi >= 4 && !p.denyTcp && abi < 6) sz = 2 * sizeof(uint64_t);
    int fd = int(syscall(SYS_landlock_create_ruleset, &attr, sz, 0));
    if (fd < 0) { err = std::string("landlock_create_ruleset: ") + std::strerror(errno); return -1; }
    const uint64_t all = fsAllFor(abi);
    const uint64_t fileRights = FS_EXECUTE | FS_WRITE_FILE | FS_READ_FILE | (abi >= 3 ? FS_TRUNCATE : 0) | (abi >= 5 ? FS_IOCTL_DEV : 0);
    auto add = [&](const std::string& path, uint64_t allowed, bool required) -> bool {
        int pfd = open(path.c_str(), O_PATH | O_CLOEXEC);
        if (pfd < 0) {
            if (!required) return true;  // optional system dirs (e.g. /lib64) may not exist
            err = "landlock: cannot open " + path + ": " + std::strerror(errno);
            return false;
        }
        struct stat st{};
        if (fstat(pfd, &st) == 0 && !S_ISDIR(st.st_mode)) allowed &= fileRights;
        LlPathBeneath pb{allowed & all, pfd};
        int r = int(syscall(SYS_landlock_add_rule, fd, LL_RULE_PATH_BENEATH, &pb, 0));
        int e = errno;
        close(pfd);
        if (r != 0) { err = "landlock_add_rule " + path + ": " + std::strerror(e); return false; }
        return true;
    };
    const uint64_t ro = FS_EXECUTE | FS_READ_FILE | FS_READ_DIR;
    bool ok = true;
    for (auto& d : p.readDirs) ok = ok && add(d, ro, false);
    for (auto& f : p.readFiles) ok = ok && add(f, FS_READ_FILE, true);
    for (auto& d : p.devFiles) ok = ok && add(d, FS_READ_FILE | FS_WRITE_FILE | (abi >= 5 ? FS_IOCTL_DEV : 0), false);
    for (auto& d : p.writeDirs) ok = ok && add(d, all & ~(FS_MAKE_CHAR | FS_MAKE_BLOCK | FS_IOCTL_DEV), true);
    if (!ok) { close(fd); return -1; }
    return fd;
}
void writeStr(int fd, const char* s) { ssize_t n = write(fd, s, std::strlen(s)); (void)n; }
bool writeFileAll(const char* path, const std::string& text) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = write(fd, text.data(), text.size()) == ssize_t(text.size());
    close(fd);
    return ok;
}
}  // namespace

int landlockAbi() {
    long v = syscall(SYS_landlock_create_ruleset, nullptr, 0, LL_CREATE_VERSION);
    return v < 0 ? 0 : int(v);
}
#else
int landlockAbi() { return 0; }
#endif

ChildProcess::~ChildProcess() { kill(); }
bool ChildProcess::spawn(const std::vector<std::string>& argv, const std::string& workdir, std::string& err, const FsPolicy* policy) {
    int in[2], out[2], er[2];
    if (pipe(in) || pipe(out) || pipe(er)) { err = "pipe() failed"; return false; }
    int ruleset = -1, abi = 0;
    int st[2] = {-1, -1};   // child -> parent confinement status, closed by exec (CLOEXEC)
#ifdef __linux__
    std::string uidMap, gidMap;
    if (policy) {
        abi = landlockAbi();
        if (abi > 0) {
            ruleset = buildRuleset(*policy, abi, err);
            if (ruleset < 0) { close(in[0]); close(in[1]); close(out[0]); close(out[1]); close(er[0]); close(er[1]); return false; }
        }
        uidMap = std::to_string(getuid()) + " " + std::to_string(getuid()) + " 1\n";
        gidMap = std::to_string(getgid()) + " " + std::to_string(getgid()) + " 1\n";
        if (pipe2(st, O_CLOEXEC) != 0) st[0] = st[1] = -1;
    }
#endif
    pid_t pid = fork();
    if (pid < 0) { err = "fork() failed"; return false; }
    if (pid == 0) {
        dup2(in[0], 0); dup2(out[1], 1); dup2(er[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]); close(er[0]); close(er[1]);
        if (!workdir.empty() && chdir(workdir.c_str()) != 0) _exit(126);
        setpgid(0, 0);  // own process group: stop kills every thread/child of the runtime
#ifdef __linux__
        if (policy) {
            if (st[0] >= 0) close(st[0]);
            // Private network namespace (no interfaces but loopback, which stays down) via an
            // unprivileged user namespace that maps only our own uid/gid.
            if (unshare(CLONE_NEWUSER | CLONE_NEWNET) == 0) {
                writeFileAll("/proc/self/setgroups", "deny");
                bool m = writeFileAll("/proc/self/uid_map", uidMap) && writeFileAll("/proc/self/gid_map", gidMap);
                if (st[1] >= 0) writeStr(st[1], m ? "N" : "n");
            } else if (st[1] >= 0) {
                writeStr(st[1], "n");
            }
            if (ruleset >= 0) {
                if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 || syscall(SYS_landlock_restrict_self, ruleset, 0) != 0) {
                    if (st[1] >= 0) writeStr(st[1], "E");
                    writeStr(2, "gameplan: Landlock confinement failed; refusing to run robot code unconfined\n");
                    _exit(125);
                }
                close(ruleset);
                if (st[1] >= 0) writeStr(st[1], "L");
            }
        }
#endif
        std::vector<char*> a;
        for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    close(in[0]); close(out[1]); close(er[1]);
    confinement_.clear();
#ifdef __linux__
    if (ruleset >= 0) close(ruleset);
    if (st[1] >= 0) {
        close(st[1]);
        char b[8]; std::string got;
        ssize_t r;
        while ((r = read(st[0], b, sizeof b)) > 0) got.append(b, size_t(r));
        close(st[0]);
        bool net = got.find('N') != std::string::npos;
        if (got.find('E') != std::string::npos) { err = "Landlock confinement failed in the runtime process"; return false; }
        confinement_ = got.find('L') != std::string::npos ? "landlock-abi" + std::to_string(abi) : std::string("unavailable (kernel has no Landlock)");
        confinement_ += net ? ", private network namespace" : ", no network namespace (user namespaces disabled)";
        if (abi >= 4) confinement_ += ", TCP denied";
    }
#endif
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
#ifndef _WIN32
        usleep(50000);
#endif
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
    if (cfg_.scratchDir.empty()) {
#ifdef _WIN32
        char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
        cfg_.scratchDir = std::string(tmp) + "gameplan-rt-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(robot_);
#else
        const char* td = getenv("TMPDIR");
        std::string tmpl = std::string(td && *td ? td : "/tmp") + "/gameplan-rt-XXXXXX";
        std::vector<char> b(tmpl.begin(), tmpl.end()); b.push_back(0);
        if (!mkdtemp(b.data())) { err = "cannot create runtime scratch directory"; return false; }
        cfg_.scratchDir = b.data();
#endif
    }
    makeDirs(cfg_.scratchDir + "/sdcard");
    makeDirs(cfg_.scratchDir + "/tmp");
    {   // absolute: the JVM runs with the package directory as its working directory
#ifdef _WIN32
        char full[MAX_PATH];
        if (_fullpath(full, cfg_.scratchDir.c_str(), MAX_PATH)) cfg_.scratchDir = full;
#else
        char* r = realpath(cfg_.scratchDir.c_str(), nullptr);
        if (r) { cfg_.scratchDir = r; free(r); }
#endif
    }
    std::string java = cfg_.java;
    FsPolicy pol;
#ifndef _WIN32
    if (java.find('/') == std::string::npos) {  // resolve through PATH so the JDK directory can be allowed
        const char* path = getenv("PATH");
        std::string ps = path ? path : "/usr/bin:/bin";
        size_t a = 0;
        while (a <= ps.size()) {
            size_t b2 = ps.find(':', a); if (b2 == std::string::npos) b2 = ps.size();
            std::string c = ps.substr(a, b2 - a) + "/" + java;
            if (access(c.c_str(), X_OK) == 0) { java = c; break; }
            a = b2 + 1;
        }
    }
    if (cfg_.sandbox) {
        auto real = [](const std::string& p) { char* r = realpath(p.c_str(), nullptr); std::string s2 = r ? r : p; free(r); return s2; };
        auto parent = [](const std::string& p) { size_t k = p.find_last_of('/'); return k == std::string::npos || k == 0 ? std::string("/") : p.substr(0, k); };
        std::string javaHome = parent(parent(real(java)));   // <home>/bin/java
        pol.readDirs = {"/usr", "/lib", "/lib64", "/lib32", "/bin", "/etc", "/proc", "/sys", javaHome,
                        real(parent(cfg_.runtimeJar)), real(cfg_.packageDir)};
        pol.devFiles = {"/dev/null", "/dev/zero", "/dev/urandom", "/dev/random"};
        pol.writeDirs = {real(cfg_.scratchDir)};
        if (!cfg_.timingProfile.empty()) pol.readFiles.push_back(real(cfg_.timingProfile));
        Json pkg = Json::parse(readTextFile(cfg_.packageDir + "/package.json"));
        for (const char* k : {"shadow", "sdk", "libs"})
            for (size_t i = 0; i < pkg["classpath"][k].size(); ++i) {
                std::string jp = pkg["classpath"][k][i].str();
                if (!jp.empty() && jp[0] != '/') jp = cfg_.packageDir + "/" + jp;
                pol.readFiles.push_back(real(jp));
            }
    }
#endif
    argv.push_back(java);
    argv.push_back("-Xss4m");
    argv.push_back("-Xmx768m");
    argv.push_back("-XX:+UseSerialGC");
    argv.push_back("-XX:-UsePerfData");    // no /tmp/hsperfdata: the robot JVM writes only its scratch dir
    argv.push_back("-Djava.awt.headless=true");
    argv.push_back("-Djava.io.tmpdir=" + cfg_.scratchDir + "/tmp");
    argv.push_back("-Dgameplan.sdcard=" + cfg_.scratchDir + "/sdcard");
    argv.push_back("-cp");
    argv.push_back(cfg_.runtimeJar);
    argv.push_back("org.gameplan.runtime.RuntimeMain");
    argv.push_back("--package");
    argv.push_back(cfg_.packageDir);
    if (!proc_.spawn(argv, cfg_.packageDir, err, cfg_.sandbox ? &pol : nullptr)) return false;
    fsConfinement_ = cfg_.sandbox ? (proc_.confinement().empty() ? std::string("unavailable on this OS") : proc_.confinement()) : "disabled";
    alive_ = true;
    Json init = Json::object();
    init.set("protocol", 1);
    init.set("devices", deviceTable());
    init.set("timingProfile", cfg_.timingProfile);
    init.set("deterministicCompute", cfg_.deterministicCompute);
    init.set("seed", double(w_.config().seed + uint64_t(robot_) * 7919));
    init.set("robot", w_.robots()[robot_].robot->label);
    init.set("sandbox", cfg_.sandbox);
    init.set("fsConfinement", fsConfinement_);
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
    if (replayInputs) replayInputs(t, gp1_, gp2_);
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
        Json da = Json::array(), ca = Json::array();
        for (auto& d : dets) {
            if (d.id <= -100) {   // per-cell cluster solve (SDK 12 AprilTagClusterDetection)
                Json cj = Json::object();
                const CellDef& cd = w_.season().hives[(-100 - d.id) / 2].cells[(-100 - d.id) % 2];
                cj.set("cell", cd.id);
                Json members = Json::array(); for (int m : cd.tags) members.push(m); cj.set("members", members);
                Json seen = Json::array(); for (int m : d.clusterIds) seen.push(m); cj.set("seen", seen);
                cj.set("x", d.x); cj.set("y", d.y); cj.set("z", d.z); cj.set("yaw", d.yaw); cj.set("pitch", d.pitch);
                cj.set("roll", d.roll); cj.set("range", d.range); cj.set("bearing", d.bearing); cj.set("elevation", d.elevation);
                ca.push(cj);
                continue;
            }
            Json dj = Json::object();
            dj.set("id", d.id); dj.set("x", d.x); dj.set("y", d.y); dj.set("z", d.z); dj.set("yaw", d.yaw); dj.set("pitch", d.pitch);
            dj.set("roll", d.roll); dj.set("range", d.range); dj.set("bearing", d.bearing); dj.set("elevation", d.elevation);
            dj.set("cx", d.centerPx[0]); dj.set("cy", d.centerPx[1]); dj.set("margin", d.decisionMargin);
            Json c = Json::array(); for (double v : d.corners) c.push(v); dj.set("corners", c);
            da.push(dj);
        }
        cam.set("detections", da);
        cam.set("clusters", ca);
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
                case 2: {
                    RunMode nm = RunMode(int(c.value));
                    if (nm != p.mode) p.integ = 0;
                    // the hub resets the encoder count on receipt of STOP_AND_RESET_ENCODER
                    if (nm == RunMode::StopAndResetEncoder) p.encoderOffset = (long long)std::llround(r.encoderTicks(int(c.dev - motorBase_)));
                    p.mode = nm;
                    break;
                }
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
