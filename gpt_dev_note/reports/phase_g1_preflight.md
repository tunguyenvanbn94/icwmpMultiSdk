# ICWMP Multi-SDK preflight

Repository: `.` (repository-relative report)

Findings: **1 errors / 2 warnings / 1 information**

Automated static observations only. Review each finding before code changes.

| Severity | Rule | Location | Observation | Recommended next action |
|---|---|---|---|---|
| ERROR | `BDK_SAMPLE_IN_BUILD` | `userspace/public/libs/libicwmp_dm/src/sdk/bdk/sdk.mk`:19 | BDK_SAMPLE_OBJECT is enabled in SDK compile flags | Move behind an explicit non-production debug/developer build profile |
| WARN | `ROOT_SELECTED_BY_SDK_HOOK` | `userspace/public/libs/libicwmp_dm/src/dmentry.c`:120 | DM runtime still references platform root selection | Review central model resolver and fail-fast unsupported-model behavior |
| INFO | `PRODUCT_MODULE_IN_PLATFORM` | `userspace/public/libs/libicwmp_dm/src/sdk/mtk/dm098/x_ais_3rdagent_mtk.c` | 17 platform C source(s) look operator/product-specific | Classify by product capability and migrate only with ACS parity tests |
| WARN | `SDK_MACRO_IN_SHARED_MODEL` | `userspace/public/libs/libicwmp_dm/src/tr098/managementserver.c`:238 | Shared TR-098 binding branches on SDK name | Move platform behavior to service/SDK hook after verifying semantics |
