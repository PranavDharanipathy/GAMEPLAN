#!/usr/bin/env python3
"""End-to-end integration tests (stdlib only; Linux/macOS/Windows).

Covers the full causal chain: real FTC Java project -> importer -> sandboxed runtime -> lockstep
physics/electrical -> rules -> recording -> deterministic re-execution -> report.
Run from the repository root after building:  python3 tests/integration_test.py
"""
import gzip, csv, json, os, shutil, statistics, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "gp-sim" + (".exe" if os.name == "nt" else ""))
JAR = os.path.join(ROOT, "runtime", "build", "gameplan-runtime.jar")
OUT = os.path.join(ROOT, "out", "itest")
fails = []

def run(cmd, **kw):
    print("$", " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)

def check(name, cond, detail=""):
    print(("[ PASS ] " if cond else "[ FAIL ] ") + name + ("" if cond else "  " + detail), flush=True)
    if not cond: fails.append(name)

def loops(rec, t0, t1):
    rows = list(csv.reader(gzip.open(os.path.join(rec, "loops_0.csv.gz"), "rt")))[1:]
    d = [(float(r[1]) - float(r[0])) * 1000 for r in rows if t0 <= float(r[0]) < t1]
    return d

shutil.rmtree(OUT, ignore_errors=True)
os.makedirs(OUT)
r = run(["java", "-cp", JAR, "org.gameplan.runtime.tools.Importer", "runtime/fixtures/basic-project", "--out", os.path.join(OUT, "pkg")])
check("import fixture project", r.returncode == 0, r.stderr[-2000:])
comp = json.load(open(os.path.join(OUT, "pkg", "compatibility.json")))
check("android-only OpMode excluded with a reason", any("DashboardUser" in k for k in comp["compilation"]["excluded"]))
check("hardware names discovered", {"lf", "rf", "lb", "rb", "imu"} <= {d["name"] for d in comp["sourceScan"]["hardwareDevices"]})

def match(teleop, auto, rec, seed=3):
    m = json.load(open(os.path.join(ROOT, "examples", "matches", "fixture-runtime.match.json")))
    m["seed"] = seed
    m["robots"][0]["runtime"]["package"] = os.path.join(OUT, "pkg")
    m["robots"][0]["runtime"]["teleop"] = teleop
    if auto: m["robots"][0]["runtime"]["auto"] = auto
    else: m["robots"][0]["runtime"].pop("auto", None)
    p = os.path.join(OUT, rec + ".match.json")
    json.dump(m, open(p, "w"))
    return run([EXE, "run", p, "--record", os.path.join(OUT, rec)], timeout=900)

r = match("Mecanum TeleOp", "Encoder Auto", "m1")
check("full match with Java runtime", r.returncode == 0, r.stdout + r.stderr[-2000:])
rec = os.path.join(OUT, "m1")
res = json.load(open(os.path.join(rec, "result.json")))
states = [s.get("opModeState") for s in res["runtimes"][0]["status"]]
check("AUTO and TELEOP lifecycle observed", states.count("running") == 2 and states.count("stopped") >= 2, str(states))
frames = list(csv.reader(gzip.open(os.path.join(rec, "frames.csv.gz"), "rt")))
ix = {c: i for i, c in enumerate(frames[0])}
x_at = lambda t: float(next(f for f in frames[1:] if float(f[0]) >= t)[ix["r0_x"]])
check("AUTO encoder drive moved the robot ~0.8 m", 0.6 < x_at(4.5) - x_at(3.0) < 1.0, "%.3f" % (x_at(4.5) - x_at(3.0)))
v = run([EXE, "verify", rec], timeout=900)
check("deterministic re-execution is EXACT", v.returncode == 0 and "EXACT" in v.stdout, v.stdout + v.stderr[-1000:])

# loop-time costs must change behaviour: the deliberately expensive loop runs much slower
r2 = match("Timing Stress", None, "m2")
check("timing stress match", r2.returncode == 0, r2.stderr[-1000:])
fast = statistics.mean(loops(rec, 45, 150))
slow = statistics.mean(loops(os.path.join(OUT, "m2"), 45, 150))
check("expensive SDK calls lengthen the loop (%.1f ms vs %.1f ms)" % (slow, fast), slow > 2.0 * fast)

# viewer replay of the Java-runtime match: re-executes it (team code runs again) and writes the frames
rx = run([EXE, "replay-export", rec, "--out", os.path.join(OUT, "m1.gpreplay")], timeout=900)
ok = rx.returncode == 0 and "EXACT" in rx.stdout
if ok:
    import struct
    blob = gzip.open(os.path.join(OUT, "m1.gpreplay")).read()
    hl = struct.unpack("<I", blob[10:14])[0]
    hdr = json.loads(blob[14:14 + hl])
    fl, nf = int(hdr["frameLength"]), int(hdr["frames"])
    first = struct.unpack("<7f", blob[14 + hl:14 + hl + 28])   # header of the first frame: t phase red blue nR nE nC
    expect_fl = 7 + 19 * int(first[4]) + 14 * int(first[5]) + 5 * int(first[6]) + 10
    ok = (blob[:10] == b"GPREPLAY1\n" and hdr["schema"] == "gameplan.replay/1" and fl == expect_fl and (14 + hl) % 4 == 0
          and len(blob) - 14 - hl == 4 * fl * nf and nf > 20 * 150 and hdr["reproduction"] == "EXACT"
          and int(first[4]) == len(hdr["robots"]))
check("replay export of the runtime match (EXACT, gameplan.replay/1, 20 Hz view frames)", ok, rx.stdout[-800:] + rx.stderr[-800:])

rp = run([EXE, "report", rec])
check("post-match report", rp.returncode == 0 and os.path.getsize(os.path.join(rec, "report.html")) > 10000)
cf = run([EXE, "counterfactual", rec, "--at", "60", "--set", "red1.battery.soc=0.35", "--out", os.path.join(OUT, "cf")], timeout=1800)
check("counterfactual branch with verified prefix", cf.returncode == 0 and "EXACT up to the branch point" in cf.stdout, cf.stdout[-1500:] + cf.stderr[-500:])

# sandbox: hostile robot code cannot read the host, write outside its scratch dir or use the network
r3 = match("Sandbox Probe", None, "m3")
check("sandbox probe match", r3.returncode == 0, r3.stderr[-1000:])
rt3 = json.load(open(os.path.join(OUT, "m3", "result.json")))["runtimes"][0]
tel = {k.strip(): v.strip() for k, v in (l.split(":", 1) for l in rt3["lastTelemetry"] if ":" in l)}
den = lambda t, k: t.get(k, "").startswith("denied")
print("  confinement:", rt3.get("fsConfinement"), tel)
if sys.platform.startswith("linux") and rt3.get("fsConfinement", "").startswith("landlock"):
    check("sandbox: host files unreadable", den(tel, "readHome") and den(tel, "readHost"), str(tel))
    check("sandbox: writes outside scratch denied", den(tel, "writePkg") and den(tel, "writeTmp"), str(tel))
    check("sandbox: no network", den(tel, "tcp"), str(tel))
    check("sandbox: virtual /sdcard writable and kept with the recording",
          tel.get("writeSdcard") == "ALLOWED" and os.path.exists(os.path.join(OUT, "m3", "robot-files", "red1", "sdcard", "FIRST", "data", "probe", "log.txt")), str(tel))
else:
    print("  [ SKIP ] OS filesystem confinement checks (not available here: %s)" % rt3.get("fsConfinement"))

# SDK 12 AprilTag clusters: default library -> HIVE tags arrive as cluster detections; a custom
# library listing them individually -> the same tags arrive as single detections with poses
def tel_of(rec):
    rt = json.load(open(os.path.join(OUT, rec, "result.json")))["runtimes"][0]
    return {k.strip(): v.strip() for k, v in (l.split(":", 1) for l in rt["lastTelemetry"] if ":" in l)}
r4 = match("AprilTag Watch", None, "m4")
t4 = tel_of("m4") if r4.returncode == 0 else {}
check("AprilTag clusters (default library): cluster detections, no singles",
      r4.returncode == 0 and int(t4.get("clusters", 0)) > 50 and int(t4.get("singlesWithPose", 1)) == 0 and int(t4.get("maxPercent", 0)) >= 50, str(t4))
r5 = match("AprilTag Watch (singles)", None, "m5")
t5 = tel_of("m5") if r5.returncode == 0 else {}
check("AprilTag custom library: same tags as singles with pose, no clusters",
      r5.returncode == 0 and int(t5.get("singlesWithPose", 0)) > 50 and int(t5.get("clusters", 1)) == 0, str(t5))

# FTCScout team imitation, offline from the committed raw response (tiny budget), then a match using it
prof = os.path.join(OUT, "8807.team.json")
r6 = run([sys.executable, "tools/ftcscout_imitate.py", "8807", "--from-cache", "profiles/teams/8807.ftcscout.json",
          "--rounds", "1", "--candidates", "3", "--seeds", "1", "--out", prof], timeout=900)
ok6 = r6.returncode == 0 and os.path.exists(prof)
p6 = json.load(open(prof)) if ok6 else {}
check("FTCScout imitation profile (offline cache)", ok6 and p6["source"]["qualMatchesPlayed"] > 0 and "achieved" in p6["calibration"], r6.stdout[-800:] + r6.stderr[-800:])
mm = json.load(open(os.path.join(ROOT, "examples", "matches", "ai-vs-ai.match.json")))
mm["robots"][0]["ai"] = {"imitate": prof}
json.dump(mm, open(os.path.join(OUT, "imit.match.json"), "w"))
r7 = run([EXE, "run", os.path.join(OUT, "imit.match.json"), "--record", os.path.join(OUT, "m7")], timeout=900)
hdr = json.load(open(os.path.join(OUT, "m7", "header.json"))) if r7.returncode == 0 else {}
check("match uses the imitation profile (hash recorded)", r7.returncode == 0 and "imitationProfileSha256" in json.dumps(hdr), r7.stderr[-800:])

print("\n%d failure(s)" % len(fails))
sys.exit(1 if fails else 0)
