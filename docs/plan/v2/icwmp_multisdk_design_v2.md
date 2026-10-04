# ICWMP Multi-SDK Architecture Design v2

> **Nguồn:** bản rà soát ngày 2026-09-30 dựng trên `main` (tới patch 0066), giữ nguyên để truy vết.
> Nó chưa biết 0067–0077 và test host của `dev`. Điểm đã đổi, phát hiện mới (K1–K9) và lộ trình
> đã sửa nằm ở [sync-main-dev.md](../sync-main-dev.md); trạng thái hiện hành ở
> [implementation-status.json](../../issue/implementation-status.json). Khi mâu thuẫn, hai file đó thắng.

**Audit baseline:** 2026-09-30  
**Source of truth:** current `icwmpMultiSdk` HEAD/source bundle through patch 0066.  
**Scope:** architecture target for one codebase supporting multiple SDK builds and TR-098/TR-181 data models.

---

## 1. Purpose

The project shall provide one maintainable ICWMP codebase that can be built for different embedded SDKs while preserving product behavior inherited from EasyCwmp and allowing gradual migration from TR-098 to TR-181.

The architecture must support:

- MediaTek/OpenWrt as the first production target.
- Broadcom/BDK as the next SDK target.
- TR-098 as the current production data model.
- TR-181 as a later data model.
- Vendor/product extensions without contaminating the generic SDK backend.
- Incremental migration from shell EasyCwmp data-model functions to native C.
- Transitional ABI/package compatibility with `libtr098`.

---

# 2. Key architectural decision

## 2.1 Multi-SDK means build-time SDK selection

The target is **one source tree, multiple SDK builds**.

It is NOT necessary for one firmware binary to select MTK or BDK at runtime.

```text
same source tree
      |
      +--> build profile MTK  -> MTK firmware
      |
      +--> build profile BDK  -> Broadcom firmware
      |
      +--> build profile SDK-X -> SDK-X firmware
```

Exactly one hardware SDK backend should normally be selected for a firmware build.

This matches the real deployment model: an MTK image cannot become a BDK image at runtime.

## 2.2 Data model is a separate dimension

Data model capability is independent from the hardware SDK.

A build may contain:

```text
TR-098 only
TR-181 only
TR-098 + TR-181
```

If both models are compiled, one model may be selected by product configuration/runtime policy.

The active model must be selected centrally and must never silently fall back to another model.

## 2.3 Product/profile is the third dimension

Product policy is not the SDK.

Examples:

```text
SDK      = MTK
MODEL    = TR098
PRODUCT  = HP2236B / AIS profile
```

```text
SDK      = BDK
MODEL    = TR181
PRODUCT  = MO77300EB
```

Vendor extensions, default values, enabled diagnostics and operator-specific behavior belong to the product/profile layer.

---

# 3. Current implementation - what already exists

The current source already contains important architectural foundations.

## 3.1 Source/ABI transition

Implemented:

```text
source:
userspace/public/libs/libicwmp_dm/src

runtime ABI/package:
libtr098
```

This is a valid transitional state. Renaming the SONAME/package does not need to block model or SDK work.

## 3.2 SDK seam

The data-model library has:

```text
sdk/sdk.h
sdk/<sdk>/
```

The application has a corresponding SDK seam for:

- SDK initialization;
- configuration synchronization;
- reload;
- event loop integration;
- end-of-session handling;
- cleanup.

This separation should be preserved.

## 3.3 Data-model registry

Current registry already provides:

- `DM_MODEL_TR098`;
- `DM_MODEL_TR181`;
- deterministic module ordering;
- recursive object merge;
- leaf override by later module;
- segment-aware path ownership;
- `{i}` wildcard matching;
- overlap/conflict detection;
- conflict count query.

Therefore overlap validation is no longer a future design item.

Remaining limitation:

- a conflict is logged/countable but is not intrinsically fatal;
- BDK TR-181 proxy ownership is still maintained partly outside this registry.

## 3.4 MTK native/compat migration

MTK already supports a useful transitional model:

```text
requested TR-098 path
        |
        +--> registry says native
        |       -> C data-model module
        |
        +--> not native
                -> EasyCwmp shell compatibility bridge
```

This is the right migration mechanism and should not be discarded.

The router is currently hard-coded to `DM_MODEL_TR098`, so it must later use the active model from the model runtime context.

## 3.5 Transaction support is partially implemented

Already available:

- VALUECHECK before VALUESET;
- UCI revert on failure;
- SDK commit/revert hooks;
- end-session action mark/rollback;
- ParameterKey update on success.

This is useful but is not a complete atomic transaction abstraction.

External side effects, service restart/reload and multi-store writes still require explicit classification and deferred-action rules.

---

# 4. Current architectural problems

## 4.1 Core still defaults to TR-098 and platform swaps the model

Current engine behavior is effectively:

```text
dmentry
  -> select TR098 registry
  -> SDK/platform hook may replace root
```

BDK uses the platform hook to switch to `Device.` / TR-181.

This makes model selection a platform responsibility.

Target:

```text
profile/config
   |
model resolver
   |
dm runtime context
   |
selected model provider
```

The SDK must not decide what ACS schema is active.

## 4.2 App and library can resolve model independently

BDK app code and library proxy code both know about the runtime data-model option.

This can create two sources of truth.

There must be one resolved active-model state shared by protocol/app and DM library.

## 4.3 Model source selection is still mixed with SDK selection

`bin/Makefile.am` unconditionally links several TR-098 sources such as ManagementServer and `icwmpcfg`.

SDK fragments also decide model-related source lists.

Therefore:

- SDK != model is not yet true at build level;
- TR-181-only pruning is not clean;
- common engine and TR-098 implementation are still coupled.

## 4.4 `tr098/managementserver.c` is not actually platform-neutral

Although treated as a portable module, it directly uses UCI/config/service assumptions.

For MTK, its STUN implementation uses a different config/service from the product's EasyCwmp implementation.

This proves that "portable model module" and "common semantic service" are not the same thing.

## 4.5 Product policy exists inside generic MTK backend

Examples include operator-specific `X_AIS_*` behavior and custom-prefix decisions.

Target:

```text
generic MTK backend
        +
product/operator extension
```

not:

```text
generic MTK backend contains AIS product policy
```

## 4.6 BDK TR-181 is a useful prototype but not final architecture

Current BDK implementation has:

- TR-181 root source under a `dm098` directory;
- TR-181 bindings reusing TR-098 ManagementServer callbacks;
- SDK context initialization that registers TR-098-specific objects;
- a manual proxy-local ownership list in addition to registry ownership;
- sample-object build flags in the SDK fragment;
- model selection inside the SDK/proxy layer.

These are valid prototyping shortcuts but must not become the generic architecture.

---

# 5. Target architecture

```text
+----------------------------------------------------------+
|                      ICWMP Protocol Core                 |
| session / RPC / Inform / event / transfer / diagnostics |
+------------------------------+---------------------------+
                               |
                               v
+----------------------------------------------------------+
|                   DM Runtime / Model Resolver            |
| compiled capabilities | active model | root | router    |
+------------------------------+---------------------------+
                               |
              +----------------+----------------+
              |                                 |
              v                                 v
      +---------------+                 +---------------+
      | TR-098 model  |                 | TR-181 model  |
      | schema/binding|                 | schema/binding|
      +-------+-------+                 +-------+-------+
              |                                 |
              +----------------+----------------+
                               |
                               v
+----------------------------------------------------------+
|              Domain / Semantic Service Contracts         |
| identity | management | LAN | WAN | Wi-Fi | diag | ... |
+------------------------------+---------------------------+
                               |
                               v
+----------------------------------------------------------+
|                     SDK Backend                          |
| MTK/OpenWrt | BDK/MDM/HAL | future SDK                  |
+------------------------------+---------------------------+
                               |
                               v
+----------------------------------------------------------+
|                Product / Operator Integration            |
| capabilities | X_AIS | defaults | optional features     |
+----------------------------------------------------------+
```

The diagram describes logical responsibility, not necessarily one shared library per box.

---

# 6. Two valid model-binding modes

Not every data-model leaf must be forced through a semantic service.

## Mode A - service-backed binding

Use this when behavior is reused between models or SDKs.

Example:

```text
TR098 ManagementServer.URL
            |
            v
management service
            |
            v
SDK backend
            ^
            |
TR181 Device.ManagementServer.URL
```

Best suited for:

- identity;
- agent settings;
- STUN/Connection Request;
- Time;
- LAN/WAN common semantics;
- Wi-Fi common semantics;
- diagnostics;
- transaction-sensitive writes.

## Mode B - native model provider

Use this where an SDK already exposes a high-quality native schema, especially BDK TR-181.

```text
TR181 request
    |
BDK TR181 provider
    |
Broadcom MDM / generic HAL
```

Do not rewrite an entire native TR-181 tree through hundreds of artificial service wrappers merely for architectural symmetry.

Shared semantics that must also support TR-098 should still be extracted behind a service contract.

This hybrid approach preserves reuse without over-engineering.

---

# 7. Model runtime design

## 7.1 Compile-time capability

Resolved build data should contain at least:

```text
ICWMP_SDK=mtk|bdk|...
ICWMP_MODEL_TR098=y|n
ICWMP_MODEL_TR181=y|n
ICWMP_DEFAULT_MODEL=tr098|tr181
ICWMP_RUNTIME_MODEL_SWITCH=y|n
```

Feature examples:

```text
ICWMP_DM_SCRIPT_COMPAT
ICWMP_STUN
ICWMP_XMPP
ICWMP_BULKDATA
ICWMP_TWAMP
ICWMP_VENDOR_AIS
ICWMP_BDK_TR181_PROXY
ICWMP_SAMPLE_OBJECT
```

The same resolved profile must drive:

- app configure/build;
- DM library build;
- package dependencies;
- installed assets;
- release manifest.

## 7.2 Active model

Initial implementation should use a **process-wide model selected before an ACS session and latched for that session**.

Reason:

- existing engine uses global `dmroot`, delimiter and other global state;
- trying to support concurrent TR-098 and TR-181 sessions immediately would require a much larger engine rewrite.

Rules:

1. If one model is compiled, active model is fixed.
2. If two models are compiled, selection must be one validated configuration value.
3. Unsupported requested model is a clear startup/config error.
4. No silent fallback.
5. Do not change model in the middle of a CWMP session.
6. Later, model/root state may move fully into `dmctx` if simultaneous model contexts ever become a real requirement.

---

# 8. Unified ownership/router design

Current ownership is split among:

- registry claims;
- MTK native/compat decisions;
- BDK proxy/static-local lists.

Target routing should have one conceptual order:

```text
1. explicit static/model binding
2. native model provider, if enabled
3. migration compatibility provider, if enabled
4. unsupported
```

Example TR-098 MTK during migration:

```text
registry native claim
    -> C implementation

otherwise
    -> EasyCwmp compat
```

Example BDK TR-181:

```text
local explicit binding
    -> local/service callback

otherwise
    -> native MDM proxy provider
```

Ownership conflicts must be observable in development and become a build/release gate.

---

# 9. Service contract rules

A service API should not mirror CWMP path names one function per leaf.

Use domain-oriented operations and typed state.

Example:

```text
management_get_agent_config()
management_validate_agent_update()
management_stage_agent_update()
management_commit()

stun_get_state()
stun_validate_config()
stun_stage_config()
```

Rules:

- no `DMOBJ`, `DMLEAF`, `dmctx` or SDK-private pointer crosses the service/backend boundary unless intentionally wrapped;
- values use defined types/units;
- backend errors map to a small normalized error vocabulary;
- side effects are staged rather than immediately restarting services during SPV;
- stable backend identity is separated from displayed CWMP instance number;
- read snapshots may be added per request where backend enumeration is expensive.

Snapshot/cache is a performance/correctness enhancement, not a prerequisite for the first service extraction.

---

# 10. Transaction model

Target write flow:

```text
ACS SetParameterValues
        |
        v
VALUECHECK
  - schema/type
  - legacy product input contract
  - domain validation
        |
        v
STAGE VALUESET
  - no destructive external action
        |
        v
all parameters valid?
   | yes
   v
backend commit
        |
        v
publish deferred actions
  - service reload
  - WAN reload
  - STUN reload
  - notification update
        |
        v
session/end-session actions
```

On failure:

```text
revert staged config
rollback only actions from current RPC
preserve actions already committed by previous RPCs
return exact CWMP fault
```

Not every SDK provides real atomic multi-store rollback. The limitation must be documented instead of pretending full atomicity.

---

# 11. Product/operator layer

Product-specific behavior should be explicitly selectable.

Examples:

```text
product/ais/
    tr098 extensions
    defaults
    firewall policy mapping
    service integration
```

`X_AIS_*` paths should not be treated as generic MTK capability.

Similarly:

- Storage;
- STB;
- LTE;
- DOCSIS;
- Mesh extensions

must be capability/profile gated.

A path present in the historical EasyCwmp tree does not automatically mean every product must expose it.

---

# 12. Source layout target

A practical target is:

```text
libicwmp_dm/src/
    core/
        dmentry
        registry
        runtime
        transaction

    models/
        tr098/
            schema/
            bindings/
        tr181/
            schema/
            bindings/

    services/
        identity/
        management/
        network/
        wifi/
        diagnostics/

    sdk/
        mtk/
            backend/
            providers/
        bdk/
            backend/
            providers/

    product/
        ais/
        <future-product>/

    compat/
        easycwmp/
```

Migration does not require one massive file move.

Responsibility should be corrected domain by domain.

---

# 13. One-SDK simplified deployment

For a firmware that only targets MTK:

```text
ICWMP_SDK=mtk
```

is fixed at build time.

No SDK registry or runtime SDK plugin is needed.

The exact same core/model/service interfaces are retained so that future BDK support does not require rewriting model code.

This keeps the MTK build small and avoids abstraction for abstraction's sake.

---

# 14. Broadcom strategy

Broadcom should reuse its native strengths.

### TR-181

Prefer:

```text
TR181 model provider
    -> libbcm_generic_hal / MDM
```

with explicit local bindings only where necessary.

### TR-098

Prefer:

```text
TR098 binding
    -> shared semantic services
    -> BDK backend / MDM
```

Do not duplicate MTK UCI implementation.

### Refactor required from current prototype

- move TR-181 model code out of `dm098`;
- centralize model selection;
- remove unconditional TR-098 registration from generic BDK backend;
- merge proxy ownership with common router semantics;
- put sample/vendor flags in profile;
- stop TR-181 bindings from directly depending on TR-098 callback implementation.

---

# 15. ABI strategy

Keep transitional:

```text
source = libicwmp_dm
ABI    = libtr098
```

until:

- MTK TR-098 is stable/full-C;
- model/profile build has stabilized;
- all known consumers can be rebuilt together.

SONAME/package rename is a packaging migration and should not be mixed with semantic refactoring unless necessary.

---

# 16. Architectural acceptance criteria

The architecture is considered established when:

- SDK selection is compile-time and independent from model selection;
- app/lib/package use one resolved profile;
- active model is centrally resolved and validated;
- engine no longer asks the SDK to choose the ACS model;
- both registries can be compiled/pruned independently;
- MTK compat routing uses active model, not hard-coded TR-098 ownership;
- BDK proxy and static bindings follow one ownership concept;
- one reference domain works end-to-end through model -> service -> backend;
- product extensions can be enabled/disabled independently;
- unsupported model/profile configurations fail explicitly.

