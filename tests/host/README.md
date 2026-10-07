# Host test of icwmp_tr098d + libtr098 (MTK build)

Runs the real agent and data model library, built on a Linux host with
`--with-sdk=mtk`, against a test ACS, a stand-in for the easycwmp shell
library and the product's own external script — no SDK, no board.  It is how
the fixes 0067–0077 were found and checked; run it before sending a change
to the SDK build.

What it is not: the getters of `sdk/mtk/dm098` read UCI/ubus/`/proc` of the
host, so values are not the board's; the shell data model is
`fake_dm.py` (783 parameters of `docs/issue/tr098_coverage_matrix.tsv`), not
the easycwmp function library.  Board testing is still needed.

## Use

In a **throwaway** Linux container (setup overwrites `/etc/config`,
`/usr/sbin/icwmp`, … and starts ubusd), Debian/Ubuntu packages:
`build-essential cmake autoconf automake libtool pkg-config rsync git python3
busybox valgrind libjson-c-dev libcurl4-openssl-dev libssl-dev zlib1g-dev`.

```sh
tests/host/build.sh            # deps (pinned libubox/uci/ubus), microxml, libtr098, icwmp_tr098d
sudo tests/host/setup.sh --yes # fake CPE: UCI config (cwmp, easycwmp, stun), ubusd, stuncd stand-in, data model shell, external script
tests/host/run.sh all          # unit smoke notify rpc msrv stun ptime p6 fw p7 p7c p8 valgrind, exit 1 on any FAIL
```

Work files go to `$ICWMP_HOST_WORK` (default `/tmp/icwmp-host`); `build.sh`
again after editing the sources rebuilds what changed.

Checked environment (06/10): `docker run -v <repo>:/repo:ro ubuntu:24.04`
(json-c 0.17, Python 3.12, gcc 13) with the packages above plus
`ca-certificates`; `build.sh` clones the pinned deps from GitHub.  An Ubuntu
18.04 container with json-c 0.15 and libubox/uci/ubus of the SDK's `dl/`
built, but the agent died with SIGSEGV in its first session, on old commits
too: use 24.04.
`ICWMP_HOST_BIN=<other icwmp_tr098d>` runs the same tests on another build.

| `run.sh` | Checks |
|---|---|
| `unit` | `dmplatform_mtk.c` + `dmscript.c` alone: pruned GPV walk returns what one plain `get_value` did (both with and without the `*_list` commands), Inform cache; `dmcmd()` with 0 B … 3 MB of output |
| `smoke [N]` | N ACS sessions (GPV/GPN/SPV/SPA/GPA/Add/Delete/GetRPCMethods), agent alive after |
| `notify` | passive notification on `IGD.Services.`, still the shell's until P8c (fake_dm; 34 parameters, the count read from fake_dm; `IGD.Device.` went to C in P8a), every value one byte longer: all written back and sent in the next Inform (the old loop lost some). The smoke plan also sets attributes on `IGD.Firewall.`, an object answered in C (K21) |
| `rpc` | Download/Upload/ScheduleDownload with empty FileType, 3 time windows, a valid single window: faults where due, agent alive |
| `valgrind [N]` | memcheck over N sessions with Download and ubus load (`notify`, `dm`, `status`): 0 bytes definitely/indirectly lost, 0 errors |
| `soak [N]` | N sessions with ubus load, RSS / fd / thread / process samples every 30 s |
| `msrv` | the ACS sets URL, Username, PeriodicInformInterval/Time, CWMPRetryMinimumWaitInterval, ConnectionRequestUsername: the values must be in `easycwmp` (the product's config of record) and in icwmpd's mirror `cwmp` after the session; PeriodicInformInterval=0 faults. Known issue K1, fixed by 0078. A URL without "://" after a letter, digit or `_` faults and keeps the old URL (K13, 0082) |
| `stun` | the ACS sets the STUN leaves: they must land in `stun.@stun[0]`, raise `/tmp/stunclient_reload_needed` and run `/etc/init.d/stuncd reload` once; GPV of UDPConnectionRequestAddress/NATDetected reads `stun.@stun[0]`; STUNServerPort=70000 faults. K2, fixed by 0078. STUNServerAddress must pass the shell's `is_valid_domain` or `is_valid_ip`: `localhost`, `-stun.example.net`, `stun..example.net`, `192.0.2.256` fault; an IPv4, an IPv6 and a name with a trailing dot are taken (K13, 0082) |
| `ptime` | `easycwmp.@acs[0].periodic_time` as the product stores it (a dateTime): the next periodic Inform must fall on its minute and second. K10, fixed by 0079. Then SPVs of PeriodicInformTime that are not a real instant (29 February of a common year, 31 April, 24:17, :60, zone +15:00 or +0730) must fault and keep the stored time; a leap day and the unknown time are taken. K14, fixed by 0081 |
| `p6` | the P6 leaves ported to C (0090) on the product's configs: Account.Web.SessionMaxTime (hmxwslbackend, 300..3600), UserInterface.CarrierLocking (isplocking, section added when missing, digits only, LockingEnable 0/1 only), UserInterface.X_AIS_WebUserInfo (account, remoteaccess, clay; CurrentLanguage only checked against the list); one queued restart per service (stub init scripts); refused values fault and write nothing; CaptivePortal/BulkData/FAP answer when addressed and are absent from a whole-tree get; K20: a refusal at VALUESET (no isplocking package) reaches the ACS as 9003 with the parameter's 9002 |
| `fw` | Firewall in C (0092) on a firewall_clay of every kind: DisablePort, ServiceControl IPv4/IPv6 (packetfilter by ipversion), IPFilter (ipfilter2); AddObject defaults and instance numbers, an SPV setting IPVersion=6 and a v6 address together, Ingress/ServiceType/Protocol mappings, 6 refused values, an SPV whose second leaf faults at VALUESET leaving the first unwritten (K20), DeleteObject renumbering |
| `p7` | P7a/P7b in C (0094) on fixture configs (sections missing on purpose): UPnP, 3rdAgent (AP mode clears client_id), CPEagent (SecretKey equal to the shell's `printf \| dd \| openssl enc \| openssl base64 \| cut` with the test key `CPEAGENT_TEST_KEY`; the product build takes the real key from the product tree), AutoWifiScan, DHCPClient, DnsLandingPage, Isolation, SSH/Telnet, MeshAPI, DDNS, Conf, Logging (level lists, TFTP upload through a stand-in `tftp`, CleanLogging); every restart / `ubus call hni` queued once (stand-in init scripts and `ubus` on PATH); same values again change nothing; 18 values the shell refused fault |
| `p7c` | P7c in C (0095): UplinkSetup against a stand-in `hni.dualuplink` that commits dualuplink itself (mode3 Backup then Main in one SPV compares with what hni just wrote; a FAIL reply is 9003/9002 at VALUESET), UplinkStatus from a stand-in `blapi_cmd`; WiFiStatus reports from stand-in `mwctl`/`iw`, JSON equal to what the product's shell functions print for the same input under busybox (except the lease lookup and the escaped `"`, see `x_ais_wifistatus_mtk.c`); MLO groups and their `wifi reload` |
| `p8` | P8a in C (0097): `IGD.Device.IP.Interface` numbered by `network.<sec>.ip_int_instance` (given and committed on a GET, loopback out, an anonymous section is `@interface[n]`), Add (`if<n>`, static, auto 0) / Delete, Enable/IPv4Enable/IPv6Enable writing network + `wan.@entry[n]` and queuing hni_wan_reload / ifdown-ifup / the IPv4 flush (stand-in tools on PATH); `IPv6Enable` over every anonymous wan entry; `Device.DHCPv6.Server.Pool` (own numbering, a section without a Device.IP number is out, odhcpd options); `Device.IP.Diagnostics.TraceRoute` (default route device, the shared store, RouteHops listed only below RouteHops. while Complete, parsed from trace_results.txt); DOCSIS constants; 7 faults |

Logs: `$ICWMP_HOST_WORK/run/` (`acs.log`, `icwmpd.out`, `vg.log`,
`fake_dm.cmds` = every request the data model shell got).

## Files

| File | |
|---|---|
| `build.sh`, `setup.sh`, `run.sh`, `env.sh` | the above |
| `acs.py` | test ACS: Inform/TransferComplete, a fixed RPC plan per session, Connection Request (digest `cr`/`crpass`) to start the next |
| `fake_dm.py` | `icwmp_dm.sh --json-input` protocol over the coverage matrix; test hooks in its docstring |
| `harness/harness.c` | unit test, includes `sdk/mtk/dmplatform_mtk.c` with the engine stubbed: a new external symbol there needs a stub here |
| `harness/claims.py` | the `.paths` claims of the `dm098` modules of this tree |
| `harness/dmcmd_test.c` | `dmcmd()` output sizes |
