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
tests/host/run.sh all          # unit smoke notify rpc msrv stun ptime valgrind, exit 1 on any FAIL
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
| `notify` | passive notification on a 100-parameter object, every value one byte longer: all 100 written back and sent in the next Inform (the old loop lost some) |
| `rpc` | Download/Upload/ScheduleDownload with empty FileType, 3 time windows, a valid single window: faults where due, agent alive |
| `valgrind [N]` | memcheck over N sessions with Download and ubus load (`notify`, `dm`, `status`): 0 bytes definitely/indirectly lost, 0 errors |
| `soak [N]` | N sessions with ubus load, RSS / fd / thread / process samples every 30 s |
| `msrv` | the ACS sets URL, Username, PeriodicInformInterval/Time, CWMPRetryMinimumWaitInterval, ConnectionRequestUsername: the values must be in `easycwmp` (the product's config of record) and in icwmpd's mirror `cwmp` after the session; PeriodicInformInterval=0 faults. Known issue K1, fixed by 0078. A URL without "://" after a letter, digit or `_` faults and keeps the old URL (K13, 0082) |
| `stun` | the ACS sets the STUN leaves: they must land in `stun.@stun[0]`, raise `/tmp/stunclient_reload_needed` and run `/etc/init.d/stuncd reload` once; GPV of UDPConnectionRequestAddress/NATDetected reads `stun.@stun[0]`; STUNServerPort=70000 faults. K2, fixed by 0078. STUNServerAddress must pass the shell's `is_valid_domain` or `is_valid_ip`: `localhost`, `-stun.example.net`, `stun..example.net`, `192.0.2.256` fault; an IPv4, an IPv6 and a name with a trailing dot are taken (K13, 0082) |
| `ptime` | `easycwmp.@acs[0].periodic_time` as the product stores it (a dateTime): the next periodic Inform must fall on its minute and second. K10, fixed by 0079. Then SPVs of PeriodicInformTime that are not a real instant (29 February of a common year, 31 April, 24:17, :60, zone +15:00 or +0730) must fault and keep the stored time; a leap day and the unknown time are taken. K14, fixed by 0081 |

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
