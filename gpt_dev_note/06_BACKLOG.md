# Optimization backlog (technical priority)

| ID | Priority | Task | Owner/Dependency | Gate / stop condition |
|---|---|---|---|---|
| OPT-01 | P0 | Capture exact `dev_181` SHA; verify clean checkout, create `dev_gpt` | GitHub access | Ancestry and SHA recorded; no change to `dev_181` |
| OPT-02 | P0 | Run static guard on actual repo and classify each report item | PH-G1 | All errors triaged, no blind automatic edits |
| OPT-03 | P0 | Re-run `tests/host/run.sh all` and source static scripts | Build host access | PASS at exact SHA, logs captured |
| OPT-04 | P1 | Single source of truth for active model, reject unsupported model | PH-G2 | Valid/invalid matrix; no silent fallback; board switch |
| OPT-05 | P1 | Remove prod `BDK_SAMPLE_OBJECT` if still enabled | SDK owner | `sdk.mk` verified, image + board smoke |
| OPT-06 | P1 | Registry/proxy ownership alignment | PH-G3 | 0 claims overlap, no GPV/GPN count regressions |
| OPT-07 | P1 | ManagementServer cross-SDK service vertical slice | MTK+BDK | No SDK-dependent shared binding, regression through ACS |
| OPT-08 | P1 | BDK WAN MTU and other MDM/runtime apply gap | Vendor SDK owner | Reflects interface after write or explicit documented limitation |
| OPT-09 | P2 | BDK BBF differences (names/access/status), whitelist valid extensions | Operator/ACS requirements | Classified mismatches and tests |
| OPT-10 | P2 | Product/operator layer for AIS/HNI | Product owner | ACS compatibility and footprint verified |
| OPT-11 | P2 | Multi-SDK release gates, 24h soak and rollback image | Lab + CI | Logs, checksums, reproducible bundle |
| OPT-12 | P3 | Generic UCI SDK qualification | Product target needed | Standalone SDK build + board + ACS acceptance |
