#!/usr/bin/env python3
"""Assemble the browser page: inline season/components/robot JSON and the WASM loader into
web/index.template.html -> build-web/site/index.html (+ gameplan.wasm alongside)."""
import json, os, shutil
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rd = lambda p: open(os.path.join(root, p), encoding="utf-8").read()
safe = lambda s: s.replace("</", "<\\/")
# the app's JavaScript lives in web/app/*.js, concatenated in name order into one module
app_dir = os.path.join(root, "web", "app")
app_js = "\n".join("// ---- web/app/%s ----\n%s" % (f, rd(os.path.join("web", "app", f)))
                    for f in sorted(os.listdir(app_dir)) if f.endswith(".js"))
page = rd("web/index.template.html").replace("/*RENDERER*/", rd("web/app.part.html").replace("/*APP_JS*/", app_js))
page = page.replace("/*SEASON_JSON*/", safe(json.dumps(json.loads(rd("seasons/biobuzz-2027/season.json")), separators=(",", ":"))))
page = page.replace("/*COMPONENTS_JSON*/", safe(json.dumps(json.loads(rd("data/hardware/components.json")), separators=(",", ":"))))
page = page.replace("/*ROBOT_JSON*/", safe(json.dumps(json.loads(rd("examples/robots/generic-launcher.robot.json")), separators=(",", ":"))))
page = page.replace("/*WASM_LOADER*/", safe(rd("web/gameplan-wasm.js")))
import datetime, subprocess
try: commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=root, capture_output=True, text=True).stdout.strip()
except Exception: commit = ""
build = {"version": rd("VERSION").strip(), "built": datetime.date.today().isoformat(), "commit": commit, "notes": []}
rel = json.loads(rd("RELEASES.json")).get("releases", [])
if rel and rel[0].get("version") == build["version"]:
    build["notes"] = rel[0].get("notes", [])   # "What's new" in About
page = page.replace("/*BUILD_INFO*/", safe(json.dumps(build)))
page = page.replace("/*TAG_JSON*/", safe(rd("web/apriltag36h11_30_45.json")))
cc = os.path.join(root, "assets", "field", "cad_colors.json")
page = page.replace("/*CAD_COLOR_JSON*/", safe(open(cc).read()) if os.path.exists(cc) else "{}")
import re
def ascii_only(html):
    # non-ASCII -> \uXXXX inside <script>, -> &#xNNNN; elsewhere (page renders identically under any charset)
    parts = re.split(r"(<script[^>]*>.*?</script>)", html, flags=re.S)
    out = []
    for i, part in enumerate(parts):
        if i % 2: out.append("".join(c if ord(c) < 128 else "\\u%04x" % ord(c) for c in part))
        else: out.append("".join(c if ord(c) < 128 else "&#x%x;" % ord(c) for c in part))
    return "".join(out)
page = ascii_only(page)
out = os.path.join(root, "build-web", "site")
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "index.html"), "w", encoding="utf-8").write(page)
shutil.copy2(os.path.join(root, "build-web", "gameplan.wasm"), os.path.join(out, "gameplan.wasm"))
# official field CAD, tessellated by tools/cad_to_glb.py and reduced by tools/decimate_glb.py
fa = os.path.join(out, "assets", "field")
os.makedirs(fa, exist_ok=True)
import base64, struct
for f in sorted(os.listdir(os.path.join(root, "assets", "field"))):
    if not f.endswith(".glb"):
        continue
    # the GLB travels as base64 inside a .json file: artifact hosting serves only standard web types,
    # and the viewer's security policy forbids fetching data: URIs (so no embedded-buffer glTF)
    data = open(os.path.join(root, "assets", "field", f), "rb").read()
    open(os.path.join(fa, f[:-4] + ".glb.json"), "w").write(json.dumps({"glb": base64.b64encode(data).decode()}))
print("site:", out, len(page), "bytes html")
