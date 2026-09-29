# Lệnh debug — icwmp_tr098d trên MTK OpenWrt (SDK `mtk`)

Áp dụng cho bản patch `0033` (overlay `cd93685`, tarball `icwmp_mtk_port.tar.gz`
sha256 `841b582c0f531b28`). Board HP2236B, SDK 2025q3.

> Bản BDK dùng file khác: `brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/debug-commands.md`.

## 0. Xác nhận đúng bản đang chạy

```sh
pidof icwmp_tr098d                       # có PID = bản mới đang chạy
pidof easycwmpd                          # phải RỖNG (gói cũ đã tắt)
ls -l /etc/init.d/icwmpd /etc/init.d/easycwmpd
strings /usr/sbin/icwmp_tr098d | grep -c icwmp_platform_end_session   # >0 = có patch 0032+
strings /usr/lib/libtr098.so | grep -m1 -E 'mtk-c\+script|mtk-c'      # backend đang dùng:
#   mtk-c+script = có lớp compat (mặc định)    mtk-c = bản C thuần (--disable-dm-script-compat)
strings /usr/lib/libtr098.so | grep -E 'mtk-deviceinfo|mtk-time'      # module C đã link
ls -l /usr/share/icwmp/icwmp_dm.sh /usr/sbin/icwmp
ls /usr/share/easycwmp/functions | wc -l                              # ~63 file thư viện hàm
```

## 1. Vận hành app

```sh
/etc/init.d/icwmpd start | stop | restart | reload
/etc/init.d/easycwmpd restart            # wrapper, gọi lại icwmpd (đường WebUI/HAL dùng)

ubus call tr069 status                   # trạng thái session, acs_url, lần cuối thành công/thất bại
ubus call tr069 notify                   # chạy ngay vòng kiểm value-change
ubus call tr069 inform '{"event":"6 CONNECTION REQUEST"}'   # ép một session
ubus call tr069 inform '{"event":"2 PERIODIC"}'
ubus call tr069 command '{"command":"reload"}'              # nạp lại config (mirror easycwmp->cwmp)
ubus call tr069 command '{"command":"exit"}'
```

## 2. Log

```sh
# log của icwmpd
tail -f /var/log/icwmpd.log
uci set cwmp.cpe.log_severity=DEBUG && uci commit cwmp && /etc/init.d/icwmpd reload
#   EMERG ALERT CRITIC ERROR WARNING NOTICE INFO DEBUG
#   (mirror từ easycwmp.@local[0].logging_level 0..4 mỗi lần reload — muốn giữ DEBUG thì
#    đặt luôn easycwmp.@local[0].logging_level=4)

# log của cầu nối shell (dmscript.c): spawn, timeout, dòng không phải JSON
tail -f /tmp/icwmp_dm.log

# lỗi shell của chính thư viện hàm easycwmp (mặc định bị nuốt vào /dev/null)
uci set cwmp.cpe.dm_script_debug=1 && uci commit cwmp && /etc/init.d/icwmpd restart
#   -> stderr của shell con ra console/stderr của icwmpd; tắt lại bằng =0 + restart
```

## 2b. Path nào đang do C phục vụ, path nào còn qua shell

`dm_platform_name()` cho biết bản build, còn ai phục vụ một path cụ thể thì xem bằng độ trễ và
bằng log của cầu nối: một GPV vào object đã port sang C **không** sinh dòng nào trong
`/tmp/icwmp_dm.log`.

```sh
: > /tmp/icwmp_dm.log
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo."}' >/dev/null
wc -l /tmp/icwmp_dm.log          # 0 dòng = DeviceInfo do module C trả lời (đúng, P1 đã xong)

: > /tmp/icwmp_dm.log
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.LANDevice."}' >/dev/null
wc -l /tmp/icwmp_dm.log          # >0 = vẫn qua shell (đúng, P2/P3 chưa làm)
```

Danh sách module C đã link vào bản build (và số path mỗi module sở hữu) được in ra stderr khi gọi
`dm_registry_dump()`; hiện chỉ gọi khi debug, dùng `strings` như §0 để xem nhanh.

## 3. Data model — không cần ACS

`ubus call tr069 dm` chạy đúng code path của RPC thật (GPV/GPN/SPV/Add/Del/GPA/SPA/Inform).

```sh
# GetParameterValues
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo."}'
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.ManagementServer.ConnectionRequestURL"}'

# GetParameterNames (next_level)
ubus call tr069 dm '{"cmd":"names","path":"InternetGatewayDevice.","next_level":true}'
ubus call tr069 dm '{"cmd":"names","path":"InternetGatewayDevice.LANDevice.1.WLANConfiguration.","next_level":true}'

# SetParameterValues (kèm ParameterKey)
ubus call tr069 dm '{"cmd":"set","path":"InternetGatewayDevice.ManagementServer.PeriodicInformInterval","value":"300","key":"k1"}'

# AddObject / DeleteObject
ubus call tr069 dm '{"cmd":"add","path":"InternetGatewayDevice.X_HNI_IPFiltering.Entry.","key":"k2"}'
ubus call tr069 dm '{"cmd":"del","path":"InternetGatewayDevice.X_HNI_IPFiltering.Entry.3.","key":"k3"}'

# Attributes (notification)
ubus call tr069 dm '{"cmd":"attr","path":"InternetGatewayDevice.ManagementServer."}'
ubus call tr069 dm '{"cmd":"setattr","path":"InternetGatewayDevice.DeviceInfo.SoftwareVersion","value":"2"}'

# Tham số forced-inform của Inform kế tiếp + DeviceId
ubus call tr069 dm '{"cmd":"inform","path":""}'

# Dump CẢ CÂY ra file (tránh giới hạn kích thước message ubus)
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.","file":"/tmp/dm_full.txt"}'
wc -l /tmp/dm_full.txt        # cột: path <tab> type <tab> value
ubus call tr069 dm '{"cmd":"names","path":"InternetGatewayDevice.","file":"/tmp/dm_names.txt"}'
```

Trường trả về: `fault` (0 = ok), `faults[]` (fault theo từng tham số của SPV),
`count`, `parameters[]` hoặc `file`, `instance` (Add), `reloaded`, `end_session[]`.

## 4. So sánh với bản cũ (hồi quy data model)

Chạy **trước** khi thay gói (còn `easycwmpd`), rồi chạy lại sau khi thay và diff:

```sh
# bản cũ (easycwmpd còn chạy)
/usr/sbin/easycwmp get name "InternetGatewayDevice." 0 > /tmp/old_names.json
/usr/sbin/easycwmp get value "InternetGatewayDevice." > /tmp/old_values.json

# bản mới
ubus call tr069 dm '{"cmd":"names","path":"InternetGatewayDevice.","file":"/tmp/new_names.txt"}'
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.","file":"/tmp/new_values.txt"}'

# so tập tên (bỏ giá trị, chỉ quan tâm path + writable)
sed -n 's/.*"parameter":"\([^"]*\)".*"writable":"\([^"]*\)".*/\1\t\2/p' /tmp/old_names.json | sort > /tmp/a
cut -f1,2 /tmp/new_names.txt | sort > /tmp/b
diff /tmp/a /tmp/b | head -50        # rỗng = cây giống hệt bản cũ
```

Gọi thẳng driver mà không qua icwmpd (khi nghi ngờ lỗi ở tầng bridge):

```sh
sh /usr/share/icwmp/icwmp_dm.sh get_value "InternetGatewayDevice.DeviceInfo."
sh /usr/share/icwmp/icwmp_dm.sh get_name "InternetGatewayDevice." 1
sh /usr/share/icwmp/icwmp_dm.sh inform

# chế độ JSON như icwmpd dùng (Ctrl-D để thoát)
sh /usr/share/icwmp/icwmp_dm.sh --json-input <<'EOF'
{"cmd":"ping"}
{"cmd":"get_value","param":"InternetGatewayDevice.ManagementServer."}
{"cmd":"get_name","param":"InternetGatewayDevice.","next_level":"1"}
EOF
```

## 5. Cầu nối shell còn sống không

```sh
ps | grep -c '[i]cwmp_dm.sh'             # 1 = shell con đang sống (spawn ở RPC đầu tiên)
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo.UpTime"}'  # ping thực tế
grep -E 'spawned|timeout|closed its stdout' /tmp/icwmp_dm.log | tail
```

Triệu chứng và hướng xử lý:

| Triệu chứng | Nguyên nhân hay gặp |
|---|---|
| `fault 9002` mọi RPC, `/tmp/icwmp_dm.log` ghi `script ... not readable` | thiếu `/usr/share/icwmp/icwmp_dm.sh` (gói `libtr098` chưa cài) |
| `timeout (240 s) waiting for the prompt` | một setter/getter trong thư viện hàm treo (thường là lệnh gọi ubus tới service đã chết). Bật `dm_script_debug=1` rồi chạy lại đúng path đó bằng §4 |
| `child ... closed its stdout` ngay lần đầu | lỗi cú pháp trong một file `/usr/share/easycwmp/functions/*` → `sh -n` từng file |
| RPC đầu chạy, RPC thứ hai `9002` | handler nào đó `exit` không nằm trong subshell (xem `handle_request` trong driver) |

## 6. Config: hai UCI và chiều mirror

```sh
uci show easycwmp                        # config of record (WebUI, DHCP option 43, stuncd)
uci show cwmp                            # config của icwmpd (mirror + phần riêng)
grep -E 'sync (easycwmp->cwmp|cwmp->easycwmp)' /var/log/icwmpd.log | tail -20
```

- Đổi ACS: sửa `easycwmp.@acs[0].*` rồi `/etc/init.d/icwmpd reload`.
- Chỉ có ở `cwmp`: `amd_version`, `session_timeout`, `cr_host`/`cr_port` (NAT),
  `dm_script_debug`, `notification`, danh sách `@notifications[0]`.
- Đọc/sửa qua ACS: object `InternetGatewayDevice.X_HNI_Icwmp.`
  (`AmdVersion`, `LogSeverity`, `SessionTimeout`, `ConnectionRequestPort`,
  `ConnectionRequestHost`, `ConnectionRequestExternalPort`, `DataModelBackend`; `DataModel` trả
  9001 trên MTK).

## 7. Connection Request

```sh
uci get easycwmp.@local[0].path          # thường 'ConnectionRequest'
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.ManagementServer.ConnectionRequestURL"}'
netstat -ltnp | grep icwmp               # nghe cwmp.cpe.port (mirror từ easycwmp.@local[0].port)
iptables -S input_wan_rule | grep easycwmp-wan-access

# thử từ LAN/WAN (đúng path, có Digest)
curl -v --digest -u "$(uci get easycwmp.@local[0].username):$(uci get easycwmp.@local[0].password)" \
     "http://<ip-wan>:$(uci get easycwmp.@local[0].port)/$(uci get easycwmp.@local[0].path)"
grep -E 'Connection Request' /var/log/icwmpd.log | tail
```

Sau NAT (AP nằm sau ONT): `uci set cwmp.cpe.cr_host='<ip ONT>'; uci set cwmp.cpe.cr_port='<port forward>'; uci commit cwmp; /etc/init.d/icwmpd reload`.

## 8. Notification / value change

```sh
cat /etc/tr098/.dm_enabled_notify        # 1 dòng JSON/tham số có notification (giá trị tham chiếu)
uci show cwmp.@notifications[0]
ubus call tr069 notify                   # chạy vòng kiểm ngay
pidof value_monitoring                   # vòng poll 30 s (init script khởi động)
```

## 9. Bắt session với ACS

```sh
tcpdump -i any -s0 -w /tmp/cwmp.pcap 'tcp port 7547 or host <acs-ip>'
grep -E 'SOAP|Inform|HTTP/1.1 [0-9]+' /var/log/icwmpd.log | tail -40
```

## 9b. Xoá bớt SDK khi giao code

```sh
cd <2025q3>/tclinux_phoenix/apps/hni/libtr098   && ./tools/sdk-prune.sh mtk
cd <2025q3>/tclinux_phoenix/apps/hni/icwmp_tr098 && ./tools/sdk-prune.sh mtk
# hoặc ngay lúc cài:  ./install-mtk.sh <2025q3> --only-mtk
./tools/sdk-prune.sh --list        # còn SDK nào
./tools/sdk-scan.sh --check        # sdk/enabled.* có khớp thư mục đang có không
```

Sau khi prune phải build lại từ đầu gói đó (`make package/feeds/airoha/<pkg>/{clean,compile}`),
vì `configure` được sinh lại.

## 9c. Bỏ hẳn lớp shell (khi các phase đã xong)

```sh
# trong feeds/libtr098/Makefile và feeds/icwmp_tr098/Makefile, thêm vào ./configure:
#     --disable-dm-script-compat
# rồi:
rm -rf <libtr098>/sdk/mtk/compat
```

Dấu hiệu đã ở chế độ C thuần: `strings /usr/lib/libtr098.so | grep mtk-c` ra `mtk-c` (không có
`+script`), không còn tiến trình `icwmp_dm.sh`, tham số lạ trả **9005** thay vì 9002.

## 10. Quay lại bản cũ (rollback)

```sh
# trên board: chỉ cần flash lại image cũ
# trên cây source: install-mtk.sh đã backup
ls -d <2025q3>/tclinux_phoenix/apps/hni/*.bak-*
# khôi phục thư mục + đảo 3 dòng config_7583 (cwmpclient=y, icwmp_tr098/libtr098 not set)
```

## Trace khởi động icwmpd (`0063`, overlay `01a1884`) — MTK và BDK

Luôn bật, chỉ ghi lúc khởi động và khi crash. Không có lệnh tắt; bỏ bằng cách không apply `0063`.

```sh
/etc/init.d/icwmpd restart; sleep 20
cat /tmp/icwmpd_boot.log                 # dòng "icwmpd-boot [...]" cuối cùng = bước cuối đã qua
logread | grep icwmpd-boot | tail -30    # cùng nội dung, qua procd
```

| Dòng cuối | Nghĩa |
|---|---|
| `... is locked by another process: EXIT 0` | tiến trình khác giữ `/var/run/icwmpd.pid`: `ls -l /proc/[0-9]*/fd \| grep icwmpd.pid` |
| `global_conf_init failed, error N` | lỗi đọc `cwmp.acs`/`cwmp.cpe` |
| `deviceid: reading identity ...` rồi hết | treo hoặc chết trong data model (coprocess `icwmp_dm.sh`) |
| `CRASH sig=11 ... pc=... lr=...` | crash, xem bên dưới |
| `ubus_connect(...) failed` | socket `cwmp.cpe.ubus_socket` sai |
| `ubus object tr069 registered` + `threads created` | khởi động xong |

Crash: lấy `pc`, tìm dòng `r-xp` trong phần maps chứa `pc`, offset = `pc - start + file_offset`, rồi
trên máy build:

```sh
TC=staging_dir/toolchain-aarch64_cortex-a53_gcc-10.2.0_musl/bin
$TC/aarch64-openwrt-linux-addr2line -f -C -e build_dir/target-aarch64_cortex-a53_musl/<gói>/<file chưa strip> <offset>
```
