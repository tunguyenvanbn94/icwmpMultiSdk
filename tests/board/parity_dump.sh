#!/bin/sh
# Runs ON THE BOARD (MTK product image with icwmpd and the easycwmp library).
# Dumps the whole TR-098 tree twice: from the C data model through icwmpd
# (ubus tr069 dm) and from the product's easycwmp shell library through
# icwmp_dm.sh, values and names.  parity.py compares the two on the host.
#   sh parity_dump.sh [dir]        default /tmp/icwmp_parity
D=${1:-/tmp/icwmp_parity}
R=InternetGatewayDevice.
mkdir -p "$D" || exit 1
t0=$(date +%s)
ubus -t 120 call tr069 dm "{\"cmd\":\"get\",\"path\":\"$R\"}" > "$D/c_get.json"
t1=$(date +%s)
ubus -t 120 call tr069 dm "{\"cmd\":\"names\",\"path\":\"$R\",\"next_level\":false}" > "$D/c_names.json"
t2=$(date +%s)
echo "{\"cmd\":\"get_value\",\"param\":\"$R\"}" | sh /usr/share/icwmp/icwmp_dm.sh --json-input > "$D/sh_get.txt" 2> "$D/sh_get.err"
t3=$(date +%s)
echo "{\"cmd\":\"get_name\",\"param\":\"$R\",\"next_level\":\"0\"}" | sh /usr/share/icwmp/icwmp_dm.sh --json-input > "$D/sh_names.txt" 2> "$D/sh_names.err"
t4=$(date +%s)
echo "c_get $((t1 - t0)) s, c_names $((t2 - t1)) s, sh_get $((t3 - t2)) s, sh_names $((t4 - t3)) s" | tee "$D/times.txt"
