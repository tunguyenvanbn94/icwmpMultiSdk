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
#   run.sh p6              P6 leaves in C: Account, CarrierLocking, X_AIS_WebUserInfo, hidden root objects
#   run.sh fw              P6e Firewall in C: add/set/delete, faults, VALUESET revert
#   run.sh p7              P7a/b operator X_AIS_* in C: writes, queued restarts, faults
#   run.sh p7c             P7c UplinkSetup (hni.dualuplink), WiFiStatus reports, MLO
#   run.sh p8              P8a Device.IP (numbering, add/delete), DHCPv6 pools, TraceRoute hops, DOCSIS
#   run.sh all             unit smoke notify rpc msrv stun ptime p6 fw p7 p7c p8 valgrind
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
	# the object of the test is IGD.Services., still answered by the shell
	# (fake_dm) -- IGD.Device. went to C in P8a; its parameter count comes
	# from fake_dm itself
	want=$(printf '%s\n' '{"cmd":"get_value","param":"InternetGatewayDevice.Services."}' '{"cmd":"exit"}' |
		FAKE_DM_MATRIX=$MATRIX FAKE_DM_EPOCH=$RUN/epoch python3 "$HOST_DIR/fake_dm.py" | grep -c '"value"')
	v10=$(grep -c '"value": "v10:InternetGatewayDevice.Services' /etc/tr098/.dm_enabled_notify)
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
	inform=$(grep "^session 2" "$RUN/acs.log" | sed 's/.*device_params=\([0-9]*\).*/\1/')
	if [ "$want" -gt 0 ] && [ "$v10" = "$want" ] && [ "$broken" = 0 ] && [ "$inform" = "$want" ]; then
		pass "notify: $want/$want changes kept and sent in the Inform"
	else
		bad "notify: file $v10/$want updated, $broken broken lines, Inform carried ${inform:-0}/$want"
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
	# --run-libc-freeres=no: K22.  valgrind runs glibc's __libc_freeres in
	# the thread that called _exit after killing the others; its _IO_cleanup
	# locks every FILE and hung on /etc/tr098/.dm_enabled_notify, whose lock
	# the killed main thread held (value-change read loop).  Without valgrind
	# _exit runs no freeres, so this is the tool, not the agent.
	start "${1:-12}" "--download-every 3" valgrind --leak-check=full --errors-for-leak-kinds=definite \
		--run-libc-freeres=no --child-silent-after-fork=yes --num-callers=25 --log-file="$RUN/vg.log"
	touch "$RUN/load.on"; load 1
	wait_done 600
	rm -f "$RUN/load.on"; sleep 3
	kill -TERM "$(cat "$RUN/icwmpd.pid")"; sleep 10
	stop
	# K22: under valgrind the agent sometimes does not finish exiting ("Zl",
	# one thread left in a glibc lock); it keeps /var/run/icwmpd.pid locked
	# and every later agent exits at once.  Report it and clear it
	# (ICWMP_KEEP_STUCK=1 leaves it for gdb).
	stuck=$(ps -eo pid=,stat=,comm= | awk '$3 ~ /^memcheck/ && $2 != "Z" {print $1}')
	if [ -n "$stuck" ]; then
		echo "  K22: agent under valgrind still there after SIGTERM: $(ps -o pid=,stat=,nlwp= -p "$stuck" | tr -s ' ')"
		[ -n "$ICWMP_KEEP_STUCK" ] || { kill -9 $stuck 2>/dev/null; sleep 1; }
	fi
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

# P6 leaves ported to C (0090): Account.Web, UserInterface.CarrierLocking and
# UserInterface.X_AIS_WebUserInfo on the product's configs (hmxwslbackend,
# isplocking, account, remoteaccess, clay), the service restarts they queue,
# and the root's objects that answer only when addressed.
P6_CONFIGS="hmxwslbackend isplocking account remoteaccess clay"
P6_INIT="isplocking remoteaccess"
do_p6() {
	for c in $P6_CONFIGS; do
		if [ -f "/etc/config/$c" ]; then cp "/etc/config/$c" "$RUN/$c.p6saved"; else rm -f "$RUN/$c.p6saved"; fi
	done
	printf 'config hmxwslbackend\n\toption SessionTimeOut 900\n' > /etc/config/hmxwslbackend
	: > /etc/config/isplocking		# no section: the setter must add one
	printf "config account 'root'\n\toption username 'su0'\n" > /etc/config/account
	: > /etc/config/remoteaccess
	printf "config language 'language'\n\tlist available 'en'\n\tlist available 'th'\n\toption current 'en'\n" > /etc/config/clay
	rm -f "$RUN/p6.calls"
	for s in $P6_INIT; do
		printf '#!/bin/sh\necho "%s $*" >> %s/p6.calls\n' "$s" "$RUN" > "/etc/init.d/$s"
		chmod +x "/etc/init.d/$s"
	done
	mkdir -p /tmp/wsl
	printf '#!/bin/sh\necho "wsl start" >> %s/p6.calls\n' "$RUN" > /tmp/wsl/start_wsl.sh
	chmod +x /tmp/wsl/start_wsl.sh
	P=InternetGatewayDevice.UserInterface
	start 1 "--set InternetGatewayDevice.Account.Web.SessionMaxTime=600
		--set $P.CarrierLocking.X_AIS_LockingEnable=1
		--set $P.CarrierLocking.X_AIS_RoundNum=5
		--set $P.CarrierLocking.X_AIS_Guard_URL=http://guard.example.net/
		--set $P.X_AIS_WebUserInfo.AdminName=adm1
		--set $P.X_AIS_WebUserInfo.RemoteAccess=true
		--set $P.X_AIS_WebUserInfo.SuperAdminEnable=0
		--set $P.X_AIS_WebUserInfo.Captcha_enable=true
		--set $P.X_AIS_WebUserInfo.CurrentLanguage=th
		--set InternetGatewayDevice.CaptivePortal.Enable=true
		--set InternetGatewayDevice.XMPP.Connection.1.KeepAliveInterval=30"
	wait_done 60; sleep 3
	bad_n=0
	expect "faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	grep '^fault ' "$RUN/acs.log" | sed 's/^/  ACS: /'
	expect "SessionTimeOut" "$(uci -q get hmxwslbackend.@hmxwslbackend[0].SessionTimeOut)" "600"
	expect "isplocking enabled" "$(uci -q get isplocking.@isplocking[0].enabled)" "1"
	expect "isplocking round_num" "$(uci -q get isplocking.@isplocking[0].round_num)" "5"
	expect "isplocking ais_guard_url" "$(uci -q get isplocking.@isplocking[0].ais_guard_url)" "http://guard.example.net/"
	expect "account.admin type" "$(uci -q get account.admin)" "account"
	expect "account.admin.username" "$(uci -q get account.admin.username)" "adm1"
	expect "account.root.superadminenable" "$(uci -q get account.root.superadminenable)" "0"
	expect "account.root.username kept" "$(uci -q get account.root.username)" "su0"
	expect "remoteaccess enabled" "$(uci -q get remoteaccess.remoteaccess.enabled)" "1"
	expect "clay captcha" "$(uci -q get clay.captcha.enabled)" "1"
	expect "clay language not switched" "$(uci -q get clay.language.current)" "en"
	expect "restarts, once each" "$(sort "$RUN/p6.calls" 2>/dev/null | tr '\n' ' ')" "isplocking restart remoteaccess restart wsl start "
	expect "GPV RoundNum" "$(dm_value $P.CarrierLocking.X_AIS_RoundNum)" "5"
	expect "GPV RemoteAccess" "$(dm_value $P.X_AIS_WebUserInfo.RemoteAccess)" "true"
	expect "GPV RemoteAccessTimeout default" "$(dm_value $P.X_AIS_WebUserInfo.RemoteAccessTimeout)" "3600"
	expect "GPV AdminPassword" "$(dm_value $P.X_AIS_WebUserInfo.AdminPassword)" ""
	expect "GPV AvailableLanguages" "$(dm_value $P.X_AIS_WebUserInfo.AvailableLanguages)" "en th"
	expect "GPV SessionMaxTime" "$(dm_value InternetGatewayDevice.Account.Web.SessionMaxTime)" "600"
	expect "GPV DeviceSummary" "$(dm_value InternetGatewayDevice.DeviceSummary)" "InternetGatewayDevice:1.0[](Baseline:1, EthernetLAN:1, WiFiLAN:1)"
	expect "GPV CaptivePortal.Status" "$(dm_value InternetGatewayDevice.CaptivePortal.Status)" "Enabled"
	expect "CaptivePortal.Enable stores nothing" "$(dm_value InternetGatewayDevice.CaptivePortal.Enable)" "false"
	expect "GPV FAP.GPS.LockedLatitude" "$(dm_value InternetGatewayDevice.FAP.GPS.LockedLatitude)" "0.000000"
	expect "XMPP KeepAliveInterval stores nothing" "$(dm_value InternetGatewayDevice.XMPP.Connection.1.KeepAliveInterval)" "60"
	expect "GPV XMPP Server.1.Port" "$(dm_value InternetGatewayDevice.XMPP.Connection.1.Server.1.Port)" "5222"
	expect "GPV LTE.RSRP0" "$(dm_value InternetGatewayDevice.LTE.RSRP0)" "-95"
	expect "XMPP and LTE in a whole-tree get" \
		"$($UBUS call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice."}' 2>/dev/null | grep -c -e '\.XMPP\.Connection\.1\.' -e '\.LTE\.')" "27"
	expect "hidden objects absent from a whole-tree get" \
		"$($UBUS call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice."}' 2>/dev/null | grep -c -e '\.CaptivePortal\.' -e '\.BulkData\.' -e '\.FAP\.')" "0"
	alive || expect "agent" "dead" "alive"
	stop
	# values the shell refused: one session each, nothing written
	long=$(printf 'a%.0s' $(seq 1 33))
	for kv in Account.Web.SessionMaxTime=299 Account.Web.SessionMaxTime=3601 \
		  UserInterface.CarrierLocking.X_AIS_LockingEnable=true \
		  UserInterface.CarrierLocking.X_AIS_RoundNum=5a \
		  UserInterface.X_AIS_WebUserInfo.CurrentLanguage=fr \
		  UserInterface.X_AIS_WebUserInfo.SuperAdminEnable=2 \
		  UserInterface.X_AIS_WebUserInfo.Captcha_enable=TRUE \
		  UserInterface.X_AIS_WebUserInfo.AdminPassword=$long; do
		start 1 "--set InternetGatewayDevice.$kv"
		wait_done 30; sleep 1
		expect "$kv faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		stop
	done
	expect "SessionTimeOut after the faults" "$(uci -q get hmxwslbackend.@hmxwslbackend[0].SessionTimeOut)" "600"
	expect "isplocking enabled after the faults" "$(uci -q get isplocking.@isplocking[0].enabled)" "1"
	expect "isplocking round_num after the faults" "$(uci -q get isplocking.@isplocking[0].round_num)" "5"
	expect "account.admin.password untouched" "$(uci -q get account.admin.password)" ""
	# K20: a refusal at VALUESET reaches the ACS.  Without the isplocking
	# package no section can be added, the setter answers 9002 there; the
	# engine used to drop it and answer success.
	rm -f /etc/config/isplocking
	start 1 "--set $P.CarrierLocking.X_AIS_RoundNum=7"
	wait_done 30; sleep 1
	expect "K20 VALUESET fault" "$(grep '^fault ' "$RUN/acs.log" | tr '\n' ' ')" "fault 9003 $P.CarrierLocking.X_AIS_RoundNum=9002 "
	stop
	for c in $P6_CONFIGS; do
		if [ -f "$RUN/$c.p6saved" ]; then cp "$RUN/$c.p6saved" "/etc/config/$c"; else rm -f "/etc/config/$c"; fi
	done
	for s in $P6_INIT; do rm -f "/etc/init.d/$s"; done
	rm -rf /tmp/wsl
	if [ $bad_n = 0 ]; then pass "p6: Account, CarrierLocking, X_AIS_WebUserInfo writes, faults, hidden root objects, XMPP/LTE"; else bad "p6: $bad_n mismatches above"; fi
}

# P6e Firewall in C (0092): firewall_clay disable_port / packetfilter (ServiceControl
# by ipversion) / ipfilter2, positional instances, AddObject defaults, DeleteObject
# renumbering, checks that need the rule's other values at VALUESET, and an SPV
# whose second leaf faults at VALUESET leaving the first one unwritten.
do_fw() {
	for c in firewall_clay network; do
		if [ -f "/etc/config/$c" ]; then cp "/etc/config/$c" "$RUN/$c.fwsaved"; else rm -f "$RUN/$c.fwsaved"; fi
	done
	cat > /etc/config/firewall_clay <<'EOC'
config disable_port
	option active '1'
	option port '23'
	option name 'TELNET'
	option interface 'WAN_1'

config disable_port
	option active '0'
	option port '22'
	option name 'SSH'
	option interface 'LAN'

config packetfilter
	option enabled '1'
	option name 'SC4'
	option action 'accept'
	option interface 'wan'
	option ipversion 'ipv4'
	option start_ip '-'
	option end_ip '-'
	option service_type 'HTTP ICMP'
	option other_port '-'
	option other_protocol 'tcp'

config packetfilter
	option enabled '0'
	option name 'NOT_SC'

config packetfilter
	option enabled '1'
	option name 'SC6'
	option action 'block'
	option interface 'pppoe-if0'
	option ipversion 'ipv6'
	option start_ip '2001:db8::'
	option end_ip '-'
	option prefix_len '32'
	option service_type '-'
	option other_port '1000:2000'
	option other_protocol 'tcp/udp'

config ipfilter2
	option active '1'
	option name 'F1'
	option target 'block'
	option priority '1'
	option ipversion '4'
	option src_addr '10.0.0.0'
	option src_mask '8'
	option ingress_ifname 'pppoe-if0'
	option protocol 'tcp_udp'
EOC
	printf "config interface 'if0'\n\toption device 'eth1.100'\n" > /etc/config/network
	F=InternetGatewayDevice.Firewall
	W=InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1
	start 1 "--add $F.X_AIS_IPFilter. --add $F.X_AIS_ServiceControl.IPV6ServiceControl. --add $F.X_AIS_DisablePort.
		--set $F.X_AIS_IPFilter.2.IPVersion=6
		--set $F.X_AIS_IPFilter.2.SourceIP=2001:db8::1
		--set $F.X_AIS_IPFilter.2.SourceMask=64
		--set $F.X_AIS_DisablePort.3.Interface=WAN_2
		--set $F.X_AIS_ServiceControl.IPV4ServiceControl.1.IPStart=192.0.2.1
		--set $F.X_AIS_ServiceControl.IPV4ServiceControl.1.ServiceType=SSH,PING
		--set $F.X_AIS_ServiceControl.IPV4ServiceControl.1.Ingress=$W.WANIPConnection.1"
	wait_done 60; sleep 2
	bad_n=0
	expect "faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	grep '^fault ' "$RUN/acs.log" | sed 's/^/  ACS: /'
	expect "AddObject instances" "$(grep '^added ' "$RUN/acs.log" | tr '\n' ' ')" "added 2 added 2 added 3 "
	expect "GPV Config" "$(dm_value $F.Config)" "High"
	expect "GPV DisablePort.2.Name" "$(dm_value $F.X_AIS_DisablePort.2.Name)" "SSH"
	expect "new DisablePort defaults" "$(uci -q get firewall_clay.@disable_port[2].active)/$(uci -q get firewall_clay.@disable_port[2].port)/$(uci -q get firewall_clay.@disable_port[2].name)" "0/NULL/FWIPF1"
	expect "DisablePort.3.Interface" "$(uci -q get firewall_clay.@disable_port[2].interface)" "WAN_2"
	S4=$F.X_AIS_ServiceControl.IPV4ServiceControl.1
	S6=$F.X_AIS_ServiceControl.IPV6ServiceControl.1
	expect "SC4 start_ip" "$(uci -q get firewall_clay.@packetfilter[0].start_ip)" "192.0.2.1"
	expect "SC4 end_ip filled" "$(uci -q get firewall_clay.@packetfilter[0].end_ip)" "255.255.255.255"
	expect "SC4 service_type" "$(uci -q get firewall_clay.@packetfilter[0].service_type)" "SSH ICMP"
	expect "SC4 interface" "$(uci -q get firewall_clay.@packetfilter[0].interface)" "eth1.100"
	expect "GPV SC4 Ingress" "$(dm_value $S4.Ingress)" "$W.WANIPConnection.1"
	expect "GPV SC4 ServiceType" "$(dm_value $S4.ServiceType)" "SSH,PING"
	expect "GPV SC4 OtherPort" "$(dm_value $S4.OtherPort)" "NULL"
	expect "GPV SC4 Mode" "$(dm_value $S4.Mode)" "Accept"
	expect "GPV SC6 Ingress" "$(dm_value $S6.Ingress)" "$W.WANPPPConnection.1"
	expect "GPV SC6 Mode" "$(dm_value $S6.Mode)" "Drop"
	expect "GPV SC6 ServiceType" "$(dm_value $S6.ServiceType)" "NULL"
	expect "GPV SC6 OtherProtocol" "$(dm_value $S6.OtherProtocol)" "TCP/UDP"
	expect "GPV SC6 PrefixLen" "$(dm_value $S6.PrefixLen)" "32"
	expect "new SC6 rule" "$(uci show firewall_clay | grep -c "ipversion='ipv6'")" "2"
	expect "new SC6 defaults" "$(dm_value $F.X_AIS_ServiceControl.IPV6ServiceControl.2.Name)/$(dm_value $F.X_AIS_ServiceControl.IPV6ServiceControl.2.Prefix)" "FWSC1/::"
	I1=$F.X_AIS_IPFilter.1
	expect "GPV IPF1 Target" "$(dm_value $I1.Target)" "Drop"
	expect "GPV IPF1 Protocol" "$(dm_value $I1.Protocol)" "TCP and UDP"
	expect "GPV IPF1 SourceInterface" "$(dm_value $I1.SourceInterface)" "$W.WANPPPConnection.1"
	expect "GPV IPF1 DestMask" "$(dm_value $I1.DestMask)" "NULL"
	expect "new IPF2 priority, name" "$(uci -q get firewall_clay.@ipfilter2[1].priority)/$(uci -q get firewall_clay.@ipfilter2[1].name)" "2/FWIPF2"
	expect "IPF2 v6 address in the same SPV" "$(uci -q get firewall_clay.@ipfilter2[1].ipversion)/$(uci -q get firewall_clay.@ipfilter2[1].src_addr)/$(uci -q get firewall_clay.@ipfilter2[1].src_mask)" "6/2001:db8::1/64"
	alive || expect "agent" "dead" "alive"
	stop
	# refused values: one session each, nothing written
	for kv in "X_AIS_IPFilter.1.Order=2" "X_AIS_IPFilter.1.SourceIP=2001:db8::5" "X_AIS_IPFilter.1.SourceMask=33" \
		  "X_AIS_ServiceControl.IPV4ServiceControl.1.OtherPort=2000:1000" \
		  "X_AIS_ServiceControl.IPV4ServiceControl.1.ServiceType=HTTP,,SSH" \
		  "X_AIS_DisablePort.1.Interface=WAN_7"; do
		start 1 "--set $F.$kv"
		wait_done 30; sleep 1
		expect "$kv faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		stop
	done
	# second leaf faults at VALUESET: the first one is reverted
	start 1 "--set $F.X_AIS_IPFilter.2.IPVersion=4 --set $F.X_AIS_IPFilter.2.SourceIP=2001:db8::9"
	wait_done 30; sleep 1
	expect "IPVersion+v6 address faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
	stop
	expect "IPF2 ipversion reverted" "$(uci -q get firewall_clay.@ipfilter2[1].ipversion)" "6"
	expect "IPF1 priority after the faults" "$(uci -q get firewall_clay.@ipfilter2[0].priority)" "1"
	expect "DisablePort.1 interface after the faults" "$(uci -q get firewall_clay.@disable_port[0].interface)" "WAN_1"
	# DeleteObject: the next rule takes the deleted one's number
	start 1 "--delete $F.X_AIS_IPFilter.1. --delete $F.X_AIS_DisablePort.1."
	wait_done 30; sleep 1
	expect "delete faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	expect "ipfilter2 left" "$(uci show firewall_clay | grep -c '=ipfilter2$')" "1"
	expect "disable_port left" "$(uci show firewall_clay | grep -c '=disable_port$')" "2"
	expect "IPFilter.1 is the old 2" "$(dm_value $F.X_AIS_IPFilter.1.SourceIP)" "2001:db8::1"
	expect "DisablePort.1 is the old 2" "$(dm_value $F.X_AIS_DisablePort.1.Name)" "SSH"
	alive || expect "agent" "dead" "alive"
	stop
	for c in firewall_clay network; do
		if [ -f "$RUN/$c.fwsaved" ]; then cp "$RUN/$c.fwsaved" "/etc/config/$c"; else rm -f "/etc/config/$c"; fi
	done
	if [ $bad_n = 0 ]; then pass "fw: DisablePort, ServiceControl v4/v6, IPFilter: get, add, set, faults, VALUESET revert, delete"; else bad "fw: $bad_n mismatches above"; fi
}

# P7 operator X_AIS_* objects in C (0094-): the product's options written,
# each service restart / hni call queued once and run after the commit, the
# shell's "unchanged, do nothing" kept, values the shell refused faulted.
P7_CONFIGS="upnpd 3rdpartyagent autowifiscan lanhost landingpage dhcp account meshapi ddns clay aisbackup system"
P7_INIT="miniupnpd 3rdpartyagent autowifiscan dnsmasq landingpage account telnet dropbear meshapi ddns log"
p7_calls() { sort "$RUN/p7.calls" 2>/dev/null | tr '\n' '|'; }
do_p7() {
	for c in $P7_CONFIGS; do
		if [ -f "/etc/config/$c" ]; then cp "/etc/config/$c" "$RUN/$c.p7saved"; else rm -f "${RUN:?}/${c:?}.p7saved"; fi
	done
	printf "config upnpd 'config'\n\toption enabled '0'\n" > /etc/config/upnpd
	printf "config 3rdpartyagent '3rdpartyagent'\n\toption enabled '0'\n\toption broker_url 'mqtts://old.example.net:8883'\n\toption client_id 'c1'\n\toption secret_key 'k3y-of-test'\n" > /etc/config/3rdpartyagent
	printf "config autowifiscan\n\toption traffic_limit '300'\n" > /etc/config/autowifiscan
	printf "config opermode 'opermode'\n\toption mode 'ap'\n" > /etc/config/clay
	printf "config service 'service'\n\toption enabled '0'\n" > /etc/config/ddns
	printf "config devinfo\n\toption modelname 'HP2236B'\n\nconfig syslog 'syslog'\n\toption log_enable '0'\n\toption selected_log_levels 'err|warn'\n\toption selected_remote_levels 'none'\n" > /etc/config/system
	# no section at all: the setters add landingpage[0], dhcp.lan,
	# account.ssh/telnet, meshapi.meshapi and aisbackup.params
	for c in lanhost landingpage dhcp account meshapi aisbackup; do : > "/etc/config/$c"; done
	# X_AIS_Logging requests: the log directory, and a tftp that reports
	# what it was given and what the archive holds
	rm -rf /backup; mkdir -p /backup/log/backup
	echo old > /backup/log/backup/old.log; echo app > /backup/log/app.log
	if [ -f /var/log/messages ]; then p7_msgs=kept; else p7_msgs=made; echo test > /var/log/messages; fi
	rm -f "${RUN:?}/p7.calls"
	for s in $P7_INIT; do
		printf '#!/bin/sh\necho "%s $*" >> %s/p7.calls\n' "$s" "$RUN" > "/etc/init.d/$s"
		chmod +x "/etc/init.d/$s"
	done
	# "ubus call hni ..." of the queued lines is logged; the engine's own
	# "ubus -S -t N call ..." goes on to the real ubus
	mkdir -p "$RUN/p7bin"
	printf '#!/bin/sh\n[ "$1 $2" = "call hni" ] || exec /usr/bin/ubus "$@"\necho "ubus $*" >> %s/p7.calls\n' "$RUN" > "$RUN/p7bin/ubus"
	chmod +x "$RUN/p7bin/ubus"
	printf '#!/bin/sh\nn=$(tar -tzf "$3" 2>/dev/null | grep -c "messages$")\necho "tftp $* archive=$([ -f "$3" ] && echo yes || echo no) messages=$n" >> %s/p7.calls\n' "$RUN" > "$RUN/p7bin/tftp"
	chmod +x "$RUN/p7bin/tftp"
	X=InternetGatewayDevice
	start 1 "--set $X.X_AIS_UPnP.Enable=true
		--set $X.X_AIS_3rdAgent.server_Cert=1
		--set $X.X_AIS_3rdAgent.server_URL=mqtts://new.example.net:8883
		--set $X.X_AIS_CPEagent.SecretKeyVersion=v2
		--set $X.X_AIS_AutoWifiScan.Enable=1
		--set $X.X_AIS_AutoWifiScan.TrafficKeepTime=90
		--set $X.X_AIS_DHCPClient.Clean=1
		--set $X.X_AIS_DnsLandingPage.Enable=1
		--set $X.X_AIS_Isolation.LANIsolation=1
		--set $X.X_AIS_SSH.Enable=true
		--set $X.X_AIS_Telnet.Username=con1
		--set $X.X_AIS_Telnet.Password=pw-of-test
		--set $X.X_AIS_MeshAPI.Delay_time=1800
		--set $X.X_AIS_MeshAPI.enable=true
		--set $X.X_AIS_DDNS.Provider=No-IP
		--set $X.X_AIS_DDNS.Enable=true
		--set $X.X_AIS_Conf.download_server=https://dl.example.net/x
		--set $X.X_AIS_Conf.upload_to_server=1
		--set $X.X_AIS_Conf.auto_upload_delay=600
		--set $X.X_AIS_Logging.EnableLogging=true
		--set $X.X_AIS_Logging.LoggingLevel=6
		--set $X.X_AIS_Logging.RemoteLoggingAddress=192.0.2.10
		--set $X.X_AIS_Logging.RemoteLoggingPort=514
		--set $X.X_AIS_Logging.DebugEnable=1
		--set $X.X_AIS_Logging.ErrorEnable=0
		--set $X.X_AIS_Logging.RemoteLogging.EmergencyEnable=true
		--set $X.X_AIS_Logging.CleanLogging=1
		--set $X.X_AIS_Logging.TFTPAddress=192.0.2.20
		--set $X.X_AIS_Logging.TFTPUploadResponse=1" env PATH="$RUN/p7bin:$PATH"
	wait_done 60; sleep 3
	bad_n=0
	expect "faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	grep '^fault ' "$RUN/acs.log" | sed 's/^/  ACS: /'
	expect "upnpd enabled" "$(uci -q get upnpd.config.enabled)" "1"
	expect "agent cert_enable" "$(uci -q get 3rdpartyagent.3rdpartyagent.cert_enable)" "1"
	expect "agent broker_url" "$(uci -q get 3rdpartyagent.3rdpartyagent.broker_url)" "mqtts://new.example.net:8883"
	expect "agent client_id cleared in AP mode" "$(uci -q get 3rdpartyagent.3rdpartyagent.client_id)" ""
	expect "agent secret_key_version" "$(uci -q get 3rdpartyagent.3rdpartyagent.secret_key_version)" "v2"
	expect "autowifiscan enabled" "$(uci -q get autowifiscan.@autowifiscan[0].enabled)" "1"
	expect "autowifiscan traffic_keeptime" "$(uci -q get autowifiscan.@autowifiscan[0].traffic_keeptime)" "90"
	expect "landingpage section added" "$(uci -q get landingpage.@landingpage[0].enabled)" "1"
	expect "dhcp.lan added" "$(uci -q get dhcp.lan)" "dhcp"
	expect "dhcp.lan.isolation" "$(uci -q get dhcp.lan.isolation)" "1"
	expect "account.ssh added" "$(uci -q get account.ssh)" "account"
	expect "account.ssh.enabled left to hni" "$(uci -q get account.ssh.enabled)" ""
	expect "account.telnet.username" "$(uci -q get account.telnet.username)" "con1"
	expect "account.ssh.username synced" "$(uci -q get account.ssh.username)" "con1"
	expect "account.telnet.password" "$(uci -q get account.telnet.password)" "pw-of-test"
	expect "meshapi.meshapi added" "$(uci -q get meshapi.meshapi)" "meshapi"
	expect "meshapi delay_time" "$(uci -q get meshapi.meshapi.delay_time)" "1800"
	expect "meshapi enable" "$(uci -q get meshapi.meshapi.enable)" "1"
	expect "ddns service_name" "$(uci -q get ddns.service.service_name)" "no-ip.com"
	expect "ddns enabled" "$(uci -q get ddns.service.enabled)" "1"
	expect "aisbackup.params added" "$(uci -q get aisbackup.params)" "aisbackup"
	expect "aisbackup download_server" "$(uci -q get aisbackup.params.download_server)" "https://dl.example.net/x"
	expect "aisbackup upload_to_server" "$(uci -q get aisbackup.params.upload_to_server)" "true"
	expect "aisbackup auto_upload_delay" "$(uci -q get aisbackup.params.auto_upload_delay)" "600"
	expect "syslog log_enable" "$(uci -q get system.syslog.log_enable)" "1"
	expect "syslog log_level" "$(uci -q get system.syslog.log_level)" "6"
	expect "syslog log_ip" "$(uci -q get system.syslog.log_ip)" "192.0.2.10"
	expect "syslog log_port" "$(uci -q get system.syslog.log_port)" "514"
	expect "syslog levels: debug added, err removed" "$(uci -q get system.syslog.selected_log_levels)" "warn|debug"
	expect "syslog remote levels: none -> emerg" "$(uci -q get system.syslog.selected_remote_levels)" "emerg"
	expect "CleanLogging responded" "$(uci -q get system.syslog.clean_logging)" "2"
	expect "backup logs removed" "$(ls /backup/log/backup | wc -l)" "0"
	expect "TFTP upload responded" "$(uci -q get system.syslog.tftp_response)" "2"
	expect "messages link removed" "$([ -e /backup/log/messages ] || [ -L /backup/log/messages ] && echo left || echo gone)" "gone"
	expect "tar.gz removed" "$(ls /tmp/AIS_*.tar.gz 2>/dev/null | wc -l)" "0"
	tgz=AIS_HP2236B_$(date +%Y%m%d).tar.gz
	want=$(printf '%s\n' "miniupnpd reload" "3rdpartyagent restart" "autowifiscan running" \
		"dnsmasq stop" "dnsmasq start" "landingpage restart" "ubus call hni doLanIsolation" \
		'ubus call hni setSshAccess {"enabled":true}' "account reload" "telnet restart" \
		"dropbear killclients" "dropbear reload" "meshapi restart" "ddns restart" "log restart" \
		"tftp -p -l /tmp/$tgz -r $tgz 192.0.2.20 archive=yes messages=1" | sort | tr '\n' '|')
	expect "restarts and hni calls, once each" "$(p7_calls)" "$want"
	# encrypt_with_specialkey of the shell, verbatim, with the test key
	printf '%s' 'k3y-of-test' > "$RUN/p7.plain"
	dd if=/dev/zero bs=1 count=256 >> "$RUN/p7.plain" 2>/dev/null
	dd if="$RUN/p7.plain" bs=256 count=1 of="$RUN/p7.pad" 2>/dev/null
	key=$(openssl enc -aes-256-ecb -K "$CPEAGENT_TEST_KEY" -nopad -in "$RUN/p7.pad" 2>/dev/null | openssl base64 -A 2>/dev/null | cut -c1-64)
	[ ${#key} = 64 ] || expect "openssl in this container" "${#key} characters" "64"
	expect "GPV SecretKey = the shell's encryption" "$(dm_value $X.X_AIS_CPEagent.SecretKey)" "$key"
	expect "GPV SecretKeyVersion" "$(dm_value $X.X_AIS_CPEagent.SecretKeyVersion)" "v2"
	expect "GPV UPnP.Enable" "$(dm_value $X.X_AIS_UPnP.Enable)" "true"
	expect "GPV 3rdAgent.enable" "$(dm_value $X.X_AIS_3rdAgent.enable)" "false"
	expect "GPV AutoWifiScan.TrafficLimit" "$(dm_value $X.X_AIS_AutoWifiScan.TrafficLimit)" "300"
	expect "GPV DHCPClient.Session" "$(dm_value $X.X_AIS_DHCPClient.Session)" "3"
	expect "GPV DHCPClient.Clean" "$(dm_value $X.X_AIS_DHCPClient.Clean)" "0"
	expect "GPV DnsLandingPage.Enable" "$(dm_value $X.X_AIS_DnsLandingPage.Enable)" "1"
	expect "GPV SSH.Enable (hni did not run)" "$(dm_value $X.X_AIS_SSH.Enable)" "false"
	expect "GPV Telnet.Password write only" "$(dm_value $X.X_AIS_Telnet.Password)" ""
	expect "GPV MeshAPI.enable" "$(dm_value $X.X_AIS_MeshAPI.enable)" "true"
	expect "GPV DDNS.Provider" "$(dm_value $X.X_AIS_DDNS.Provider)" "No-IP"
	expect "GPV Conf.upload_to_server" "$(dm_value $X.X_AIS_Conf.upload_to_server)" "1"
	expect "GPV Conf.download_from_server" "$(dm_value $X.X_AIS_Conf.download_from_server)" "0"
	expect "GPV Logging.ErrorEnable" "$(dm_value $X.X_AIS_Logging.ErrorEnable)" "0"
	expect "GPV Logging.WarningEnable" "$(dm_value $X.X_AIS_Logging.WarningEnable)" "1"
	expect "GPV Logging.DebugEnable" "$(dm_value $X.X_AIS_Logging.DebugEnable)" "1"
	expect "GPV RemoteLogging.EmergencyEnable" "$(dm_value $X.X_AIS_Logging.RemoteLogging.EmergencyEnable)" "1"
	expect "GPV RemoteLogging.DebugEnable" "$(dm_value $X.X_AIS_Logging.RemoteLogging.DebugEnable)" "0"
	expect "GPV TFTPUploadResponse" "$(dm_value $X.X_AIS_Logging.TFTPUploadResponse)" "2"
	alive || expect "agent" "dead" "alive"
	stop
	# same values again: nothing written, nothing restarted
	rm -f "${RUN:?}/p7.calls"
	start 1 "--set $X.X_AIS_DnsLandingPage.Enable=1 --set $X.X_AIS_Isolation.LANIsolation=1
		--set $X.X_AIS_MeshAPI.Delay_time=1800 --set $X.X_AIS_Telnet.Username=con1" env PATH="$RUN/p7bin:$PATH"
	wait_done 30; sleep 1
	expect "unchanged values: faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	expect "unchanged values: no restart" "$(p7_calls)" ""
	stop
	# values the shell refused: one session each, nothing written
	long=$(printf 'a%.0s' $(seq 1 65))
	for kv in X_AIS_UPnP.Enable=TRUE X_AIS_SSH.Enable=yes \
		  X_AIS_CPEagent.SecretKeyVersion=$long \
		  X_AIS_AutoWifiScan.TrafficLimit=-1 \
		  X_AIS_DHCPClient.Session=2 X_AIS_DHCPClient.Clean=0 \
		  X_AIS_DnsLandingPage.Enable=true X_AIS_Isolation.LANIsolation=2 \
		  X_AIS_Telnet.Username= X_AIS_MeshAPI.Domain_name= \
		  X_AIS_DDNS.Provider=Dyn X_AIS_DDNS.Enable=on \
		  X_AIS_Conf.upload_to_server=yes X_AIS_Conf.auto_upload_delay=x1 \
		  X_AIS_Logging.LoggingLevel=8 X_AIS_Logging.RemoteLoggingAddress=host.example \
		  X_AIS_Logging.RemoteLoggingPort=70000 X_AIS_Logging.TFTPUploadResponse=4; do
		start 1 "--set $X.$kv" env PATH="$RUN/p7bin:$PATH"
		wait_done 30; sleep 1
		expect "$kv faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		stop
	done
	expect "upnpd after the faults" "$(uci -q get upnpd.config.enabled)" "1"
	expect "secret_key_version after the faults" "$(uci -q get 3rdpartyagent.3rdpartyagent.secret_key_version)" "v2"
	expect "isolation after the faults" "$(uci -q get dhcp.lan.isolation)" "1"
	expect "ddns service_name after the faults" "$(uci -q get ddns.service.service_name)" "no-ip.com"
	expect "log_level after the faults" "$(uci -q get system.syslog.log_level)" "6"
	expect "log_port after the faults" "$(uci -q get system.syslog.log_port)" "514"
	rm -rf /backup
	[ "$p7_msgs" = made ] && rm -f /var/log/messages
	for c in $P7_CONFIGS; do
		if [ -f "$RUN/$c.p7saved" ]; then cp "$RUN/$c.p7saved" "/etc/config/$c"; else rm -f "/etc/config/${c:?}"; fi
	done
	for s in $P7_INIT; do rm -f "/etc/init.d/${s:?}"; done
	rm -rf "${RUN:?}/p7bin"
	if [ $bad_n = 0 ]; then pass "p7: operator X_AIS_* writes, queued restarts/hni calls, log upload/clean, unchanged values, faults"; else bad "p7: $bad_n mismatches above"; fi
}

# P7c in C (0095): X_AIS_UplinkSetup against a stand-in of hni.dualuplink
# that writes and commits dualuplink itself, as hni does (the setters must
# compare with the files, not with the session's copy), X_AIS_WiFiStatus
# reports on fixed mwctl/iw output (the expected JSON is what the product's
# shell functions printed for the same input under busybox, with the two
# differences documented in x_ais_wifistatus_mtk.c), X_AIS_MLO groups.
P7C_CONFIGS="dualuplink clay wireless"
p7c_fixtures() {
	F=${RUN:?}/p7fix
	rm -rf "${RUN:?}/p7fix"; mkdir -p "$F"
	cat > "$F/scan_ra0.txt" <<'EOF'
Total=0012
No  Ch  SSID          BSSID              Security     Signal  W-Mode
0   1   HomeNet       aa:bb:cc:00:00:01  WPA2PSK/AES  -45     11b/g/n
1   6   Cafe_Free     aa:bb:cc:00:00:02  NONE         -70     11b/g/n
2   11                aa:bb:cc:00:00:03  WPA2PSK/AES  -60     11ax
3   3   My"Net        AA:BB:CC:00:00:04  WPA3SAE      -60     11ax
4   9   NoSignal      aa:bb:cc:00:00:05  WPA2PSK/AES  weak    11ax
5   1   Net6          aa:bb:cc:00:00:06  WPA2PSK/AES  -81     11n
6   1   Net7          aa:bb:cc:00:00:07  WPA2PSK/AES  -82     11n
7   1   Net8          aa:bb:cc:00:00:08  WPA2PSK/AES  -83     11n
8   1   Net9          aa:bb:cc:00:00:09  WPA2PSK/AES  -84     11n
9   1   Net10         aa:bb:cc:00:00:0a  WPA2PSK/AES  -85     11n
10  1   Net11         aa:bb:cc:00:00:0b  WPA2PSK/AES  -86     11n
11  1   Net12         aa:bb:cc:00:00:0c  WPA2PSK/AES  -87     11n
EOF
	cat > "$F/scan_rai0.txt" <<'EOF'
Total=0001
No  Ch  SSID          BSSID              Security     Signal  W-Mode
0   36  Office5G      aa:bb:cc:00:01:01  WPA2PSK/AES  -55     11ax
EOF
	printf 'Station aa:bb:cc:11:22:33 (on ra0)\n\tinactive time:\t1000 ms\n\tsignal:  \t-51 [-51, -53] dBm\n\tsignal avg:\t-52 dBm\nStation aa:bb:cc:11:22:44 (on ra0)\n\tinactive time:\t10 ms\n\tlast ack signal:\t-40 dBm\n' > "$F/sta_ra0.txt"
	printf 'Station aa:bb:cc:11:22:55 (on rai0)\n\tsignal:  \t-66 [-66, -70] dBm\n' > "$F/sta_rai0.txt"
	printf '1700000000 aa:bb:cc:11:22:33 192.168.1.101 phone-a *\n1700000001 aa:bb:cc:11:22:55 192.168.1.102 laptop-b 01:aa:bb:cc:11:22:55\n' > "$F/dhcp.leases"
	# the shell's report for scan_*.txt, '"' escaped
	cat > "$F/neighbor.json" <<'EOF'
{
  "WiFi_Neighbor": {
    "2.4GHz": [
      {"SSID":"NoSignal","BSSID":"aa:bb:cc:00:00:05","Ch":"9","Signal":"NO"},
      {"SSID":"HomeNet","BSSID":"aa:bb:cc:00:00:01","Ch":"1","Signal":"-45"},
      {"SSID":"My\"Net","BSSID":"AA:BB:CC:00:00:04","Ch":"3","Signal":"-60"},
      {"SSID":"11","BSSID":"aa:bb:cc:00:00:03","Ch":"11","Signal":"-60"},
      {"SSID":"Cafe_Free","BSSID":"aa:bb:cc:00:00:02","Ch":"6","Signal":"-70"},
      {"SSID":"Net6","BSSID":"aa:bb:cc:00:00:06","Ch":"1","Signal":"-81"},
      {"SSID":"Net7","BSSID":"aa:bb:cc:00:00:07","Ch":"1","Signal":"-82"},
      {"SSID":"Net8","BSSID":"aa:bb:cc:00:00:08","Ch":"1","Signal":"-83"},
      {"SSID":"Net9","BSSID":"aa:bb:cc:00:00:09","Ch":"1","Signal":"-84"},
      {"SSID":"Net10","BSSID":"aa:bb:cc:00:00:0a","Ch":"1","Signal":"-85"}
    ],
    "5GHz": [
      {"SSID":"Office5G","BSSID":"aa:bb:cc:00:01:01","Ch":"36","Signal":"-55"}
    ]
  }
}
EOF
	# the shell's report for sta_*.txt with the lease lookup it meant
	cat > "$F/client.json" <<'EOF'
{
  "WiFi_Client": {
    "2.4GHz": [
      {"Hostname":"phone-a","MAC":"aa:bb:cc:11:22:33","IP":"192.168.1.101","RSSI":"-51"},
      {"Hostname":"","MAC":"aa:bb:cc:11:22:44","IP":"","RSSI":"NO"}
    ],
    "5GHz": [
      {"Hostname":"laptop-b","MAC":"aa:bb:cc:11:22:55","IP":"192.168.1.102","RSSI":"-66"}
    ]
  }
}
EOF
	mkdir -p "$RUN/p7bin"
	cat > "$RUN/p7bin/ubus" <<'EOF'
#!/bin/sh
# hni.dualuplink stand-in: writes and commits dualuplink like hni, main2=eth2 fails
if [ "$1 $2" = "call hni.dualuplink" ]; then
	p=$(echo "$4" | sed -n 's/.*"param": *"\([^"]*\)".*/\1/p')
	v=$(echo "$4" | sed -n 's/.*"value": *"\([^"]*\)".*/\1/p')
	echo "hni.dualuplink $p=$v" >> @CALLS@
	case "$p" in
		enable) o=common.enabled ;; mode) o=common.mode ;; vlan) o=common.tagged ;;
		backup1) o=@uplink[0].backup ;; main2) o=@uplink[1].main ;;
		main3) o=@uplink[2].main ;; backup3) o=@uplink[2].backup ;;
		*) echo '{"result":"FAIL"}'; exit 0 ;;
	esac
	[ "$p=$v" = main2=eth2 ] && { echo '{"result":"FAIL"}'; exit 0; }
	uci set "dualuplink.$o=$v" && uci commit dualuplink && echo '{ "result": "SUCCESS" }'
	exit 0
fi
exec /usr/bin/ubus "$@"
EOF
	printf '#!/bin/sh\necho "killall $*" >> @CALLS@\n' > "$RUN/p7bin/killall"
	printf '#!/bin/sh\n[ "$2 $3" = "scan type=partial" ] && cat @FIX@/scan_$1.txt\nexit 0\n' > "$RUN/p7bin/mwctl"
	printf '#!/bin/sh\n[ "$1 $3 $4" = "dev station dump" ] && exec cat @FIX@/sta_$2.txt\nexit 1\n' > "$RUN/p7bin/iw"
	sed -i "s|@CALLS@|$RUN/p7.calls|g; s|@FIX@|$F|g" "$RUN/p7bin/ubus" "$RUN/p7bin/killall" "$RUN/p7bin/mwctl" "$RUN/p7bin/iw"
	chmod +x "$RUN/p7bin/ubus" "$RUN/p7bin/killall" "$RUN/p7bin/mwctl" "$RUN/p7bin/iw"
}
do_p7c() {
	for c in $P7C_CONFIGS; do
		if [ -f "/etc/config/$c" ]; then cp "/etc/config/$c" "$RUN/$c.p7csaved"; else rm -f "${RUN:?}/${c:?}.p7csaved"; fi
	done
	[ -f /tmp/dhcp.leases ] && cp /tmp/dhcp.leases "$RUN/dhcp.leases.p7csaved"
	p7c_fixtures
	cp "$F/dhcp.leases" /tmp/dhcp.leases
	rm -f /tmp/ais_neighborap_state /tmp/ais_neighborap_response.json /tmp/ais_wificlient_state /tmp/ais_wificlient_response.json
	cat > /etc/config/dualuplink <<'EOF'
config common 'common'
	option enabled '0'
	option allow_admin '0'
	option tagged '1'
	option mode '0'
	option flag '0'

config uplink
	option main 'pon'
	option backup 'eth3'

config uplink
	option main 'eth3'
	option backup 'pon'

config uplink
	option main 'eth3'
	option backup 'eth4'

config timer 'timer'
	option backup_over_time '86400'
	option no_wanip_time '180'
	option delay_before_switch '30'
	option increase_time '1800'
EOF
	printf "config opermode 'opermode'\n\toption uplink 'eth2'\n" > /etc/config/clay
	{ for r in MT7993_1_1 MT7993_1_2; do printf "config wifi-device '%s'\n\toption map_mode '0'\n\n" $r; done
	  for i in apmld1 apmld2 ra4 rai4 ra5 rai5; do printf "config wifi-iface '%s'\n\toption disabled '0'\n\n" $i; done; } > /etc/config/wireless
	mkdir -p /userfs/bin
	printf '#!/bin/sh\nprintf "LAN1=DOWN,0\\nLAN2=UP,1000,FULL\\n"\n' > /userfs/bin/blapi_cmd
	chmod +x /userfs/bin/blapi_cmd
	if [ -e /sbin/wifi ]; then p7c_wifi=kept; else p7c_wifi=made
		printf '#!/bin/sh\necho "wifi $*" >> %s/p7.calls\n' "$RUN" > /sbin/wifi; chmod +x /sbin/wifi; fi
	rm -f "${RUN:?}/p7.calls"
	U=InternetGatewayDevice.X_AIS_UplinkSetup
	W=InternetGatewayDevice.X_AIS_WiFiStatus
	M=InternetGatewayDevice.X_AIS_MLO
	start 1 "--set $U.mode=1 --set $U.AllowAdmin=1 --set $U.DualUplink.mode=2
		--set $U.DualUplink.mode3.BackupUplink=lan1 --set $U.DualUplink.mode3.MainUplink=lan4
		--set $U.DualUplink.BackupOver=3600 --set $U.DualUplink.VlanTaggingEnable=1
		--set $U.DualUplink.mode1.BackupUplink=lan3
		--set $M.Fronthaul.Enable=0 --set $M.Backhaul.Enable=0
		--set $W.X_AIS_WiFiClient=1 --set $W.X_AIS_NeighborAP=1" env PATH="$RUN/p7bin:$PATH"
	wait_done 90; sleep 3
	bad_n=0
	expect "faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	grep '^fault ' "$RUN/acs.log" | sed 's/^/  ACS: /'
	expect "dualuplink enabled (hni)" "$(uci -q get dualuplink.common.enabled)" "1"
	expect "dualuplink mode (hni)" "$(uci -q get dualuplink.common.mode)" "2"
	expect "mode3 backup (hni)" "$(uci -q get dualuplink.@uplink[2].backup)" "eth1"
	expect "mode3 main (hni, compared with the backup hni had just written)" "$(uci -q get dualuplink.@uplink[2].main)" "eth4"
	expect "allow_admin, committed over hni's writes" "$(uci -q get dualuplink.common.allow_admin)" "1"
	expect "backup_over_time" "$(uci -q get dualuplink.timer.backup_over_time)" "3600"
	expect "wireless apmld1/ra5/rai5" "$(uci -q get wireless.apmld1.disabled)$(uci -q get wireless.ra5.disabled)$(uci -q get wireless.rai5.disabled)" "111"
	expect "wireless apmld2/ra4/rai4" "$(uci -q get wireless.apmld2.disabled)$(uci -q get wireless.ra4.disabled)$(uci -q get wireless.rai4.disabled)" "110"
	want=$(printf '%s\n' "hni.dualuplink enable=1" "hni.dualuplink mode=2" "hni.dualuplink backup3=eth1" \
		"hni.dualuplink main3=eth4" "killall -USR1 dualuplink" "wifi reload" "wifi reload" | sort | tr '\n' '|')
	expect "hni calls (unchanged leaves none), signals, reloads" "$(p7_calls)" "$want"
	expect "GPV mode" "$(dm_value $U.mode)" "1"
	expect "GPV CurrentUplinkType" "$(dm_value $U.CurrentUplinkType)" "lan2"
	expect "GPV UplinkStatus" "$(dm_value $U.UplinkStatus)" "up"
	expect "GPV mode1.BackupUplink" "$(dm_value $U.DualUplink.mode1.BackupUplink)" "lan3"
	expect "GPV mode2.MainUplink" "$(dm_value $U.DualUplink.mode2.MainUplink)" "lan3"
	expect "GPV mode3.MainUplink" "$(dm_value $U.DualUplink.mode3.MainUplink)" "lan4"
	expect "GPV DualUplinkFlag" "$(dm_value $U.DualUplink.DualUplinkFlag)" "0"
	expect "GPV MLO Fronthaul" "$(dm_value $M.Fronthaul.Enable)" "0"
	expect "GPV MLO Backhaul" "$(dm_value $M.Backhaul.Enable)" "0"
	expect "GPV NeighborAP state" "$(dm_value $W.X_AIS_NeighborAP)" "2"
	expect "GPV WiFiClient state" "$(dm_value $W.X_AIS_WiFiClient)" "2"
	expect "GPV NeighborAPResponse" "$(dm_value $W.X_AIS_NeighborAPResponse)" "$(cat "$F/neighbor.json")"
	expect "GPV WiFiClientResponse" "$(dm_value $W.X_AIS_WiFiClientResponse)" "$(cat "$F/client.json")"
	alive || expect "agent" "dead" "alive"
	stop
	# refused values, one session each; the two VALUESET ones reach the ACS
	# as 9003 with the leaf's own code
	for kv in mode=2 DualUplink.mode=3 DualUplink.mode1.BackupUplink=optic DualUplink.BackupOver=0 \
		  DualUplink.NoWANIPTime=1a DualUplink.mode3.MainUplink=lan1 DualUplink.mode2.MainUplink=lan2; do
		start 1 "--set $U.$kv" env PATH="$RUN/p7bin:$PATH"
		wait_done 30; sleep 1
		expect "$kv faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		case $kv in
			DualUplink.mode3.MainUplink=*) expect "$kv at VALUESET" "$(grep '^fault ' "$RUN/acs.log")" "fault 9003 $U.${kv%%=*}=9007" ;;
			DualUplink.mode2.MainUplink=*) expect "$kv hni refusal" "$(grep '^fault ' "$RUN/acs.log")" "fault 9003 $U.${kv%%=*}=9002" ;;
		esac
		stop
	done
	expect "mode3 after the faults" "$(uci -q get dualuplink.@uplink[2].main)" "eth4"
	for kv in $M.Fronthaul.Enable=true $W.X_AIS_NeighborAP=4; do
		start 1 "--set $kv" env PATH="$RUN/p7bin:$PATH"
		wait_done 30; sleep 1
		expect "$kv faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		stop
	done
	for c in $P7C_CONFIGS; do
		if [ -f "$RUN/$c.p7csaved" ]; then cp "$RUN/$c.p7csaved" "/etc/config/$c"; else rm -f "/etc/config/${c:?}"; fi
	done
	if [ -f "$RUN/dhcp.leases.p7csaved" ]; then mv "$RUN/dhcp.leases.p7csaved" /tmp/dhcp.leases; else rm -f /tmp/dhcp.leases; fi
	[ "$p7c_wifi" = made ] && rm -f /sbin/wifi
	rm -rf /userfs "${RUN:?}/p7bin" "${RUN:?}/p7fix"
	rm -f /tmp/ais_neighborap_state /tmp/ais_neighborap_response.json /tmp/ais_wificlient_state \
		/tmp/ais_wificlient_response.json /tmp/ais_scan_24g.tmp /tmp/ais_scan_5g.tmp \
		/tmp/ais_sta_24g.tmp /tmp/ais_sta_5g.tmp
	if [ $bad_n = 0 ]; then pass "p7c: UplinkSetup over hni.dualuplink, WiFiStatus reports, MLO groups, faults"; else bad "p7c: $bad_n mismatches above"; fi
}

# P8a in C (0097): IGD.Device.IP (numbering by ip_int_instance, given and
# committed on a GET too; Add/Delete; restarts queued), Device.DHCPv6 pools,
# Device.IP.Diagnostics.TraceRoute with RouteHops, DOCSIS.
P8_CONFIGS="network wan dhcp"
do_p8() {
	for c in $P8_CONFIGS; do
		if [ -f "/etc/config/$c" ]; then cp "/etc/config/$c" "$RUN/$c.p8saved"; else rm -f "${RUN:?}/${c:?}.p8saved"; fi
	done
	cat > /etc/config/network <<'EOF'
config interface 'loopback'
	option device 'lo'
	option proto 'static'

config interface 'lan'
	option device 'br-lan'
	option proto 'static'

config interface 'if0'
	option device 'eth1.100'
	option proto 'pppoe'
	option ip_int_instance '5'

config interface 'if0_6'
	option device '@if0'
	option proto 'dhcpv6'

config interface
	option device 'lo'
	option proto 'static'
EOF
	printf "config entry\n\toption active '1'\n\toption v6_active '0'\n\nconfig entry\n\toption active '1'\n\toption v6_active '1'\n" > /etc/config/wan
	printf "config dhcp 'lan'\n\toption interface 'lan'\n\toption dhcpv6 'server'\n\nconfig dhcp 'wan'\n\toption interface 'if0'\n\nconfig dhcp 'ghost'\n\toption interface 'nosuch'\n" > /etc/config/dhcp
	rm -rf /var/state/traceroute
	rm -f "${RUN:?}/p8.calls"
	mkdir -p "$RUN/p8bin"
	printf '#!/bin/sh\necho "ifdown $*" >> %s/p8.calls\n' "$RUN" > "$RUN/p8bin/ifdown"
	printf '#!/bin/sh\necho "ifup $*" >> %s/p8.calls\n' "$RUN" > "$RUN/p8bin/ifup"
	printf '#!/bin/sh\ncase "$*" in *flush*) echo "ip $*" >> %s/p8.calls; exit 0 ;; esac\nexec /usr/sbin/ip "$@"\n' "$RUN" > "$RUN/p8bin/ip"
	chmod +x "$RUN/p8bin/ifdown" "$RUN/p8bin/ifup" "$RUN/p8bin/ip"
	if [ -e /usr/sbin/hni_wan_reload.sh ]; then p8_hni=kept; else p8_hni=made
		printf '#!/bin/sh\necho "hni_wan_reload" >> %s/p8.calls\n' "$RUN" > /usr/sbin/hni_wan_reload.sh; chmod +x /usr/sbin/hni_wan_reload.sh; fi
	printf '#!/bin/sh\necho "odhcpd $*" >> %s/p8.calls\n' "$RUN" > /etc/init.d/odhcpd; chmod +x /etc/init.d/odhcpd
	if [ -d /usr/share/easycwmp/functions ]; then p8_fn=kept; else p8_fn=made; mkdir -p /usr/share/easycwmp/functions; fi
	printf '#!/bin/sh\necho "traceroute_launch $*" >> %s/p8.calls\n' "$RUN" > /usr/share/easycwmp/functions/traceroute_launch
	D=InternetGatewayDevice.Device
	bad_n=0
	# a GET numbers the interfaces without a number and commits them:
	# lan 1, if0 keeps 5, if0_6 2, the anonymous one 3; loopback is out
	start 1 "--readonly"
	wait_done 30; sleep 1
	expect "GET lists" "$($UBUS call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.Device.IP.Interface."}' 2>/dev/null | grep -o 'Interface\.[0-9]*\.Name' | tr '\n' ' ')" \
		"Interface.1.Name Interface.2.Name Interface.3.Name Interface.5.Name "
	expect "lan numbered, committed" "$(grep -A3 "interface 'lan'" /etc/config/network | grep -c "ip_int_instance '1'")" "1"
	expect "if0_6 numbered" "$(uci -q get network.if0_6.ip_int_instance)" "2"
	expect "anonymous numbered" "$(uci -q get network.@interface[4].ip_int_instance)" "3"
	expect "loopback not numbered" "$(uci -q get network.loopback.ip_int_instance)" ""
	expect "Interface.3.Name" "$(dm_value $D.IP.Interface.3.Name)" "@interface[4]"
	expect "Interface.5.Name" "$(dm_value $D.IP.Interface.5.Name)" "if0"
	expect "Interface.2.LowerLayers" "$(dm_value $D.IP.Interface.2.LowerLayers)" "if0"
	expect "Interface.1.Status" "$(dm_value $D.IP.Interface.1.Status)" "Down"
	expect "InterfaceNumberOfEntries" "$(dm_value $D.IP.InterfaceNumberOfEntries)" "4"
	nh=$(cat /sys/class/net/lo/statistics/rx_nohandler 2>/dev/null || echo 0)
	expect "Stats from sysfs" "$(dm_value $D.IP.Interface.3.Stats.UnknownProtoPacketsReceived)" "$nh"
	expect "IPv6Enable (an entry has v6_active 1)" "$(dm_value $D.IP.IPv6Enable)" "true"
	expect "IPv4Status" "$(dm_value $D.IP.IPv4Status)" "Enabled"
	expect "DHCPv6 pools (ghost has no network section)" "$($UBUS call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.Device.DHCPv6."}' 2>/dev/null | grep -o 'Pool\.[0-9]*\.Status' | tr '\n' ' ')" \
		"Pool.1.Status Pool.2.Status "
	expect "dhcp.lan numbered" "$(uci -q get dhcp.lan.dhcpv6_int_instance)" "1"
	expect "Pool.1.Status" "$(dm_value $D.DHCPv6.Server.Pool.1.Status)" "Enabled"
	expect "Pool.2.Interface" "$(dm_value $D.DHCPv6.Server.Pool.2.Interface)" "$D.IP.Interface.5"
	expect "DOCSIS version" "$(dm_value InternetGatewayDevice.DOCSIS.Interface.1.DOCSISVersion)" "3.0"
	expect "DOCSIS upstream ID" "$(dm_value InternetGatewayDevice.DOCSIS.UpstreamChannel.1.ID)" "1"
	expect "DOCSIS modulation" "$(dm_value InternetGatewayDevice.DOCSIS.UpstreamChannel.1.Status.ModulationType)" "default"
	stop
	start 1 "--set $D.IP.Interface.5.Enable=false --set $D.IP.Interface.5.IPv4Enable=false
		--set $D.IP.Interface.1.IPv4Enable=false --set $D.IP.Interface.2.IPv6Enable=false
		--set $D.DHCPv6.Server.Pool.1.Enable=false --set $D.DHCPv6.Server.Pool.2.Interface=$D.IP.Interface.1.
		--set $D.IP.Diagnostics.TraceRoute.Host=example.com --set $D.IP.Diagnostics.TraceRoute.NumberOfTries=2
		--set $D.IP.Diagnostics.TraceRoute.DiagnosticsState=Requested" env PATH="$RUN/p8bin:$PATH"
	wait_done 60; sleep 3
	expect "faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	grep '^fault ' "$RUN/acs.log" | sed 's/^/  ACS: /'
	expect "if0 auto" "$(uci -q get network.if0.auto)" "0"
	expect "wan entry 0 active" "$(uci -q get wan.@entry[0].active)" "0"
	expect "if0 ipv4 + noip" "$(uci -q get network.if0.ipv4) $(uci -q get network.if0.pppd_options)" "0 noip"
	expect "wan entry 0 v4_active" "$(uci -q get wan.@entry[0].v4_active)" "0"
	expect "lan ipv4" "$(uci -q get network.lan.ipv4)" "0"
	expect "if0_6 ipv6, entry 0 v6_active" "$(uci -q get network.if0_6.ipv6) $(uci -q get wan.@entry[0].v6_active)" "0 0"
	expect "dhcp.lan off" "$(uci -q get dhcp.lan.dhcpv6) $(uci -q get dhcp.lan.ra_slaac) $(uci -q get dhcp.lan.ra_dns) $(uci -q get dhcp.lan.ra_flags)" "disabled 1 1 none"
	expect "dhcp.wan interface" "$(uci -q get dhcp.wan.interface)" "lan"
	defdev=$(awk '$2 == "00000000" {print $1; exit}' /proc/net/route)
	expect "traceroute store" "$(uci -q -P /var/state/traceroute get easycwmp.@local[0].Host) $(uci -q -P /var/state/traceroute get easycwmp.@local[0].NumberOfTries) $(uci -q -P /var/state/traceroute get easycwmp.@local[0].DiagnosticsState) $(uci -q -P /var/state/traceroute get easycwmp.@local[0].Interface)" \
		"example.com 2 Requested $defdev"
	want=$(printf '%s\n' "hni_wan_reload" "ifdown if0" "ifup if0" "ip -4 addr flush dev br-lan" "ip -4 route flush dev br-lan" \
		"ifdown if0_6" "ifup if0_6" "odhcpd reload" "traceroute_launch run" | sort | tr '\n' '|')
	expect "queued restarts, once each" "$(sort "$RUN/p8.calls" 2>/dev/null | tr '\n' '|')" "$want"
	stop
	rm -f "${RUN:?}/p8.calls"
	start 1 "--set $D.IP.IPv6Enable=false" env PATH="$RUN/p8bin:$PATH"
	wait_done 30; sleep 1
	expect "IPv6Enable=false on every entry" "$(uci -q get wan.@entry[0].v6_active) $(uci -q get wan.@entry[1].v6_active)" "0 0"
	expect "IPv6Status" "$(dm_value $D.IP.IPv6Status)" "Disabled"
	expect "hni_wan_reload queued" "$(cat "$RUN/p8.calls" 2>/dev/null)" "hni_wan_reload"
	stop
	# AddObject: if1 (if0 is taken), static, auto 0, the first free number 4
	start 1 "--add $D.IP.Interface."
	wait_done 30; sleep 1
	expect "AddObject" "$(grep '^added' "$RUN/acs.log")" "added 4"
	expect "if1 added" "$(uci -q get network.if1) $(uci -q get network.if1.proto) $(uci -q get network.if1.auto) $(uci -q get network.if1.ip_int_instance)" "interface static 0 4"
	stop
	start 1 "--delete $D.IP.Interface.4."
	wait_done 30; sleep 1
	expect "DeleteObject faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "0"
	expect "if1 removed" "$(uci -q get network.if1)" ""
	stop
	# RouteHops: listed for a request below RouteHops. only, while Complete
	uci -q -P /var/state/traceroute set easycwmp.@local[0].DiagnosticsState=Complete
	uci -q -P /var/state/traceroute set easycwmp.@local[0].RouteHopsNumberOfEntries=2
	printf 'traceroute to example.com (93.184.216.34), 30 hops max, 38 byte packets\n 1  192.168.1.1 (192.168.1.1)  0.512 ms  0.401 ms  0.390 ms\n 2  * * *\n' > /var/state/trace_results.txt
	T=$D.IP.Diagnostics.TraceRoute
	start 1 "--readonly"
	wait_done 30; sleep 1
	expect "hops under RouteHops." "$($UBUS call tr069 dm "{\"cmd\":\"get\",\"path\":\"$T.RouteHops.\"}" 2>/dev/null | grep -o 'RouteHops\.[0-9]*\.HopHost"' | tr '\n' ' ')" \
		'RouteHops.1.HopHost" RouteHops.2.HopHost" '
	expect "no hops for TraceRoute." "$($UBUS call tr069 dm "{\"cmd\":\"get\",\"path\":\"$T.\"}" 2>/dev/null | grep -c 'RouteHops\.[0-9]')" "0"
	expect "hop 1 host/address/rtt/error" "$(dm_value $T.RouteHops.1.HopHost) $(dm_value $T.RouteHops.1.HopHostAddress) $(dm_value $T.RouteHops.1.HopRTTTimes) $(dm_value $T.RouteHops.1.HopErrorCode)" \
		"192.168.1.1 192.168.1.1 0.512,0.401,0.390 0"
	expect "hop 2 error" "$(dm_value $T.RouteHops.2.HopHost) $(dm_value $T.RouteHops.2.HopErrorCode)" "* 1"
	stop
	for kv in $T.NumberOfTries=4 $T.DSCP=64 $T.Host=bad_host $T.DiagnosticsState=Complete \
		  $D.DHCPv6.Server.Pool.1.Interface=$D.IP.Interface.99 $D.IP.Interface.1.Enable=maybe \
		  InternetGatewayDevice.DOCSIS.Interface.1.Status=Up; do
		start 1 "--set $kv" env PATH="$RUN/p8bin:$PATH"
		wait_done 30; sleep 1
		expect "$kv faults" "$(grep -c 'Preparing the Fault message' /var/log/icwmpd.log)" "1"
		stop
	done
	for c in $P8_CONFIGS; do
		if [ -f "$RUN/$c.p8saved" ]; then cp "$RUN/$c.p8saved" "/etc/config/$c"; else rm -f "/etc/config/${c:?}"; fi
	done
	[ "$p8_hni" = made ] && rm -f /usr/sbin/hni_wan_reload.sh
	rm -f /etc/init.d/odhcpd /usr/share/easycwmp/functions/traceroute_launch /var/state/trace_results.txt
	[ "$p8_fn" = made ] && rmdir -p /usr/share/easycwmp/functions 2>/dev/null
	rm -rf /var/state/traceroute "${RUN:?}/p8bin"
	if [ $bad_n = 0 ]; then pass "p8: Device.IP numbering/add/delete/queued restarts, DHCPv6 pools, TraceRoute hops, DOCSIS, faults"; else bad "p8: $bad_n mismatches above"; fi
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
	p6) do_p6 ;;
	fw) do_fw ;;
	p7) do_p7 ;;
	p7c) do_p7c ;;
	p8) do_p8 ;;
	all) do_unit; do_smoke; do_notify; do_rpc; do_msrv; do_stun; do_ptime; do_p6; do_fw; do_p7; do_p7c; do_p8; do_valgrind ;;
	*) sed -n '2,/^# Needs/p' "$0"; exit 1 ;;
esac
exit $fail
