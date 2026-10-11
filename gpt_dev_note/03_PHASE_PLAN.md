# Phase execution plan (dev_gpt)

**Base dependency:** fetch `origin/dev_181` to the exact SHA recorded in `BASELINE.md`. All commits belong to `dev_gpt` only. Workloads are provisional for one experienced engineer with MTK+BDK build/board access.

| Phase | Scope / output | Completion gate | Approx. workdays | Commit rule |
|---|---|---|---:|---|
| PH-G0 | Capture baseline, review, architecture ADR, plan, evidence | Clean ancestry; docs complete | 1–2 | Single docs commit |
| PH-G1 | Static preflight tool, test fixtures, report | 7+ unit cases green; run on real repo; triage warnings | 1–3 | Single tool/test commit |
| PH-G2a | Model config capability validation and clear init failure | Host dual/single model tests, no implicit fallback | 2–4 | Single behavior/test commit |
| PH-G2b | Stable latch and inter-session model isolation | MTK+BDK ACS/ubus, thread/session lifecycle tests | 2–3 | Separate commit |
| PH-G3a | Registry ownership constraints and CI claims gate | No duplicate claims in compiled model variants | 2–4 | Separate commit |
| PH-G3b | One ManagementServer semantic service vertical slice | MTK and BDK parity + rollback | 4–6 | Separate commit |
| PH-G4a | BDK compiler profile cleanup (sample/debug) | SDK build, image, board regression | 2–4 | Separate commit |
| PH-G4b | BDK BBF gap triage + runtime apply | Confirmed ACS behavior and side effects | 5–8 | Separate commit(s) per domain |
| PH-G4c | Product policy extraction | Product profile + board parity | 3–6 | Separate commit per extension |
| PH-G5 | CI matrix, dual-model ACS suite, soak, packaging | Evidence per SDK/model/RPC, checksums + reproducibility | 5–10 | Test/release commits |

**Do not mark a phase complete before its gate.** If board is not available, track subphase as `HOST_PASS / BOARD_PENDING` instead of `DONE`.

## Phase execution checklist

1. Verify `git status --porcelain` is empty; `git rev-parse HEAD` recorded.
2. Record target behavior and reason in `02_ARCHITECTURE_DECISIONS.md` or `06_BACKLOG.md`.
3. Inspect code at exact SHA; write regression tests first if possible.
4. Make minimal source edits (no unrelated renames or dynamic plugin framework).
5. Run static tests, host tests, SDK builds, then board and soak when needed.
6. Save commands, outcomes and artifacts under `reports/` with date/SHA/image.
7. Update `04_EXECUTION_LOG.md`, `05_TEST_MATRIX.md`, `CHANGELOG.md`.
8. Commit only related changes with message `[gpt-dev NNNN] <area>: <summary>`.
9. Push to `origin/dev_gpt` when credentials/connection are available (no force push).
