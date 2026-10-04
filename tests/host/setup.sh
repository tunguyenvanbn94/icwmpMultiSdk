#!/bin/sh
# Prepare THIS machine as a fake CPE for the host test.  It overwrites system
# files (/etc/config/cwmp, /etc/config/easycwmp, /etc/config/stun,
# /etc/init.d/stuncd, /usr/share/icwmp,
# /usr/sbin/icwmp, /sbin/uci, /usr/bin/ubus, /usr/share/libubox/jshn.sh,
# /lib/functions.sh) and starts ubusd: run it as root in a throwaway
# container, never on a workstation or a board.
set -e
. "$(dirname "$0")/env.sh"
[ "$1" = "--yes" ] || { sed -n '2,7p' "$0"; echo "then: $0 --yes"; exit 1; }
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
command -v busybox >/dev/null || { echo "missing busybox (the product's scripts run under ash)"; exit 1; }
[ -x "$PREFIX/bin/ubus" ] || { echo "run build.sh first"; exit 1; }

mkdir -p /etc/config /etc/icwmpd /etc/tr098 /var/run/ubus /var/state /var/log /usr/share/icwmp /usr/share/libubox /tmp/.uci
cp "$APP_SRC/sdk/mtk/files/cwmp" /etc/config/cwmp
cat > /etc/config/easycwmp <<'EOC'
config local
	option enable '1'
	option interface 'lo'
	option port '7547'
	option path ''
	option username 'cr'
	option password 'crpass'
	option provisioning_code ''
	option logging_level '3'
	option network 'lan'

config acs
	option enablecwmp '1'
	option url 'http://127.0.0.1:18080/acs'
	option username 'acs'
	option password 'acspass'
	option periodic_enable '1'
	option periodic_interval '86400'
	option parameter_key ''
	option ssl_verify 'disable'

config device
	option manufacturer 'Test'
	option oui '001122'
	option product_class 'HostSim'
	option serial_number 'SN0001'
	option software_version '1.0'
	option hardware_version '1.0'
	option modelname 'Host'
	option description ''
EOC
# the product's stunclient config (stun.@stun[0], what the STUN leaves read
# and write) and a stand-in of its init script that only logs the call
cat > /etc/config/stun <<'EOC'
config stun
	option stun_enable '0'
	option serveraddress ''
	option serverport '3478'
	option username ''
	option password ''
	option min_keepalive '30'
	option max_keepalive '60'
	option natdetect '0'
	option udpcontnreqaddr ''
EOC
mkdir -p /etc/init.d
printf '#!/bin/sh\necho "$*" >> %s/stuncd.calls\n' "$RUN" > /etc/init.d/stuncd
chmod +x /etc/init.d/stuncd
ln -sf "$PREFIX/bin/uci" /sbin/uci
ln -sf "$PREFIX/bin/ubus" /usr/bin/ubus
ln -sf "$PREFIX/bin/jshn" /usr/bin/jshn
cp "$WORK/src/libubox/sh/jshn.sh" /usr/share/libubox/jshn.sh
[ -f /lib/functions.sh ] || echo '# host test stub' > /lib/functions.sh
uci set cwmp.cpe.interface=lo
uci set cwmp.cpe.log_to_file=enable
uci set cwmp.cpe.log_severity=INFO
uci commit cwmp

# data model shell: the stand-in of fake_dm.py (no easycwmp library here)
cat > /usr/share/icwmp/icwmp_dm.sh <<EOS
#!/bin/sh
FAKE_DM_LOG=$RUN/fake_dm.cmds FAKE_DM_EPOCH=$RUN/epoch FAKE_DM_MATRIX=$MATRIX exec python3 $HOST_DIR/fake_dm.py "\$@"
EOS
chmod +x /usr/share/icwmp/icwmp_dm.sh
# external action backend: the product's own script, under ash like on the board
cp "$APP_SRC/sdk/mtk/scripts/icwmp.sh" /usr/sbin/icwmp.real
printf '#!/bin/sh\nexec busybox sh /usr/sbin/icwmp.real "$@"\n' > /usr/sbin/icwmp
chmod +x /usr/sbin/icwmp /usr/sbin/icwmp.real

if ! $UBUS list >/dev/null 2>&1; then
	"$PREFIX/sbin/ubusd" -s /var/run/ubus/ubus.sock >/dev/null 2>&1 &
	sleep 0.5
fi
$UBUS list >/dev/null 2>&1 && echo "ok: ubusd up, fake CPE ready"
