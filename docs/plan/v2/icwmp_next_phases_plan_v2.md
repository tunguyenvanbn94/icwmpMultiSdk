# ICWMP Revised Implementation Plan v2

> **Nguồn:** bản rà soát ngày 2026-09-30 dựng trên `main` (tới patch 0066), giữ nguyên để truy vết.
> Nó chưa biết 0067–0077 và test host của `dev`. Điểm đã đổi, phát hiện mới (K1–K9) và lộ trình
> đã sửa nằm ở [sync-main-dev.md](../sync-main-dev.md); trạng thái hiện hành ở
> [implementation-status.json](../../issue/implementation-status.json). Khi mâu thuẫn, hai file đó thắng.

**Baseline date:** 2026-09-30  
**Purpose:** execution order after code/design re-audit.  
**Rule:** finish each phase gate before broadening the next phase.

---

# 1. Revised phase map

```text
Phase 0  Close and freeze current MTK runtime baseline
    |
Phase 1  Build profile + model capability + central model resolver
    |
Phase 2  Unified model routing/build ownership
    |
Phase 3  Reference vertical service extraction
         ManagementServer -> DeviceInfo/Time
    |
Phase 4  Complete remaining TR-098
         P6 -> P7 -> P8 + dynamic lifecycle gaps
    |
Phase 5  Full-C TR-098 production hardening
    |
Phase 6  TR-181 foundation and MTK implementation
    |
Phase 7  Refactor/integrate Broadcom on the same contracts
    |
Phase 8  Multi-SDK productization, CI, release, optional ABI rename
```

This supersedes the previous roadmap where service-layer work was treated as one large prerequisite and where model routing was not a distinct phase.

---

# 2. Phase 0 - Close current MTK baseline

## Goal

Prove the current source bundle through 0066 works on the real MTK board before architecture changes.

## Actions

### 0.1 Rebuild current HEAD/0066

- clean `libtr098`;
- clean `icwmp_tr098`;
- compile both;
- build target image;
- record source/release manifest and exact image.

### 0.2 Boot/runtime gate

Verify:

- init script returns;
- no inherited procd fd deadlock;
- daemon remains alive;
- DM initial traversal completes;
- ubus `tr069` registers;
- initial Inform succeeds;
- periodic Inform can still occur after notification thread starts.

### 0.3 Notification regression

Verify after 0066:

- `/etc/tr098` state directory exists;
- no mutex deadlock;
- `ubus dm get` remains responsive after multiple notification polling cycles;
- Get/SetParameterAttributes;
- passive notification;
- active notification;
- state persists across daemon restart/reboot where expected.

### 0.4 GPV/GPN/SPV regression

At least:

```text
DeviceInfo
ManagementServer
LAN
Wi-Fi
WAN
Diagnostics/routing read paths
```

Test SPV success and multi-parameter failure rollback.

### 0.5 Board-test the 0061 input contract

Required negative/edge values:

- empty string;
- whitespace-only;
- `;`, `&`, `|`, backtick, `$`;
- boolean `true/false/1/0`;
- reject `yes/on` if legacy shell rejects them;
- invalid unsigned integer;
- IPv4/IPv6 invalid/valid values.

### 0.6 Fix MTK STUN parity

Implement a minimal MTK-specific override for historical STUN semantics:

```text
TR098 ManagementServer STUN leaves
    -> stun.@stun[0].*
    -> stuncd
    -> reload flag/action
```

Verify:

- enable/disable;
- server address/port;
- credentials;
- min/max keepalive;
- NATDetected;
- UDPConnectionRequestAddress;
- actual UDP Connection Request where environment permits.

### 0.7 Decide debug instrumentation policy

0063 startup/crash tracing is useful.

Classify it as one of:

```text
keep always
build-time debug feature
remove after stabilization
```

Do not leave an accidental debug behavior unspecified for production.

## Deliverables

- verified baseline commit/bundle ID;
- build log;
- board regression report;
- updated implementation status;
- updated known-issues list;
- STUN parity result.

## Exit criteria

- 0066 board gate passes;
- no notify deadlock;
- GPV/GPN/SPV basic regression passes;
- input contract verified;
- STUN behavior is correct or a clearly accepted scoped exception exists;
- no critical crash/deadlock remains.

---

# 3. Phase 1 - Profile, capability and central model resolver

## Goal

Establish one source of truth for:

```text
SDK
compiled models
default model
runtime model policy
features/product
```

No large service refactor yet.

## Actions

### 1.1 Define resolved profile

Example conceptual output:

```text
SDK=mtk
MODEL_TR098=y
MODEL_TR181=n
DEFAULT_MODEL=tr098
RUNTIME_MODEL_SWITCH=n

DM_SCRIPT_COMPAT=y
VENDOR_AIS=y
STUN=y
```

### 1.2 Feed all build layers from that profile

The same resolution must drive:

- library;
- app;
- package;
- installed config/assets;
- apply/export;
- release manifest.

### 1.3 Separate engine enable from `TR098` naming

Current app/lib build logic still uses TR-098 as a master switch in places.

Introduce model-neutral engine enablement while preserving legacy option aliases if needed for compatibility.

### 1.4 Introduce central active-model resolver

Engine chooses:

```text
DM_MODEL_TR098
or
DM_MODEL_TR181
```

before DM context is used.

Remove model selection responsibility from SDK/platform code.

### 1.5 Share resolved model between app and library

BDK app and BDK DM proxy must not independently parse/decide model mode.

### 1.6 Session policy

Initially:

- active model process-wide;
- selected before session;
- latched during session;
- unsupported requested model is an error;
- no silent fallback.

## Tests

Required profile cases:

```text
MTK + TR098 only           valid
MTK + TR181 only           reject until implementation capability enabled
MTK + dual                 development only until TR181 exists
unknown SDK                reject
default model not compiled reject
runtime switch requested with one model -> reject/normalize explicitly
```

## Exit criteria

- one resolved profile exists;
- app/lib/package agree on it;
- dmentry does not start from TR098 then ask SDK to swap model;
- unsupported model fails before ACS session.

---

# 4. Phase 2 - Unified model routing and build ownership

## Goal

Make SDK/model/product source ownership real in the build and runtime router.

## Actions

### 2.1 Split source categories

Build should conceptually resolve:

```text
core
+ selected models
+ selected SDK backend
+ selected product extensions
+ selected compatibility/native providers
```

### 2.2 Stop unconditional TR-098 model linkage

Move TR-098-specific sources out of unconditional core linkage where possible.

Priority:

- ManagementServer;
- `icwmpcfg`;
- SoftwareModules if capability/model-specific.

### 2.3 Generalize MTK ownership

Replace hard-coded:

```text
dm_registry_owns(DM_MODEL_TR098, ...)
```

with active-model-aware routing where appropriate.

TR-098 EasyCwmp compat remains available only for the TR-098 migration profile.

### 2.4 Define common provider order

```text
explicit binding
native model provider
compatibility provider
unsupported
```

### 2.5 Prepare BDK ownership integration

Do not refactor all BDK yet.

Define the interface that later replaces separate:

- registry claim list;
- TR-181 local static list;
- proxy fallback list.

### 2.6 Make conflicts a validation gate

Runtime logging may remain, but CI/release check must fail if profile ownership is ambiguous.

## Exit criteria

- TR098-only build does not accidentally require TR181 source;
- future TR181-only source resolution is possible;
- MTK compat routing is expressed as a provider/fallback, not the definition of the SDK;
- ownership conflict test is zero for selected profile.

---

# 5. Phase 3 - Reference vertical service extraction

## Goal

Prove the target pattern with real product-critical domains before porting P6-P8.

Do not create hundreds of abstract APIs up front.

## 3A ManagementServer first

Reason:

- needed by every ACS session;
- shared across TR-098/TR-181;
- current shared module contains platform assumptions;
- STUN mismatch already proves the need.

### Actions

Separate:

```text
TR098 schema/binding
management/agent service
MTK backend
BDK backend/provider adapter
```

Service scope should include:

- ACS URL/credentials;
- Periodic Inform;
- Connection Request configuration;
- ParameterKey where appropriate;
- STUN state/config;
- retry/policy fields that are genuinely shared.

Preserve product config-of-record ownership.

### Tests

Compare with EasyCwmp on same board configuration:

- GPV;
- SPV;
- validation;
- fault;
- reload;
- WebUI interaction;
- Inform;
- Connection Request;
- STUN.

## 3B DeviceInfo and Time

Use as a simpler read-heavy proof after ManagementServer.

### Goals

- demonstrate common semantic service;
- verify no model-specific structs leak into backend API;
- establish normalized error/value conventions.

## 3C Transaction contract refinement

During these vertical slices:

- stage writes;
- defer reload/restart;
- verify rollback/fault injection;
- explicitly document non-atomic backend operations.

## Exit criteria

At least ManagementServer and one simpler domain run:

```text
TR098 binding -> service -> MTK backend
```

with no regression against Phase 0 baseline.

---

# 6. Phase 4 - Complete TR-098

Now continue parameter migration using the proven pattern.

Do not stop all implementation waiting for a "perfect" universal service framework.

---

## 4.1 P6 - 97 parameters

Domains:

- Firewall;
- UserInterface;
- CaptivePortal;
- Account/User;
- XMPP;
- remaining root behavior.

Actions per domain:

1. extract exact historical shell paths/types/access;
2. identify semantic/product owner;
3. identify backend calls;
4. decide service-backed vs product-specific binding;
5. implement native C;
6. claim only completed ownership;
7. test getter/setter/fault/notify/side effect;
8. update manifest/status.

Special attention:

- `firewall_clay`;
- service commit/reload;
- addressed-only hidden objects.

---

## 4.2 P7 - 79 operator parameters

Create/establish product extension layer.

`X_AIS_*` must:

- preserve exact name;
- preserve exact type;
- preserve exact side effect;
- be enabled by product profile;
- not become generic MTK schema.

TR-181 counterpart is only added where an approved semantic mapping exists.

---

## 4.3 P8 - 144 parameters

First classify each branch.

Categories:

```text
required standard behavior
operator extension
optional product capability
unsupported hardware
legacy compatibility
hybrid path required by ACS
```

Domains include:

- Storage;
- STB;
- DOCSIS;
- LTE;
- hybrid `InternetGatewayDevice.Device.*`.

Do not implement fake hardware state just to make a count reach 783.

Document profile exclusions explicitly.

---

## 4.4 Close lifecycle gaps from earlier phases

Before declaring TR-098 complete, revisit P1-P5 for:

- AddObject/DeleteObject;
- dynamic instances;
- stable identity;
- notification;
- persistence;
- write side effects;
- behavior currently still delegated to compat.

The known P4 WAN Add/Delete compatibility must be closed or explicitly retained as a non-full-C exception.

## Exit criteria

All required/reachable paths for the production MTK profile have native implementation and behavioral evidence.

---

# 7. Phase 5 - Full-C TR-098 production hardening

## Goal

Make MTK TR-098 independently deployable without EasyCwmp DM shell.

## Actions

### 5.1 Compat-off build

Build and run with:

```text
DM_SCRIPT_COMPAT=n
```

System shell scripts used for normal product service control are allowed; the EasyCwmp data-model bridge must not be required.

### 5.2 Full RPC regression

Cover:

- Inform;
- GPN;
- GPV;
- SPV;
- GPA;
- SPA;
- AddObject;
- DeleteObject;
- Reboot;
- FactoryReset if supported;
- Download/Upload;
- ScheduleInform if supported;
- Connection Request;
- diagnostics.

### 5.3 Notification/persistence

- active/passive;
- persistent attributes;
- value monitoring;
- reboot/restart;
- no deadlock.

### 5.4 Performance/soak

Measure:

- boot/start;
- initial tree load;
- full-tree GPN/GPV;
- Wi-Fi/host enumeration;
- memory;
- CPU;
- long-running sessions;
- repeated network/service restarts.

### 5.5 Freeze TR-098 mapping manifest

Machine-readable per path:

```text
path
type/access
module
owner
backend
dynamic identity
notification
test status
product capability
```

## Exit criteria

TR-098 MTK is a production baseline independent from EasyCwmp DM shell.

---

# 8. Phase 6 - TR-181 foundation and MTK implementation

## Goal

Add TR-181 without cloning TR-098 implementation.

## Actions

### 6.1 Mapping manifest

Map semantic concepts, not textually similar paths.

Status per item:

```text
reused service
MTK binding implemented
native provider
mapping_todo
not applicable
product extension
```

### 6.2 Instance/Alias policy

Define stable identity for:

- Interface;
- Ethernet;
- Wi-Fi Radio/SSID/AP;
- AssociatedDevice;
- DHCP objects;
- IP addresses/routes.

### 6.3 Implement by domain

Suggested order:

1. DeviceInfo
2. ManagementServer
3. Time
4. IP/Ethernet
5. Wi-Fi
6. DHCPv4/DNS/Routing
7. diagnostics
8. required product extensions

### 6.4 Profile tests

Eventually support:

```text
MTK TR098-only
MTK TR181-only
MTK dual-model
```

Dual model means one active model at a time unless a future explicit requirement justifies concurrent contexts.

## Exit criteria

Required MTK TR-181 profile passes model and board regression without duplicated backend logic.

---

# 9. Phase 7 - Broadcom integration/refactor

## Goal

Bring the existing BDK work under the same model/runtime/service architecture.

## Do not rewrite what already works conceptually

Retain:

- MDM/generic-HAL access;
- native TR-181 provider/proxy approach;
- transaction batching where sound;
- existing mappings that are correct.

## Required refactor

- move TR181 model source out of `dm098`;
- centralize model selection;
- remove unconditional TR098 registration in generic BDK context;
- replace TR181 -> TR098 callback dependency with shared services;
- integrate proxy/static ownership with common router;
- move sample/product flags to profile;
- build-test all supported BDK profiles.

## TR-181 strategy

Native MDM provider is allowed for large native TR-181 coverage.

Only cross-model semantics need shared service extraction.

## TR-098 strategy

Use:

```text
TR098 binding
 -> shared services
 -> BDK backend/MDM
```

where possible.

## Exit criteria

Broadcom profile builds and board-tests under the same resolved profile/model/runtime contracts as MTK.

---

# 10. Phase 8 - Multi-SDK productization and release

## Build matrix

As supported:

```text
MTK + TR098
MTK + TR181
MTK + dual
BDK + TR098
BDK + TR181
BDK + dual
```

Invalid/unsupported combinations must fail intentionally.

## CI/static gates

- compiler warnings/errors;
- ownership conflicts;
- missing module registration;
- profile dependency errors;
- schema/mapping drift;
- path coverage;
- unsafe input rules;
- source pruning test;
- apply/rollback/idempotency tests.

## Board matrix

At least one validated board per supported production profile.

## Export/prune

Release copy may contain one SDK/model subset, but development source remains multi-SDK.

## ABI/package rename

Only now consider:

```text
libtr098 -> libicwmp_dm
```

if worth the migration cost.

Rebuild every consumer and validate `DT_NEEDED`, headers, packages and upgrade behavior.

---

# 11. Immediate execution order

The next work should be:

```text
0.1 Build current 0066 HEAD
0.2 Board boot + Inform
0.3 Notification/ubus regression
0.4 GPV/GPN/SPV + input-contract tests
0.5 STUN MTK parity
0.6 Freeze baseline

1.1 Implement resolved profile
1.2 Implement compiled model capability
1.3 Centralize active model
1.4 Remove SDK-owned model selection

2.1 Clean build source ownership
2.2 Generalize router/provider ownership
2.3 Make claim conflicts a gate

3.1 Refactor ManagementServer vertical slice
3.2 Refactor DeviceInfo/Time
3.3 Prove transaction/service pattern

4.1 Port P6
4.2 Port P7 under product layer
4.3 Classify/port P8
4.4 Close old Add/Delete/lifecycle compat gaps

5. Full-C TR098 gate

6. TR181 MTK

7. BDK integration

8. Multi-SDK release
```

---

# 12. What should NOT be done next

Do not:

- start P6 immediately before closing 0066 runtime baseline;
- duplicate TR-098 source to create TR-181;
- make SDK runtime-selectable;
- put `X_AIS_*` permanently into generic MTK contracts;
- refactor every domain into services before proving one vertical slice;
- force the complete BDK native TR-181 tree through artificial service wrappers;
- rename SONAME/package during early semantic refactors;
- declare a phase complete from path-count/static compile alone.

---

# 13. Phase status after this audit

| Phase | Status now |
|---|---|
| Phase 0 - baseline closure | **IN PROGRESS**; 0065 boot/Inform passed, 0066 board recheck pending, STUN gap open |
| Phase 1 - profile/model resolver | **NOT STARTED** |
| Phase 2 - unified routing/build ownership | **PARTIAL foundation** via registry/native compat, target work not started |
| Phase 3 - service reference slices | **NOT STARTED** |
| Phase 4 - P6-P8 TR098 | **NOT STARTED** |
| Phase 5 - full-C production | **NOT STARTED** |
| Phase 6 - MTK TR181 | **NOT STARTED** |
| Phase 7 - BDK integration | **PROTOTYPE exists**, target refactor not started |
| Phase 8 - productization/release | apply/release tooling foundation exists; architecture gate not ready |

---

# 14. Gate to start Phase 1

Phase 1 should start only after Phase 0 produces a frozen baseline with:

```text
exact source/bundle
exact firmware
board test report
known issues
STUN status
notification status
GPV/GPN/SPV status
```

That baseline becomes the regression oracle for every architecture change that follows.
