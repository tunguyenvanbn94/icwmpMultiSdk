#!/bin/sh
# Runs ON THE BOARD (Broadcom BDK MO77300EB, image with icwmp): does a write
# through the data model reach the running system, in both models?
#
# For tr181 then tr098 (cwmp.cpe.datamodel, switched with a config reload),
# each case: read the value, write a new one through "ubus call tr069 dm set"
# (the same path as an ACS SetParameterValues, end of session included: MDM
# write, RCL apply, flash save), wait, check the runtime (netdev, udhcpd
# config, wl/nvram, nft), write the old value back, check again.
#   WAN   IP interface MTU                      -> stored in the MDM (the SDK does
#         not apply a new MTU to a running WAN interface: netdev printed as INFO)
#   LAN   DHCP pool MinAddress, lease time      -> /var/udhcpd/udhcpd.conf
#   WiFi  a disabled 2.4 GHz guest SSID: name, enable, passphrase
#                                               -> wl ssid / bss, nvram psk
#         2.4 GHz radio: 20 MHz, fixed channel 11 -> wl chanspec (with Auto
#         bandwidth the SDK writes chanspec 11l and the 20/40 coexistence of
#         acsd moves the radio elsewhere: analysis 94)
#   NAT   PortMapping add (Description set by icwmp), set, delete -> nft ruleset
# The ACS is unreachable for the whole run (blackhole route to the host of
# cwmp.acs.url), the model and the ACS are put back at the end.  The Wi-Fi
# cases restart the radios: run it detached, a Wi-Fi SSH session drops:
#   ( trap '' HUP; sh /tmp/bdk_apply_check.sh ) > /tmp/bdk_apply.log 2>&1 < /dev/null &
# Result lines: "PASS <case>" / "FAIL <case>: <why>", then "RESULT: PASS|FAIL".
# Instances (BDK MO77300EB): WAN IP.Interface 2 (eth1.1), LAN pool 1 (br0),
# radio 3 = 2.4 GHz (wl2), guest SSID 20 (wl2.3) -- override with env vars.

UCI="uci -c /data/icwmp/config"
WAN_IF=${WAN_IF:-2}
GUEST=${GUEST:-20}
RADIO=${RADIO:-3}
RADIO_SSID=${RADIO_SSID:-17}
WAIT_WIFI=${WAIT_WIFI:-45}
WAIT_SVC=${WAIT_SVC:-10}
fails=0

val() {
	ubus -t 120 call tr069 dm "{\"cmd\":\"get\",\"path\":\"$1\"}" 2>/dev/null |
		sed -n 's/^[[:space:]]*"value": "\(.*\)",*$/\1/p' | sed -n 1p
}
set1() {
	ubus -t 180 call tr069 dm "{\"cmd\":\"set\",\"path\":\"$1\",\"value\":\"$2\",\"key\":\"$KEY\"}" 2>/dev/null |
		sed -n 's/^[[:space:]]*"fault": \([0-9]*\).*/\1/p' | sed -n 1p
}
dmcmd() {
	ubus -t 180 call tr069 dm "{\"cmd\":\"$1\",\"path\":\"$2\"}" 2>/dev/null
}
ok() { echo "PASS $*"; }
ko() { echo "FAIL $*"; fails=$((fails + 1)); }
check() {	# check <case> <expected> <actual>
	if [ "$2" = "$3" ]; then ok "$1 ($3)"; else ko "$1: expected '$2', got '$3'"; fi
}
conf() { sed -n "s/^$1[[:space:]][[:space:]]*//p" /var/udhcpd/udhcpd.conf | sed -n 1p; }
mtu() { ip link show "$1" 2>/dev/null | sed -n 's/.* mtu \([0-9]*\).*/\1/p'; }
wlssid() { wl -i "$1" ssid 2>/dev/null | sed -n 's/^Current SSID: "\(.*\)"$/\1/p'; }
wlbss() { wl -i "$1" bss 2>/dev/null; }
wlchan() { wl -i "$1" chanspec 2>/dev/null | sed -n 's/^\([0-9]*\).*/\1/p'; }

# one model: $1 = tr181 | tr098
run_model() {
	m=$1
	if [ "$m" = tr181 ]; then
		R=Device; KEY=$(val Device.ManagementServer.ParameterKey)
		WAN=Device.IP.Interface.$WAN_IF
		POOL=Device.DHCPv4.Server.Pool.1
		LEASE=$POOL.LeaseTime
		SSIDP=Device.WiFi.SSID.$GUEST
		AP=$(for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24; do
			[ "$(val Device.WiFi.AccessPoint.$i.SSIDReference)" = "Device.WiFi.SSID.$GUEST" ] && echo $i && break; done)
		PSK=Device.WiFi.AccessPoint.$AP.Security.KeyPassphrase
		CH=Device.WiFi.Radio.$RADIO
		BW=$CH.OperatingChannelBandwidth
		PMROOT=Device.NAT.PortMapping
	else
		R=InternetGatewayDevice; KEY=$(val InternetGatewayDevice.ManagementServer.ParameterKey)
		WAN=$(ubus -t 120 call tr069 dm '{"cmd":"names","path":"InternetGatewayDevice.WANDevice.","next_level":false}' 2>/dev/null |
			sed -n 's/^[[:space:]]*"parameter": "\(.*WANIPConnection\.[0-9]*\)\.MaxMTUSize",*$/\1/p' | sed -n 1p)
		POOL=InternetGatewayDevice.LANDevice.1.LANHostConfigManagement
		LEASE=$POOL.DHCPLeaseTime
		SSIDP=InternetGatewayDevice.LANDevice.1.WLANConfiguration.$GUEST
		PSK=$SSIDP.KeyPassphrase
		CH=InternetGatewayDevice.LANDevice.1.WLANConfiguration.$RADIO_SSID
		BW=InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.Radio.$RADIO.OperatingChannelBandwidth
		PMROOT=$WAN.PortMapping
	fi
	echo "== $m: WAN $WAN, pool $POOL, guest $SSIDP (psk $PSK), channel on $CH, key '$KEY'"
	[ -n "$WAN" ] || { ko "$m WAN connection not found"; return; }
	# the netdev: TR-181 IP.Interface Name; TR-098 Name is the connection name
	wanif=$(val $WAN.X_MARUSYS_COM_IfName); [ -n "$wanif" ] || wanif=$(val $WAN.Name)
	gif=$(val $SSIDP.Name); [ -n "$gif" ] || gif=wl2.3
	rif=$(val Device.WiFi.Radio.$RADIO.Name 2>/dev/null); [ -n "$rif" ] || rif=wl2

	# WAN MTU
	old=$(val $WAN.MaxMTUSize)
	check "$m WAN set MaxMTUSize 1400 fault" 0 "$(set1 $WAN.MaxMTUSize 1400)"
	sleep $WAIT_SVC
	check "$m WAN MaxMTUSize stored" 1400 "$(val $WAN.MaxMTUSize)"
	echo "   INFO $m WAN $wanif mtu $(mtu $wanif) (not applied to a running WAN by the SDK)"
	check "$m WAN restore MaxMTUSize $old fault" 0 "$(set1 $WAN.MaxMTUSize $old)"
	sleep $WAIT_SVC
	check "$m WAN MaxMTUSize restored" "$old" "$(val $WAN.MaxMTUSize)"

	# LAN DHCP pool
	omin=$(val $POOL.MinAddress); olease=$(val $LEASE)
	check "$m LAN set MinAddress 192.168.1.120 fault" 0 "$(set1 $POOL.MinAddress 192.168.1.120)"
	check "$m LAN set lease 7200 fault" 0 "$(set1 $LEASE 7200)"
	sleep $WAIT_SVC
	check "$m LAN udhcpd start" 192.168.1.120 "$(conf start)"
	check "$m LAN udhcpd lease" 7200 "$(conf 'option lease')"
	check "$m LAN dhcp server running" yes "$([ -n "$(pidof dhcpd udhcpd)" ] && echo yes)"
	check "$m LAN restore MinAddress fault" 0 "$(set1 $POOL.MinAddress $omin)"
	check "$m LAN restore lease fault" 0 "$(set1 $LEASE $olease)"
	sleep $WAIT_SVC
	check "$m LAN udhcpd start restored" "$omin" "$(conf start)"
	check "$m LAN udhcpd lease restored" "$olease" "$(conf 'option lease')"

	# Wi-Fi guest SSID: name, passphrase, enable
	oname=$(val $SSIDP.SSID); oen=$(val $SSIDP.Enable); opsk=$(nvram get ${gif}_wpa_psk)
	check "$m WiFi set SSID fault" 0 "$(set1 $SSIDP.SSID icwmp_$m)"
	check "$m WiFi set passphrase fault" 0 "$(set1 $PSK icwmpTest$m)"
	check "$m WiFi set Enable fault" 0 "$(set1 $SSIDP.Enable true)"
	sleep $WAIT_WIFI
	check "$m WiFi nvram ${gif}_ssid" "icwmp_$m" "$(nvram get ${gif}_ssid)"
	check "$m WiFi wl $gif ssid" "icwmp_$m" "$(wlssid $gif)"
	check "$m WiFi wl $gif bss" up "$(wlbss $gif)"
	[ "$(nvram get ${gif}_wpa_psk)" = "icwmpTest$m" ] && ok "$m WiFi nvram psk is the new one" || ko "$m WiFi nvram psk not the new one"
	check "$m WiFi read passphrase (secured)" "" "$(val $PSK)"
	check "$m WiFi restore Enable fault" 0 "$(set1 $SSIDP.Enable $oen)"
	check "$m WiFi restore SSID fault" 0 "$(set1 $SSIDP.SSID $oname)"
	check "$m WiFi restore passphrase fault" 0 "$(set1 $PSK "$opsk")"
	sleep $WAIT_WIFI
	check "$m WiFi nvram ${gif}_ssid restored" "$oname" "$(nvram get ${gif}_ssid)"
	[ "$(nvram get ${gif}_wpa_psk)" = "$opsk" ] && ok "$m WiFi nvram psk restored" || ko "$m WiFi nvram psk not restored"
	b=$(wlbss $gif)
	[ "$b" = down ] || [ -z "$b" ] && ok "$m WiFi $gif down or removed (${b:-removed})" || ko "$m WiFi $gif still '$b'"

	# Wi-Fi 2.4 GHz channel
	oauto=$(val $CH.AutoChannelEnable); obw=$(val $BW)
	check "$m WiFi set bandwidth 20MHz fault" 0 "$(set1 $BW 20MHz)"
	check "$m WiFi set AutoChannelEnable false fault" 0 "$(set1 $CH.AutoChannelEnable false)"
	check "$m WiFi set Channel 11 fault" 0 "$(set1 $CH.Channel 11)"
	sleep $WAIT_WIFI
	check "$m WiFi wl $rif chanspec" 11 "$(wlchan $rif)"
	check "$m WiFi restore AutoChannelEnable fault" 0 "$(set1 $CH.AutoChannelEnable $oauto)"
	check "$m WiFi restore bandwidth fault" 0 "$(set1 $BW $obw)"
	sleep $WAIT_WIFI
	echo "   $m channel after restore: $(wlchan $rif) (auto=$(val $CH.AutoChannelEnable))"

	# NAT port mapping
	inst=$(dmcmd add $PMROOT. | sed -n 's/^[[:space:]]*"instance": "\([0-9]*\)".*/\1/p')
	[ -n "$inst" ] && ok "$m NAT add PortMapping ($inst)" || { ko "$m NAT add PortMapping"; return; }
	P=$PMROOT.$inst
	if [ "$m" = tr181 ]; then d=$(val $P.Description); else d=$(val $P.PortMappingDescription); fi
	[ -n "$d" ] && ok "$m NAT new PortMapping has a Description ($d)" || ko "$m NAT new PortMapping without Description (the SDK cannot delete its rules)"
	if [ "$m" = tr181 ]; then
		check "$m NAT set Interface fault" 0 "$(set1 $P.Interface $WAN)"
		check "$m NAT set Protocol fault" 0 "$(set1 $P.Protocol TCP)"
		EN=$P.Enable
	else
		check "$m NAT set Protocol fault" 0 "$(set1 $P.PortMappingProtocol TCP)"
		EN=$P.PortMappingEnabled
	fi
	check "$m NAT set ExternalPort fault" 0 "$(set1 $P.ExternalPort 40001)"
	check "$m NAT set InternalPort fault" 0 "$(set1 $P.InternalPort 22)"
	check "$m NAT set InternalClient fault" 0 "$(set1 $P.InternalClient 192.168.1.250)"
	check "$m NAT set Enable fault" 0 "$(set1 $EN true)"
	sleep $WAIT_SVC
	n=$(nft list ruleset 2>/dev/null | grep -c 40001)
	[ "$n" -gt 0 ] && ok "$m NAT nft has 40001 ($n)" || ko "$m NAT nft has no rule for 40001"
	check "$m NAT delete fault" "" "$(dmcmd del $P. | sed -n 's/^[[:space:]]*"fault": \([1-9][0-9]*\).*/\1/p')"
	sleep $WAIT_SVC
	check "$m NAT nft rule gone" 0 "$(nft list ruleset 2>/dev/null | grep -c 40001)"
}

ACS=$($UCI -q get cwmp.acs.url | sed -n 's|^[a-z]*://\([^:/]*\).*|\1|p')
[ -n "$ACS" ] || { echo "no ACS host in cwmp.acs.url"; exit 1; }
model0=$($UCI -q get cwmp.cpe.datamodel)
echo "start $(date), model '$model0', ACS $ACS blocked"
ip route add blackhole "$ACS/32" || { echo "cannot block the ACS, nothing changed"; exit 1; }

for m in tr181 tr098; do
	if [ "$($UCI -q get cwmp.cpe.datamodel)" != "$m" ]; then
		$UCI set cwmp.cpe.datamodel=$m && $UCI commit cwmp
		ubus call tr069 command '{"command":"reload"}' >/dev/null 2>&1
		sleep 5
	fi
	run_model $m
done

# model as found, ACS open again
if [ -n "$model0" ]; then $UCI set cwmp.cpe.datamodel="$model0"; else $UCI -q delete cwmp.cpe.datamodel; fi
$UCI commit cwmp
ubus call tr069 command '{"command":"reload"}' >/dev/null 2>&1
sleep 5
ip route del blackhole "$ACS/32"
echo "end $(date), model '$($UCI -q get cwmp.cpe.datamodel)', ACS unblocked"
[ $fails -eq 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL ($fails)"
