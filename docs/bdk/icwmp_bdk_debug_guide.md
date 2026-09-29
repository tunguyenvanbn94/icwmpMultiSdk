# iCWMP trên BDK — debug guide: log level, dump data model, giả lập ACS, soi flow

Scope: lệnh chạy **trên board `MO77300EB`** (trừ khi ghi "host"/"ACS") để vận hành và debug
`icwmpd` + `libtr098` backend BDK: bật log, xem process/bus, dump toàn bộ hoặc một phần data model
ở cả hai model (TR-098 `InternetGatewayDevice.` / TR-181 `Device.`), giả lập RPC của ACS không cần
ACS, và đọc log để biết một request đi tới đâu. Flow nền: [icwmp_bdk_runtime_flow.md](icwmp_bdk_runtime_flow.md).
So sánh hai model: [icwmp_datamodel_tr098_tr181_matrix.md](icwmp_datamodel_tr098_tr181_matrix.md).
Lệnh theo gate của lần test cụ thể (build, 401, crash): issue
[debug-commands.md](../issues/20260916_tr069_app_use_icwmp/debug-commands.md).

Snapshot: overlay HEAD `e5e5de4` (patch `0030`, 20/09/2026). Lệnh `tr069 dm` với `attr/setattr/inform/file`
cần image từ `0028`; các lệnh khác từ `0017`. **Chưa board-test** (`0017`..`0028` chưa flash) — output mẫu
là kỳ vọng theo source, không phải log thật. Mask password trước khi dán log vào issue.

## START HERE — muốn gì, gõ gì

| Muốn | Lệnh |
|---|---|
| icwmpd sống không, session gần nhất | `ps \| grep -E 'tr69_md\|icwmpd'` · `ubus call tr069 status` |
| Bật log chi tiết (icwmp + CMS glue), không restart | `U="uci -c /data/icwmp/config"; $U set cwmp.cpe.log_severity=DEBUG; $U commit cwmp; ubus call tr069 command '{"command":"reload"}'` |
| Xem SOAP vào/ra | `tail -f /var/log/icwmpd.log` (DEBUG) |
| Đang chạy model nào | `ubus call tr069 dm '{"cmd":"inform","path":""}'` → `"root"` |
| Dump cả cây ra file | `ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.","file":"/tmp/icwmp/dm.txt"}'` (TR-181: `"path":"Device."`) |
| Dump một object/param | `ubus call tr069 dm '{"cmd":"get","path":"Device.WiFi.SSID.1."}'` |
| Giả lập ACS Set | `ubus call tr069 dm '{"cmd":"set","path":"…","value":"…","key":"k1"}'` |
| Giả lập ACS Inform ngay | `ubus call tr069 inform` (`'{"event":"6 CONNECTION REQUEST"}'` để chọn event) |
| Đổi model TR-098 ↔ TR-181 | `$U set cwmp.cpe.datamodel=tr181; $U commit cwmp; ubus call tr069 command '{"command":"reload"}'` |
| Sự thật phía MDM (không qua icwmp) | `tr69_mdmcli` → `mdm getpv Device.ManagementServer. 0` · `dumpmdm` |

## 1. Process, bus, file

**[Verified source, board 19/09 với image 0012–0016]**

```sh
ps | grep -E 'tr69_md|icwmpd|tr69c'             # tr69_md + icwmpd, KHÔNG có tr69c
cat /proc/$(pidof icwmpd)/cmdline | tr '\0' ' '  # icwmpd -b -S <shmId>  (-b = boot launched)
ubus list | grep -E '^tr069$|com.broadcom'       # tr069 = icwmpd, com.broadcom.<comp>_md = MDM các component
ubus -v list tr069                               # 5 method: notify, command, status, inform, dm
ipcs -m                                          # SHM MDM tr69 (nattch tăng khi icwmpd attach)
ls -la /data/icwmp /data/icwmp/config /data/icwmp/tr098 /tmp/icwmp
#   /data/icwmp/config/cwmp                  UCI của icwmp (giữ qua reboot)
#   /data/icwmp/tr098/.dm_enabled_notify     param có notification + giá trị lần cuối (JSON lines)
#   /data/icwmp/.icwmpd_backup_session.xml   backup session (event/RPC chưa gửi)
#   /data/icwmp/crash.log                    backtrace lần chết gần nhất (0010)
#   /var/log/icwmpd.log                      log icwmp (cwmp.cpe.log_severity)
#   /var/state/cwmp                          cwmp.cpe.ip / ipv6 do netlink watcher ghi
uci -c /data/icwmp/config show cwmp | sed 's/passwd=.*/passwd=<masked>/'
uci -c /data/icwmp/config -P /var/state get cwmp.cpe.ip     # IP icwmp dùng cho ConnectionRequestURL
```

Không thấy `icwmpd`: `grep -E 'launch (icwmpd|tr69c)' /var/log/messages` — `launch tr69c` = image
không có `SUPPORT_ICWMP`; exit ngay `EnableCWMP is false` → bật `Device.ManagementServer.EnableCWMP`
(WebUI / `tr69_mdmcli`), `tr69_md` relaunch khi `ACS_CONFIG_CHANGED`. Chạy tay sau khi chết:
`icwmpd -b -X -S <shmId>` (shmId từ `ipcs -m`).

## 2. Log level — bốn chỗ, một biến chính

| Log | Ở đâu | Mức | Bật/tắt | Nhãn |
|---|---|---|---|---|
| icwmp core (session, SOAP, events, `ubus dm …`) | `/var/log/icwmpd.log` (`cwmp.cpe.log_file_name`, quay vòng `log_max_size`) | `cwmp.cpe.log_severity` = `EMERG ALERT CRITIC ERROR WARNING NOTICE INFO DEBUG` (`log.c:30`) | `uci … set cwmp.cpe.log_severity=DEBUG; commit; ubus call tr069 command '{"command":"reload"}'` — hoặc từ ACS: SPV `…ManagementServer.X_MARUSYS_COM_Icwmp.LogSeverity=DEBUG` (reload cuối session) | Verified |
| CMS log của glue BDK + libtr098 backend + HAL/PHL (`cmsLog_*`) | syslog `/var/log/messages` (board không có `logread`) | `ERR` mặc định, `NOTICE` khi `log_severity=DEBUG` (`bdk_set_cms_log_level`, đọc lại mỗi reload từ `0028`) | như trên. `DEBUG` của CMS chưa có gate runtime (sửa `cmsLog_setLevel(LOG_LEVEL_DEBUG)` trong `bdk_set_cms_log_level` nếu cần) | Verified |
| `tr69_md` (launch, forward event) | syslog | Error mặc định | `tr69_mdmcli` → `loglevel set tr69_md Notice` (help: `loglevel set appname loglevel`, `mdm_cmddebug.c:572`) — tên app đúng của bảng `Device.X_BROADCOM_COM_AppCfg.*` **[Not established]**, thử `loglevel get tr69_md` trước | Conditional |
| Component chủ param (`wifi_md`, `sysmgmt_md`, `devinfo_md`, `diag_md`) — RCL/STL khi SPV | syslog | Error | `wifi_mdmcli` / `sysmgmt_mdmcli` → `loglevel set <app> Debug`, hoặc SPV `Device.X_BROADCOM_COM_AppCfg.WiFiMdCfg.LoggingLevel` | Conditional |
| SOAP trên dây (HTTP không TLS) | `tcpdump -l -i <if> -nn -A -s0 host <ACS> and port 7547` | — | `-l` bắt buộc (buffer) | Verified board |

Log to console: `cwmp.cpe.log_to_console=enable` (mặc định `disable`), khi chạy `icwmpd` bằng tay.

## 3. Marker log — request đi tới đâu

Đọc **hai** log cùng lúc: `icwmpd.log` (icwmp/libtr098 engine) và `messages` (CMS: glue + HAL).

| Bước | Marker `/var/log/icwmpd.log` | Marker `/var/log/messages` (app `icwmpd`) |
|---|---|---|
| Khởi động | `icwmpd <ver> starting (shmId=N, boot=B)` | `attached to tr69 MDM shmId=N, data model is TR-181`, `REGISTER_EVENT_INTEREST 0x… ok` ×4, `sync MDM->UCI …`, `data model: TR-181 Device.` (icwmpd), `data model root: Device.` (libtr098) |
| Session | `Start session`, `MESSAGE OUT`/`MESSAGE IN` (DEBUG), `ACS <url> answered HTTP <code>`, `InformResponse`, `End session` | — |
| GPV/GPN | tên RPC trong `MESSAGE IN`; fault trong `MESSAGE OUT` | `GPV <path> failed ret=<BcmRet>` (9005 = không có instance), `leaf X not in BDK map` (TR-098 thiếu map) |
| SPV | `MESSAGE IN` SetParameterValues, `MESSAGE OUT` Status/fault | `batch SPV of N params applied to MDM` / `SPV fault <code> on <param> (<fullpath>)`, `SPV applied, reboot required` |
| Add/Del | — | `addObject <path> failed ret=` (nếu lỗi) |
| Connection Request | `Connection Request server initiated with the port: N`, `Connection Request from <ip>` (0029), `Receive Connection Request: success authentication` / `Return 401 Unauthorized` / `Return 503 Service Unavailable (not a GET /)` (0030), `incomplete request (timeout 10 s), dropped` (0030: peer không gửi hết header) | — |
| MloCfg | — | `nvram set wl_mlo_…`, `nvram kset wl_mlo_config=…`, `wl_mlo_config="…": takes effect after reboot`, `Enable=true without LinkRadios/SelectedConfig` |
| Cuối session | `Config reload: end session request` (nếu reload) | `sync UCI->MDM …`, `ACS changed ManagementServer.* in the MDM: reloading icwmpd config` (TR-181), `config saved to flash` |
| Đổi cấu hình từ WebUI | — | `ACS config changed in MDM: reloading icwmpd config` |
| Value change | `thread_handle_notify`/`4 VALUE CHANGE` event | `TR69_ACTIVE_NOTIFICATION from MDM` |
| Diagnostics | `8 DIAGNOSTICS COMPLETE` | `diagnostics complete (msg 0x…)` |
| ubus dm | `ubus dm <cmd> <path>`, `ubus dm: config reload requested by the setter, reloading now` | như SPV/GPV |
| Reboot / image | `Executing Reboot: end session request` | `reboot requested by icwmpd`, `firmware written`, `factory reset:` |

Lệnh gom nhanh:

```sh
grep -E 'Start session|answered HTTP|MESSAGE (IN|OUT)|End session|ubus dm|VALUE CHANGE|reload' /var/log/icwmpd.log | tail -40
grep -E 'icwmpd' /var/log/messages | grep -E 'data model|attached|sync|batch SPV|SPV fault|GPV .* failed|not in BDK map|nvram|saved to flash|reload' | tail -40
```

## 4. `ubus call tr069` — điều khiển icwmpd

**[Verified source]** `ubus.c:327-333`, `bdk/icwmp_bdk_dm.c`.

| Method | Args | Làm gì |
|---|---|---|
| `status` | — | `status up`, `last_session{start_time,end_time,status}`, `next_session`, `statistics{success_sessions,failure_sessions,total_sessions}` |
| `inform` | `{"event":"<idx hoặc tên>"}` (mặc định `2 PERIODIC`), `{"GetRPCMethods":true}` | xếp event vào queue và mở session ngay (hoặc cuối session đang chạy) |
| `command` | `{"command":"reload"}` | đọc lại UCI (`cwmp_config_reload`): ACS, identity, `datamodel`, `log_severity`, dựng lại `.dm_enabled_notify`; nếu session đang chạy → cuối session |
| | `reload_end_session` / `reboot_end_session` / `action_end_session` | đặt cờ, chạy cuối session |
| | `exit` | thoát sạch (đóng CR server) — `tr69_md` không tự relaunch, xem mục 1 |
| `notify` | — | kích thread notify (như `TR69_ACTIVE_NOTIFICATION`) |
| `dm` | `{"cmd","path","value","key","next_level","file"}` | mục 5 |

## 5. `ubus call tr069 dm` — chạy RPC của ACS trên board

**[Verified source]** cùng đường `dm_entry_param_method`/`dm_entry_apply` như `xml.c`; giữ
`mutex_session_send` (chờ nếu session ACS đang chạy); sau `set/add/del/setattr` chạy phần cuối session
như ACS: `apply_end_session` + `dm_entry_restart_services` + `icwmp_bdk_end_session` (sync
ManagementServer + **save flash ngay**), reload config ngay nếu setter yêu cầu (`X_MARUSYS_COM_Icwmp.*`,
SPA TR-181). Path theo model đang chạy (`InternetGatewayDevice.` hoặc `Device.`); root sai → fault 9005.

```sh
D() { ubus call tr069 dm "$1"; }
# GetParameterValues (leaf hoặc object)
D '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo.SerialNumber"}'
D '{"cmd":"get","path":"Device.WiFi.SSID.1."}'
# GetParameterNames (next_level true = một mức, false = cả cây con), có cột writable
D '{"cmd":"names","path":"InternetGatewayDevice.","next_level":true}'
D '{"cmd":"names","path":"Device.WiFi.","next_level":false}'
# SetParameterValues (key = ParameterKey, không truyền = rỗng như ACS gửi rỗng)
D '{"cmd":"set","path":"Device.ManagementServer.PeriodicInformInterval","value":"600","key":"k1"}'
# AddObject / DeleteObject
D '{"cmd":"add","path":"Device.NAT.PortMapping."}'                      # -> "instance":"N"
D '{"cmd":"del","path":"Device.NAT.PortMapping.N."}'
# GetParameterAttributes / SetParameterAttributes (0028; value = 0 off, 1 passive, 2 active)
D '{"cmd":"attr","path":"Device.ManagementServer."}'
D '{"cmd":"setattr","path":"Device.ManagementServer.PeriodicInformInterval","value":"2"}'
# Danh sách param Inform kế sẽ mang (+ DeviceId, root) — kiểm identity override / model
D '{"cmd":"inform","path":""}'
# Ghi kết quả ra file thay vì trả qua ubus (dump lớn)
D '{"cmd":"get","path":"Device.","file":"/tmp/icwmp/dm_full.txt"}'
```

Reply: `cmd`, `root` (`InternetGatewayDevice`|`Device`), `fault` (0 = ok, 9xxx = CWMP fault), `faults[]`
(`{parameter,fault}` khi SPV lỗi từng param), `count` + `parameters[]` (`{parameter,value,type}` /
`{parameter,writable}` / `{parameter,notification}`), `instance` (add), `file` (đường dẫn đã ghi, rỗng
nếu không ghi được), `deviceid{manufacturer,oui,product_class,serial_number}` (inform), và sau lệnh
đổi: `reloaded` (đã reload config ngay) + `end_session[]` (cờ còn treo tới cuối session ACS kế:
`reboot`, `factory_reset`, `ipping_diagnostic`, …).

File dump: một dòng/param, tab: `path<TAB>type<TAB>value` (`get`/`inform`), `path<TAB>writable`
(`names`), `path<TAB>notification` (`attr`).

| Fault | Nghĩa trên BDK | Xem gì |
|---|---|---|
| 9003 | lệnh/arg sai (`cmd` lạ, `set` thiếu `value`, notification ngoài 0..6) | — |
| 9005 | path không có: sai root cho model đang chạy, object TR-098 chưa port, MDM không có instance | `names` object cha; `messages` `GPV … failed ret=` / `not in BDK map` |
| 9007 | giá trị bị setter từ chối (MloCfg: radio không tồn tại/trùng/>3, SSID >32…) | `messages` (MloCfg log lý do) |
| 9008 | MDM param không writable | `names` cột `writable`; `tr69_mdmcli` `mdm getpn` |
| 9002 | HAL trả lỗi khác (RCL từ chối, timeout remote) | `messages` `batch SPV … failed ret=<BcmRet>`, `SPV fault` |
| 9001 | attribute trên `X_MARUSYS_COM_Device.*` (TR-098) | dùng model TR-181 nếu cần attribute cho path đó |

## 6. Dump toàn bộ data model

### 6.1 Qua icwmp (đúng cái ACS sẽ thấy)

```sh
mkdir -p /tmp/icwmp
# TR-098: cây IGD đã port (vài trăm dòng)
D '{"cmd":"names","path":"InternetGatewayDevice.","next_level":false,"file":"/tmp/icwmp/igd_names.txt"}'
D '{"cmd":"get","path":"InternetGatewayDevice.","file":"/tmp/icwmp/igd_values.txt"}'
# TR-098: phần TR-181 qua proxy (không nằm trong "InternetGatewayDevice." vì object proxy rỗng)
D '{"cmd":"get","path":"InternetGatewayDevice.X_MARUSYS_COM_Device.","file":"/tmp/icwmp/igd_proxy_values.txt"}'
# TR-181: toàn bộ MDM (lớn — dùng file, không dùng reply ubus)
D '{"cmd":"names","path":"Device.","next_level":false,"file":"/tmp/icwmp/dev_names.txt"}'
D '{"cmd":"get","path":"Device.","file":"/tmp/icwmp/dev_values.txt"}'
D '{"cmd":"attr","path":"Device.","file":"/tmp/icwmp/dev_attr.txt"}'      # notification từng param (MDM)
wc -l /tmp/icwmp/*.txt; grep -c $'\t' /tmp/icwmp/dev_values.txt
grep -E '^Device\.WiFi\.SSID\.[0-9]+\.(SSID|Name|Enable)\b' /tmp/icwmp/dev_values.txt
```

Không có `file` (image < `0028`): `ubus call tr069 dm '{"cmd":"get","path":"Device.WiFi."}' > /tmp/icwmp/wifi.json`
theo từng object con để tránh reply quá lớn (**[Not established]** giới hạn thực tế của ubus trên board với
cả `Device.`).

### 6.2 Phía MDM (bỏ qua icwmp — đối chứng "sự thật")

**[Verified source]** `mdm_cmd.c:77-93`, `mdm_cmddebug.c:939-951`. `*_mdmcli` là shell tương tác đọc stdin
(`cmdedit`), attach vào MDM của component đó: `tr69_mdmcli` thấy `Device.ManagementServer.*` local và
mọi path khác qua remote (như icwmpd); `wifi_mdmcli`, `sysmgmt_mdmcli`, `devinfo_mdmcli`, `diag_mdmcli`
thấy MDM local của component chủ.

```sh
tr69_mdmcli
  help                                          # danh sách lệnh
  mdm getpv Device.ManagementServer. 0          # 0 = cả cây con, 1 = một mức (mask password khi dán)
  mdm getpn Device.WiFi.SSID. 1
  mdm getpa Device.ManagementServer.PeriodicInformInterval 0     # attribute (notification 0/1/2)
  mdm setpv Device.ManagementServer.PeriodicInformInterval 600   # ghi thẳng MDM (không qua icwmp)
  mdm setpa Device.ManagementServer.PeriodicInformInterval 0 0 1 2 0 0   # setNotification=1 notif=2 (active)
  mdm addobj Device.NAT.PortMapping.
  dumpmdm                                       # toàn bộ MDM (local + remote) — rất dài, chạy qua tee
  save                                          # ghi flash
  exit
```

Ghi ra file: chạy `tr69_mdmcli 2>&1 | tee /tmp/icwmp/dumpmdm.txt` rồi gõ `dumpmdm`, `exit`. Pipe lệnh
vào stdin (`printf 'mdm getpv Device.DeviceInfo. 0\nexit\n' | tr69_mdmcli`) — `cmdedit` đọc từng byte fd 0
và dừng ở EOF (`cmdedit.c:1346`) nên **khả năng chạy được**, **[Not established]** chưa thử trên board.

Gọi thẳng component chủ qua ubus (đúng boundary `remote_objd` dùng; arg theo `ubus_mdm.c:67-186`):

```sh
ubus call com.broadcom.wifi_md getParameterValues '{"fullpath_array":["Device.WiFi.SSID.1."],"nextlevel":false,"flags":0}'
ubus call com.broadcom.sysmgmt_md getParameterValues '{"fullpath_array":["Device.IP.Interface."],"nextlevel":false,"flags":0}'
ubus call com.broadcom.devinfo_md getParameterValues '{"fullpath_array":["Device.DeviceInfo."],"nextlevel":true,"flags":0}'
ubus call com.broadcom.wifi_md getParameterNames '{"fullpath":"Device.WiFi.","nextlevel":true,"flags":0}'
```

### 6.3 Đối chiếu ba tầng

Một param sai giá trị → so **ba** chỗ, lệch ở đâu là lỗi ở tầng đó:

| Tầng | Lệnh | Lệch nghĩa là |
|---|---|---|
| icwmp (ACS thấy) | `D '{"cmd":"get","path":"…"}'` | map/override/proxy của libtr098 |
| MDM | `tr69_mdmcli` `mdm getpv … 0` hoặc `ubus call com.broadcom.<comp>_md …` | icwmp đọc đúng, MDM chưa được RCL/STL cập nhật |
| Runtime | `nvram get wl0_ssid`, `wl -i wl0 ssid`, `ip addr`, `iptables -t nat -S` | MDM có, chưa apply xuống driver/kernel (RCL) |

## 7. Dump một node / param, theo dõi một request

```sh
# object + toàn bộ cây con, kèm type
D '{"cmd":"get","path":"Device.WiFi.AccessPoint.1.Security."}'
# chỉ tên + writable (không gọi getter — nhanh, không đụng driver)
D '{"cmd":"names","path":"InternetGatewayDevice.LANDevice.1.WLANConfiguration.1.","next_level":true}'
# một leaf, rồi soi đường đi trong log (bật DEBUG trước)
D '{"cmd":"get","path":"InternetGatewayDevice.LANDevice.1.WLANConfiguration.1.SSID"}'
grep -E 'ubus dm' /var/log/icwmpd.log | tail -1
grep -E 'icwmpd' /var/log/messages | tail -5            # GPV nào tới HAL, ret bao nhiêu (NOTICE khi DEBUG)
# thread notify: param nào đang được theo dõi và giá trị lần cuối
cat /data/icwmp/tr098/.dm_enabled_notify
```

## 8. Giả lập ACS — không cần ACS, hoặc ACS thật

### 8.1 RPC không cần ACS (`tr069 dm`) — kịch bản chuẩn

```sh
# 1. Set rồi đọc lại ở ba tầng (mục 6.3)
D '{"cmd":"set","path":"Device.WiFi.SSID.1.SSID","value":"TestSSID","key":"t1"}'
D '{"cmd":"get","path":"Device.WiFi.SSID.1.SSID"}'; nvram get wl0_ssid; wl -i wl0 ssid
grep -E 'batch SPV|SPV fault|saved to flash' /var/log/messages | tail -3
D '{"cmd":"get","path":"Device.ManagementServer.ParameterKey"}'            # = t1
# 2. Attribute → value change (TR-181: attribute nằm trong MDM)
D '{"cmd":"setattr","path":"Device.ManagementServer.PeriodicInformInterval","value":"2"}'
D '{"cmd":"attr","path":"Device.ManagementServer.PeriodicInformInterval"}'  # notification 2
grep PeriodicInformInterval /data/icwmp/tr098/.dm_enabled_notify         # có dòng
tr69_mdmcli   # mdm setpv Device.ManagementServer.PeriodicInformInterval 900 ; exit   (đổi ngoài icwmp)
sleep 5; ubus call tr069 status; grep -E 'VALUE CHANGE|TR69_ACTIVE_NOTIFICATION' /var/log/icwmpd.log /var/log/messages | tail -3
# 3. Inform sẽ mang gì (DeviceId, forced param, root)
D '{"cmd":"inform","path":""}'
# 4. Cờ cuối session sau một set: reload ngay hay treo
D '{"cmd":"set","path":"Device.ManagementServer.X_MARUSYS_COM_Icwmp.LogSeverity","value":"DEBUG"}'   # reply reloaded:true
```

Khác biệt với ACS thật: ACS gộp nhiều param trong một SPV (một batch) — `tr069 dm set` mỗi lần một param
= một batch; RCL của SDK có thể hành xử khác khi các param liên quan tới cùng transaction (ví dụ đổi
`ModeEnabled` và `KeyPassphrase` cùng lúc). Cờ `reboot/factory_reset/diagnostic` do setter đặt qua
`tr069 dm` chỉ chạy ở cuối session ACS kế (reply `end_session[]`).

### 8.2 Session với ACS thật, ép từ board

```sh
ubus call tr069 inform                                     # 2 PERIODIC
ubus call tr069 inform '{"event":"6 CONNECTION REQUEST"}'
ubus call tr069 inform '{"GetRPCMethods":true}'
ubus call tr069 command '{"command":"reload"}'             # BOOTSTRAP nếu URL đổi (event.c)
tail -f /var/log/icwmpd.log                                # MESSAGE IN = RPC ACS gửi
```

### 8.3 Connection Request từ ACS/host

```sh
uci -c /data/icwmp/config get cwmp.cpe.port                # 30005 (CR server bind [::]:30005, http.c:483)
D '{"cmd":"get","path":"Device.ManagementServer.ConnectionRequestURL"}'   # http://<ip>:30005/
# host trong cùng subnet với <ip>:
curl -v --digest -u '<CRuser>:<CRpass>' 'http://<ip>:30005/'     # 401 → 200 → board log "Connection Request from <host>" (0029) → session 6 CONNECTION REQUEST
```

ACS báo `EHOSTUNREACH`/timeout khi Summon = ACS không có đường tới `<ip>` (AP sau ONT NAT/route). Chẩn đoán
bằng IP nguồn trong access log của GenieACS; sau NAT: port forward trên ONT + `uci set cwmp.cpe.cr_host=<IP WAN ONT>`
(`cr_port` nếu forward cổng khác) hoặc từ ACS `…X_MARUSYS_COM_Icwmp.ConnectionRequestHost` (`0029`) — quy trình đầy đủ:
issue debug-commands gate 4a/4b.

### 8.4 Inform từ PC (loại board khỏi phương trình khi ACS từ chối)

`issues/20260916_tr069_app_use_icwmp/inform_replay.xml` + `curl --anyauth` — xem issue debug-commands
mục "Bước tiếp sau 401".

### 8.5 Từ phía GenieACS (host)

Debug phía server khi session fail (log, `cwmp.auth`, `cwmp.debug` ghi request nguyên văn, Mongo):
[knowledge/protocol/genieacs-server-side-cwmp-auth-debug.md](../../../knowledge/protocol/genieacs-server-side-cwmp-auth-debug.md);
kịch bản hai terminal board + server: issue debug-commands "Debug hai phía".

GenieACS UI: Devices → device → *Summon* (connection request) hoặc tab *Get/Set parameter*. NBI
(port 7557, theo tài liệu GenieACS — **[Not established]** trên lab này): `curl -X POST
'http://<acs>:7557/devices/<DeviceId>/tasks?connection_request' -H 'Content-Type: application/json'
-d '{"name":"getParameterValues","parameterNames":["Device.WiFi.SSID.1.SSID"]}'`
(`setParameterValues` với `parameterValues:[["path","value","xsd:string"]]`). Đổi model xong phải xóa
device trên GenieACS (cache root cũ).

## 9. Kịch bản theo lĩnh vực (rút gọn)

Đặt `R=InternetGatewayDevice` (TR-098) hoặc dùng path `Device.` (TR-181) — cột phải của
[matrix](icwmp_datamodel_tr098_tr181_matrix.md#ví-dụ-getset--cùng-một-việc-hai-path).

| Lĩnh vực | Đọc | Ghi thử (vô hại) | Đối chứng runtime |
|---|---|---|---|
| WAN | `get $R.WANDevice.1.WANConnectionDevice.1.WANIPConnection.` / `get Device.IP.Interface.2.` | `set …MaxMTUSize 1500` (giá trị hiện tại) | `ip addr show eth1.1`, `ip route` |
| Wi-Fi | `get $R.LANDevice.1.WLANConfiguration.1.` / `get Device.WiFi.SSID.1.` + `Radio.1.` + `AccessPoint.1.Security.` | `set …SSID <SSID hiện tại>` | `nvram get wl0_ssid`, `wl -i wl0 ssid`, `wl -i wl0 status` |
| Mesh | `get $R.X_MARUSYS_COM_Device.WiFi.DataElements.Network.` / `get Device.WiFi.DataElements.Network.` (`nvram set wldataeld_enable=1; nvram commit; wlssk restart` trước) | `set …X_BROADCOM_COM_WbdCfg.WbdMsgLevel 1` | `pidof wldataeld wbd_master wbd_slave`, `wb_cli -m` |
| MLO | `get $R.X_MARUSYS_COM_MloCfg.` / `get Device.WiFi.X_MARUSYS_COM_MloCfg.`; `get …Device.WiFi.Radio.` để biết instance ↔ `wlX`/band | `set …MloCfg.Description test` (chỉ nvram) → `set …SSID`, `.KeyPassphrase`, `.LinkRadios 1,2,3`, `.Enable 1` → `Status`=`RebootRequired` → reboot | `nvram get wl_mlo_ssid wl_mlo_bss_enabled wl_mlo_selected_config`, `nvram kget wl_mlo_config`, `nvram get wl0.1_ssid wl1.1_ssid wl2.1_ssid`, sau reboot `wl -i wl0.1 mlo info` |

## 10. Vòng dev nhanh — nạp `icwmpd` / `libtr098.so` mới không cần flash

**[Conditional — chưa thử trên board]** Image đã có đủ SDK/lib (`SUPPORT_ICWMP`); giữa hai patch chỉ hai
file đổi: `icwmpd` (`bin/icwmp_tr098d` trong cây build) và `libtr098.so.0`. Rootfs read-only nhưng
`/data` ghi được, `tr69_md` không tự relaunch icwmpd đã chết (mục 1) nên chạy bản dev bằng tay được.
Điều kiện: cùng toolchain/cây build với image đang chạy (`ldd` phải resolve hết).

```sh
# host (cây build): sau make -C userspace/public/libs/libtr098 -f Bcmbuild.mk / apps/icwmp
B=targets/MO77300EB
scp userspace/public/apps/icwmp/icwmp/bin/icwmp_tr098d root@<board>:/data/icwmp/dev/bin/icwmpd
scp userspace/public/libs/libtr098/libtr098/bin/.libs/libtr098.so.0.0.0 root@<board>:/data/icwmp/dev/lib/libtr098.so.0
# board
mkdir -p /data/icwmp/dev/bin /data/icwmp/dev/lib; chmod 0755 /data/icwmp/dev/bin/icwmpd
SHM=$(cat /proc/$(pidof icwmpd)/cmdline | tr '\0' ' ' | sed 's/.*-S *\([0-9]*\).*/\1/')   # shmId của MDM tr69 (hoặc ipcs -m)
killall icwmpd; sleep 1; ps | grep icwmpd                                                # KHÔNG còn /bin/icwmpd
LD_LIBRARY_PATH=/data/icwmp/dev/lib /data/icwmp/dev/bin/icwmpd -b -X -S $SHM 2>/tmp/icwmp/dev.err &
#   -b = boot launched (event 1 BOOT), -X = không coi là launch bởi tr69_md, -S = shmId
ldd /data/icwmp/dev/bin/icwmpd | grep -E 'not found|libtr098'   # libtr098 phải trỏ /data/icwmp/dev/lib
grep -E 'starting|attached to tr69 MDM' /var/log/icwmpd.log /var/log/messages | tail -2
```

Bẫy: đổi `Device.ManagementServer.*` từ WebUI trong lúc chạy bản dev → `tr69_md` thấy "icwmpd chết"
là sai (đang chạy, chỉ khác path) — nhưng nếu bản dev cũng thoát, `tr69_md` launch lại `/bin/icwmpd`
**cũ** → luôn `ps` trước khi đọc log. Xong phase dev thì flash image full (side-load không chứng minh boot
order/persistence — `knowledge/protocol/broadcom-bdk-tr69-sideload-boundary.md`).

## 11. Sức khỏe chạy dài — leak, fd, thread (soak)

```sh
P=$(pidof icwmpd)
while :; do date '+%F %T'; grep -E 'VmRSS|VmData|Threads' /proc/$P/status | tr '\n' ' '; echo " fd=$(ls /proc/$P/fd | wc -l)"; sleep 300; done >> /tmp/icwmp/soak.txt &
ubus call tr069 status | grep -E 'success_sessions|failure_sessions'    # phải chỉ tăng success
ls -la /var/log/icwmpd.log /data/icwmp/.icwmpd_backup_session.xml       # log quay vòng theo log_max_size, backup không phình
```

`VmData` tăng đều qua các session/`4 VALUE CHANGE` = leak (upstream icwmp có nhiều fix leak sau bản base —
`analysis.md` §16); `Threads` tăng = thread không join; `fd` tăng = socket CR/curl không đóng.

## 12. Crash, core, giải mã offset

Xem issue [debug-commands.md gate 1b](../issues/20260916_tr069_app_use_icwmp/debug-commands.md#gate-1b--icwmpd-crash-đã-tìm-ra-1909-fix-fdd56fc):
`/data/icwmp/crash.log`, `dmesg | grep -A25 'Comm: icwmpd'`, `addr2line` trên
`icwmp/bin/icwmp_tr098d` và `libtr098/bin/.libs/libtr098.so.0.0.0` chưa strip, core dump qua
`core_pattern`. Dấu hiệu race thread: `cmsMdm_getThreadMsgHandle … multiple threads accessing MDM`.

## Tài liệu debug đã đủ chưa — bản đồ

| Nhu cầu khi phát triển / sửa lỗi | Ở đâu |
|---|---|
| Build, lỗi build, kiểm binary đúng bản | issue `debug-commands.md` gate 0 + "Bản test" |
| Process/bus/file, launch tay sau crash | mục 1 |
| Log level 4 tầng, marker từng bước, gom log | mục 2–3 |
| Chạy RPC như ACS, dump cả cây / một node, attribute, inform preview | mục 4–7 |
| Giả lập ACS, CR, replay Inform, GenieACS phía server (`cwmp.auth`, `cwmp.debug`, on/off) | mục 8, `genieacs-debug.sh`, `knowledge/protocol/genieacs-server-side-cwmp-auth-debug.md` |
| Mạng: 400/401/EHOSTUNREACH/NAT | issue `debug-commands.md` gate 2, "Debug hai phía", gate 4a |
| Kịch bản WAN/Wi-Fi/mesh/MLO, hai model | mục 9, [icwmp_datamodel_tr098_tr181_matrix.md](icwmp_datamodel_tr098_tr181_matrix.md) |
| Thêm param/object mới (icwmp hoặc MDM) | matrix "Khi nào phải sửa code", [tr181_parameter_development_guide.md](tr181_parameter_development_guide.md), mẫu `mlo_bdk.c`/`icwmpcfg_bdk.c` |
| Vòng dev không flash, soak/leak | mục 10–11 |
| Crash/core/addr2line | mục 12 + issue gate 1b |
| Flow bên trong để đặt breakpoint/log đúng chỗ | [icwmp_bdk_runtime_flow.md](icwmp_bdk_runtime_flow.md) |

Còn thiếu (chưa có bằng chứng board nên chưa viết): output mẫu thật của từng lệnh, `gdbserver` (không
có trong image — chỉ core + addr2line), HTTPS/CA (xem issue `20260914_…/genieacs-configuration.md` §4).

## Chưa chứng minh được

- **[Not established]** Output thật của mọi lệnh `tr069 dm` (chưa flash `0017`..`0028`); giới hạn kích
  thước reply ubus với `Device.` không dùng `file`.
- **[Not established]** Tên app cho `loglevel set` trong `*_mdmcli`, và pipe lệnh vào `tr69_mdmcli`.
- **[Not established]** GenieACS NBI trên lab (`7557` có mở không).
- **[Not established]** Thời gian từ `setpv` ngoài icwmp tới `4 VALUE CHANGE` (timer notify của icwmp
  + `TR69_ACTIVE_NOTIFICATION`).

## Patch/debug artifact liên quan

- `issues/20260916_tr069_app_use_icwmp/sdk-overlay/0017-*.patch` (ubus `tr069 dm`), `0028-*.patch`
  (`attr/setattr/inform/file`, reload log level), tarball `icwmp_bdk_port_overlay.tar.gz`.
- Issue [debug-commands.md](../issues/20260916_tr069_app_use_icwmp/debug-commands.md) — gate 0..5 và
  "Bản test" của từng lần build.
- Flow: [icwmp_bdk_runtime_flow.md](icwmp_bdk_runtime_flow.md); model: [icwmp_datamodel_tr098_tr181_matrix.md](icwmp_datamodel_tr098_tr181_matrix.md).
