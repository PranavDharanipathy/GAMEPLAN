# MERGE.md — integrating parallel feature branches into `main`

How GamePlan is built: several features are written **at the same time, on separate branches, no
matter how invasive** (a feature may rewrite a subsystem another feature also touches). An agent
(or a person) then integrates them and puts the result on `main`. This file is the procedure for
that agent. It applies to every pull request and every branch merge.

The bar is not "it compiles" and not "git reported no conflicts". **Each merged feature must work as
its author intended, together with every other feature, on `main`.** To get there the integrator is
allowed — and expected — to write new code and restructure existing code, including large changes.

Read [AGENTS.md](AGENTS.md) first; its architectural principles override anything here.

---

## 1. Branch model

| Branch | Purpose | Who writes it |
|---|---|---|
| `main` | Always builds, passes every gate in §6, and is what users get. Never commit work in progress here. | Integrator only (fast-forward from a verified integration branch) |
| `feat/<short-name>` (or an agent's assigned branch, e.g. `claude/<name>`) | One feature per branch. May be as invasive as the feature needs. | The feature's author |
| `integrate/<yyyy-mm-dd>[-n]` | Scratch branch where the integrator combines features, adapts them to each other and verifies. | Integrator |

- Branch every feature from the newest `main`.
- Never force-push, rebase or rewrite history on a branch you don't own. The integrator merges
  feature branches as they are; adaptations happen in new commits on the integration branch.
- A feature branch that has been merged to `main` is finished. Follow-up work starts a new branch
  from `main`, never more commits on the merged one.
- After a successful integration, delete the merged feature branches and the integration branch (if
  the hosting tool can't delete branches, list them for the user to delete).

## 2. What every feature branch must carry: the feature brief

The integrator can only preserve intent it can read. Each feature PR description (or a
`docs/features/<name>.md` file on the branch) states:

1. **Intent** — what the user/driver/team gets, in one paragraph.
2. **Acceptance checks** — concrete, runnable checks that prove it works (commands, tests, a scripted
   playtest step, a screenshot to compare, a number with a tolerance). "Looks right" is not a check.
3. **Touched areas** — subsystems, file formats, protocols (`runtime/PROTOCOL.md`), season/robot
   JSON schemas, recordings, WASM frame layout, UI.
4. **Invariants it relies on** — anything the feature assumes about the rest of the system.
5. **Migrations** — data, schema, recording or config changes, and how old data is handled.

If a brief is missing, the integrator writes one from the branch's commits and code, marks it
`reconstructed`, and lists it in the merge report for the author to confirm.

## 3. Integration procedure

```text
1. Inventory     list open feature branches, their briefs, base commits, and diff size per area
2. Plan          decide the merge order and where features collide (§4)
3. Integrate     integrate/<date> from main; merge one feature at a time; adapt; verify (§5, §6)
4. Cross-check   run every feature's acceptance checks together + interaction checks
5. Land          fast-forward main to the integration branch; push; release if user-facing (§8)
6. Report        merge report in the PR / summary to the user (§9); delete merged branches
```

### Step 2 — merge order
- Foundations first: core physics/data formats/protocols before features that consume them;
  renderer and UI last.
- Within a layer, the more invasive restructuring goes first, so smaller features adapt to the new
  structure instead of the reverse.
- Write the plan down (in the integration PR) before merging: order, expected collision points, and
  for each collision which design wins and why (§4).

### Step 3 — one feature at a time
For each feature, in order:
1. `git merge --no-ff feat/<name>` into the integration branch. Resolve textual conflicts by
   **intent**, never by blindly taking "ours"/"theirs".
2. Build everything (§6 G1). Fix breakage in a separate commit titled `integrate: <feature> — <what was adapted>`.
3. Run that feature's acceptance checks **and** the acceptance checks of every feature merged before
   it. A newly merged feature must not silently break an earlier one.
4. Commit the adaptation. One merge commit + adaptation commits per feature keeps history bisectable.

## 4. When features collide

Collisions are expected. Handle them in this order of preference:

1. **Compose.** Both behaviors can coexist: restructure so they do (an extension point, a shared
   abstraction, a configuration switch with both paths tested). This is the default, even when it
   means rewriting a large part of a subsystem.
2. **Generalize.** One feature's design subsumes the other: adopt it and port the other feature onto
   it. The ported feature must still pass its acceptance checks.
3. **Decide.** The intents genuinely contradict (two different meanings for the same button, two
   physics models for the same effect). Do not pick silently: ask the user a yes/no or two-option
   question that states both intents and the consequence of each choice, then implement the answer.

Never:
- drop, stub, hide behind a disabled flag, or "temporarily" comment out a feature to get green;
- skip, weaken or delete a test to get green (fix the code or, if the test's expectation is
  genuinely wrong, change it in its own commit with the reason);
- add a second, divergent implementation of something that already exists (AGENTS.md principle 1);
- paper over a bug with a special case for the test's inputs.

When restructuring is large, keep it reviewable: move first (behavior-preserving commit, all gates
green), then change behavior in a separate commit.

## 5. Invariants every merge must preserve

From AGENTS.md — verify them explicitly, they break quietly:
- **One simulation**: headless, browser (WASM) and Unreal all use `gp::World`; no forks of the model.
- **Determinism**: the same seed gives the same state hash; recordings re-execute EXACT (`gp-sim verify`).
- **Virtual time** for robot code; rendering never changes physics.
- **Team source is read-only**; imported projects are never modified.
- **Hidden ground truth** stays hidden from robot code and AI.
- **Provenance** on every new physical constant; estimates are labelled.
- **Season packages are immutable** once released; rule changes ship as a new package version.
- **Sandbox** (Landlock, network namespace, process denial) is never weakened to make something run.
- **Wire formats** stay consistent across producers and consumers: runtime protocol, WASM frame layout
  (`core/include/gameplan/view.hpp` ↔ `web/app/50_view.js` ↔ `web/app/88_replay.js` ↔ `web/playtest*.js`), replay files (`gameplan.replay/1`), recording files, season and
  robot schemas. If a feature changes one, every consumer is updated in the same integration.

## 6. Verification gates (all required before `main` moves)

| Gate | Command / check | Must show |
|---|---|---|
| G1 Build | `cmake -S . -B build && cmake --build build`; `java runtime/tools/Build.java`; `python3 tools/build_web.py && python3 tools/build_web_page.py` | no errors; no new warnings in touched files |
| G2 Unit/system | `./build/gp-tests` | all pass |
| G3 Integration | `python3 tests/integration_test.py` | 0 failures (Java runtime in the loop, sandbox, AprilTags, imitation) |
| G4 Determinism | `./build/gp-sim run <match> --record out/x && ./build/gp-sim verify out/x` | EXACT |
| G5 Browser | `node web/node_test.js`; `node web/playtest.js 3`; `node web/playtest_browser.js <dir>` (serve `build-web/site` on :8765); `node web/csp_test.js <dir>` (serve with `web/serve_csp.py` on :8766: the claude.ai viewer's security policy) | full match, a human-style driver scores, menus/settings/pause/report flow, keyboard + touch drive, all field CAD loads under the viewer's policy, no page errors or policy violations |
| G6 Feature acceptance | every merged feature's acceptance checks (§2), run on the integration branch together | all pass |
| G7 Interaction | at least one scenario exercising each pair of features that touch the same area | behaves as both briefs describe |
| G8 Performance | `gp-sim run examples/matches/ai-vs-ai.match.json` real-time factor; browser frame time | no regression > 15 % without a stated reason |
| G9 Visual (if rendering changed) | headless screenshots of the driver, overhead and close-up views | compared with the previous ones; differences explained |
| G10 Docs | IMPLEMENTATION_LEDGER rows, affected docs, `RELEASES.json` notes | honest status, no feature marked done without evidence |

A gate that cannot run in the current environment (for example the Unreal build) is reported as
**not run**, with the reason — never as passed.

## 7. Pull requests

- Feature PRs target `main` but are **merged only through an integration** (§3); a single-feature
  integration is fine.
- PR description: the feature brief (§2). Integration PR description: the plan (§3 step 2) and, when
  done, the merge report (§9). Use `.github/pull_request_template.md`.
- Commit messages and PR bodies carry the attribution lines required by the session.
- Don't merge with failing or skipped gates. Don't merge your own unverified work just because the
  branch is yours.

## 8. Landing and releasing

1. All gates green on `integrate/<date>`.
2. `git checkout main && git merge --ff-only integrate/<date> && git push origin main`.
   If `main` moved meanwhile, merge `main` into the integration branch and re-run the gates.
3. If anything user-facing changed: bump `VERSION`, add the `RELEASES.json` entry, follow
   AGENTOPS.md "Releasing" (publish the browser app, update the live `releases/latest` record).
4. Delete merged feature branches and the integration branch.

## 9. Merge report (in the integration PR, and summarized to the user)

For each feature: **status** — `working` (acceptance checks pass), `adapted` (works, but the
integrator changed how; say what), `deferred` (not landed; why, and what it needs). Then:
collisions found and how each was resolved (§4); restructuring done; gate results with numbers
(tests passed, determinism, real-time factor, playtest score rate); gates not run and why; follow-ups.
