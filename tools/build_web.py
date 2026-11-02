#!/usr/bin/env python3
"""Build the browser (WebAssembly/WASI) version of gameplan_core + apps/web/gp_web.cpp.

Uses clang's wasm32-wasi target with the WASI libc and libc++ sysroot (Ubuntu packages wasi-libc,
libc++-18-dev-wasm32, libc++abi-18-dev-wasm32, libclang-rt-18-dev-wasm32). No exceptions in WASI:
the core's GP_THROW aborts with a message instead.
"""
import os, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
srcs = ["json", "math", "uncertainty", "electrical", "season", "robot", "world", "balls", "rules", "sensors", "ai", "view"]
out = os.path.join(root, "build-web")
os.makedirs(out, exist_ok=True)
cxx = os.environ.get("CXX_WASM", "clang++-18" if subprocess.call(["which", "clang++-18"], stdout=subprocess.DEVNULL) == 0 else "clang++")
flags = ["--target=wasm32-wasi", "-O2", "-std=c++17", "-fno-exceptions", "-DGP_NO_EXCEPTIONS=1", "-ffp-contract=off",
         "-I" + os.path.join(root, "core", "include"), "-isystem", "/usr/include/wasm32-wasi/c++/v1", "-isystem", "/usr/include/wasm32-wasi",
         "-DGP_SOURCE_ROOT=\"/\"", "-Wno-unused-command-line-argument"]
objs = []
for s in srcs + ["web"]:
    src = os.path.join(root, "core", "src", s + ".cpp") if s != "web" else os.path.join(root, "apps", "web", "gp_web.cpp")
    obj = os.path.join(out, s + ".o")
    subprocess.check_call([cxx] + flags + ["-c", src, "-o", obj])
    objs.append(obj)
link = [cxx, "--target=wasm32-wasi", "-O2", "-nostartfiles", "-Wl,--no-entry", "-Wl,--export-dynamic", "-Wl,-z,stack-size=1048576",
        "-L/usr/lib/wasm32-wasi", "-L/usr/lib/llvm-18/lib/wasm32-wasi", "/usr/lib/wasm32-wasi/crt1-reactor.o"] + objs + \
       ["-lc++", "-lc++abi", "-o", os.path.join(out, "gameplan.wasm")]
subprocess.check_call(link)
print("built", os.path.join(out, "gameplan.wasm"), os.path.getsize(os.path.join(out, "gameplan.wasm")), "bytes")
