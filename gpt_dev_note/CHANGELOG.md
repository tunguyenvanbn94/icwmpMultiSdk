# Change log (append-only)

## 2026-10-11 — Staged PH-G0 / PH-G1, not yet committed in target repository

- Added a consolidated code-vs-architecture review derived from the project progress snapshot as of 2026-10-10 18:02.
- Added ADRs, phase gates, risk/backlog and evidence matrix.
- Added Python static preflight tool (read-only) and seven fixture-based unit tests.
- No runtime source change, SDK image modification or board command executed.
- Cannot claim Git commits or remote branch until the real source has been fetched and validated.
- Exercised importer on an isolated synthetic Git remote; 2 staged commits and test push succeeded in that fixture, not on GitHub.

## Imported PH-G0 and PH-G1 on dev_gpt
- Base SHA: 76eb0f543fd72f08a71460345cd80db088c2eb45
- Added baseline notes and read-only SDK/model preflight with fixture-based tests.
- Existing source files not modified.

## 2026-10-11 — PH-G2a model policy and runtime error propagation

- Added `dm_model_policy.c/.h` to common libtr098 sources; both model choices are selected only if compiled/registered.
- `dm_entry_load_model` rejects unknown, empty and uncompiled requested models without mutating previous latch.
- MTK/BDK/UCI startup and config reload now propagate the error; BDK app-mode boolean conversion handles negatives explicitly.
- Added standalone production policy tests (37 assertions PASS) to `tests/host/run.sh unit`.
- PH-G2b root-hook removal, concurrency hardening and MTK/BDK board regression remain outstanding.
