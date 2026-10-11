# PH-G2a — Central data-model capability validation

**Baseline:** `dev_gpt` at `29d314a1cab95928177901ed20e607263c89f71c`; **status:** `CODE_INTEGRATED / ISOLATED_C_TEST_PASS / SDK_HOST_BOARD_PENDING`.

## Observed original behavior

`dmentry.c:dm_entry_load_model()` previously returned TR-098 if `cwmp.cpe.datamodel=tr181` but the build lacked TR-181 modules. Unknown or empty values also silently became TR-098. MTK/BDK callers interpreted the result as a boolean, making negative error returns unsafe without caller changes. In BDK, both app and lib use the latched selection; the SDK still owns root switching through `dm_platform_select_root()`.

## Implemented behavior

- A new platform-neutral production `dm_model_policy.c/.h` resolves explicit model names against registered `DM_MODEL_TR098` / `DM_MODEL_TR181` module counts. It has no UCI, SDK or runtime dependencies.
- Explicit `tr098` / `tr181` succeeds only when the corresponding model has registered modules; case-insensitive spelling remains supported for legacy compatibility.
- Empty, unknown or unsupported explicit values fail with a diagnostic; `dm_entry_load_model()` returns `-1` **without changing the prior model**.
- **Missing** option still defaults to TR-098 (existing shipped product behavior); a future product-profile implementation can make this explicit.
- MTK and BDK startup propagate failure and stop daemon startup; UCI uses the same selection check.
- Config reload returns failure through `icwmp_platform_config_reloaded()` and `cwmp_config_reload()`; on rejection the old active model remains selected and device identity is not rebuilt.
- Only the registered model tree is considered available; the SDK name does not imply a particular data model.

## Accepted boundaries / deferred work

- The existing `dm_platform_select_root()` hook remains. PH-G2b should centralize root resolution, model capability/profile metadata, and safe synchronization with ACS/ubus/notify concurrency.
- Dynamic model switching **between** sessions remains as it was before this commit; this change does not prove it race-free.
- `cwmp_config_reload()` may have refreshed other non-model settings before rejecting an invalid model. PH-G2b should add transaction semantics for reload or require controlled restart.
- No MTK or BDK firmware image/build/board validation has been performed in this session. This is **not** release-ready until the SDK and board evidence gates pass.

## Quick tests

```sh
./gpt_dev_note/tests/run_dm_model_policy.sh
python3 -m unittest discover -s gpt_dev_note/tests -p 'test_check_multisdk_gates.py' -v
# On prepared host test environment:
tests/host/run.sh unit
tests/host/run.sh tr181
tests/host/run.sh all
```

## Required board matrix

| SDK | Model config | Expected |
|---|---|---|
| MTK | unset/tr098 | IGD model, no behavior regression |
| MTK | tr181 | Device model, ACS GPV/GPN/SPV |
| MTK | empty/tr182 | startup/reload error, no silent IGD fallback |
| BDK | unset/tr098 | IGD facade, ManagementServer/MDM parity |
| BDK | tr181 | Device MDM, ACS session and side effects |
| BDK | invalid config during reload | error returned; active model preserved |
| Generic UCI | absent/tr098/tr181 | absent/tr098 use IGD; tr181 rejected unless compiled |

Do not mark PH-G2a DONE until the required SDK builds and board regressions are recorded with exact SHA/image/commands in `05_TEST_MATRIX.md`.
