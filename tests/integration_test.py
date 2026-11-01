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

rp = run([EXE, "report", rec])
check("post-match report", rp.returncode == 0 and os.path.getsize(os.path.join(rec, "report.html")) > 10000)
cf = run([EXE, "counterfactual", rec, "--at", "60", "--set", "red1.battery.soc=0.35", "--out", os.path.join(OUT, "cf")], timeout=1800)
check("counterfactual branch with verified prefix", cf.returncode == 0 and "EXACT up to the branch point" in cf.stdout, cf.stdout[-1500:] + cf.stderr[-500:])

print("\n%d failure(s)" % len(fails))
sys.exit(1 if fails else 0)
