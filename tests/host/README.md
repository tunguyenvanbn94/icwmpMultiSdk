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
sudo tests/host/setup.sh --yes # fake CPE: UCI config, ubusd, data model shell, external script
tests/host/run.sh all          # unit smoke notify rpc valgrind, exit 1 on any FAIL
```

Work files go to `$ICWMP_HOST_WORK` (default `/tmp/icwmp-host`); `build.sh`
again after editing the sources rebuilds what changed.
`ICWMP_HOST_BIN=<other icwmp_tr098d>` runs the same tests on another build.

| `run.sh` | Checks |
|---|---|
| `unit` | `dmplatform_mtk.c` + `dmscript.c` alone: pruned GPV walk returns what one plain `get_value` did (both with and without the `*_list` commands), Inform cache; `dmcmd()` with 0 B … 3 MB of output |
| `smoke [N]` | N ACS sessions (GPV/GPN/SPV/SPA/GPA/Add/Delete/GetRPCMethods), agent alive after |
| `notify` | passive notification on a 100-parameter object, every value one byte longer: all 100 written back and sent in the next Inform (the old loop lost some) |
| `rpc` | Download/Upload/ScheduleDownload with empty FileType, 3 time windows, a valid single window: faults where due, agent alive |
| `valgrind [N]` | memcheck over N sessions with Download and ubus load (`notify`, `dm`, `status`): 0 bytes definitely/indirectly lost, 0 errors |
| `soak [N]` | N sessions with ubus load, RSS / fd / thread / process samples every 30 s |
| `msrv` | the ACS sets `ManagementServer.PeriodicInformInterval`: the value must still be in `cwmp` and `easycwmp` (the product's config of record) after the session. FAILS up to 0077 (known issue K1, [docs/plan/sync-main-dev.md](../../docs/plan/sync-main-dev.md) §3.1); not part of `all` until the fix |

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
