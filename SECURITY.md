# SECURITY.md — imported robot projects are untrusted code

## Threat model
A team project (or a project downloaded from someone else) contains arbitrary Java/Kotlin and
third-party libraries. It must not be able to read unrelated files, exfiltrate data, spawn
processes, or leave code running after a match.

## Layers (current state)
| Layer | Mechanism | State |
|---|---|---|
| Separate process | Robot code runs in its own JVM, never in the simulator/Unreal process | implemented |
| Lifetime | Own process group (POSIX) / Job Object (Windows); killed on match end; host watchdog | implemented (Windows untested) |
| Network | Linux: the forked child enters a new unprivileged user + network namespace (`unshare(CLONE_NEWUSER\|CLONE_NEWNET)`, own uid/gid mapped) before exec, so sockets cannot reach anything; with Landlock ABI ≥ 4 TCP bind/connect are also denied by the kernel | implemented on Linux |
| Process creation | `ProcessBuilder.start` / `Runtime.exec` / `Runtime.halt` rewritten at load time to always-denying stubs | implemented |
| `System.exit` | rewritten: stops the OpMode, not the simulator | implemented |
| API scan | constant-pool scan of team classes reports sockets, file writes, Unsafe, Android APIs (READY `apiFindings`) | implemented |
| Supply chain | SDK jars and Maven dependencies verified against repository SHA-1; POM parsing has DOCTYPE disabled (no XXE) | implemented |
| Filesystem | Linux **Landlock** (kernel ≥ 5.13), applied in the child before exec, inherited by every thread. Readable: system dirs (`/usr`, `/lib*`, `/bin`, `/etc`, `/proc`, `/sys`), the JDK, the runtime jar dir, the package dir and exactly the jars `package.json` lists. Writable: only the per-run scratch dir (the virtual `/sdcard` + `java.io.tmpdir`), kept with the recording under `robot-files/<robot>/`. Everything else (home dir, the simulator repository, the team's source, `/tmp`) fails with `EACCES`. If Landlock is present but cannot be applied, the run is refused (fail closed). ABI ≥ 6 also scopes signals and abstract unix sockets to the runtime. | implemented on Linux; the recording's `runtime_sandbox` event and `result.json` state what was applied |
| Filesystem on kernels without Landlock | runs without FS confinement; `fsConfinement` says `unavailable (kernel has no Landlock)` | reported, not hidden |
| macOS / Windows OS sandbox | `sandbox-exec` profile / AppContainer | **not implemented** |

The Java `SecurityManager` is removed in modern JDKs and is not used.

## Remaining work (tracked in the ledger)
1. Filesystem confinement on macOS (`sandbox-exec` profile) and Windows (AppContainer + restricted token).
   Linux is done (Landlock); `tests/integration_test.py` runs the hostile `Sandbox Probe` OpMode, which
   tries to read the home dir and the simulator repository, write outside its scratch dir and open a
   TCP connection, and checks that every attempt is denied while its virtual `/sdcard` stays writable.
2. Resource limits: memory (`-Xmx` is set), CPU time per match.
3. Signed GamePlan release artifacts and update channel.

Never weaken the sandbox to make a project run; report the incompatibility instead.
