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
| Network | Linux: started inside a new user+network namespace (`unshare --user --map-root-user --net`), so sockets cannot reach anything | implemented on Linux when `unshare` exists |
| Process creation | `ProcessBuilder.start` / `Runtime.exec` / `Runtime.halt` rewritten at load time to always-denying stubs | implemented |
| `System.exit` | rewritten: stops the OpMode, not the simulator | implemented |
| API scan | constant-pool scan of team classes reports sockets, file writes, Unsafe, Android APIs (READY `apiFindings`) | implemented |
| Supply chain | SDK jars and Maven dependencies verified against repository SHA-1; POM parsing has DOCTYPE disabled (no XXE) | implemented |
| Filesystem | working directory is the package directory; **no OS-level filesystem confinement yet** | **gap** |
| macOS / Windows OS sandbox | `sandbox-exec` profile / AppContainer | **not implemented** |

The Java `SecurityManager` is removed in modern JDKs and is not used.

## Remaining work (tracked in the ledger)
1. Filesystem confinement: Linux Landlock or bubblewrap (bind the package dir read-only, a scratch dir
   writable); macOS `sandbox-exec`; Windows AppContainer + restricted token.
2. Resource limits: memory (`-Xmx` is set), CPU time per match.
3. Signed GamePlan release artifacts and update channel.

Never weaken the sandbox to make a project run; report the incompatibility instead.
