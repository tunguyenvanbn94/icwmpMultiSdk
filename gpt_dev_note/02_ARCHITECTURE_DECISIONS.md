# Architecture decision log

## ADR-001 — One protocol core; one backend SDK per firmware
**Status:** RETAIN. **Rationale:** one SOAP/session/event implementation avoids fork divergence; compile-time SDK ensures small footprint and reproducible dependencies. **Consequence:** `sdk/<sdk>` hooks in app and lib; build/export must validate included backend.

## ADR-002 — Data model independent from SDK
**Status:** RETAIN / STABILIZE. **Rationale:** MTK and BDK can expose both TR-098 and TR-181 against their product store. **Required improvement:** central capability-based model resolver; bad/unsupported model must not silently downgrade. **Gate:** TR-098-only/TR-181-only/dual host tests and inter-session board isolation. **Verification:** inspect the current init path before editing.

## ADR-003 — Native provider where platform already implements model
**Status:** RETAIN. **Rationale:** Broadcom Distributed MDM already implements TR-181 semantics and lifecycle. Rewriting all leaf callbacks through generic services would add mapping layers and new failure modes. **Action:** shared domain services only for reusable semantics such as ManagementServer, DeviceInfo and Time.

## ADR-004 — Registry path ownership is a release contract
**Status:** PROPOSED. **Rationale:** overlap must be deterministic and visible in test/release gates. **Action:** enforce `claims conflicts = 0` for all supported model builds; reconcile any additional proxy local lists with registry definitions. **Gate:** no loss of existing path inventory or runtime parity.

## ADR-005 — Keep product policies separate from SDK backend
**Status:** PROPOSED, requires product acceptance. **Rationale:** `X_AIS_*` and equivalent extensions are product/operator capabilities, not generic MTK semantics. **Action:** migrate incrementally to product profiles while preserving exact ACS path/value/fault behavior. **Gate:** board parity and no unexpected claim overlaps.

## ADR-006 — Safeguard transaction side effects, do not rewrite wholesale
**Status:** PROPOSED. **Rationale:** existing VALUECHECK/SET and platform commit/revert are valuable. Improve with fault injection and staged external actions rather than replacing engine. **Gate:** test multi-param failure, store rollback, HAL commit failure, and restart with documented limitations.

## ADR-007 — Do not silently clean sample/debug flags in vendor builds
**Status:** PROPOSED guard first. **Rationale:** build ownership and sample compile flags must be inspected before removal. **Action:** preflight scan plus a focused change gated by host+BDK compile and board smoke test.
