#!/usr/bin/env python3
"""Team imitation from FTCScout statistics (stdlib only).

    python3 tools/ftcscout_imitate.py 21229                       # fetch + calibrate
    python3 tools/ftcscout_imitate.py 21229 --event USWASAM1      # one event only
    python3 tools/ftcscout_imitate.py 21229 --estimate            # print the compute estimate, run nothing
    python3 tools/ftcscout_imitate.py 21229 --from-cache profiles/teams/21229.ftcscout.json

1. Fetches the team's BIOBUZZ (FTCScout season 2026) event statistics from the public FTCScout
   GraphQL API and keeps the raw response next to the profile (reproducible, hash-recorded).
2. Target = the team's OPR components (per-team contribution estimates of alliance score
   components), weighted by qualification matches when several events exist. LEAVE / PARK use
   FTCScout's per-robot "Individual" values.
3. Calibration (numerical, no AI model involved): the GamePlan AI drives the team's robot ALONE on
   its alliance (partner idle) against two default AI opponents, so the alliance's component scores
   are this robot's contribution - the quantity OPR estimates. A budget-limited, deterministic
   search over AI skill/personality/task weights minimises the standardised error between simulated
   and target components, using common random seeds for every candidate.
4. Writes profiles/teams/<team>.team.json; use it in a match with
       "controller": "ai", "ai": {"imitate": "profiles/teams/<team>.team.json"}

The result imitates the team's SCORING PROFILE (how many points of each kind it contributes), not
its mechanism or strategy details, which FTCScout does not record. Residuals the simulated robot
cannot reach are reported, never hidden.
"""
import argparse, datetime, hashlib, json, math, os, random, subprocess, sys, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API = "https://api.ftcscout.org/graphql"
SEASON = 2026  # FTCScout numbers seasons by start year: 2026 = BIOBUZZ (2026-2027)

GROUP_FIELDS = ["autoLeavePointsIndividual", "autoParkPointsIndividual", "autoHivePoints", "dcHivePoints",
                "dcFlowerPoints", "dcGardenPoints", "dcParkPointsIndividual", "tips", "penaltyPointsCommitted",
                "autoPoints", "dcPoints", "totalPointsNp"]
# components the calibration matches, with a noise floor for the standardised error
TARGETS = {"autoLeavePointsIndividual": 1.0, "autoParkPointsIndividual": 1.0, "autoHivePoints": 4.0,
           "dcHivePoints": 6.0, "dcFlowerPoints": 3.0, "dcGardenPoints": 2.0, "dcParkPointsIndividual": 1.0,
           "tips": 0.5, "penaltyPointsCommitted": 3.0}

QUERY = """query($n: Int!, $s: Int!) { teamByNumber(number: $n) { number name events(season: $s) {
  eventCode event { name start end }
  stats { ... on TeamEventStats%d { qualMatchesPlayed opr { %s } avg { %s } dev { %s } } } } } }""" % (
    SEASON, " ".join(GROUP_FIELDS), " ".join(GROUP_FIELDS), " ".join(GROUP_FIELDS))

# search space (normalised 0..1 per dimension)
SPACE = [("difficulty", 0.05, 1.0), ("aggression", 0.0, 1.0), ("caution", 0.0, 1.0), ("maxSpeedFrac", 0.25, 1.0),
         ("autoQuality", 0.0, 1.0), ("wHive", 0.2, 3.0), ("wFlower", 0.0, 3.0), ("wGarden", 0.0, 3.0),
         ("reactionS", 0.15, 2.5), ("aimSigmaRad", 0.01, 0.2)]


def fetch(team):
    body = json.dumps({"query": QUERY, "variables": {"n": team, "s": SEASON}}).encode()
    req = urllib.request.Request(API, body, {"content-type": "application/json", "user-agent": "GamePlan-simulator"})
    with urllib.request.urlopen(req, timeout=60) as r:
        raw = r.read()
    doc = json.loads(raw)
    if doc.get("errors"):
        raise SystemExit("FTCScout error: %s" % doc["errors"])
    if not doc["data"]["teamByNumber"]:
        raise SystemExit("team %d not found on FTCScout" % team)
    return {"query": QUERY, "variables": {"n": team, "s": SEASON}, "fetchedAt": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "api": API, "responseSha256": hashlib.sha256(raw).hexdigest(), "response": doc}


def target_from(cache, event=None):
    team = cache["response"]["data"]["teamByNumber"]
    evs = [e for e in team["events"] if e.get("stats") and e["stats"].get("qualMatchesPlayed")]
    if event:
        evs = [e for e in evs if e["eventCode"] == event]
    if not evs:
        raise SystemExit("no BIOBUZZ event with played qualification matches for team %d%s" % (team["number"], " at " + event if event else ""))
    n = sum(e["stats"]["qualMatchesPlayed"] for e in evs)
    tgt, dev = {}, {}
    for k in GROUP_FIELDS:
        tgt[k] = sum(e["stats"]["opr"][k] * e["stats"]["qualMatchesPlayed"] for e in evs) / n
        dev[k] = math.sqrt(sum((e["stats"]["dev"][k] or 0) ** 2 * e["stats"]["qualMatchesPlayed"] for e in evs) / n)
    events = [{"code": e["eventCode"], "name": e["event"]["name"], "start": e["event"]["start"], "qualMatchesPlayed": e["stats"]["qualMatchesPlayed"]} for e in evs]
    return team, events, n, tgt, dev


def denorm(x):
    return {name: lo + (hi - lo) * min(1.0, max(0.0, v)) for (name, lo, hi), v in zip(SPACE, x)}


def ai_block(p):
    return {"personality": {"name": "balanced", "aggression": round(p["aggression"], 3), "caution": round(p["caution"], 3),
                            "maxSpeedFrac": round(p["maxSpeedFrac"], 3), "autoQuality": round(p["autoQuality"], 3), "defense": 0.0,
                            "reactionS": round(p["reactionS"], 3), "aimSigmaRad": round(p["aimSigmaRad"], 4)},
            "difficulty": round(p["difficulty"], 3),
            "weights": {"hive": round(p["wHive"], 3), "flower": round(p["wFlower"], 3), "garden": round(p["wGarden"], 3)}}


def match_doc(alliance):
    """Calibration match: imitated robot alone on its alliance (partner idle), default AI opponents."""
    opp = "blue" if alliance == "red" else "red"
    lr = "examples/robots/generic-launcher.robot.json"
    return {"season": "seasons/biobuzz-2027/season.json", "components": "data/hardware/components.json", "seed": 1,
            "mode": "competition", "fidelity": "realtime", "uncertainty": {"all": "realistic"}, "referee": {"profile": "standard"},
            "robots": [
                {"label": alliance + "1", "alliance": alliance, "robot": lr, "controller": "ai", "ai": {"personality": "balanced", "difficulty": 0.6}},
                {"label": alliance + "2", "alliance": alliance, "robot": lr, "controller": "idle"},
                {"label": opp + "1", "alliance": opp, "robot": lr, "controller": "ai", "ai": {"personality": "balanced", "difficulty": 0.6}},
                {"label": opp + "2", "alliance": opp, "robot": lr, "controller": "ai", "ai": {"personality": "balanced", "difficulty": 0.6}}]}


def run_round(work, rnd, candidates, seeds, gpsim, threads, estimate_only=False):
    os.makedirs(work, exist_ok=True)
    mpath = os.path.join(work, "calibration.match.json")
    json.dump(match_doc("red"), open(mpath, "w"), indent=1)
    exp = {"name": "imitation-r%d" % rnd, "match": os.path.abspath(mpath), "mode": "stochastic",
           "trials": len(seeds), "baseSeed": seeds[0],   # seeds baseSeed+i, identical for every candidate
           "objective": "margin.red", "threads": threads,
           "variants": [{"name": "c%d" % i, "patch": {"robots.0.ai": ai_block(denorm(x))}} for i, x in enumerate(candidates)],
           "limits": {"maxWall_s": 3600, "maxStorage_MB": 50, "maxTrials": 2000},
           "out": os.path.abspath(os.path.join(work, "r%d" % rnd))}
    epath = os.path.join(work, "r%d.experiment.json" % rnd)
    json.dump(exp, open(epath, "w"), indent=1)
    if estimate_only:
        r = subprocess.run([gpsim, "estimate", epath], cwd=ROOT, capture_output=True, text=True)
        return r.stdout
    r = subprocess.run([gpsim, "experiment", epath], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("experiment failed:\n" + r.stdout[-2000:] + r.stderr[-2000:])
    res = json.load(open(os.path.join(exp["out"], "experiment.json")))
    per = {}
    for row in res["trials"]:
        if "error" in row:
            raise SystemExit("trial failed: %s" % row["error"])
        c = dict(row["redComponents"])
        c.update({k: v for k, v in row["robotIndividual"][0].items() if k != "label"})
        per.setdefault(row["variant"], []).append(c)
    return [{k: sum(t[k] for t in per["c%d" % i]) / len(per["c%d" % i]) for k in per["c%d" % i][0]} for i in range(len(candidates))]


def score(sim, tgt, dev):
    z = {k: (sim.get(k, 0.0) - tgt[k]) / max(floor, dev.get(k, 0.0) * 0.5) for k, floor in TARGETS.items()}
    return sum(v * v for v in z.values()), z


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("team", type=int)
    ap.add_argument("--event", help="use one event code instead of all BIOBUZZ events")
    ap.add_argument("--from-cache", help="use a saved FTCScout response instead of fetching")
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--candidates", type=int, default=12, help="candidates per round")
    ap.add_argument("--seeds", type=int, default=4, help="matches per candidate (common random numbers)")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--estimate", action="store_true", help="print the compute estimate and exit")
    ap.add_argument("--out", help="profile path (default profiles/teams/<team>.team.json)")
    a = ap.parse_args()
    gpsim = os.path.join(ROOT, "build", "gp-sim" + (".exe" if os.name == "nt" else ""))
    out = a.out or os.path.join(ROOT, "profiles", "teams", "%d.team.json" % a.team)
    cache_path = os.path.splitext(out)[0].replace(".team", "") + ".ftcscout.json"
    work = os.path.join(ROOT, "out", "imitation", str(a.team))
    seeds = [1000 + i for i in range(a.seeds)]

    if a.estimate:
        cands = [[0.5] * len(SPACE)] * a.candidates
        print(run_round(work, 0, cands, seeds, gpsim, a.threads, estimate_only=True))
        print("rounds: %d -> total = %d x the estimate above" % (a.rounds, a.rounds))
        return

    if a.from_cache:
        cache = json.load(open(a.from_cache))
    else:
        cache = fetch(a.team)
        os.makedirs(os.path.dirname(cache_path), exist_ok=True)
        json.dump(cache, open(cache_path, "w"), indent=1)
        print("fetched FTCScout statistics -> %s" % os.path.relpath(cache_path, ROOT))
    team, events, nmatches, tgt, dev = target_from(cache, a.event)
    print("team %d %s: %d qualification matches at %s" % (team["number"], team["name"], nmatches, ", ".join(e["code"] for e in events)))
    print("target (OPR): " + ", ".join("%s %.1f" % (k, tgt[k]) for k in TARGETS))

    rng = random.Random(a.team)
    best_x, best_s, best_sim, best_z = None, float("inf"), None, None
    history = []
    radius = 0.35
    for rnd in range(a.rounds):
        if best_x is None:  # stratified start: one candidate per difficulty band, other dims random
            # anchors (max / mid / min skill) guarantee the search sees the reachable extremes
            anchors = [[1, 1, 0, 1, 1, 0.65, 0, 0, 0, 0], [0.5] * len(SPACE), [0.1, 0.3, 0.7, 0.2, 0.2, 0.3, 0, 0.2, 0.9, 0.9]]
            k = max(0, a.candidates - len(anchors))
            cands = anchors[:a.candidates] + [[(i + rng.random()) / max(1, k)] + [rng.random() for _ in SPACE[1:]] for i in range(k)]
        else:
            cands = [best_x] + [[min(1, max(0, v + rng.gauss(0, radius))) for v in best_x] for _ in range(a.candidates - 1)]
            radius *= 0.55
        sims = run_round(work, rnd, cands, seeds, gpsim, a.threads)
        for x, sim in zip(cands, sims):
            s, z = score(sim, tgt, dev)
            history.append({"round": rnd, "params": denorm(x), "objective": s})
            if s < best_s:
                best_x, best_s, best_sim, best_z = x, s, sim, z
        print("round %d: best objective %.2f (difficulty %.2f)" % (rnd, best_s, denorm(best_x)["difficulty"]))

    params = denorm(best_x)
    unreachable = [k for k, v in best_z.items() if abs(v) > 2.0]
    profile = {
        "schema": "gameplan.team-imitation/1",
        "team": {"number": team["number"], "name": team["name"]},
        "source": {"api": cache["api"], "season": SEASON, "seasonName": "BIOBUZZ 2026-2027", "fetchedAt": cache["fetchedAt"],
                   "responseSha256": cache["responseSha256"], "rawResponse": os.path.relpath(cache_path, os.path.dirname(out)),
                   "events": events, "qualMatchesPlayed": nmatches},
        "target": {"method": "FTCScout OPR components, weighted by qualification matches; LEAVE/PARK per robot",
                   "components": {k: round(tgt[k], 3) for k in GROUP_FIELDS}, "dev": {k: round(dev[k], 3) for k in GROUP_FIELDS}},
        "ai": ai_block(params),
        "calibration": {
            "method": "imitated robot alone on its alliance (partner idle) vs two default AI opponents; deterministic search, common random seeds",
            "robot": "examples/robots/generic-launcher.robot.json", "seeds": seeds, "rounds": a.rounds, "candidatesPerRound": a.candidates,
            "matches": a.rounds * a.candidates * len(seeds), "objective": round(best_s, 4),
            "achieved": {k: round(best_sim.get(k, 0.0), 3) for k in list(TARGETS) + ["totalPointsNp"]},
            "standardisedError": {k: round(v, 3) for k, v in best_z.items()},
            "unreachable": unreachable, "history": history},
        "limitations": [
            "Imitates the scoring profile (points of each kind contributed), not the team's mechanisms or strategy, which FTCScout does not record.",
            "OPR is a least-squares estimate; with few matches it is noisy (see target.dev).",
            "Calibrated with the generic launcher robot description; another robot description changes what the same AI parameters achieve.",
            "Defense is not calibrated (OPR does not measure it) and is set to 0."] +
            (["Components the simulated robot could not match within 2 standard errors: " + ", ".join(unreachable)] if unreachable else []),
    }
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(profile, open(out, "w"), indent=1)
    print("\n%-28s %8s %8s %6s" % ("component", "target", "sim", "z"))
    for k in TARGETS:
        print("%-28s %8.1f %8.1f %6.2f" % (k, tgt[k], best_sim.get(k, 0.0), best_z[k]))
    print("%-28s %8.1f %8.1f" % ("totalPointsNp", tgt["totalPointsNp"], best_sim.get("totalPointsNp", 0.0)))
    print("\nprofile: %s" % os.path.relpath(out, ROOT))
    if unreachable:
        print("not matched within 2 standard errors: " + ", ".join(unreachable))


if __name__ == "__main__":
    main()
