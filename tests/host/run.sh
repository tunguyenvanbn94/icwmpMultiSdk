#!/bin/sh
# Host test of icwmp_tr098d + libtr098 (MTK build) against a test ACS.
#   run.sh unit            data model unit test (pruned walk, Inform cache) + dmcmd sizes
#   run.sh smoke [N]       N sessions (default 5), the agent must survive
#   run.sh notify          value-change check: 100 values change length, all reported
#   run.sh rpc             malformed and valid transfer RPCs, the agent must survive
#   run.sh valgrind [N]    memcheck over N sessions with downloads and ubus load
#   run.sh soak [N]        N sessions (default 300), RSS/fd/thread samples
#   run.sh msrv            ACS writes of ManagementServer.* land in easycwmp and cwmp (K1)
#   run.sh stun            STUN leaves on stun.@stun[0], reload flag, stuncd reload (K2)
#   run.sh ptime           PeriodicInformTime dateTime aligns the periodic Inform (K10)
#   run.sh all             unit smoke notify rpc msrv stun ptime valgrind
# Needs build.sh, then setup.sh --yes (root, throwaway container).
. "$(dirname "$0")/env.sh"

fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

stop() {
	for f in "$RUN/acs.pid" "$RUN/load.pid" "$RUN/icwmpd.pid"; do
		[ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null
		rm -f "$f"
	done
	pkill -x icwmp_tr098d 2>/dev/null
	sleep 1
}

# start <sessions> <acs args> [wrapper...]
start() {
	n=$1; acsargs=$2; shift 2
	stop
	rm -f /var/log/icwmpd.log /etc/icwmpd/.icwmpd_backup_session.xml /etc/tr098/.dm_enabled_notify "$RUN/fake_dm.cmds"
	echo 1 > "$RUN/epoch"
	python3 "$HOST_DIR/acs.py" --sessions "$n" $acsargs > "$RUN/acs.log" 2>&1 &
	echo $! > "$RUN/acs.pid"
	sleep 0.5
	"$@" "$BIN" -b > "$RUN/icwmpd.out" 2>&1 &
	echo $! > "$RUN/icwmpd.pid"
}

# what value_monitoring and a WebUI/CLI do over ubus, every $1 s
load() {
	( while [ -f "$RUN/load.on" ]; do
		$UBUS call tr069 notify >/dev/null 2>&1
		$UBUS call tr069 status >/dev/null 2>&1
		$UBUS call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo."}' >/dev/null 2>&1
		$UBUS call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.WANDevice."}' >/dev/null 2>&1
		$UBUS call tr069 dm '{"cmd":"names","path":"InternetGatewayDevice.","next_level":true}' >/dev/null 2>&1
		$UBUS call tr069 dm '{"cmd":"inform","path":""}' >/dev/null 2>&1
		sleep "$1"
	done ) &
	echo $! > "$RUN/load.pid"
}

wait_done() {	# wait_done <seconds>
	i=0
	while [ $i -lt "$1" ] && ! grep -q DONE "$RUN/acs.log"; do sleep 1; i=$((i+1)); done
	grep -q DONE "$RUN/acs.log"
}

alive() { kill -0 "$(cat "$RUN/icwmpd.pid" 2>/dev/null)" 2>/dev/null; }

do_unit() {
	H=$HOST_DIR/harness
	CF="-std=gnu11 -g -Wno-comment -DDM_SDK_MTK=1 -DDM_PLATFORM_MTK=1 -DDM_MTK_SCRIPT_COMPAT=1 -DCUSTOM_PREFIX=\"X_HNI_\" -DTR098_VERSION=\"3\""
	INC="-I$LIB_SRC -I$LIB_SRC/tr098 -I$LIB_SRC/tr098/common -I$LIB_SRC/upnp -I$LIB_SRC/sdk/mtk -I$LIB_SRC/sdk/mtk/dm098 -I$PREFIX/include -I$RUN"
	python3 "$H/claims.py" "$LIB_SRC" "$PREFIX/include" > "$RUN/claims.txt" || { bad "unit: claims"; return; }
	sed -n '/^static int path_match/,/^}/p' "$LIB_SRC/dm_registry.c" > "$RUN/path_match.inc"
	printf '#!/bin/sh\nFAKE_DM_MATRIX=%s exec python3 %s/fake_dm.py "$@"\n' "$MATRIX" "$HOST_DIR" > "$RUN/fake_dm.sh"
	if ! (cd "$LIB_SRC" && gcc $CF $INC -fsanitize=address,undefined "$H/harness.c" sdk/mtk/compat/dmscript.c \
		-o "$RUN/harness" -L"$PREFIX/lib" -ljson-c -lpthread) > "$RUN/harness.build" 2>&1; then
		tail -5 "$RUN/harness.build"; bad "unit: harness build"; return
	fi
	for mode in list nolist; do
		if [ $mode = nolist ]; then x=1; else x=; fi
		if FAKE_DM_NO_LIST=$x ASAN_OPTIONS=detect_leaks=0 "$RUN/harness" "$RUN/fake_dm.sh" "$RUN/claims.txt" > "$RUN/harness.$mode" 2>&1 &&
		   FAKE_DM_NO_LIST=$x FAKE_DM_INSTANCE_INFORM=1 ASAN_OPTIONS=detect_leaks=0 "$RUN/harness" "$RUN/fake_dm.sh" "$RUN/claims.txt" instance >> "$RUN/harness.$mode" 2>&1; then
			pass "unit: pruned GPV walk + Inform cache, script $mode ($(grep -m1 'GPV (root)' "$RUN/harness.$mode" | sed 's/  */ /g'))"
		else
			grep FAIL "$RUN/harness.$mode" | head -5; bad "unit: script $mode"
		fi
	done
	gcc -o "$RUN/dmcmd_test" "$H/dmcmd_test.c" -L"$PREFIX/lib" -ltr098 -Wl,-rpath,"$PREFIX/lib" &&
	if timeout 60 "$RUN/dmcmd_test" > "$RUN/dmcmd.out" 2>&1 && ! grep -q NO "$RUN/dmcmd.out"; then
		pass "unit: dmcmd 0 B .. 3 MB output, no hang, no fd leak"
	else
		cat "$RUN/dmcmd.out"; bad "unit: dmcmd"
	fi
}

do_smoke() {
	start "${1:-5}" ""
	if wait_done 120 && alive; then pass "smoke: $(tail -1 "$RUN/acs.log")"; else bad "smoke: $(tail -1 "$RUN/acs.log")"; fi
	stop
}

do_notify() {
	start 1 "--readonly"
	wait_done 60 || { bad "notify: first session"; stop; return; }
	sleep 1
	echo 10 > "$RUN/epoch"		# every value one byte longer
	$UBUS call tr069 notify >/dev/null 2>&1; sleep 2
	v10=$(grep -c '"value": "v10:InternetGatewayDevice.Firewall' /etc/tr098/.dm_enabled_notify)
	broken=$(python3 -c "
import json
n = 0
for l in open('/etc/tr098/.dm_enabled_notify'):
    try: json.loads(l)
    except ValueError: n += 1
print(n)")
	python3 - <<'PY'
import urllib.request
pm = urllib.request.HTTPPasswordMgrWithDefaultRealm(); pm.add_password(None, "http://127.0.0.1:7547/", "cr", "crpass")
urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(pm)).open("http://127.0.0.1:7547/", timeout=10).read()
PY
	i=0; while [ $i -lt 30 ] && ! grep -q "^session 2" "$RUN/acs.log"; do sleep 1; i=$((i+1)); done
	inform=$(grep "^session 2" "$RUN/acs.log" | sed 's/.*firewall_params=\([0-9]*\).*/\1/')
	if [ "$v10" = 100 ] && [ "$broken" = 0 ] && [ "$inform" = 100 ]; then
		pass "notify: 100/100 changes kept and sent in the Inform"
	else
		bad "notify: file $v10/100 updated, $broken broken lines, Inform carried ${inform:-0}/100"
	fi
	stop
}

do_rpc() {
	for c in schedule_download_ok:0 schedule_download_3win:1 bad_download:1 bad_upload:1 bad_schedule_download:1; do
		plan=${c%:*}; want=${c#*:}
		start 1 "--plan $plan"
		wait_done 30; sleep 1
		got=$(grep -c "Preparing the Fault message" /var/log/icwmpd.log)
		if alive && [ "$got" = "$want" ]; then pass "rpc: $plan (faults $got)"; else bad "rpc: $plan (alive: $(alive && echo yes || echo NO), faults $got, want $want)"; fi
		stop
	done
}

do_valgrind() {
	command -v valgrind >/dev/null || { bad "valgrind not installed"; return; }
	rm -f "$RUN"/vg.log
	start "${1:-12}" "--download-every 3" valgrind --leak-check=full --errors-for-leak-kinds=definite \
		--child-silent-after-fork=yes --num-callers=25 --log-file="$RUN/vg.log"
	touch "$RUN/load.on"; load 1
	wait_done 600
	rm -f "$RUN/load.on"; sleep 3
	kill -TERM "$(cat "$RUN/icwmpd.pid")"; sleep 10
	stop
	def=$(sed -n 's/.*definitely lost: \([0-9,]*\) bytes.*/\1/p' "$RUN/vg.log")
	ind=$(sed -n 's/.*indirectly lost: \([0-9,]*\) bytes.*/\1/p' "$RUN/vg.log")
	err=$(sed -n 's/.*ERROR SUMMARY: \([0-9]*\) errors.*/\1/p' "$RUN/vg.log")
	if [ "$def" = 0 ] && [ "$ind" = 0 ] && [ "$err" = 0 ]; then
		pass "valgrind: $(grep DONE "$RUN/acs.log"), 0 lost, 0 errors"
	else
		bad "valgrind: definitely ${def:-?} indirectly ${ind:-?} errors ${err:-?}, see $RUN/vg.log"
	fi
}

# value of one leaf as "ubus call tr069 dm get" returns it
dm_value() {
	$UBUS call tr069 dm "{\"cmd\":\"get\",\"path\":\"$1\"}" 2>/dev/null | python3 -c "
import json, sys
try: print(json.load(sys.stdin)['parameters'][0]['value'])
except Exception: print('<none>')"
}

# expect <what> <got> <want>: one line per mismatch, sets bad_n
expect() {
	if [ "$2" != "$3" ]; then echo "  $1: '$2', want '$3'"; bad_n=$((bad_n+1)); fi
}

save_cfg() { for c in "$@"; do cp "/etc/config/$c" "$RUN/$c.saved"; done; }
restore_cfg() { for c in "$@"; do cp "$RUN/$c.saved" "/etc/config/$c"; done; }

# The ACS writes ManagementServer.*: the values must land in easycwmp, the
# product's config of record (WebUI, next boot), and icwmpd's mirror cwmp
# must carry them after the session.  Up to 0077 libtr098 wrote cwmp only
# and the end-of-session mirror easycwmp -> cwmp put the old values back
# (known issue K1, docs/plan/sync-main-dev.md).
do_msrv() {
	save_cfg easycwmp cwmp
	start 1 "--set InternetGatewayDevice.ManagementServer.URL=http://127.0.0.1:18080/acs-msrv
		--set InternetGatewayDevice.ManagementServer.Username=acs-msrv
		--set InternetGatewayDevice.ManagementServer.PeriodicInformInterval=3600
		--set InternetGatewayDevice.ManagementServer.PeriodicInformTime=2026-01-01T00:17:00Z
		--set InternetGatewayDevice.ManagementServer.CWMPRetryMinimumWaitInterval=7
		--set InternetGatewayDevice.ManagementServer.ConnectionRequestUsername=cr-msrv"
	wait_done 60; sleep 2
	bad_n=0
	expect "easycwmp url" "$(uci -q get easycwmp.@acs[0].url)" "http://127.0.0.1:18080/acs-msrv"
	expect "easycwmp username" "$(uci -q get easycwmp.@acs[0].username)" "acs-msrv"
	expect "easycwmp periodic_interval" "$(uci -q get easycwmp.@acs[0].periodic_interval)" "3600"
	expect "easycwmp periodic_time" "$(uci -q get easycwmp.@acs[0].periodic_time)" "2026-01-01T00:17:00Z"
	expect "easycwmp cwmpretryinterval" "$(uci -q get easycwmp.@acs[0].cwmpretryinterval)" "7"
	expect "easycwmp local username" "$(uci -q get easycwmp.@local[0].username)" "cr-msrv"
	expect "cwmp url" "$(uci -q get cwmp.acs.url)" "http://127.0.0.1:18080/acs-msrv"
	expect "cwmp userid" "$(uci -q get cwmp.acs.userid)" "acs-msrv"
	expect "cwmp periodic_inform_interval" "$(uci -q get cwmp.acs.periodic_inform_interval)" "3600"
	expect "cwmp retry_min_wait_interval" "$(uci -q get cwmp.acs.retry_min_wait_interval)" "7"
	expect "cwmp cpe userid" "$(uci -q get cwmp.cpe.userid)" "cr-msrv"
	expect "GPV URL" "$(dm_value InternetGatewayDevice.ManagementServer.URL)" "http://127.0.0.1:18080/acs-msrv"
	expect "GPV PeriodicInformTime" "$(dm_value InternetGatewayDevice.ManagementServer.PeriodicInformTime)" "2026-01-01T00:17:00Z"
	alive || expect "agent" "dead" "alive"
	stop
	restore_cfg easycwmp cwmp
	start 1 "--set InternetGatewayDevice.ManagementServer.PeriodicInformInterval=0"
	wait_done 30; sleep 1
	expect "PeriodicInformInterval=0 faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
	expect "easycwmp periodic_interval after a fault" "$(uci -q get easycwmp.@acs[0].periodic_interval)" "86400"
	stop
	# K13: the shell refused a URL without "://" after [a-zA-Z0-9_]
	url0=$(uci -q get easycwmp.@acs[0].url)
	for v in acs.example.net:7547/acs "://acs.example.net/acs"; do
		start 1 "--set InternetGatewayDevice.ManagementServer.URL=$v"
		wait_done 30; sleep 1
		expect "URL=$v faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		expect "easycwmp url after $v" "$(uci -q get easycwmp.@acs[0].url)" "$url0"
		stop
	done
	restore_cfg easycwmp cwmp
	if [ $bad_n = 0 ]; then pass "msrv: ACS writes of ManagementServer.* kept in easycwmp and cwmp, range fault"; else bad "msrv: $bad_n mismatches above"; fi
}

# STUN leaves are the product's stunclient: stun.@stun[0], the reload flag
# of the shell setter and one stuncd reload at the end of the session (K2).
do_stun() {
	save_cfg stun
	rm -f /tmp/stunclient_reload_needed "$RUN/stuncd.calls"
	uci set stun.@stun[0].udpcontnreqaddr=192.0.2.7:3479; uci set stun.@stun[0].natdetect=1; uci commit stun
	start 1 "--set InternetGatewayDevice.ManagementServer.STUNEnable=1
		--set InternetGatewayDevice.ManagementServer.STUNServerAddress=stun.example.net
		--set InternetGatewayDevice.ManagementServer.STUNServerPort=3479
		--set InternetGatewayDevice.ManagementServer.STUNUsername=su
		--set InternetGatewayDevice.ManagementServer.STUNPassword=sp
		--set InternetGatewayDevice.ManagementServer.STUNMinimumKeepAlivePeriod=20
		--set InternetGatewayDevice.ManagementServer.STUNMaximumKeepAlivePeriod=-1"
	wait_done 60; sleep 2
	bad_n=0
	expect "stun_enable" "$(uci -q get stun.@stun[0].stun_enable)" "1"
	expect "serveraddress" "$(uci -q get stun.@stun[0].serveraddress)" "stun.example.net"
	expect "serverport" "$(uci -q get stun.@stun[0].serverport)" "3479"
	expect "username" "$(uci -q get stun.@stun[0].username)" "su"
	expect "password" "$(uci -q get stun.@stun[0].password)" "sp"
	expect "min_keepalive" "$(uci -q get stun.@stun[0].min_keepalive)" "20"
	expect "max_keepalive" "$(uci -q get stun.@stun[0].max_keepalive)" "-1"
	expect "reload flag" "$([ -f /tmp/stunclient_reload_needed ] && echo yes)" "yes"
	expect "stuncd calls" "$(cat "$RUN/stuncd.calls" 2>/dev/null | tr '\n' ' ')" "reload "
	expect "GPV UDPConnectionRequestAddress" "$(dm_value InternetGatewayDevice.ManagementServer.UDPConnectionRequestAddress)" "192.0.2.7:3479"
	expect "GPV NATDetected" "$(dm_value InternetGatewayDevice.ManagementServer.NATDetected)" "1"
	expect "GPV STUNPassword" "$(dm_value InternetGatewayDevice.ManagementServer.STUNPassword)" ""
	expect "GPV STUNServerPort" "$(dm_value InternetGatewayDevice.ManagementServer.STUNServerPort)" "3479"
	expect "cwmp_stun untouched" "$(uci -q get cwmp_stun.stun.server_address)" ""
	alive || expect "agent" "dead" "alive"
	stop
	start 1 "--set InternetGatewayDevice.ManagementServer.STUNServerPort=70000"
	wait_done 30; sleep 1
	expect "STUNServerPort=70000 faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
	expect "serverport after a fault" "$(uci -q get stun.@stun[0].serverport)" "3479"
	stop
	# K13: is_valid_domain || is_valid_ip of the shell in front of serveraddress
	for v in localhost -stun.example.net stun..example.net 192.0.2.256; do
		start 1 "--set InternetGatewayDevice.ManagementServer.STUNServerAddress=$v"
		wait_done 30; sleep 1
		expect "STUNServerAddress=$v faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		expect "serveraddress after $v" "$(uci -q get stun.@stun[0].serveraddress)" "stun.example.net"
		stop
	done
	for v in 192.0.2.10 2001:db8::10 stun.example.net.; do
		start 1 "--set InternetGatewayDevice.ManagementServer.STUNServerAddress=$v"
		wait_done 30; sleep 1
		expect "STUNServerAddress=$v faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
		expect "serveraddress after $v" "$(uci -q get stun.@stun[0].serveraddress)" "$v"
		stop
	done
	restore_cfg stun
	if [ $bad_n = 0 ]; then pass "stun: STUN leaves on stun.@stun[0], reload flag, one stuncd reload, range fault"; else bad "stun: $bad_n mismatches above"; fi
}

# PeriodicInformTime as easycwmp stores it (a dateTime) must align the
# periodic Inform on that instant.  Up to 0078 config.c read it with atol():
# "2026-01-01T00:17:00Z" -> 2026 s, Informs at hh:33:46 (K10).
do_ptime() {
	save_cfg easycwmp cwmp
	uci set easycwmp.@acs[0].periodic_enable=1
	uci set easycwmp.@acs[0].periodic_interval=3600
	uci set easycwmp.@acs[0].periodic_time=2026-01-01T00:17:00Z
	uci commit easycwmp
	start 1 "--readonly"
	wait_done 60; sleep 2
	next=$($UBUS call tr069 status 2>/dev/null | python3 -c "
import json, sys
try: print(json.load(sys.stdin)['next_session']['start_time'])
except Exception: print('')")
	off=$(date +%z)
	stop
	# K14: an SPV of a dateTime that is not a real instant faults and keeps
	# the stored time; a leap day and the unknown time are taken.  Up to 0080
	# only the shape was checked: 2026-02-29 or 24:17:00 was stored.
	bad_n=0
	for v in 2026-02-29T00:17:00Z 2026-04-31T00:17:00Z 2026-01-01T24:17:00Z 2026-01-01T00:60:00Z \
		2026-01-01T00:17:60Z 2026-01-01T00:17:00+15:00 2026-01-01T00:17:00+0730; do
		start 1 "--set InternetGatewayDevice.ManagementServer.PeriodicInformTime=$v"
		wait_done 30; sleep 1
		expect "$v faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		expect "periodic_time after $v" "$(uci -q get easycwmp.@acs[0].periodic_time)" "2026-01-01T00:17:00Z"
		stop
	done
	for v in 2028-02-29T00:17:00Z 0001-01-01T00:00:00Z; do
		start 1 "--set InternetGatewayDevice.ManagementServer.PeriodicInformTime=$v"
		wait_done 30; sleep 1
		expect "$v faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
		expect "periodic_time after $v" "$(uci -q get easycwmp.@acs[0].periodic_time)" "$v"
		stop
	done
	restore_cfg easycwmp cwmp
	if [ $bad_n = 0 ]; then pass "ptime: SPV of a time that is not a real instant faults, stored time kept (K14)"; else bad "ptime: $bad_n mismatches above (K14)"; fi
	# minutes:seconds of the next Inform; the zone of the host shifts whole
	# hours in the usual case, check it is one
	case "$off" in *00) ;; *) bad "ptime: host zone $off is not whole hours, cannot check"; return ;; esac
	ms=$(echo "$next" | cut -c15-19)
	if [ "$ms" = "17:00" ]; then pass "ptime: next periodic Inform $next (aligned on :17:00)"; else bad "ptime: next periodic Inform '$next', want minute:second 17:00"; fi
}

do_soak() {
	start "${1:-300}" ""
	touch "$RUN/load.on"; load 2
	sleep 1
	P=$(cat "$RUN/icwmpd.pid")
	f0=$(awk '/^processes/{print $2}' /proc/stat)
	while ! grep -q DONE "$RUN/acs.log" && kill -0 "$P" 2>/dev/null; do
		echo "sessions=$(grep -c '^session' "$RUN/acs.log") rss=$(awk '/VmRSS/{print $2}' /proc/$P/status)kB fds=$(ls /proc/$P/fd | wc -l) threads=$(awk '/Threads/{print $2}' /proc/$P/status) forks=$(( $(awk '/^processes/{print $2}' /proc/stat) - f0 ))"
		sleep 30
	done
	rm -f "$RUN/load.on"
	if alive; then pass "soak: $(tail -1 "$RUN/acs.log")"; else bad "soak: agent died"; fi
	stop
}

case "$1" in
	unit) do_unit ;;
	smoke) do_smoke "$2" ;;
	notify) do_notify ;;
	rpc) do_rpc ;;
	valgrind) do_valgrind "$2" ;;
	soak) do_soak "$2" ;;
	msrv) do_msrv ;;
	stun) do_stun ;;
	ptime) do_ptime ;;
	all) do_unit; do_smoke; do_notify; do_rpc; do_msrv; do_stun; do_ptime; do_valgrind ;;
	*) sed -n '2,/^# Needs/p' "$0"; exit 1 ;;
esac
exit $fail
