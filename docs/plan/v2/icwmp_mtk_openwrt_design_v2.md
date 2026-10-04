# ICWMP MTK/OpenWrt Architecture and Migration Design v2

> **Nguồn:** bản rà soát ngày 2026-09-30 dựng trên `main` (tới patch 0066), giữ nguyên để truy vết.
> Nó chưa biết 0067–0077 và test host của `dev`. Điểm đã đổi, phát hiện mới (K1–K9) và lộ trình
> đã sửa nằm ở [sync-main-dev.md](../sync-main-dev.md); trạng thái hiện hành ở
> [implementation-status.json](../../issue/implementation-status.json). Khi mâu thuẫn, hai file đó thắng.

**Audit baseline:** 2026-09-30  
**Primary target:** MediaTek/Airoha OpenWrt, HP2236B/config_7583 family  
**Current production model target:** TR-098

---

# 1. Goal

The immediate goal is not to make MTK a generic runtime plugin.

The goal is to produce a stable MTK/OpenWrt ICWMP implementation that:

1. preserves EasyCwmp product behavior;
2. migrates the data model from shell to native C safely;
3. provides complete reachable TR-098 coverage;
4. establishes reusable domain contracts so TR-181 can be added later;
5. allows the same upper architecture to be reused by another SDK.

For MTK firmware:

```text
SDK = MTK
```

is fixed at build time.

---

# 2. Current MTK flow

Current transitional flow:

```text
ACS
 |
 v
icwmpd
 |
 +--> app sdk/mtk
 |      |
 |      +--> easycwmp <-> cwmp config synchronization
 |
 v
libicwmp_dm / ABI libtr098
 |
 v
TR-098 registry
 |
 +--> claimed native path
 |       |
 |       v
 |    sdk/mtk/dm098/*.c
 |       |
 |       +--> UCI
 |       +--> ubus
 |       +--> proc/sys
 |       +--> product commands/services
 |
 +--> unclaimed path
         |
         v
      EasyCwmp shell compatibility
```

This hybrid migration flow should remain available until full TR-098 parity is reached.

---

# 3. What has already been ported

Current inventory:

```text
total historical parameter inventory      783
reachable inventory                       778
native C P1-P5                            458
remaining reachable P6-P8                 320
```

Current native coverage:

```text
P1   DeviceInfo / Time / ManagementServer          65
P2   LAN / DHCP / Hosts / Mesh                     70
P3   WLANConfiguration                             67
P4   WAN / NAT / IPv6 / PortMapping / ServiceList 173
P5   Diagnostics / Layer3Forwarding / hidden       83
                                                   ---
                                                   458
```

Five P5 inventory leaves are documented as unreachable.

Important exception:

- WAN connection leaf coverage is native;
- AddObject/DeleteObject for connection objects still uses compatibility behavior.

Therefore "458 C parameters" does not mean "all corresponding object lifecycle behavior is native".

---

# 4. Runtime validation status

## 4.1 What has been demonstrated on board

With the 0065 bundle:

- daemon passed full initial DM tree traversal;
- `tr069` ubus object registered;
- daemon status reported up;
- initial Inform to ACS succeeded.

This is a real runtime milestone.

## 4.2 What 0066 fixes

A subsequent board test found a deadlock:

- notification thread locked `mutex_session_send`;
- enabled-notification file could not be opened;
- error path returned without unlocking;
- `/etc/tr098` was not created by MTK integration.

0066:

- fixes unlock/cleanup;
- ensures the state directory exists;
- replaces cross-filesystem `rename()` behavior with copy logic.

0066 passed static/cross-compile/apply checks but has not yet been rebuilt and revalidated on board as the complete HEAD bundle.

Therefore the correct status is:

```text
0065 board: startup + first Inform PASS
HEAD/0066: static/cross-build checks PASS
HEAD/0066 board regression: NOT DONE
```

Do not call current HEAD production-stable yet.

---

# 5. Critical ManagementServer/STUN gap

This must be closed before freezing the MTK TR-098 baseline.

EasyCwmp product behavior uses:

```text
stun.@stun[0].*
stuncd
/tmp/stunclient_reload_needed
```

Current shared C ManagementServer module uses another configuration/service convention:

```text
cwmp_stun
icwmp_stund
```

These are not behaviorally equivalent on the MTK product.

Affected TR-098 leaves include:

```text
UDPConnectionRequestAddress
STUNEnable
STUNServerAddress
STUNServerPort
STUNUsername
STUNPassword
STUNMaximumKeepAlivePeriod
STUNMinimumKeepAlivePeriod
NATDetected
```

Practical impact:

- product `stuncd` can receive UDP Connection Requests when configured outside ICWMP;
- ACS control/visibility of STUN through current C ManagementServer is incomplete/wrong;
- UDPConnectionRequestAddress may not be reported correctly.

## Immediate solution

Before broad service refactor, implement the smallest MTK behavioral override necessary for these STUN leaves.

Then, in the service-refactor phase, move the behavior behind the shared ManagementServer/STUN contract.

This avoids blocking baseline validation on a large architecture rewrite.

---

# 6. MTK configuration-of-record

The app integration currently mirrors fields between:

```text
easycwmp
and
cwmp
```

This is intentional transitional integration.

The design must explicitly define ownership for each field:

| Category | Config of record | Mirror/cache | Apply owner |
|---|---|---|---|
| ACS URL/credentials | product `easycwmp` during transition | `cwmp` | MTK app/service |
| Periodic Inform | product `easycwmp` during transition | `cwmp` | MTK app/service |
| Connection Request settings | explicit per-field mapping | `cwmp` where required | ICWMP |
| STUN | product `stun` package | not `cwmp_stun` on this product | STUN service |
| ICWMP-private runtime options | `cwmp` | none | ICWMP |

Do not assume every ManagementServer field belongs to the same UCI package.

---

# 7. Input compatibility

Patch 0061 introduced the legacy EasyCwmp input contract for native paths.

This is important because historical shell behavior performs safety/type filtering before setters.

Native C must preserve behavior for:

- dangerous shell metacharacters;
- boolean spelling;
- signed/unsigned integer rules;
- IPv4/IPv6 validation;
- empty/space-only input behavior;
- exact fault mapping where the old product depends on it.

Current implementation is statically verified but still needs board/SPV validation.

Required board cases should include:

```text
";" / shell metacharacters
empty string
true / false
yes / on
negative unsigned integer
invalid IPv4
valid/invalid IPv6
```

---

# 8. Target MTK flow after architecture cleanup

```text
ACS
 |
 v
ICWMP protocol/session
 |
 v
central model runtime
 |
 +--> TR098 binding
 |       |
 |       v
 |    domain service
 |       |
 |       v
 |    MTK backend
 |
 +--> TR181 binding              [future]
         |
         v
      same service where semantics match
         |
         v
      MTK backend
```

During migration, TR-098 also has:

```text
unowned path -> EasyCwmp compatibility provider
```

The compatibility provider disappears only after reachable baseline coverage and lifecycle behavior are equivalent.

---

# 9. MTK backend responsibilities

Backend code may use MTK/OpenWrt mechanisms:

- UCI;
- ubus;
- `/proc`;
- `/sys`;
- `wlanconfig`;
- `iwpriv`;
- product service commands;
- HNI/Airoha APIs.

But these details should not leak into reusable TR-098/TR-181 bindings.

Backend API should express device semantics rather than CWMP path strings where reasonable.

Example:

```text
wifi_get_radios()
wifi_get_ssids()
wifi_get_associated_devices()
wifi_stage_security_update()
wifi_commit()
```

rather than:

```text
get_InternetGatewayDevice_LANDevice_1_WLANConfiguration_i_...
```

---

# 10. Migration ownership rules

## Native claim

Claim a whole subtree only when its required behavior is truly covered.

If a module only replaces selected leaves, claim exact leaf/instance patterns.

## Compatibility fallback

Unclaimed TR-098 paths continue to use EasyCwmp shell.

## Dynamic object lifecycle

Parameter ownership and object lifecycle ownership are separate concerns.

For every dynamic object verify:

- browse/enumeration;
- stable instance behavior;
- AddObject;
- DeleteObject;
- persistent mapping;
- notification;
- reboot behavior.

Do not mark a domain full-C based only on leaf count.

---

# 11. Product/operator extensions

`X_AIS_*` is operator/product schema, not generic MTK hardware behavior.

Target:

```text
MTK backend
   +
AIS product bindings/policy
```

P7 should therefore establish a product extension layer instead of adding all `X_AIS_*` code permanently to a generic MTK namespace.

The exact ACS path and old behavior must be preserved.

---

# 12. Remaining TR-098 work

## P6 - 97 parameters

Includes:

- Firewall;
- UserInterface;
- CaptivePortal;
- Account/User;
- XMPP;
- remaining root/hidden behavior.

Main risk:

- `firewall_clay`;
- product service side effects;
- hidden/addressed-only objects.

## P7 - 79 parameters

Operator-specific `X_AIS_*`.

Main rule:

- preserve exact names/types/side effects;
- product-profile gating.

## P8 - 144 parameters

Includes:

- StorageService;
- STBService;
- DOCSIS;
- LTE;
- hybrid `InternetGatewayDevice.Device.*`.

Before implementing, classify each branch:

```text
required on HP2236B?
reachable?
used by ACS?
backend exists?
product optional?
legacy only?
```

Do not fake unsupported hardware.

The hybrid paths must remain exactly as provisioned by the historical ACS contract.

---

# 13. TR-181 plan for MTK

Do not copy the TR-098 C tree.

TR-181 should be added domain by domain after shared service contracts are proven.

Suggested order:

```text
Device.DeviceInfo
Device.ManagementServer
Device.Time
Device.IP
Device.Ethernet
Device.WiFi
Device.DHCPv4
Device.DNS
Device.Routing
Diagnostics
product extensions as required
```

A semantic mapping manifest should record:

```text
semantic ID
TR098 path
TR181 path
type
access
instance identity
SDK capability
implementation status
test status
```

`mapping_todo` is better than inventing a false counterpart.

---

# 14. MTK Definition of Done

## For a parameter

- exact path;
- type/access;
- getter;
- setter if writable;
- legacy input validation;
- correct fault;
- notification;
- persistence;
- side effect;
- transaction behavior;
- board or integration evidence.

## For a dynamic object

All parameter criteria plus:

- enumeration;
- stable instance;
- Add/Delete if supported;
- reboot persistence;
- lifecycle regression.

## For full TR-098 MTK

- all required/reachable profile paths are native C;
- compat-off build works;
- STUN and Connection Request are equivalent;
- notification stable;
- ACS regression passes;
- board soak passes;
- unsupported/hardware-specific branches are explicitly profile-gated;
- no hidden dependency on EasyCwmp DM shell remains.

