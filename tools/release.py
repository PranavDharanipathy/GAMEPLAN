#!/usr/bin/env python3
"""Cut a release of the browser app (stdlib only).

    python3 tools/release.py            # check VERSION vs RELEASES.json, build, print the live record

1. VERSION must equal the newest entry of RELEASES.json (bump both together).
2. Builds build-web/site (the page embeds VERSION, build date and commit).
3. Prints the `releases/latest` record. After publishing the page (same artifact URL), write that
   record to the artifact database (document `releases/latest`); every open GamePlan sees it live and
   shows the update bar between matches. See AGENTOPS.md "Releasing".
"""
import json, os, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
version = open(os.path.join(root, "VERSION")).read().strip()
rel = json.load(open(os.path.join(root, "RELEASES.json")))["releases"][0]
if rel["version"] != version:
    sys.exit("VERSION (%s) and the newest RELEASES.json entry (%s) differ - bump both" % (version, rel["version"]))
for cmd in (["python3", "tools/build_web.py"], ["python3", "tools/build_web_page.py"]):
    r = subprocess.run(cmd, cwd=root)
    if r.returncode: sys.exit("build failed: " + " ".join(cmd))
url = os.environ.get("GAMEPLAN_WEB_URL", "https://claude.ai/artifact/7xfXr5HGTdA4tZgXXosdTA")
record = {"version": version, "date": rel["date"], "required": bool(rel.get("required")), "notes": rel["notes"], "webUrl": url}
print(json.dumps(record, indent=1))
