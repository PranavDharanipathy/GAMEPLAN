# BUILD_RULES.md

## Toolchains (pinned by policy, verified in CI on Linux, macOS, Windows)
- CMake ≥ 3.20, a C++17 compiler (GCC 11+, Clang 14+, MSVC 2022). zlib (optional; without it
  recordings are plain CSV).
- JDK 21 to build and run the runtime (the shipped app bundles a jlink'd JDK so users need nothing).
- Python 3.9+ only for `tests/integration_test.py` and `tools/build_unreal_core.py` (stdlib only).
- Unreal Engine 5.4+ for `unreal/` (not available in the dev container; that code is unvalidated).

## Commands
| Step | Command |
|---|---|
| Configure/build core + CLI + tests | `cmake -S . -B build && cmake --build build -j` |
| Unit/system tests | `./build/gp-tests` or `ctest --test-dir build` |
| Runtime (bridge + SDK shadows; provisions SDK jars) | `java runtime/tools/Build.java [--sdk 12.0.0] [--offline]` |
| End-to-end tests | `python3 tests/integration_test.py` |
| Core library for Unreal | `python3 tools/build_unreal_core.py` |

## Standards
- Warnings: `-Wall -Wextra` clean for new code. Floating point: `-ffp-contract=off -fno-fast-math`,
  `/fp:precise` on MSVC (determinism).
- No new third-party dependency without: licence check (redistribution in a desktop installer),
  availability on all three platforms, a pinned version and checksum, and a DEEP_ANALYSIS entry.
  Current third-party: zlib (system), FTC SDK jars (fetched per SDK version, SHA-1 verified, not
  committed), gson (SDK support), Kotlin compiler (fetched only when a project has `.kt` files).
- Every feature lands with a test that exercises behaviour, not just construction. Cross-system
  features need an end-to-end causal test.
- Data files are JSON with a `schema` field and provenance on physical values.
- Reproducible artifacts: Build.java writes jars with fixed timestamps; the recorder stores hashes.

## Release gates
1. `gp-tests` and `integration_test.py` green on Linux and macOS; Windows build + unit tests green.
2. A fresh recording verifies `EXACT`.
3. Ledger updated; no "Validated" status without the validation having been run.
