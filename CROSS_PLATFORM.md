# CROSS_PLATFORM.md

| Concern | Linux | macOS | Windows |
|---|---|---|---|
| Core build | GCC/Clang, validated here | Clang (CI) | MSVC 2022 (CI, vcpkg zlib) |
| Runtime child process | `fork/execvp`, process group kill (validated) | same code path | `CreateProcess` + Job Object (kill-on-close) — **implemented, not yet validated** |
| Runtime sandbox | user+network namespace via `unshare` when present (validated) | **no OS sandbox yet**: planned `sandbox-exec` profile | **no OS sandbox yet**: planned AppContainer |
| Paths | `realpath` | `realpath` | `_fullpath`, `_mkdir` |
| Recording compression | zlib | zlib | zlib via vcpkg |
| Unreal host | UE Linux toolchain (clang) | UE Mac (universal lib via `CMAKE_OSX_ARCHITECTURES`) | UE Win64 |
| Bitwise determinism | same binary ⇒ identical | same binary ⇒ identical | same binary ⇒ identical |

Cross-platform bit-identical results are **not** promised: libm transcendental functions differ
between platforms. A recording made on one OS verifies `EXACT` on the same OS/binary; on another OS
`verify` may report `APPROXIMATE` with the first divergent second (this is detected, never hidden).

The runtime needs only a JDK; SDK jars are downloaded per SDK version on first build and cached.
Platform-specific code is confined to `core/src/link.cpp` (process spawn), `core/src/recorder.cpp`
(mkdir) and `core/src/match.cpp` (absolute paths).
