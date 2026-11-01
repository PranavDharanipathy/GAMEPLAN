#!/usr/bin/env python3
"""Assemble the browser page: inline season/components/robot JSON and the WASM loader into
web/index.template.html -> build-web/site/index.html (+ gameplan.wasm alongside)."""
import json, os, shutil
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rd = lambda p: open(os.path.join(root, p), encoding="utf-8").read()
safe = lambda s: s.replace("</", "<\\/")
page = rd("web/index.template.html")
page = page.replace("/*SEASON_JSON*/", safe(json.dumps(json.loads(rd("seasons/biobuzz-2027/season.json")), separators=(",", ":"))))
page = page.replace("/*COMPONENTS_JSON*/", safe(json.dumps(json.loads(rd("data/hardware/components.json")), separators=(",", ":"))))
page = page.replace("/*ROBOT_JSON*/", safe(json.dumps(json.loads(rd("examples/robots/generic-launcher.robot.json")), separators=(",", ":"))))
page = page.replace("/*WASM_LOADER*/", safe(rd("web/gameplan-wasm.js")))
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
print("site:", out, len(page), "bytes html")
