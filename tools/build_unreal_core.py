#!/usr/bin/env python3
"""Build gameplan_core for the Unreal plugin on the current platform (same sources as gp-sim).

Unreal links with its own toolchain/runtime; pass the engine's clang on Linux via --cxx if needed.
"""
import os, platform, shutil, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plat = {"Windows": "Win64", "Linux": "Linux", "Darwin": "Mac"}[platform.system()]
build = os.path.join(root, "build-unreal-" + plat)
args = ["cmake", "-S", root, "-B", build, "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"]
if "--cxx" in sys.argv: args.append("-DCMAKE_CXX_COMPILER=" + sys.argv[sys.argv.index("--cxx") + 1])
if plat == "Mac": args.append("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
subprocess.check_call(args)
subprocess.check_call(["cmake", "--build", build, "--config", "Release", "--target", "gameplan_core"])
out = os.path.join(root, "unreal", "GamePlan", "Plugins", "GamePlanSim", "ThirdParty", "gameplan_core", "lib", plat)
os.makedirs(out, exist_ok=True)
for dirpath, _, files in os.walk(build):
    for f in files:
        if f in ("libgameplan_core.a", "gameplan_core.lib"):
            shutil.copy2(os.path.join(dirpath, f), out)
print("gameplan_core ->", out)
