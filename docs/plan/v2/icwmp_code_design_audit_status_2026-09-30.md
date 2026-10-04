# ICWMP Code vs Design Audit - Current Status and Gaps

> **Nguồn:** bản rà soát ngày 2026-09-30 dựng trên `main` (tới patch 0066), giữ nguyên để truy vết.
> Nó chưa biết 0067–0077 và test host của `dev`. Điểm đã đổi, phát hiện mới (K1–K9) và lộ trình
> đã sửa nằm ở [sync-main-dev.md](../sync-main-dev.md); trạng thái hiện hành ở
> [implementation-status.json](../../issue/implementation-status.json). Khi mâu thuẫn, hai file đó thắng.

**Audit date:** 2026-09-30  
**Repository:** `tunguyenvanbn94/icwmpMultiSdk`  
**Audit principle:** current code is the source of truth; older design/status text is treated as historical if it conflicts with code or later board evidence.

---

# 1. Executive result

The project is farther along than the top-level design/status documents currently state.

The most important corrections are:

1. MTK native TR-098 coverage is already **P1-P5 = 458/783** parameters, not P1/65.
2. Registry path conflict detection is already implemented.
3. End-session action rollback support is already partially implemented.
4. Board startup + first Inform passed with the 0065 bundle.
5. 0066 fixes a real notification deadlock but has not yet been revalidated on board.
6. MTK STUN ManagementServer behavior is still not equivalent to the historical product.
7. A2 model/profile separation and actual common service contracts remain unimplemented.
8. BDK TR-181 exists as a valuable prototype but is not yet the final multi-model architecture.

Therefore the old design should be re-baselined rather than followed literally.

---

# 2. Documentation that is stale

## `docs/mtk/icwmp_multiplatform_tr098_design.md`

Stale statements include:

- MTK described around P1/65 coverage;
- registry overlap checking described as missing;
- several implemented engine/registry changes still described as target design.

Keep its high-level layered architecture, but update all implementation-status sections.

## `docs/issue/tr098_c_port_phases.md`

The early coverage table is historical and no longer reflects current P1-P5 implementation.

The later A2/A3/A4 architecture plan is still useful, but phase status must be refreshed.

Several lines explicitly say "all planned" even though registry/rollback and P2-P5 work were implemented later.

## `docs/issue/implementation-status.json`

Last updated 2026-09-25 and stops at patches 0034-0061.

It does not include:

- 0062 init fd fix;
- 0063 startup trace;
- 0065 WLAN associated-device JSON crash fix;
- board startup/Inform pass;
- 0066 notification deadlock/state-directory fix;
- STUN gap identified after board work.

This file must not be used as current board status without updating it.

---

# 3. Architecture/status matrix

| Area | Design target | Current code | Status | Required correction |
|---|---|---|---|---|
| Source rename | model-neutral source tree | `libicwmp_dm/src`, ABI still `libtr098` | Implemented | Keep ABI transition |
| SDK seam | SDK-specific backend isolated | `sdk/<sdk>` in lib and app | Mostly implemented | Preserve |
| SDK selection | profile/build-time | configure/sdk fragments | Implemented basic | Central profile source still missing |
| Model enum/registry | TR098/TR181 independent | enum and per-model registry exist | Partial | Build/runtime resolution missing |
| Model selection | core resolver | engine chooses TR098 then platform may swap | Not correct target | Move selection to model runtime |
| Registry merge | modular deterministic tree | implemented | Implemented | Document |
| Path wildcard/ownership | exact + `{i}` | implemented | Implemented | Generalize MTK use to active model |
| Claim conflict | detect overlapping ownership | implemented/logged | Implemented partial | Make release gate |
| Unified router | static/proxy/compat ownership | split mechanisms | Missing | Add common routing semantics |
| Service layer | model-independent domain API | no real `services/` contracts | Missing | Implement incrementally |
| Transaction | staged/revert/deferred actions | partial UCI/platform/end-session rollback | Partial | Side-effect discipline/tests |
| Snapshot/cache | request-consistent expensive reads | not established | Missing | Add per-domain when needed |
| Product layer | vendor policy separate from SDK | `X_AIS` lives with MTK implementation | Missing | Introduce profile/product layer |
| TR098 MTK P1-P5 | native C | 458 parameters | Implemented coverage | Runtime equivalence still incomplete |
| TR098 P6-P8 | native C | 320 reachable remain | Missing | Port after reference architecture |
| Compat-off | no EasyCwmp DM shell | compat default on | Missing | Final TR098 gate |
| TR181 MTK | reuse services | not implemented | Planned | After shared contracts |
| TR181 BDK | native provider/proxy | prototype exists | Partial/prototype | Refactor onto shared runtime |
| ABI rename | `libicwmp_dm` package/SONAME | still `libtr098` | Deferred | Do late |

---

# 4. MTK coverage status

Historical inventory:

```text
783 total parameters
778 reachable
5 documented unreachable
```

Native C:

| Phase | Parameters | Current status |
|---|---:|---|
| P1 | 65 | C implementation exists |
| P2 | 70 | C implementation exists |
| P3 | 67 | C implementation exists |
| P4 | 173 | C implementation exists; WAN connection Add/Delete remains compat |
| P5 | 83 | C implementation exists; 5 inventory leaves unreachable |
| **Total P1-P5** | **458** | native parameter coverage |
| P6 | 97 | not implemented |
| P7 | 79 | not implemented |
| P8 | 144 | not implemented |
| **Remaining P6-P8** | **320** | reachable work planned |

Coverage count is not equivalent to production readiness.

---

# 5. Runtime evidence status

## Passed with 0065 bundle

Board evidence confirms:

```text
full initial DM traversal
dm_entry_load_enabled_notify complete
tr069 ubus registration
cwmp status up
first Inform success
```

This closes an important "does the C tree start at all?" risk.

## Failure found afterward

`ubus dm get` later hung because notification handling leaked `mutex_session_send` on a missing state-file path.

0066 fixes:

```text
unlock/cleanup on error
create notification state directory
copy rather than cross-filesystem rename
```

Current HEAD/0066 has not yet completed the same board gate.

Correct validation state:

| Layer | Status |
|---|---|
| source/apply/static checks | PASS |
| cross syntax/build checks documented | PASS |
| 0065 startup + first Inform board | PASS |
| 0066 complete SDK rebuild | required |
| 0066 board regression | required |
| GPV/GPN/SPV full regression | not complete |
| notification regression | not complete |
| Connection Request/STUN | not complete |
| input-contract board validation | not complete |
| soak | not complete |

---

# 6. Critical code/design mismatch: ManagementServer

Current build unconditionally links shared TR-098 ManagementServer code.

That module contains UCI/service-specific behavior and is therefore not a pure schema/model binding.

For MTK STUN, the mismatch is concrete:

Historical product:

```text
stun.@stun[0].*
stuncd
```

Current C shared module:

```text
cwmp_stun
icwmp_stund
```

This is a production behavior gap.

Design consequence:

> ManagementServer must be the first reference domain for service extraction.

Immediate consequence:

> MTK needs a minimal correct STUN override before baseline freeze, even if the larger service refactor follows in the next phase.

---

# 7. Model-selection mismatch

Current engine roughly does:

```text
ctx root = TR098 registry
platform hook may replace root
```

This is incompatible with the intended SDK/model independence.

For BDK, model choice is also known in both app and proxy/library code.

Required design:

```text
one profile/model resolver
        |
one active model state
        |
engine selects correct model provider
        |
SDK only implements backend capability
```

This deserves its own implementation phase; it should not be hidden inside a generic "service layer" task.

---

# 8. Build-system mismatch

Current core library Makefile still links TR-098-specific sources unconditionally.

This prevents clean:

```text
TR181-only
```

or model-pruned source sets.

Required:

```text
core sources
+ selected model sources
+ selected SDK sources
+ selected product sources
+ selected compatibility providers
```

All generated from one resolved profile.

The current SDK selection mechanism can be retained, but model source lists must stop being an accidental SDK responsibility.

---

# 9. MTK compatibility router assessment

The MTK router is one of the stronger parts of the implementation.

Current behavior:

```text
registry-owned TR098 path -> native C
otherwise                -> EasyCwmp script
```

It allows incremental migration and avoids a flag day rewrite.

Required changes are evolutionary:

- use active model rather than hard-coded `DM_MODEL_TR098`;
- define object-operation ownership explicitly;
- make compatibility a profile feature;
- ensure native and compat claims cannot overlap silently;
- eventually disable compat after full required coverage.

Do not replace this with a completely new migration mechanism.

---

# 10. Transaction assessment

Design documents should no longer say transaction rollback is entirely missing.

Already implemented:

- VALUECHECK/VALUESET separation;
- UCI revert on set failure;
- SDK commit/revert hooks;
- end-session action mark/rollback.

Still missing/uncertain:

- formal domain transaction API;
- complete classification of external side effects;
- guarantee that no setter restarts/reloads prematurely;
- true atomicity across UCI + external stores;
- fault injection tests at multiple set positions;
- reconciliation strategy when backend cannot roll back.

Status should be **PARTIAL**, not PLANNED and not COMPLETE.

---

# 11. Registry assessment

Already implemented:

```text
per-model registry
ordered module merge
later-leaf override
segment-aware path match
{i} wildcard
claim overlap check
conflict count
```

Remaining:

- conflict should fail CI/release;
- BDK proxy ownership should participate in the same routing concept;
- registry initialization should not publish a partial tree after fatal initialization failure;
- explicit generated registration may be considered later, but constructor registration need not be replaced immediately.

---

# 12. BDK assessment

Do not discard the existing BDK work.

Useful pieces:

- generic HAL/MDM access;
- transaction/batch mechanisms;
- TR-098 facade work;
- broad TR-181 proxy/provider concept.

But classify it as **prototype/partial**, because:

- it is not comprehensively build/board verified in the current release;
- model selection is SDK/proxy-owned;
- TR-181 code resides under `dm098`;
- TR-181 uses TR-098 callback implementation for some shared leaves;
- BDK context registers TR-098-specific pieces;
- proxy local ownership is separate from registry ownership;
- sample feature flag is SDK-hardcoded.

Target Broadcom work is therefore a refactor/integration of existing code, not a rewrite.

---

# 13. Design decisions changed from the previous roadmap

## Previous assumption: build a broad service layer before further parameter work

**Adjusted:** build one or two vertical reference services first, then continue parameter migration domain by domain.

Reason:

- native/compat routing already provides safe incremental ownership;
- a large up-front abstraction rewrite increases regression risk;
- P6-P8 can establish the service pattern as they are ported.

## Previous assumption: profile separation and service extraction can be one architecture phase

**Adjusted:** model runtime/resolver/router needs an explicit phase between them.

Reason:

- current engine model selection is structurally in the wrong layer;
- app/lib duplicate model knowledge;
- BDK and MTK routing need the same active-model concept before TR-181 growth.

## Previous assumption: every TR-181 operation should use the common service layer

**Adjusted:** support both shared service bindings and SDK-native model providers.

Reason:

- BDK already has native TR-181 MDM/HAL;
- wrapping every native leaf would add cost without useful reuse.

## Previous assumption: ABI rename belongs with architecture cleanup

**Adjusted:** keep ABI `libtr098` until semantic/model architecture and consumers are stable.

Reason:

- ABI rename gives little functional benefit and increases change surface.

---

# 14. Source files that should drive the next implementation reviews

Before each phase, re-open at minimum:

```text
userspace/public/libs/libicwmp_dm/src/dmentry.c
userspace/public/libs/libicwmp_dm/src/dm_registry.[ch]
userspace/public/libs/libicwmp_dm/src/sdk/sdk.h
userspace/public/libs/libicwmp_dm/src/bin/Makefile.am

userspace/public/libs/libicwmp_dm/src/sdk/mtk/sdk.mk
userspace/public/libs/libicwmp_dm/src/sdk/mtk/dmplatform_mtk.c
userspace/public/libs/libicwmp_dm/src/sdk/mtk/dmmtk.c
userspace/public/libs/libicwmp_dm/src/sdk/mtk/input_contract_mtk.c
userspace/public/libs/libicwmp_dm/src/sdk/mtk/dm098/*

userspace/public/libs/libicwmp_dm/src/tr098/managementserver.c
userspace/public/libs/libicwmp_dm/src/tr098/common/icwmpcfg.c

userspace/public/libs/libicwmp_dm/src/sdk/bdk/*
userspace/public/apps/icwmp/.../sdk/mtk/*
userspace/public/apps/icwmp/.../sdk/bdk/*

docs/issue/analysis.md
docs/issue/implementation-status.json
docs/issue/tr098_c_port_phases.md
```

---

# 15. Baseline conclusion

Current code should be treated as:

```text
A1 source-layout migration              DONE
A3 registry/rollback subset             PARTIAL DONE
MTK TR098 P1-P5 parameter coverage      IMPLEMENTED
MTK current runtime baseline            PARTIALLY VERIFIED
MTK STUN parity                          OPEN BLOCKER
A2 profile/model separation             NOT IMPLEMENTED
common service layer                     NOT IMPLEMENTED
TR098 P6-P8                              NOT IMPLEMENTED
MTK TR181                                NOT IMPLEMENTED
BDK TR098/TR181                          PROTOTYPE/PARTIAL
release/full-C                           NOT READY
```

This status is the correct starting point for the revised phase plan.

