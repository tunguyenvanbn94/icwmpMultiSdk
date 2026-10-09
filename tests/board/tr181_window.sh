#!/bin/sh
# Runs ON THE BOARD (dev_181 image, MTK): the same device read as TR-098 and as
# TR-181 (gate T6), without the ACS ever seeing the Device. tree.
#  1. GPV InternetGatewayDevice. as configured (tr098)
#  2. reject all traffic to the ACS host of cwmp.acs.url, stop icwmpd, keep its
#     queued events (.icwmpd_backup_session.xml), the notify list and /etc/config
#  3. cwmp.cpe.datamodel=tr181, start: GPV/GPN Device., writes through TR-181
#     names with the current ParameterKey (Time.Client.1.Servers set and put back, a
#     wrongly typed X_AIS_Conf value that must fail 9007)
#  4. stop, model and saved files back, start, then open the ACS again
# Results in $1 (default /tmp/t6); on the host:
#   python3 docs/issue/tr181-map.py equiv <dir>/tr098.gpv <dir>/tr181.gpv
#   and compare <dir>/config_before.tgz with config_after.tgz.
# Steps 2-4 stop the agent: start it detached so a dropped SSH session cannot
# leave the board in tr181 with the ACS blocked:
#   start-stop-daemon -S -b -m -p /tmp/t6.pid -x /bin/sh -- -c "sh /tmp/tr181_window.sh > /tmp/t6_run.log 2>&1"
D=${1:-/tmp/t6}
ACS=$(uci -q get cwmp.acs.url | sed -n 's|^[a-z]*://\([^:/]*\).*|\1|p')
[ -n "$ACS" ] || { echo "no ACS host in cwmp.acs.url"; exit 1; }
rm -rf "${D:?}"
mkdir -p "$D/bk" || exit 1

val() {
	ubus call tr069 dm "{\"cmd\":\"get\",\"path\":\"$1\"}" 2>/dev/null |
		sed -n 's/^[[:space:]]*"value": "\(.*\)",*$/\1/p' | head -n 1
}
fault_get() {
	ubus call tr069 dm "{\"cmd\":\"get\",\"path\":\"$1\"}" 2>/dev/null |
		sed -n 's/^[[:space:]]*"fault": \([0-9]*\).*/\1/p' | head -n 1
}
fault_set() {
	ubus call tr069 dm "{\"cmd\":\"set\",\"path\":\"$1\",\"value\":\"$2\",\"key\":\"$3\"}" 2>/dev/null |
		sed -n 's/^[[:space:]]*"fault": \([0-9]*\).*/\1/p' | head -n 1
}
log() {
	echo "$*" | tee -a "$D/t6.log"
}

# 1. TR-098, as the ACS sees the device
log "model before: '$(uci -q get cwmp.cpe.datamodel)'"
ubus -t 120 call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice."}' > "$D/tr098.gpv"
log "tr098 dump: $(grep -c '"parameter"' "$D/tr098.gpv") values"

# 2. close the ACS, stop the agent, keep its state and the config
iptables -I OUTPUT 1 -d "$ACS" -j REJECT || { log "iptables failed, nothing changed"; exit 1; }
log "ACS $ACS blocked"
/etc/init.d/icwmpd stop
sleep 2
cp -a /etc/icwmpd/.icwmpd_backup_session.xml /etc/tr098/.dm_enabled_notify "$D/bk/" 2>/dev/null
tar -czf "$D/config_before.tgz" -C / etc/config
old=$(uci -q get cwmp.cpe.datamodel)

# 3. TR-181 window
uci set cwmp.cpe.datamodel=tr181
uci commit cwmp
/etc/init.d/icwmpd start
sleep 20
log "agent: $(ps w | grep -c '[i]cwmp_tr098d')"
ubus call tr069 status > "$D/status181.json"
log "RootDataModelVersion: $(val Device.RootDataModelVersion)"
log "DataModelBackend: $(val Device.X_HNI_Icwmp.DataModelBackend)"
log "IGD path fault: $(fault_get InternetGatewayDevice.DeviceInfo.)"
t0=$(date +%s)
ubus -t 120 call tr069 dm '{"cmd":"get","path":"Device."}' > "$D/tr181.gpv"
t1=$(date +%s)
ubus -t 120 call tr069 dm '{"cmd":"names","path":"Device.","next_level":false}' > "$D/tr181.gpn"
t2=$(date +%s)
log "tr181 dump: $(grep -c '"parameter"' "$D/tr181.gpv") values in $((t1 - t0)) s, names $((t2 - t1)) s"
# writes through TR-181 names, the current ParameterKey kept
key=$(val Device.ManagementServer.ParameterKey)
ntp=$(val Device.Time.Client.1.Servers)
log "Time.Client.1.Servers before: '$ntp' uci '$(uci -q get system.ntp.server)', Status $(val Device.Time.Status)"
log "set Servers (third replaced): fault $(fault_set Device.Time.Client.1.Servers "$(echo "$ntp" | awk -F, -v OFS=, '{$3="t6.pool.ntp.org"; print}')" "$key")"
log "  uci after set: '$(uci -q get system.ntp.server)'"
log "restore Servers: fault $(fault_set Device.Time.Client.1.Servers "$ntp" "$key")"
log "  uci after restore: '$(uci -q get system.ntp.server)'"
log "set Device.X_AIS_Conf.auto_upload_delay=abc: fault $(fault_set Device.X_AIS_Conf.auto_upload_delay abc "$key")"
log "ParameterKey after: '$(val Device.ManagementServer.ParameterKey)' (was '$key')"
# T7 (analysis section 77, 78): Channel in use under automatic selection, a
# PPP address is IPCP, a diagnostic's Interface as a Device.IP.Interface
# reference (written, stored as the device name, read back, then put back)
for r in 1 2; do
	log "Radio $r Channel/ChannelsInUse/AutoChannelEnable: $(val Device.WiFi.Radio.$r.Channel)/$(val Device.WiFi.Radio.$r.ChannelsInUse)/$(val Device.WiFi.Radio.$r.AutoChannelEnable)"
done
wan=$(val Device.NAT.InterfaceSetting.1.Interface)
log "first WAN $wan: IPv4 AddressingType $(val $wan.IPv4Address.1.AddressingType), PPP $(val Device.PPP.InterfaceNumberOfEntries)"
X=Device.XPON.ONU.1.ANI.1
log "XPON: Status $(val $X.Status), PONMode $(val $X.PONMode), ONUState $(val $X.TC.ONUActivation.ONUState), ONUID $(val $X.TC.ONUActivation.ONUID), VendorID $(val $X.TC.ONUActivation.VendorID)"
# T7 S4 (analysis section 82): the interface stack against what the device
# really has (bridge members, netdevs) -- compare on the host with tr181.gpv
for i in 1 2 3 4 5 6; do
	ll=$(val Device.IP.Interface.$i.LowerLayers)
	[ -n "$ll" ] && log "IP.Interface.$i ($(val Device.IP.Interface.$i.Name)) LowerLayers $ll"
done
log "Ethernet.Link $(val Device.Ethernet.LinkNumberOfEntries), VLANTermination $(val Device.Ethernet.VLANTerminationNumberOfEntries), Bridge $(val Device.Bridging.BridgeNumberOfEntries) (Bridge.1 ports $(val Device.Bridging.Bridge.1.PortNumberOfEntries)), InterfaceStack $(val Device.InterfaceStackNumberOfEntries)"
log "Link.1 $(val Device.Ethernet.Link.1.Name) $(val Device.Ethernet.Link.1.Status) $(val Device.Ethernet.Link.1.MACAddress), Link.2 $(val Device.Ethernet.Link.2.Name) $(val Device.Ethernet.Link.2.Status) on $(val Device.Ethernet.Link.2.LowerLayers)"
uci show network | grep -E "=device|\.ports=|\.name=|\.device=" > "$D/network_devices.txt"
ls /sys/class/net > "$D/netdevs.txt"
for b in /sys/class/net/*/brif; do echo "$b: $(ls "$b" | tr '\n' ' ')"; done > "$D/bridge_members.txt" 2>/dev/null
trc=$(uci -q -P /var/state/traceroute get easycwmp.@local[0].Interface)
log "set TraceRoute.Interface=$wan: fault $(fault_set Device.IP.Diagnostics.TraceRoute.Interface "$wan" "$key"), stored '$(uci -q -P /var/state/traceroute get easycwmp.@local[0].Interface)', reads '$(val Device.IP.Diagnostics.TraceRoute.Interface)'"
uci -q -P /var/state/traceroute set easycwmp.@local[0].Interface="$trc"
log "  TraceRoute store put back: '$(uci -q -P /var/state/traceroute get easycwmp.@local[0].Interface)'"
# T7 S5d (analysis section 91): a tcp/udp rule is two NAT.PortMapping rows,
# TCP then UDP; a write on the UDP row splits it.  A disabled test rule,
# firewall_clay put back after
M=Device.NAT.PortMapping
cp /etc/config/firewall_clay "$D/bk/firewall_clay"
pfs() {
	for s in $(uci -q show firewall_clay | sed -n "s/^firewall_clay\.\([^.]*\)\.service_type='t7s5d'$/\1/p"); do
		printf '%s ' "$(uci -q get firewall_clay.$s.protocol)/$(uci -q get firewall_clay.$s.ext_start_port)"
	done
}
n0=$(val Device.NAT.PortMappingNumberOfEntries)
s=$(uci add firewall_clay port_forwarding)
for o in enabled=0 lease_dur=0 interface= service_type=t7s5d protocol=tcp/udp ext_start_port=40001 ext_end_port=40001 \
	local_start_port=40001 local_end_port=40001 ip_address=192.168.1.250; do
	uci set "firewall_clay.$s.$o"
done
uci commit firewall_clay
t=$((n0 + 1)); u=$((n0 + 2))
log "NAT tcp/udp rule: count $n0 -> $(val Device.NAT.PortMappingNumberOfEntries), row $t $(val $M.$t.Protocol)/$(val $M.$t.ExternalPort), row $u $(val $M.$u.Protocol)/$(val $M.$u.ExternalPort)"
log "  set row $u ExternalPort 40002: fault $(fault_set $M.$u.ExternalPort 40002 "$key"), rules $(pfs), rows $(val $M.$t.Protocol)/$(val $M.$t.ExternalPort) $(val $M.$u.Protocol)/$(val $M.$u.ExternalPort), count $(val Device.NAT.PortMappingNumberOfEntries)"
log "  set row $t Protocol TCP/UDP: fault $(fault_set $M.$t.Protocol TCP/UDP "$key")"
log "  delete rows $u and $t: fault $(ubus call tr069 dm "{\"cmd\":\"del\",\"path\":\"$M.$u.\"}" 2>/dev/null | sed -n 's/^[[:space:]]*"fault": \([0-9]*\).*/\1/p') $(ubus call tr069 dm "{\"cmd\":\"del\",\"path\":\"$M.$t.\"}" 2>/dev/null | sed -n 's/^[[:space:]]*"fault": \([0-9]*\).*/\1/p'), count $(val Device.NAT.PortMappingNumberOfEntries), rules '$(pfs)'"
cp "$D/bk/firewall_clay" /etc/config/firewall_clay
rm -f /tmp/.uci/firewall_clay
logread | grep -E 'icwmp|tr069' | tail -n 40 > "$D/logread181.txt"

# 4. back to TR-098, state as before the window, then open the ACS again
/etc/init.d/icwmpd stop
sleep 2
if [ -n "$old" ]; then uci set cwmp.cpe.datamodel="$old"; else uci -q delete cwmp.cpe.datamodel; fi
uci commit cwmp
[ -f "$D/bk/.icwmpd_backup_session.xml" ] && cp -a "$D/bk/.icwmpd_backup_session.xml" /etc/icwmpd/
[ -f "$D/bk/.dm_enabled_notify" ] && cp -a "$D/bk/.dm_enabled_notify" /etc/tr098/
tar -czf "$D/config_after.tgz" -C / etc/config
/etc/init.d/icwmpd start
sleep 10
log "model after: '$(uci -q get cwmp.cpe.datamodel)', IGD ProductClass: '$(val InternetGatewayDevice.DeviceInfo.ProductClass)', Device. fault: $(fault_get Device.DeviceInfo.)"
iptables -D OUTPUT -d "$ACS" -j REJECT && log "ACS unblocked"
log "done"
