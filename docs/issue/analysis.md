# Phân tích: đưa icwmp + libtr098 chạy multi-platform (BDK + MTK OpenWrt), TR-098

Snapshot khảo sát: `projects/mtk_openwrt_wifi7/src/2025q3` (git `b207c4518`, 2026-09-18),
overlay `brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace` (trước refactor
`f191e18`, sau refactor `99f4988`). Số dòng trích dẫn là anchor của snapshot này.

Nhãn bằng chứng theo [.claude/rules/evidence.md](../../../../.claude/rules/evidence.md):
**Verified** = đã đọc cả hai đầu và dữ liệu ở ranh giới; **Conditional** = đúng trong điều kiện ghi kèm;
**Not established** = chưa chứng minh được.

---

## 1. Hiện trạng TR-069 trên 2025q3

**Verified.** Trên cây 2025q3 có **ba** gói liên quan, chỉ một gói được build:

| Gói | Nguồn | Trạng thái trong `profile/HP2236B/config_7583` |
|---|---|---|
| `cwmpclient` | `tclinux_phoenix/apps/hni/cwmpclient` (easycwmp 1.8.6 + thư viện hàm của HNI/AIS) | `CONFIG_PACKAGE_cwmpclient=y` — **đang chạy** |
| `icwmp_tr098` | `tclinux_phoenix/apps/hni/icwmp_tr098` | `# ... is not set` |
| `libtr098` | `tclinux_phoenix/apps/hni/libtr098` | `# ... is not set` |

`icwmp_tr098` + `libtr098` được đưa vào bằng commit `2d6f314f7` *[ARHT-339] Porting tr069 (iopsys)
with TR-098 data model* (Steven Tu, 2026-01-11) nhưng **chưa bao giờ được bật**. Diff với bản
`tunv_bk` (bản gốc lấy trên mạng, cũng là base của overlay BDK):

- `icwmp_tr098` vs `tunv_bk/icwmp098`: 8 file khác nhau, **toàn bộ chỉ là xóa comment**
  (`// extern char *ns;`, `// TuNV`) — không có thay đổi hành vi.
- `libtr098` vs `tunv_bk/libtr098`: 2 file — `dmubus.c` xóa comment, `tr098/upnp.c` đổi
  `enable_natpmp` `1` → `0`.

→ **Cây `icwmp_tr098`/`libtr098` trên 2025q3 = base gốc**, không có công việc port nào bị mất khi
ghi đè bằng overlay. (Kiểm bằng `diff -rq`, xem §7.)

## 2. Data model TR-098 mà ACS đang dùng

**Verified.** `cwmpclient` không chứa data model trong C: `easycwmpd` fork một shell
(`/usr/sbin/easycwmp --json-input`, `src/external.c:218`) và thư viện hàm ở
`/usr/share/easycwmp/functions/*` trả lời từng RPC.

Thống kê (trích tự động → [easycwmp_tr098_inventory.txt](easycwmp_tr098_inventory.txt)):

| | Số lượng |
|---|---|
| `common_execute_method_param` (tham số) | **824** |
| `common_execute_method_obj` (object) | **204** |
| File script | 63 (`tr098/` 60, `common/` 8, `tr143/` 2) |
| Dòng shell | ~25 000 |

Các file lớn nhất: `lan_device` (4616 dòng, 147 param), `wan_device` (3230, 207),
`firewall` (1898, 53), `services_storage_service` (884, 36), `common/common` (1352 — thư viện lõi).
Vendor tree của nhà mạng: `X_AIS_*` (Mesh, UplinkSetup, WebUserInfo, Logging, CarrierLocking,
WiFiStatus, Conf, DDNS, SSH, Telnet, …) và `X_HNI_*`/`x_hni_*` (firewall, portfiltering, samba,
speedtest, …).

**Không established:** không có tài liệu nào trong cây liệt kê tập tham số ACS thực sự dùng; giả
định an toàn là **toàn bộ** tập trên phải giữ nguyên tên/permission/type.

### Giao thức của thư viện hàm (Verified, `functions/common/common`)

| Entry | Ý nghĩa |
|---|---|
| `common_entry_get_value <param>` | GPV; in `{"parameter","value","type"}` từng dòng |
| `common_entry_get_name <param> <0\|1>` | GPN; in `{"parameter","writable"}` |
| `common_entry_set_value <param> <value>` | SPV **pha 1**: validate theo `xsd:` type, append `"<param><delim><setcmd><delim><getcmd>"` vào `/tmp/.easycwmp_set_command_tmp` |
| (easycwmp.sh `apply value <key>`) | SPV **pha 2**: `eval` từng setcmd, lỗi thì `uci revert`, xong thì `uci commit` + ghi `parameter_key` |
| `common_entry_add_object` / `..._delete_object` | Add/Delete, in `{"status":"1","instance":"N"}` |
| `common_entry_inform` | tham số forced-inform |
| (easycwmp.sh `apply service`) | `common_restart_services` (ucitrack) + chạy `/tmp/.easycwmp_apply_service` |

Đúng mô hình 2 pha của `dm_entry_param_method` (VALUECHECK) / `dm_entry_apply` (VALUESET +
`dm_platform_commit`) trong libtr098 → ánh xạ 1-1 được, **không phải sửa thư viện hàm**.

**Bẫy đã xử lý (Verified):** nhiều hàm trong thư viện gọi `exit` giữa chừng
(`common_get_name_inparam_isparam_check_param` … `exit 0`) — easycwmp.sh sống được vì mỗi RPC là
một tiến trình hoặc chạy trong subshell `( … )`. Driver mới phải bọc **mọi** handler trong subshell,
nếu không shell con chết sau RPC đầu tiên (`icwmp_dm.sh` `handle_request`).

## 3. Ai đang phụ thuộc vào easycwmp (không được phá)

**Verified** (grep toàn cây, loại trừ chính 3 gói cwmp):

| Thành phần | Phụ thuộc |
|---|---|
| `hal_unify/src/hal_gateway.c:802,1929,2155` | `HalUtils_cmdExec("/etc/init.d/easycwmpd reload")` |
| `hal_unify/common/include/hal_params.h:718-721` | `NODE_CWMP "easycwmp"`, `easycwmp.@local[0]`, `@acs[0]`, `@device[0]` (WebUI đọc/ghi UCI này) |
| `ubusmon/event.c:362` | `(/etc/init.d/easycwmpd reload; /etc/init.d/stuncd reload) &` |
| `stunclient/files/stuncd.init:21,27-39` | đọc `easycwmp.@local[0].ip`, **sed vào** `/usr/share/easycwmp/functions/management_server` để bật/tắt forced-inform của `UDPConnectionRequestAddress` |
| `isplocking/utils.c:165` | `ubus call tr069 inform '{"event":"2 PERIODIC"}'` |
| `backend/api/.../ApiGateway.h:74` | API WebUI `tr069` GET/PUT |

→ Hai ràng buộc thiết kế:
1. **UCI `easycwmp` phải tiếp tục là config of record** (WebUI ghi thẳng vào đó).
2. **`/etc/init.d/easycwmpd` và `ubus tr069` phải còn** — giữ tên, đổi ruột.

`stuncd.init` sed vào file thư viện hàm ⇒ **thư viện hàm phải cài đúng chỗ cũ**
(`/usr/share/easycwmp/functions`), không được đổi path. Gói `icwmp_tr098` mới cài đúng đường dẫn đó.

## 4. Vì sao không viết lại data model bằng C

| Phương án | Đánh giá |
|---|---|
| A. Viết lại 824 param bằng C trong `tr098/mtk/` | ~25k dòng shell → vài chục nghìn dòng C, mỗi param là một cơ hội hồi quy trên thiết bị đang chạy thật. Không có lợi ích chức năng. **Loại.** |
| B. Bỏ icwmp, giữ easycwmpd | Không đạt yêu cầu (một app cho cả hai SDK). **Loại.** |
| C. libtr098 chạy chính thư viện hàm qua bridge, port dần sang C | Ngày 1 đã tương đương easycwmpd về data model; engine/session/HTTP/backup là của icwmp (được hưởng các fix `0028`..`0031`); port sang C là tuỳ chọn, từng object. **Chọn.** |

**Conditional (chi phí):** mỗi RPC tốn một lần round-trip tới shell con. GPV toàn cây fork vài nghìn
`uci get` — đây đúng là chi phí mà `easycwmpd` đang trả hôm nay, không tệ hơn. Điểm khác: shell con
**sống suốt đời icwmpd** (source 25k dòng thư viện **một lần**), trong khi `easycwmpd` cũng giữ một
tiến trình `--json-input` thường trú → tương đương.

## 5. Kiến trúc đã chọn

### 5.1 libtr098: platform seam 3 nhánh

`platform/dmplatform.h` giữ nguyên contract cũ, thêm nhánh `mtk`. Hai thay đổi ảnh hưởng BDK:

- `dm_platform_commit(struct dmctx *, const char *parameter_key)` — mtk cần ParameterKey để ghi
  `easycwmp.@acs[0].parameter_key` trong pha `set_apply` (BDK bỏ qua, engine đã ghi UCI `cwmp`).
- `tr098/bdk/icwmpcfg_bdk.c` → `tr098/common/icwmpcfg.c` (object `X_..._Icwmp.` dùng chung).
  `DataModel` (tr098↔tr181) trả **9001** khi `dm_platform_name() != "bdk"`.

### 5.2 Định tuyến trong `dmplatform_mtk.c` (Verified theo code engine)

```
dm_entry_param_method(ctx, cmd, inparam, …)
  └─ dm_platform_param_method()        ← hook chạy TRƯỚC walk cây tĩnh (dmentry.c:204)
       ├─ mtk_is_native(inparam)  → return 0   → engine tĩnh phục vụ
       └─ còn lại                 → script
            └─ mtk_merge_static(): nếu path phủ cả object C (root) thì walk tĩnh rồi gộp
```

Gộp list an toàn vì `add_list_paramameter()` (dmtr098.c:673) chèn **theo thứ tự tên và bỏ trùng**.

| RPC | Script command |
|---|---|
| GPV / GPN | `get_value` / `get_name` |
| GPA (GetParameterAttributes) | `get_name` (lấy tên) + notification từ UCI `cwmp.@notifications[0]` của libtr098 |
| SPA | `dm_set_parameter_notification()` (UCI libtr098) + `END_SESSION_RELOAD` |
| SPV pha VALUECHECK | `set_check` (thư viện validate + queue), rồi `add_set_list_tmp` |
| SPV pha VALUESET | không làm gì (đã queue) |
| `dm_platform_commit` | `set_apply <ParameterKey>` |
| `dm_platform_revert` | `set_abort` |
| Add/Delete | `add` / `delete` |
| Inform | `inform` |
| `dm_entry_restart_services` | `uci commit` các package cây tĩnh + `apply_service` |

**Notification để ở libtr098, không ở easycwmp** (khác BDK — BDK để trong MDM): thư viện hàm có
`common_set_parameter_notification` ghi `easycwmp.@notifications[0]`, nhưng icwmp đọc
`DM_ENABLED_NOTIFY` do libtr098 dựng từ `cwmp.@notifications[0]`; dùng một nguồn duy nhất tránh
hai danh sách lệch nhau. `dm_platform_enabled_notify()` duyệt các list `passive/active/
passive_passive_lw/passive_active_lw` (đúng 4 mức mà `enabled_notify_check_param()` ghi ra file,
dmtr098.c:1745) và GPV từng entry qua script.

### 5.3 `ConnectionRequestURL` (Verified, sửa hành vi)

Thư viện hàm dựng URL từ `easycwmp.@local[0].ip` — giá trị do init script ghi **một lần** lúc start.
icwmpd có netlink watcher ghi varstate `cwmp.cpe.ip/ipv6` mỗi khi IP WAN đổi (`netlink.c:114-140`)
và CR server nghe trên `cwmp.cpe.port`. Nếu để script trả giá trị cũ, ACS sẽ giữ URL sai sau khi WAN
đổi IP. → `mtk_cr_url()` ghi đè giá trị của script bằng `varstate ip` + `cwmp.cpe.port` + path
`easycwmp.@local[0].path`, cộng override NAT `cwmp.cpe.cr_host/cr_port` (đã có từ patch `0029` cho BDK).

### 5.4 icwmpd: seam `inc/icwmp_platform.h`

6 hook thay các `#ifdef ICWMP_BDK` rải trong `config.c`, `cwmp.c`, `ubus.c`, `backupSession.c`:

| Hook | bdk | mtk | uci |
|---|---|---|---|
| `init` | attach MDM | tạo `/etc/icwmpd`, mirror easycwmp→cwmp | – |
| `config_reload` | – (CMS event đã sync) | mirror easycwmp→cwmp | – |
| `config_reloaded` | refresh DeviceId + `datamodel` | refresh DeviceId | – |
| `uloop_register` | fd CMS msg | – | – |
| `end_session` | sync MDM + save flash | mirror cwmp→easycwmp, rồi easycwmp→cwmp (+reload nếu đổi) | – |
| `cleanup` | detach MDM | – | – |

`ubus call tr069 dm` (trước chỉ có ở BDK) thành tool chung: `bdk/icwmp_bdk_dm.c` → `icwmp_dm.c`.

### 5.5 Mirror `easycwmp` ↔ `cwmp` (Verified theo cả hai phía)

16 cặp option, có 3 phép biến đổi giá trị:

| easycwmp | cwmp | Biến đổi |
|---|---|---|
| `@acs[0].url/username/password` | `cwmp.acs.url/userid/passwd` | – |
| `@acs[0].periodic_enable` | `cwmp.acs.periodic_inform_enable` | bool |
| `@acs[0].periodic_interval/periodic_time` | `…inform_interval/inform_time` | – |
| `@acs[0].cwmpretryinterval / …multiplier` | `retry_min_wait_interval / retry_interval_multiplier` | – |
| `@acs[0].ssl_verify` | `cwmp.acs.insecure_enable` | `disable` → `1` |
| `@acs[0].parameter_key` | `cwmp.acs.ParameterKey` | **chiều ngược** (icwmpd sở hữu) |
| `@local[0].interface/port/username/password/provisioning_code` | `cwmp.cpe.*` | – |
| `@local[0].logging_level` | `cwmp.cpe.log_severity` | `0..4` → `CRITIC..DEBUG` |

Quy tắc: easycwmp thắng (config of record), trừ `parameter_key` và `@local[0].ip` (icwmpd biết IP
thật qua netlink).

## 6. Sửa kèm: Connection Request path (Verified — lỗi thật)

`http.c http_cr_new_client()` (bản `0030`) chỉ nhận `GET / HTTP/1.` và tính Digest với URI `"/"`.
Sản phẩm MTK dùng `easycwmp.@local[0].path='ConnectionRequest'` → URL quảng bá là
`http://<ip>:7547/ConnectionRequest`; ACS sẽ gửi `GET /ConnectionRequest HTTP/1.1`:

1. `method_is_get` = false → trả **503**, CR không bao giờ chạy;
2. kể cả nếu qua được, Digest tính theo `uri="/ConnectionRequest"` phía ACS sẽ không khớp với `"/"`.

Sửa: lấy path từ chính request line, dùng đúng path đó khi `http_digest_auth_check()`. Không ảnh
hưởng BDK (path vẫn là `/`).

## 7. Kiểm tra đã chạy (không có compiler)

| Kiểm tra | Kết quả |
|---|---|
| `diff -rq` 2025q3 `icwmp_tr098`/`libtr098` vs `tunv_bk` | chỉ khác comment + `enable_natpmp` (§1) |
| Trích inventory data model easycwmp | 824 param / 204 obj → `easycwmp_tr098_inventory.txt` |
| Cân bằng `{}` / `()` comment-aware mọi file C mới/sửa | PASS (dmplatform_mtk.c depth 0, parens 0) |
| Resolve mọi `#include "..."` cục bộ theo đúng `-I` của Makefile.am, cho cả `mtk` và `bdk` | PASS (chỉ thiếu header SDK Broadcom — ngoài overlay) |
| Symbol: hàm/bảng mà tập source `mtk` tham chiếu nhưng chỉ định nghĩa trong file không link | **rỗng** |
| `sh -n` cho `icwmp_dm.sh`, `icwmp.sh`, `icwmpd.init`, `easycwmpd`, `wan_interface_up`, `value_monitoring`, `install-mtk.sh` | PASS |
| `install-mtk.sh --dry-run` trên cây 2025q3 thật | PASS (in đúng lệnh, không ghi gì) |
| `git status --porcelain` của `src/2025q3` | chỉ còn thay đổi có trước (`hostapd/patches/addr`), **không** do task này |

**Chưa chứng minh được / rủi ro còn lại:**

1. **Chưa compile.** Mọi file C mới chưa qua trình biên dịch. Dự kiến 1-2 vòng sửa lỗi.
2. **Thời gian GPV toàn cây qua script**: chưa đo. Timeout mặc định 240 s
   (`DMSCRIPT_TIMEOUT_SEC`). Nếu ACS hay gọi GPV `InternetGatewayDevice.` thì phải đo lại và cân
   nhắc cache.
3. **`common_restart_services` trong shell con thường trú**: thư viện dùng biến shell
   `uci_change_packages` tích luỹ trong tiến trình; vì mỗi handler chạy trong subshell nên biến
   không sống sót → driver ghi danh sách package ra `/tmp/.icwmp_dm_changed_pkgs` và đọc lại lúc
   `apply_service`. Chưa test trên board.
4. **`value_monitoring`** poll `ubus call tr069 notify` mỗi 30 s như bản cũ; đường value-change đi
   qua script cho từng tham số có notification → nếu ACS bật notification cho nhiều param, mỗi chu kỳ
   là N lần GPV. Chưa đo.
5. **TR-181 trên MTK**: chưa làm (ngoài phạm vi lượt này).
6. **Alias-based addressing / InstanceAlias**: thư viện hàm easycwmp không hỗ trợ; engine libtr098
   có `update_instance_alias` nhưng chỉ cho cây tĩnh. Nếu ACS dùng alias sẽ hỏng — **Not established**,
   cần hỏi vận hành. (`cwmp.cpe.instance_mode` mặc định `InstanceNumber`.)

---

## 8. Kiến thức tái sử dụng

Phần dùng lại được cho project khác đã tách sang
[knowledge/protocol/cwmp-reuse-shell-data-model-bridge.md](../../../../knowledge/protocol/cwmp-reuse-shell-data-model-bridge.md)
(bridge shell↔C, các bẫy subshell/timeout/fd, cách cùng tồn tại với phần còn lại của sản phẩm,
bẫy Connection Request path, cách kiểm chứng khi không build được tại chỗ).

---

## 9. Lượt 23/09 — đếm lại data model, tách SDK, data model bằng C

### 9.1 Đính chính số liệu (Verified, thay cho §2)

§2 viết "824 param + 204 object, gồm `X_AIS_*` và `X_HNI_*`". Trích lại bằng bộ trích có resolve
biến shell và **bỏ dòng comment**, rồi khử trùng theo path:

| Chỉ số | §2 (22/09) | Đúng (23/09) | Vì sao lệch |
|---|---|---|---|
| Parameter | 824 | **749** | 824 đếm cả dòng trùng (cùng path khai ở nhiều nhánh `case`) |
| Object | 204 | **181** | như trên |
| `X_AIS_*` | "có" | **281 tham số** | — |
| `X_HNI_*` | "có" | **0 tham số** | toàn bộ `functions/tr098/x_hni_*` bị comment: cả `prefix_list`, cả `entry_execute_method_list`, cả thân hàm |

Cách kiểm lại `X_HNI_*`: mở `functions/tr098/X_HNI_IPFiltering` — dòng 7-8 (`prefix_list`,
`entry_execute_method_list`) và toàn bộ `entry_execute_method_root_X_HNI_IPFiltering()` đều bắt
đầu bằng `#`. Các file `x_hni_*` khác giống hệt. **Conditional**: đúng với snapshot `src/2025q3`
hiện tại; nếu profile khác bật lại thì phải đếm lại.

Hệ quả cho quyết định ở §4: vẫn giữ nguyên kết luận "không viết lại một phát", nhưng lý do mạnh
nhất không phải "cây vendor khổng lồ" mà là **449 hàm shell** phía sau 692 tham số:

```
getter là $UCI_GET thuần : 34
getter là hàm shell      : 692   (449 hàm khác nhau)
getter là echo hằng số   : 58
không có getter          : 13
```

Backend mà các hàm đó gọi (đếm trên toàn thư viện): `$UCI_GET` 939, `$UCI_SET` 1039,
`ubus call` 127, `jsonfilter` 79, `/sys` 47, `ifconfig` 32, `wlanconfig` 33, `/proc` 23,
`iwpriv` 12, helper `hni_*` 58.

### 9.2 Tách SDK — cái gì đã đổi

Trước: SDK nằm rải ở 5 chỗ (`platform/<n>/`, `tr098/<n>/`, `scripts/<n>/`, `files/<n>/`,
`<n>/` trong icwmp) và `configure.ac` + `bin/Makefile.am` liệt kê cứng từng tên.

Sau: **một thư mục một SDK**, mảnh build nằm trong chính thư mục đó, hai file `sdk/enabled.m4` và
`sdk/enabled.mk` sinh tự động từ các thư mục đang tồn tại.

Chi tiết phải nhớ khi sửa tiếp:

1. **`sdk.m4` được `m4_include` vô điều kiện.** Mọi `AM_CONDITIONAL` trong đó phải được chạy qua
   trong mọi lần configure, phần riêng của SDK bọc trong `AS_IF([test "x$with_sdk" = "x<n>"], ...)`.
   Nếu đặt `AM_CONDITIONAL` bên trong nhánh `AS_CASE` thì `config.status` báo
   *"conditional X was never defined"* khi chọn SDK khác. **Verified** bằng tài liệu automake và
   bằng cách dựng lại cấu trúc sinh ra.
2. **`sdk.mk` được `include` ở cuối `bin/Makefile.am`**, nên đường dẫn source trong đó vẫn là
   `../` (tương đối `bin/`), giống phần còn lại của file.
3. Trong icwmp, điều kiện của SDK **kèm luôn** `enable_icwmp_tr098=yes`: glue chỉ link vào
   `icwmp_tr098d`. Bản `icwmpd` (bbfdm/TR-181 của upstream) không dùng trong sản phẩm này và
   không được hỗ trợ ở đây.

### 9.3 Registry data model (Verified trên source, chưa chạy)

`dm_registry.c`: module tự đăng ký bằng constructor, gộp theo `(order, name)`, merge **đệ quy**
theo tên object, cây dựng một lần bằng `calloc()` (không phải `dmcalloc()` — bộ nhớ dm chết theo
dm context), khoá bằng `pthread_mutex` vì icwmpd đụng data model từ hai thread.

Ba thứ registry thay thế:

| Trước | Sau |
|---|---|
| `tEntry098Obj[]` viết tay ở `tr098/root.c`, `root_bdk.c`, `root_mtk.c` | `dm_registry_entry(DM_MODEL_TR098)` |
| `tEntry181Obj[]` trong `root181_bdk.c` | `dm_registry_entry(DM_MODEL_TR181)` |
| `mtk_native_objs[]` (danh sách path đã port) | `.paths` của từng module + `dm_registry_owns()` |

### 9.4 Kiểm tra tĩnh đã chạy ở lượt này

| Kiểm | Kết quả |
|---|---|
| Mọi source mà mảnh build tham chiếu có tồn tại (3 SDK × 2 component) | PASS |
| Resolve mọi `#include "..."` theo đúng `-I` của từng SDK (6 tổ hợp) | PASS — chỉ còn header ngoài (`cms*.h`, `bcm_generic_hal.h`, `uci_config.h`) đến từ toolchain |
| Symbol của file **không** được link nhưng bị tham chiếu | rỗng. `tEntry098ObjUPNP` chỉ xuất hiện khi bật `UPNP_TR064` — có từ trước, không phải hồi quy |
| Cân bằng `#if/#ifdef/#endif` của `dmplatform_mtk.c` sau khi thêm guard | 0 |
| `sh -n` mọi script mới, `--dry-run` của `install-mtk.sh` (kể cả `--only-mtk`) | PASS |
| Prune còn build được: copy cây, `./sdk-prune.sh mtk`, kiểm lại source + tên SDK còn sót | PASS |
| P1: tập tên tham số của module C so với cây shell cũ | 65/65, dôi 19 tham số của icwmp |

**Chưa chứng minh được**: giá trị trả về có khớp client cũ không (phải chạy trên board), thời gian
GPV cả cây, và hành vi thực của `AliasBasedAddressing`.

### 9.5 Bảng tra để làm tiếp

`tr098_coverage_matrix.tsv` — 930 dòng, mỗi dòng một object/parameter:

```
phase  phase_name  kind  path  perm  getter  setter  type  forced_inform  easycwmp_file
```

Lọc việc của một phase:

```sh
awk -F'\t' '$1=="3" && $3=="param"' tr098_coverage_matrix.tsv | cut -f4,5,6,7
```


## 10. Review kiến trúc 23/09 — Codex, snapshot `d3c82a4`

**Kết luận: đạt bước gom code theo SDK, chưa đạt tiêu chí app unify theo layer hoặc bàn giao
một datamodel bằng cách xóa code + chọn build profile.** Lượt này chỉ review và cập nhật thiết
kế, không sửa source, patch hoặc tarball. Các mục dưới thay thế khẳng định quá rộng ở §9 và
README. Thiết kế đích nằm tại [design §10–17](../../docs/icwmp_multiplatform_tr098_design.md#10-kết-quả-review-và-mục-tiêu-kiến-trúc).

### 10.1 Snapshot và phạm vi bằng chứng

Source được review là **overlay đang phát triển**, không phải code đã cài vào SDK:

- `O = projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/`.
- `L = O/public/libs/libtr098/libtr098/`, `A = O/public/apps/icwmp/icwmp/`.
- `F = projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098/feeds/`.
- Anchor `L/...:line`, `A/...:line` dưới đây thuộc commit `d3c82a4`, tìm lại theo symbol nếu
  snapshot thay đổi. **Verified** nghĩa là đã đối chiếu source, không phải đã chạy trên board.

Nhận bàn giao: overlay HEAD đúng `d3c82a4`, `git status --short` rỗng. `git apply --reverse
--check` với patch `0033` thành công, chỉ xác nhận patch khớp nội dung đã áp trong overlay,
**không phải** forward-apply toàn stack hoặc build. Hai source vendor `src/2025q3` và
`src/bcm963xx` có `git status --porcelain -uno` rỗng. Tarball có SHA-256
`2a738d0eaa0117a2f57cdf7124e716819647408dcfd243d62b2a8dfc35467214` đúng README.
Không tạo scratch, không apply, không commit, không build trong lượt review.

### 10.2 Findings theo mức ưu tiên

#### R1 — P1: chưa có lựa chọn build độc lập cho hai model (Verified)

[A/configure.ac:29](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/apps/icwmp/icwmp/configure.ac:29) khai báo `--enable-icwmp_tr098`. [A/bin/Makefile.am:5](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/apps/icwmp/icwmp/bin/Makefile.am:5) chọn
`icwmp_tr098d`, nhánh kia link `-lbbfdm -lbbf_api` ở `:72`. Nhánh libtr098 link `-ltr098`.
Đây là lựa chọn **engine**, không phải lựa chọn model ACS. [L/sdk/bdk/sdk.mk:8](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/sdk/bdk/sdk.mk:8) và `:9`
cùng đưa `root_bdk.c` và `root181_bdk.c` vào một link set, không có conditional model.
`L/configure.ac` chỉ có lựa chọn SDK/TR-064, chưa có `--enable-model-tr098/tr181`.

**Tác động:** tắt `--enable-icwmp_tr098` để lấy TR-181 sẽ chọn engine khác, không phải TR-181
đã port trên BDK. Xóa `root181_bdk.c` hiện tại làm thiếu source trong build list. Phải tách
`DM engine`, `compiled models`, `active model` thành ba khái niệm khác nhau.

#### R2 — P1: TR-181-only còn phụ thuộc code đặt dưới TR-098 (Verified)

Chuỗi phụ thuộc thực tế:

1. [L/bin/Makefile.am:25](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/bin/Makefile.am:25) luôn compile `tr098/managementserver.c`, `softwaremodules.c`,
   `tr098/common/icwmpcfg.c`, luôn thêm include path `tr098/` và `tr098/common/`.
2. [L/sdk/bdk/dm098/root181_bdk.c:24](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/sdk/bdk/dm098/root181_bdk.c:24) include `root_bdk.h`, `mlo_bdk.h`, `icwmpcfg.h`,
   `managementserver.h`. Bảng `tManagementServer181Params` tại `:41` dùng getter/setter
   của `tr098/managementserver.c`; các bảng MLO/sample cũng đang ở `dm098/`.
3. [L/sdk/bdk/dmplatform_bdk.c:427](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/sdk/bdk/dmplatform_bdk.c:427) gọi `tr098_bdk_register_all()` vô điều kiện khi init
   context. Hàm đó tại `dm098/root_bdk.c:57` đăng ký DeviceInfo/LAN/WAN/System TR-098.
4. Chiều ngược lại: [L/sdk/bdk/dmproxy_bdk.c:113](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/sdk/bdk/dmproxy_bdk.c:113) tham chiếu
   `tManagementServer181Params` và `tDeviceInfo181Params` được định nghĩa trong root TR-181.
   Proxy này còn dùng cho `InternetGatewayDevice.X_MARUSYS_COM_Device.` của bản TR-098
   (`root_bdk.c:44`), nên không thể xóa cả proxy chỉ vì tắt ACS TR-181.
5. Wrapper install [O/public/libs/libtr098/Makefile:38](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/Makefile:38) copy `tr098/*.h` vô điều kiện,
   OpenWrt `F/libtr098/Makefile` cũng làm tương tự trong `Build/InstallDev`.

**Tác động:** xóa `tr098/`, `sdk/bdk/dm098/`, hoặc chỉ root181 chưa tạo ra release một model.
Đưa implementation dùng chung sang `services/`/`common/`, tách bảng path theo model, gate
registration/include/source/install từ cùng profile. Giữ MDM backend TR-181 của BDK kể cả
khi ACS chỉ thấy TR-098. Proxy vendor là feature riêng, có thể tắt độc lập.

#### R3 — P1: compat-off chưa kín ở compile và package (Verified / Conditional)

**Verified:** [L/sdk/mtk/sdk.m4:12](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/sdk/mtk/sdk.m4:12) nhận `--disable-dm-script-compat`, và `sdk.mk:14`
loại `compat/dmscript.c`. `dmplatform_mtk.c:68` guard include `compat/dmscript.h`, nhưng
các hàm `mtk_get_value()` (`:303`), `mtk_get_name()` (`:315`), `mtk_set_value()` (`:353`)
và các helper đến `mtk_inform()` vẫn chứa `dmscript_request()` ngoài guard. Nhánh hook
runtime đã được guard ở `:594`, nhưng đó không loại các helper khỏi translation unit.

**Conditional:** compiler có thể báo implicit declaration, build với cảnh báo thành lỗi
sẽ fail. Undefined reference còn lại ở link phụ thuộc tối ưu loại bỏ static function và
link flags, chưa chạy compiler nên không khẳng định mọi profile đều lỗi link.

**Verified:** `F/libtr098/Makefile` vẫn cài `sdk/mtk/compat/icwmp_dm.sh` vô điều kiện trong
`Package/libtr098/install`. Nếu đã xóa thư mục compat, bước install này dùng đường dẫn
không tồn tại. Feed chưa có Kconfig/profile gate truyền compat-off. `F/icwmp_tr098/Makefile`
vẫn copy function library từ `cwmpclient` vô điều kiện.

**Tác động:** cân bằng `#if/#endif` và kiểm file tồn tại ở profile mặc định không chứng minh
feature-off an toàn. Cần gate đủ source, include, dependency, install, init và runtime.
Giữ riêng phần tích hợp `easycwmp` mà WebUI/STUN còn cần, không đồng nhất nó với shell DM.

#### R4 — P1: runtime không kiểm model có trong binary (Verified)

[L/tr098/common/icwmpcfg.c:98](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/tr098/common/icwmpcfg.c:98) chỉ kiểm `dm_platform_name() == "bdk"` để cho đổi DataModel.
Không kiểm capability của binary. [L/sdk/bdk/dmproxy_bdk.c:130](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/sdk/bdk/dmproxy_bdk.c:130) đọc config rồi chuyển root
ở `:147`; [L/dmentry.c:115](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/dmentry.c:115) luôn dựng TR-098 trước rồi mới gọi SDK đổi root.
[L/dm_registry.c:181](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/dm_registry.c:181) dựng entry ngay cả khi không có module và đánh dấu `built=1`.

**Conditional:** nếu tương lai chỉ bỏ model registration mà giữ nhánh runtime, cấu hình cũ
có thể chọn model rỗng hoặc trả cây chỉ còn provider động. Hiện chưa có single-model build
để runtime-test kịch bản này. Ngoài ra app giữ `bdkTr181` riêng ([A/sdk/bdk/icwmp_bdk.c:98](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/apps/icwmp/icwmp/sdk/bdk/icwmp_bdk.c:98)),
lib giữ `proxy_tr181` riêng: hai consumer cần dùng cùng model selection đã resolve.

**Sửa thiết kế:** model registry trả descriptor/capability hợp lệ, chọn một lần cho session.
Từ chối model không compile. Profile một model bỏ quyền chuyển model hoặc công bố read-only.
Config cũ sai model phải báo lỗi rõ, không tự rơi về một cây khác.

#### R5 — P2: "portable" đang lẫn với "dùng lại được trên schema UCI cũ" (Verified)

[L/dmentry.c:79](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/dmentry.c:79) tạo global UCI contexts trực tiếp, `:81` init store tên `tr098` và
`L/bin/Makefile.am` link UCI/ubus cho mọi SDK. Điều này không tự nó là bug: UCI có thể là
**app-private store** dùng chung cả BDK. Tuy nhiên đọc schema thiết bị phải nằm phía adapter.
Ví dụ [L/tr098/softwaremodules.c:149](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/tr098/softwaremodules.c:149) gọi `swmodules.environment`, `:166` gọi
`swmodules.du_list`. Ubus transport dùng chung không đảm bảo SDK khác có hai service đó.
Module stock khác cũng đọc schema sản phẩm trực tiếp.

Khẳng định "không file ngoài SDK nhắc SDK" cũng quá rộng: [A/config.c:64](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/apps/icwmp/icwmp/config.c:64), `:1017`, `:1149`
còn `ICWMP_BDK`, CLI `-S/-X`, gọi `icwmp_bdk_set_shm_id()`/`set_boot_launched()`.
[L/tr098/common/icwmpcfg.c:101](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/tr098/common/icwmpcfg.c:101) còn so literal `bdk`.

**Khuyến nghị:** phân biệt common utility, app store và device backend. Tách CLI SDK qua hook,
DataModel dùng capability. Không cần loại UCI khỏi toàn binary hoặc đổi SONAME chỉ để đẹp layout.

#### R6 — P2: registry không thực thi hợp đồng một path một chủ (Verified)

[L/dm_registry.c:99](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/dm_registry.c:99) (`merge_leaf`) ghi đè leaf trùng bằng module sau; `merge_entry()`
chỉ ghi đè callback non-NULL. Không có khai báo override target, không phát hiện xung đột
kiểu/access/owner. `dm_registry_owns():244` chỉ duyệt union các prefix `.paths`, không
kiểm overlap hoặc xác nhận claim khớp leaf thực sự hiện diện. `root181_bdk.c` còn không
có `.paths`, BDK dùng `proxy_local_objs`/`proxy_static_leaves` viết tay riêng.

Do đó câu ".paths đảm bảo một path một chủ" ở §9 là **ý định thiết kế, chưa được thực thi**.
Với claim cả subtree, chỉ nên loại fallback khi subtree đã port đủ. Nếu chỉ port vài leaf
cần claim theo leaf/instance pattern và xử lý enumerate parent rõ ràng.

`merge_leaf/merge_obj` trả cây cũ khi `calloc` fail (`:110`, `:161`), nhưng `build()` vẫn
đánh dấu hoàn tất. `dm_registry_add():45` bỏ qua overflow/registration muộn và duplicate
name không báo lỗi. Đây là fail-open ở khâu dựng schema, cần trả lỗi init thay vì publish
cây thiếu module. Bộ nhớ merge trung gian giữ lại là vấn đề startup hữu hạn, chưa có bằng
chứng leak tăng theo từng RPC. Mutex registry không bảo vệ toàn bộ global UCI/dmmem/dmroot.

#### R7 — P1 trước rollout: VALUECHECK/VALUESET chưa chứng minh atomic transaction (Verified / Not established)

**Verified:** [L/dmentry.c:343](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/dmentry.c:343) apply từng leaf rồi `:362` gọi commit SDK, cuối cùng mới
commit UCI/ParameterKey ở `:366`. Khi VALUESET fail, gọi `dmuci_revert` + platform revert.
Khi SDK commit fail, chỉ gọi `dmuci_revert` tại `:364`.

MTK `time_mtk.c:202` sửa UCI và `:203` gọi `mtk_apply_service()` ngay trong VALUESET.
`dmmtk.c:193` append trực tiếp file `/tmp/.easycwmp_apply_service`; platform revert
(`dmplatform_mtk.c:536`) không xóa các entry C của RPC thất bại. Cuối session
`dm_platform_restart_services():549` chạy hàng đợi. **Conditional:** một RPC có Time setter
đã chạy, sau đó leaf khác hoặc commit thất bại, có thể để lại restart đã xếp hàng dù SPV fault.

BDK gọi batch HAL tại `dmplatform_bdk.c:482` rồi engine mới commit UCI. Một batch HAL không
chứng minh atomic xuyên MDM + UCI + persistence. **Not established:** khả năng rollback
mọi SDK, mọi leaf hoặc lỗi giữa các store; cần fault injection trên SDK/board.

Hợp đồng đích phải có transaction-local pending writes/actions, abort trên mọi đường lỗi,
phân biệt apply runtime với persist và nêu rõ giới hạn backend. Hai pha validate/apply là
cần thiết nhưng không được dùng làm bằng chứng "transaction giống nhau trên mọi SDK".

#### R8 — P2: build profile chưa là nguồn lựa chọn chung app/lib/package (Verified)

[O/public/libs/libtr098/Makefile:20](/home/nvtu/workspace/AI-WORKSPACE-v2/projects/brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/Makefile:20) chỉ configure nếu chưa có generated Makefile. Đổi SDK
hoặc feature khi reuse build directory có thể giữ config cũ. App/lib có biến chọn SDK riêng,
scanner chỉ biết directory tồn tại, không kiểm khả năng của cặp SDK × model × feature.
`L/sdk/bdk/sdk.mk` bật `BDK_SAMPLE_OBJECT` trực tiếp. Packaging/headers nằm ngoài scanner.

Cần profile manifest chung, fingerprint build, báo cấu hình effective, clean/reconfigure khi
profile đổi và kiểm app/lib cùng fingerprint lúc startup. Kiểm source reference và grep tên
SDK là static audit, **không phải** `autoreconf/configure/build/install` PASS.

### 10.3 Quyết định thiết kế được đề xuất

- Một binary chọn đúng một SDK khi build. Model compiled có thể là TR-098, TR-181 hoặc cả hai,
  chỉ một model active trong mỗi session. Product profile chọn board/operator/features.
- Layers: protocol app → DM engine/facade → service contracts → SDK backend. Model binding riêng
  SDK được phép ở `sdk/<sdk>/models/<model>/`, gọi backend của chính SDK qua private API.
- Tách `services` dùng chung (identity, agent settings, transaction, action queue, instance
  identity) khỏi bảng path của model. Giữ implementation dùng chung nếu model kia bị xóa.
- Giữ ABI/package `libtr098` trong đợt đầu. Đổi tên logic engine bằng API/manifest trước,
  không đổi binary/package đồng thời với semantics và persistence.
- Ưu tiên đóng R1–R4 và lỗi transaction/registry cần thiết trước khi mở rộng P2–P8. Chưa nên
  viết thêm hàng trăm getter dựa vào helper chưa có hợp đồng transaction/capability rõ.

### 10.4 Verification và giới hạn

- Đã trace caller/callee và build fragments cho R1–R8. `sdk-scan.sh --check` PASS cho cả app/lib.
  Không chạy negative build bằng compiler.
- Patch `0033` reverse-check PASS trên overlay hiện tại, tarball hash khớp, vendor source read-only.
- `check-docs.sh` PASS kiểm hiện có (Mermaid semicolon, patch header/path), cảnh báo 9 issue khác
  thiếu chatlog đã có trước lượt này. Đây không phải Mermaid render hoặc kiểm kiến trúc C.
- Build SDK, link/install từng profile, runtime model switch, rollback, giá trị P1 và coverage
  P2–P8 vẫn **NOT RUN / chưa hoàn tất**. Không gắn nhãn release-ready cho bản `0033`.

---

## 11. Đồng bộ 23/09 16:30 — trả lời review, sửa R3

Lượt này **có sửa source** (khác lượt review §10). Overlay `d3c82a4` → `cd93685`, vẫn trong
patch `0033`.

### 11.1 R3 — compat-off chưa kín ở compile: ĐÃ SỬA (Verified)

Review đúng: §9 chỉ guard **hook runtime** (`dm_platform_param_method`, `commit`, `revert`,
`restart_services`, hai hàm enabled-notify), còn các helper `mtk_get_value()`, `mtk_get_name()`,
`mtk_set_value()`, `mtk_add_object()`, `mtk_del_object()`, `mtk_inform()` vẫn nằm trong
translation unit và vẫn gọi `dmscript_request()` khi `--disable-dm-script-compat`.

Sửa: bọc **cả khối transport** — từ `mtk_script_path()` tới hết `mtk_merge_static()`, kèm
`struct mtk_reply`, `mtk_line_cb()`, `mtk_reply_init()`, `mtk_xsd_type()`, `mtk_cr_url()` — trong
một `#ifdef DM_MTK_SCRIPT_COMPAT`. Hai khai báo biến `struct mtk_reply r;` trong
`dm_platform_commit()` và `dm_platform_restart_services()` cũng phải chuyển vào trong guard,
nếu không chúng tham chiếu một kiểu không còn tồn tại.

Cách kiểm (không có compiler): bỏ mọi khối `#ifdef DM_MTK_SCRIPT_COMPAT` theo đúng ngữ nghĩa
`#else`, rồi tìm symbol của lớp compat trong phần còn lại.

```
bản --disable-dm-script-compat còn tham chiếu symbol của compat: KHÔNG
số dòng code còn lại khi tắt compat: 196
```

Phần package trong R3 giữ nguyên có chủ ý: feed `libtr098` vẫn cài `icwmp_dm.sh` vô điều kiện,
**đúng** với bản đang ship vì feed không truyền `--disable-dm-script-compat`. Khi bật cờ đó thì
hai dòng install phải bỏ cùng lúc — đã ghi comment ngay tại chỗ trong `feeds/libtr098/Makefile`.

### 11.2 Ba lỗi build do refactor, phát hiện khi người dùng hỏi về BDK (Verified)

| Lỗi | Hậu quả | Sửa |
|---|---|---|
| `tr098_bdk_register_all()` thành `static` (`root_bdk.c`) nhưng `dmplatform_bdk.c:427` vẫn gọi | BDK **lỗi link** | trả lại non-static; registry vẫn gọi qua `.init`, cờ `done` chặn chạy hai lần |
| `static get_empty()` trong `deviceinfo_mtk.c` trùng hàm engine `dmtr098.h:485` cùng chữ ký | MTK **lỗi compile** (`static declaration follows non-static`) | bỏ, dùng `get_empty()` của engine |
| Bỏ `--enable-bdk` | script build BDK cũ **âm thầm rơi về SDK `uci`** | trả lại làm alias; generator không còn xoá `$with_sdk` đã đặt trước |

Phép kiểm mới, bổ sung vào bộ "không có compiler": **tìm hàm `static` mà file khác trong cùng
link set gọi**. Bộ kiểm trước chỉ so link set với **file không được link**, nên không thấy lỗi
thứ nhất. Chạy cho cả 4 tổ hợp (libtr098/icwmp × bdk/mtk) — sạch sau khi sửa.

### 11.3 BDK có còn chạy được không (Verified ở mức source)

So **nội dung từng file** của 15 file BDK giữa `99f4988` (trước refactor) và hiện tại: thay đổi
duy nhất là đường `#include`, thay `tEntry098Obj[]`/`tEntry181Obj[]` bằng khai báo module cho
registry (**cùng bảng** `tRoot_098_Obj`/`tRoot_098_Params`), `ctx->dm_entryobj =
dm_registry_entry(DM_MODEL_TR181)`, và comment. **Không dòng logic nào của getter/setter/hook
BDK thay đổi.** Danh sách source BDK giống hệt (10 file), CFLAGS giống hệt —
`-DDM_PLATFORM_BDK` nay đến từ `AC_DEFINE` qua `$(DEFS)` vì tree không dùng `AC_CONFIG_HEADERS`.

**Ranh giới cần nhớ**: bản đã chạy được trên board Broadcom của người dùng là
`icwmp_bdk_port_overlay_2_brcm_20260921_OK.tar.gz` = tới patch **`0031`**. Cả `0032` và `0033`
đều **chưa từng build trên BDK**.

### 11.4 Trạng thái các finding còn lại

R1, R2, R4, R5, R6, R7, R8 **còn mở**, là thiết kế đích trong design §10–18 và gói A0–A6 của
`tr098_c_port_phases.md` §6, **chưa implement**. Rẻ nhất và nên làm trước trong source hiện tại:
R6 (registry báo trùng owner, ~20 dòng) và R7 (gắn transaction cho action queue).


## 12. Hoàn thiện layout, component flow và execution plan — 23/09, Codex

### 12.1 Baseline mới đã xác minh khi resume

**Verified:** overlay HEAD cd93685, worktree sạch. `git diff d3c82a4 cd93685` chỉ đổi
`public/libs/libtr098/libtr098/sdk/mtk/dmplatform_mtk.c`: guard toàn khối compat transport và
chuyển biến phụ thuộc vào đúng nhánh. Ba sửa lỗi §11.2 đã nằm trong d3c82a4, không phải delta
mới này. R3 sửa source guard được xác nhận, **chưa compile/link**; feed/package compat-off
và release xóa thư mục vẫn cần gate riêng. Kết quả strip không chứng minh build thành công.

MTK HEAD b207c4518 và BDK HEAD 7f837f5f6 không đổi, tracked worktree sạch. Re-extract inventory
MTK qua script hiện có, so 930 row với TSV: 749 param/181 object, không thêm/bớt/đổi row.
Tarball MTK hiện tại SHA256 `841b582c0f531b2884e1cc80185ae7d9ab728baf45051366a83ba8bb63d3e059`.
Lượt thiết kế này không sửa overlay/vendor source, patch, tarball hay feed.

### 12.2 Layout và hai model

Đề xuất source canonical **public/libs/libicwmp_dm/src/**, wrapper nằm một cấp trên, public
include dưới `include/icwmp_dm/`. Engine phục vụ hai facade nên không giữ tên thư mục theo
TR-098. SONAME/package cũ có thể giữ chuyển tiếp trong A1, đổi bằng gói có clean rebuild consumer
riêng. Không di chuyển persistent state chỉ vì rename source.

TR-098 implement thật theo inventory sản phẩm. TR-181 BDK reuse provider hiện có. Phần thiếu
có semantic mapping manifest, schema và callback body TODO, trạng thái theo SDK/operation.
Chỉ generate boilerplate từ mapping đã duyệt, không generate semantics bằng đổi root string.
Stub development trả lỗi rõ, không mutate. Production không enumerate stub. Counterpart chưa
xác định giữ mapping_todo, không bịa path chuẩn. Chi tiết [design §12 và §18](../../docs/icwmp_multiplatform_tr098_design.md#12-source-layout-đề-xuất-và-bản-đồ-di-chuyển).

### 12.3 Component/process flow và những boundary cần giữ

[Flow chi tiết](../../docs/icwmp_multiplatform_tr098_flow.md) có overview cùng MTK WAN/LAN,
Wi-Fi/STA/Stats, Mesh và BDK Distributed MDM. Source anchors nằm ngay trong tài liệu đó.

- **Verified:** hni.wan là ubus object trong ubusmon, gọi HAL set/commit. Mutation có side effects,
  không đưa vào VALUECHECK. **Not established:** atomic rollback nhiều WAN field/store.
- **Verified:** HalWifi_getAssocDeviceList join host information và bỏ station chưa có IP.
  Canonical station inventory cần raw association riêng, Hosts chỉ enrich. Một số legacy Wi-Fi
  counter đọc br-lan, không xem nó là canonical BSS counter. Mỗi stats field phải có scope/unit/epoch.
- **Conditional:** HAL vendor GET_STA → OSAL/vendor driver theo build flag. Exact loaded .ko,
  firmware ABI và HW-offload counter semantics vẫn cần board evidence.
- **Verified:** Mesh topology dùng mapd control/file dump. Proposed snapshot có lock, timeout,
  freshness, unique path, không dùng projection /tmp/P_CPE10 làm source of truth.
- **Verified/Conditional theo nhánh:** BDK giữ MDM owner dispatch, Wi-Fi STL/rutWifi helper,
  wldataeld/WBD collector đã có. Không dựng lại controller trong agent. WAN/PON lower callback
  chưa trace đủ thì ghi Not established, không suy driver từ tên parameter.

### 12.4 Thứ tự triển khai và giới hạn

[Phase plan §6](tr098_c_port_phases.md#6-kế-hoạch-thực-thi-từ-source-hiện-tại--2309) là nguồn kế hoạch:
A0 baseline → A1 rename → A2 profile/model/scaffold → A3 service/registry/transaction →
A4 migrate 65 param P1 → A5 P2–P8 theo domain → A6 full-C/export/prune/build/board/soak.
P2/P3/P4 chia gói nhỏ, mỗi gói có TR-181 disposition và acceptance. Full Mesh topology ngoài
749 baseline có inventory extension riêng. R6/R7 phải xong trước nhân rộng port, không coi R7
là sửa rẻ theo số dòng. Nếu xử lý sớm trên layout cũ, tính vào A3 thay vì làm lại.

Các API/profile mới là Proposed. Không có rename/implementation patch mới trong lượt này.
Build, link/install và board tests vẫn NOT RUN. Bản BDK đã chạy trên board là tới 0031,
không dùng bằng chứng đó để xác nhận 0032/0033 hay thiết kế mới.


### 12.5 Kiểm tài liệu và bàn giao

- `./scripts/check-docs.sh`: PASS, 9 cảnh báo chatlog thuộc issue khác giữ nguyên.
- Local link target/whitespace cho design, flow, phase plan, README: PASS. Có 6 Mermaid diagram
  trong flow chi tiết. Chưa chạy Mermaid renderer, checker không thay cho render.
- `git diff --check` trong phạm vi memory/knowledge: PASS. Không tạo scratch/copy source.
- Đóng riêng phần thiết kế `_1` sau khi lưu kết quả. Giữ phiên gốc PAUSED để nhận build/board
  và triển khai code theo phase, không coi toàn issue đã hoàn thành.


## 13. Thực thi A1 theo yêu cầu — 23/09, Codex

User yêu cầu code và command theo dõi. Đã rename overlay library sang libicwmp_dm/src, update
app includes/autodetect, BDK wrapper, SDK prune, shared header installer, feed MTK và hai installer.
Giữ SONAME/package libtr098, source C/header parity 228 file chỉ khác include paths. Không đổi
semantics getter/setter và không implement các phase A2–A6 ở gói layout này.

Đã thêm `progress.py [--watch [seconds] | --json]` và `implementation-status.json`, hiển thị phase,
current/next, pending tasks và validation. Trạng thái lưu bền vững, không giả làm live heartbeat AI.

[Hướng dẫn](a1-implementation.md), [verification](a1-verification.md), patch overlay 0034,
bundle standalone icwmp_a1_port.tar.gz. Installer BDK reject integration profile không khớp trước
khi archive/copy, sau khi test phát hiện thứ tự cũ có thể ghi source rồi mới fail.

Static + fixture + patch replay PASS. Không compiler/SDK build/board, không commit, không apply
vendor src. A1 build gate là bước kế tiếp trước A2 semantics, session gốc giữ để tiếp tục.

## 14. Lượt 23/09 tối — R6, R7, P2 và inventory bị sai (Claude Code)

### 14.1 Inventory cũ vừa thừa vừa thiếu

`gen-coverage-matrix.py` gom phép gán biến theo **file** rồi mới bung, lại `setdefault` nên
**gán đầu tiên thắng**. Với thư viện shell này, `base`/`obj`/`stats_path` được đặt lại trong từng
hàm, nên:

| Hậu quả | Số lượng | Ví dụ |
|---|---|---|
| Path **bịa ra** (ghép `base` của hàm này với tên lá của hàm khác) | 29 | `WLANConfiguration.{i}.WPSAlias`, `WPSStatus`, `WPS.Stats.BytesSent` |
| Path thật **bị mất** (không bung được → không bắt đầu bằng `InternetGatewayDevice.` → loại im lặng) | 61 | 8 leaf port của `LANEthernetInterfaceConfig`, 17 leaf `WLANConfiguration.AssociatedDevice`, 36 leaf `Firewall.X_AIS_*` |
| Leaf gán nhầm nhánh | 2 | `Prefix`, `PrefixLen` vào `IPV4ServiceControl`, trong source chỉ `IPV6ServiceControl` có (`functions/tr098/firewall:422`) |

Đã sửa thành quét **tuần tự, gán sau đè gán trước**. Bảng mới sinh lại khớp byte-for-byte,
**968 dòng = 184 object + 783 param**, `X_AIS_*` 235, `X_HNI_*` 0, getter `$UCI_GET` thuần 33.
Phase đổi: P2 62 → **70**, P3 75 → **67**, P6 63 → **97**.

Bảng cũ không được giữ lại: nó chứa path không tồn tại, giữ lại chỉ tạo rủi ro có người port theo.
Dựng lại bản bất kỳ bằng chính `gen-coverage-matrix.py` trên snapshot tương ứng.

### 14.2 R6 — registry báo trùng chủ sở hữu path

`dm_registry_owns()` quét module theo thứ tự và trả về ngay khi khớp, nên hai module cùng khai một
path thì cầu nối compat lọc theo một chủ **tùy ý** và một trong hai cây im lặng biến mất.
`check_claims()` chạy trong `build()` báo từng cặp chồng nhau (bằng nhau hoặc một bên là tiền tố),
`dm_registry_conflicts()` trả số. Không fatal: cây sai lúc chạy còn tệ hơn log ồn lúc dev.

Cách mở rộng object của module khác **không** đổi: khai `.objs` cùng tên object và **không khai
`.paths`** — đúng pattern `managementserver_core_mtk.c` đang dùng.

### 14.3 R7 — transaction cho hàng đợi action cuối phiên

Setter xếp action (reboot, factory reset, diagnostic) vào `list_execute_end_session` **trong lúc**
RPC còn đang ghi. Khi RPC fault, engine `dmuci_revert()` + `dm_platform_revert()` nhưng hàng đợi
vẫn còn — một SetParameterValues đã rollback vẫn reboot được board.

`dm_end_session_mark()` chụp đuôi danh sách + `end_session_flag`, `dm_end_session_rollback()` bỏ
mọi action xếp sau mốc và khôi phục cờ. Gọi ở cả ba nhánh fault của `dm_entry_apply`
(SET_VALUE per-param, SET_VALUE commit hỏng, SET_NOTIFICATION).

Sửa kèm: nhánh `dm_platform_commit()` hỏng trước đây chỉ `dmuci_revert()`, không gọi
`dm_platform_revert()` — hàng đợi phía platform còn nguyên cho RPC sau. Đã thêm. An toàn trên cả ba
SDK: BDK `bdk_pending_free_all()` idempotent, MTK gửi `set_abort`, uci là no-op.

### 14.4 P2 — nhánh LAN bằng C, 70/70

| File | Nhánh | Backend giữ nguyên như shell |
|---|---|---|
| `lan_mtk.c` | `LANDevice.1` + `LANHostConfigManagement` + `IPInterface.1` | `dhcp.lan.*`, `network.lan.*`, `/rom/etc/config` cho default, `wireless.<radio>.txpower` |
| `lanhosts_mtk.c` | `Hosts.Host.{i}` | UCI `lanhost` (section `host`), `/tmp/dhcp.leases` cho lease |
| `laneth_mtk.c` | `LANEthernetInterfaceConfig.{i}` + `Stats` | `network.@SwitchPara[i-1]`, `/proc/tc3162/gsw_stats`, `/sys/.../eth0.{i}/statistics`, `ethphxcmd`, `switchmgr` |
| `x_ais_mesh_mtk.c` | `X_AIS_Mesh` (4 leaf forced-inform) | `wireless.*.map_mode`, `1905d_cfg.map.max_hop`, `clay.opermode`, `mapd_cli dump_topology_v1` |

Giữ nguyên có chủ ý:

- `dhcp.lan.configurable=0` chặn ghi với **9002**, riêng `MaxAddress` trả **9007** — đúng như shell.
- Các leaf writable nhưng shell không có setter (`ReservedAddresses`, `AssociatedConnection`,
  `PassthroughMACAddress`, `AllowedMACAddresses`, `UseAllocatedWAN`, `PassthroughLease`,
  `IPInterface.1.Enable/Alias/AddressingType`) vẫn **nhận rồi bỏ**, không đổi thành 9008 — đổi sẽ
  làm hỏng script provisioning đang ghi chúng nhiều năm nay.
- `LANEthernetInterfaceConfig` luôn công bố **4 instance**, như `sub_entry_LANEthIfConfig_all()`
  lặp 1..4 bất kể `LANEthernetInterfaceNumberOfEntries`.
- Reload dịch vụ và reboot của mesh **xếp vào apply-service**, không gọi thẳng trong setter: shell
  commit UCI ngay trong hàm rồi mới gọi, engine thì commit ở cuối RPC, nên xếp hàng mới đúng thứ tự.

Khác biệt có chủ ý: `MeshEnabled`/`MeshMode` thiếu option UCI thì trả mặc định mà không fault
(shell trả 9002). Một fault giữa GetParameterValues toàn cây làm hỏng cả RPC.

### 14.5 Kiểm tĩnh đã chạy (chưa có compiler)

| Kiểm | Kết quả |
|---|---|
| Tên tham số P2 so cây shell | **70/70**, 0 dôi |
| Cân bằng brace/paren/bracket/`#if` (nhận biết comment + string) | PASS 4 module P2 + `dmmtk.c/h` |
| Symbol dùng nhưng không có khai báo | PASS (rỗng) |
| Hàm `static` bị file khác trong link set gọi | PASS (3 kết quả đều nằm trong comment) |
| Sinh lại ma trận từ source | PASS byte-for-byte |
| Bundle giao + `apply --dry-run` hai cây SDK thật | PASS (sha256 `76f032df3193`) |

**Chưa compile, chưa link, chưa chạy trên board.**

## 15. P3a — Wi-Fi bằng C, 54/67 (Claude Code, 24/09)

### 15.1 Chia P3 làm hai

67 tham số của `WLANConfiguration` không port một lượt: 13 leaf bảo mật quyết định việc khách có
vào được Wi-Fi hay không, sai một bước ánh xạ `encryption` là khoá máy khách ngoài mạng. Tách:

- **P3a (lượt này, 54)**: radio/identity, kênh + auto channel, công suất, chuẩn và tốc độ,
  MU-OFDMA, bộ đếm, WPS, `AssociatedDevice`.
- **P3b (kế tiếp, 13)**: `BeaconType`, `BasicAuthenticationMode`, `BasicEncryptionModes`,
  `WPAAuthenticationMode`, `WPAEncryptionModes`, `IEEE11iAuthenticationMode`,
  `IEEE11iEncryptionModes`, `KeyPassphrase`, `PreSharedKey.1.KeyPassphrase`,
  `PreSharedKey.1.PreSharedKey`, `WEPEncryptionLevel`, `WEPKeyIndex`, `WEPKey.{i}.WEPKey`.

### 15.2 Bản đồ instance là hợp đồng với ACS

| Instance | Interface | Vai trò |
|---|---|---|
| 1–4 | `ra0` `ra1` `ra2` `ra3` | 2.4 GHz fronthaul |
| 5–8 | `rai0` `rai1` `rai2` `rai3` | 5 GHz fronthaul |
| 9 | `rai4` | 5 GHz backhaul |
| 10 | `ra4` | 2.4 GHz backhaul |
| 11, 12 | `ra5` `rai5` | cặp MLO fronthaul |

Thứ tự này **không được sắp lại cho gọn**: ACS đã provision theo đúng số instance. Instance 9 là
`rai4` chứ không phải `ra4` — nhìn thì lệch, nhưng đó là cây hiện hành.

### 15.3 Hai cặp phải đồng bộ, và mapd

Setter nào đụng `ssid`/`disabled` đều phải ghi cả cặp và cả section MLO, đúng như
`mlo_sync_*`/`backhaul_sync_*`:

| Cặp | Thành viên | Section MLO |
|---|---|---|
| Fronthaul MLO | `ra5` ↔ `rai5` | `apmld1` |
| Backhaul | `ra4` ↔ `rai4` | `apmld2` |

Khi mesh đang bật (`map_mode` khác 0 trên **cả hai** radio), giá trị còn phải ghi sang node riêng
của mapd (`mapd.1` … `mapd.12`) — nếu không, lần `wifi reload` sau mapd ghi đè `wireless` từ
config của nó và thay đổi của ACS biến mất. `SSIDAdvertisementEnabled` là chỗ dễ sai nhất: mapd
đánh vần `Y`/`N` và **ngược nghĩa** với `hidden` của `wireless`.

### 15.4 AssociatedDevice: bỏ được lớp cache tạm

Shell phải spool `ubus call hni getWlanDeviceList` ra `/tmp/…cache` vì hàm shell không giữ được
state giữa các lần getter trong cùng một RPC, kèm theo cả cơ chế "ai là chủ cache" để dọn. Trong C,
callback browse đọc một lần rồi phát từng station cho instance của nó — bỏ hẳn file tạm, biến đếm
chủ sở hữu và bước dọn. Interface đang `disabled` thì không gọi ubus, đúng như `assoc_build_cache`.

### 15.5 Registry: claim theo segment, có wildcard

Nửa còn lại của chính object này vẫn do cầu nối shell trả lời, nên không được claim cả nhánh
`WLANConfiguration.` — claim vậy sẽ **giấu mất** 13 leaf bảo mật. Mà claim từng path cụ thể thì
phải liệt kê 12 instance × 30 leaf.

`dm_registry.c` được bổ sung `path_match()` so khớp theo **segment**:

| Dạng claim | Nghĩa |
|---|---|
| `IGD.Foo.` | object đó và mọi thứ bên dưới |
| `IGD.Foo.Bar` | đúng một leaf đó — trước đây `strncmp` khiến nó nuốt cả `IGD.Foo.BarBaz` |
| `IGD.Foo.{i}.Bar` | leaf đó của **mọi** instance |

Nhờ vậy P3a claim 30 leaf + `Stats.` + `WPS.` + `AssociatedDevice.` bằng 33 dòng, không đụng vào
phần shell còn giữ. Đây cũng là thứ P4 (WAN, nhiều object có instance) sẽ cần.

### 15.6 Kiểm tĩnh

| Kiểm | Kết quả |
|---|---|
| Tên tham số P3a | **54/54**; P2+P3a **124/124**, dôi 0, thiếu đúng 13 leaf P3b |
| `path_match()` — wildcard, leaf chính xác, prefix, đối xứng cho `covers`/`check_claims` | 23/23 case |
| 44 claim của P2+P3a chồng nhau | 0 cặp |
| Cân bằng brace/paren/`#if` | PASS |
| Symbol dùng nhưng không khai báo | PASS |
| Bundle + `apply --dry-run` hai cây SDK thật | PASS, sha256 `fa0db684e98d`, 371 file |

Matcher được kiểm bằng cách **port thuật toán sang Python rồi chạy bảng case** — host không có
compiler, nên đây là kiểm *thuật toán*, không phải kiểm cú pháp C. **Chưa compile, chưa board.**

## 16. P3b — bảo mật Wi-Fi, xong cả phase P3 (Claude Code, 24/09)

### 16.1 Một option quyết định tất cả

13 leaf còn lại đều đọc/ghi `wireless.<iface>.encryption`. Bảng chính tả của sản phẩm:

| `encryption` | Nghĩa |
|---|---|
| `none` | mở |
| `wep+shared+64` / `+128` | WEP 40 bit / 104 bit, key nằm ở `key1..key4` |
| `psk` | WPA personal, TKIP |
| `psk2+ccmp` | WPA2 personal, AES |
| `psk-mixed+ccmp` | WPA/WPA2 mixed, AES |
| `psk-mixed+tkip+ccmp` | WPA/WPA2 mixed, TKIP + AES |
| `sae` / `sae-mixed` | WPA3 / WPA3 transition |

### 16.2 Hai cái bẫy giữ nguyên, không dọn

1. **`.key` mang hai nghĩa.** Với WPA nó là passphrase; với WEP nó là **số thứ tự key** (1–4) còn
   key thật nằm ở `key1..key4`. `set_wep_key_index()` ghi số vào đúng option đó. Nhìn như bug,
   nhưng WebUI và mapd đang đọc theo quy ước này.
2. **Chỉ 3 trong 6 setter đẩy sang mapd.** `BeaconType`, `BasicAuthenticationMode`,
   `WEPEncryptionLevel` ghi `authmode`/`EncryptType` vào node mapd; `WPA*` và `IEEE11i*` **chưa
   từng** làm. Port sang C giữ y nguyên — "sửa cho nhất quán" sẽ đổi thứ mapd ghi đè ở lần reload
   sau, đó là quyết định của sản phẩm chứ không phải của lớp CWMP.

### 16.3 `KeyPassphrase` ≠ `PreSharedKey.1.*`

Hai leaf trông như một, hành vi khác hẳn — cả hai được giữ:

| | `WLANConfiguration.KeyPassphrase` | `PreSharedKey.1.{KeyPassphrase,PreSharedKey}` |
|---|---|---|
| Từ chối khi đang WEP | mọi `wep+` | chỉ `wep+shared+64/128` |
| Kiểm độ dài | **không** | 8–63, ngoài khoảng → 9007 |
| Ghi thêm | — | `key1` = 5 ký tự đầu (digest WebUI hiển thị) |
| Đồng bộ cặp MLO | **có** | không |
| Getter | trả `key` hiện tại | trả rỗng |

### 16.4 Một khác biệt có chủ ý

`BeaconType` = `11i` hoặc `WPAand11i` khi BSS **đang chạy SAE** thì giữ nguyên `sae`/`sae-mixed`
thay vì ghi đè `psk2+ccmp`. Shell cũng có nhánh này, và nó quan trọng: ACS đọc `BeaconType` ra
`11i` rồi ghi lại đúng giá trị đó là chuyện thường, nếu hạ xuống `psk2` thì một BSS WPA3 bị âm
thầm hạ cấp và client chỉ hỗ trợ SAE rớt mạng.

### 16.5 Claim gộp lại sau khi cả object là C

P3a phải claim **30 leaf** vì nửa còn lại của object vẫn do shell trả lời. Xong P3b thì cả
`WLANConfiguration` là C, nên:

- `wlan_mtk.c` giữ **một** claim nhánh `InternetGatewayDevice.LANDevice.1.WLANConfiguration.`
- `wlanassoc_mtk.c` và `wlansec_mtk.c` **không claim gì** — claim thêm sẽ là trùng chủ sở hữu,
  đúng thứ `check_claims()` (R6) báo.

Kiểm lại toàn bộ 7 module MTK: **16 claim, 0 cặp chồng nhau.**

### 16.6 Kiểm tĩnh, và một báo động giả

| Kiểm | Kết quả |
|---|---|
| Tên tham số P3 đầy đủ | **67/67**; P2+P3 **137/137**, thiếu 0, dôi 0 |
| Claim chồng nhau giữa 7 module | 0 cặp |
| Cân bằng brace/paren/`#if` | PASS |
| Symbol dùng nhưng không khai báo | PASS |
| Bundle + `apply --dry-run` hai cây SDK thật | PASS, sha256 `62195378bfc4`, 373 file |

Lần chạy đầu script kiểm claim báo **15 cặp chồng** với `InternetGatewayDevice.` của `root_mtk.c`.
Đó là lỗi của **script**, không phải của code: claim thật là
`"InternetGatewayDevice." CUSTOM_PREFIX "Icwmp."` — ba literal C nối lại lúc biên dịch thành
`InternetGatewayDevice.X_HNI_Icwmp.`, còn regex thì tách rời từng literal. Đã sửa script nối
literal liền nhau và bung `CUSTOM_PREFIX` trước khi so.

**Chưa compile, chưa link, chưa board.**

## 17. P4a — khung `WANDevice` bằng C, 22/173 (Claude Code, 24/09)

Phase lớn nhất (173 param) nên chia làm năm bước, bước này là **P4a**: khung nhánh cộng ba object
không phụ thuộc entry WAN nào. Patch `0040`, overlay `3cf999f`.

| Object | Param | Nguồn dữ liệu |
|---|---|---|
| `WANDevice.1.WANCommonInterfaceConfig.` | 9 | `clay.opermode.uplink`, `pon.xpon_link.trafficStatus`, `/sys/class/net/<uplink>/statistics/*` |
| `WANDevice.1.WANEthernetInterfaceConfig.` | 4 | hằng số (`get_fake_WANEthernet*`) |
| `WANDevice.1.WANEthernetInterfaceConfig.Stats.` | 4 | cùng bộ đếm uplink như trên |
| `WANDevice.1.WANConnectionDevice.1.WANDSLLinkConfig.` | 5 | hằng số (`wan_dsl_link_*`) |

`WANIPConnection` / `WANPPPConnection` vẫn do `sdk/mtk/compat/` trả lời, nên `.paths` chỉ claim ba
nhánh trên chứ không claim cả `WANDevice.`.

### 17.1 `WANAccessType` rỗng — lỗi có thật của sản phẩm, giữ nguyên

`functions/tr098/wan_device:3095` đăng ký getter `wan_common_get_access_type`. Hàm đó **không được
định nghĩa ở bất kỳ đâu** trong `ext/` — `grep -rn wan_common_get_access_type` trên cả cây cho
đúng một dòng, chính là dòng đăng ký. `common_get_value_param()` chạy `` local val=`$getcmd` `` nên
lệnh không tồn tại → `val` rỗng → ACS nhận chuỗi rỗng cho một tham số mà TR-098 định nghĩa là enum
`DSL | Ethernet | POTS`.

Bản C trả về `""` **y như thiết bị đang chạy**. Điền `"Ethernet"` là đổi hành vi sản phẩm: ACS đã
đọc rỗng suốt vòng đời máy, và luật provisioning phía ACS có thể đang rẽ nhánh theo giá trị rỗng
đó. Đây là quyết định của sản phẩm, không phải của lớp CWMP — ghi lại ở đây để lúc nào chốt thì
sửa một dòng trong `get_wancommon_access_type()`.

### 17.2 Hai thứ "giả" được giữ nguyên

`WANEthernetInterfaceConfig` và `WANDSLLinkConfig` là hằng số — chính shell đặt tên hàm là
`get_fake_*` và `wan_dsl_link_*`. Máy này là gateway PON/Ethernet, không có đường DSL. Giữ vì
template của ACS vẫn duyệt qua các object đó, bỏ đi là mất object trong GPN.

Đáng chú ý: `WANEthernetInterfaceConfig.Status` luôn là `"Down"` kể cả khi uplink đang chạy —
`PhysicalLinkStatus` của `WANCommonInterfaceConfig` mới là cái bám trạng thái thật
(`pon.xpon_link.trafficStatus`). Không sửa, vì cùng lý do 17.1.

### 17.3 Leaf ghi được nhưng không có setter

`WANEthernetInterfaceConfig.Enable` khai permission `1` với setter là **literal `true`** — tức là
`/bin/true`: nhận lệnh ghi, thành công, không làm gì. Bốn leaf của `WANDSLLinkConfig` dùng
`wan_dsl_link_set_fake()` cũng vậy. Bản C dùng `set_accept_and_drop()`, giống hệt cách P2 xử lý
nhóm leaf này. Trả `9008` sẽ làm fault những script provisioning đã ghi thành công nhiều năm.

### 17.4 Chính tả boolean giữ đúng chữ của shell

`Enable` của cả hai object trả `"true"` (shell là `echo true`), còn `EnabledForInternet` trả `"1"`
(shell là `echo 1`). Engine **không chuẩn hoá** giá trị boolean trên đường ra — `add_list_paramameter()`
đẩy nguyên chuỗi của getter. Đổi `"true"` thành `"1"` sẽ làm gate 2 trên board (so từng giá trị
với client cũ) báo lệch ở chỗ thực ra không lệch.

### 17.5 Kiểu trên dây: hai leaf là `xsd:string`, không phải số

`MaxBitRate` và `Status` được shell đăng ký **không có tham số type** (`$5` rỗng), và
`mtk_xsd_type("")` trả `DMT_TYPE[DMT_STRING]`. Nên bản C để `DMT_STRING`. Trùng hợp là TR-098 cũng
định nghĩa `MaxBitRate` là string enum, nên vừa đúng chuẩn vừa không đổi định dạng trên dây.

Sẽ gặp lại ở P4b: nhiều dòng `Stats.*` của `WANIPConnection` truyền `"xsd:unsignedInt"` **vào ô
setter** (`$4`) thay vì ô type (`$5`), nên cũng đang đi ra dưới dạng `xsd:string`.

### 17.6 Object container không claim — và vì sao không sinh dòng đôi

`WANDevice.`, `WANDevice.1.`, `WANConnectionDevice.`, `WANConnectionDevice.1.` do **cả hai bên**
sinh ra: cây C (vì phải có đường tới ba object đã port) và shell (vì `WANIPConnection` vẫn của nó).
Không sinh dòng trùng trong GPN vì `add_list_paramameter()` (`dmtr098.c:673`) chèn theo thứ tự tên
và `strcmp == 0` thì **return ngay**. Đây cũng là lý do P2 claim từng object chứ không claim cả
`LANDevice.`.

### 17.7 Công cụ mới: `verify-dm-paths.py`

Các phase trước so **tên leaf**; từ đây so **đường dẫn đầy đủ**. Script dựng lại cây từ bảng
`DMOBJ`/`DMLEAF` rồi đối chiếu với `tr098_coverage_matrix.tsv`:

```sh
./verify-dm-paths.py --phase 4        # theo phase của ma trận
./verify-dm-paths.py --prefix InternetGatewayDevice.WANDevice.
./verify-dm-paths.py --claims         # .paths có cặp nào phủ nhau không
```

Ba thứ phải làm đúng mới ra số đúng, cả ba đều là lỗi tôi mắc rồi sửa trong lượt này:

1. **Chỉ đọc file build thật sự biên dịch** — lấy danh sách `.c` từ `bin/Makefile.am` cộng
   `sdk/<sdk>/sdk.mk`. Quét cả thư mục sẽ kéo vào model portable của iopsys (`tr098/landevice.c`)
   — không nằm trong build MTK và **trùng tên bảng** với module MTK (`tIPInterfaceParam`,
   `tWepKeyParam`), làm kết quả sai lặng lẽ.
2. **Gộp cây theo tên object trước khi duyệt**, đúng như `dm_registry.c` gộp. `lanhosts_mtk.c` để
   `browseinstobj` của `LANDevice` là `NULL` và mượn `{i}` của `lan_mtk.c`; duyệt từng bảng riêng
   thì `Hosts.Host.{i}.*` bị dựng thành `LANDevice.Hosts...`, thiếu `{i}`.
3. **Quy một chính tả cho số instance.** Bộ trích xuất giữ lại biến shell (`$1`, `$2`, `$3`) và cả
   số viết cứng (`IPInterface.1.`); cây C dùng `{i}`. Quy mọi segment toàn chữ số về `{i}` ở cả
   hai phía.

Bằng chứng script đúng: chạy lại trên phase đã chốt bằng tay cho **đúng con số cũ** —
P2+P3 `137/137, thiếu 0, dôi 0`. P1 `thiếu 0, dôi 11` (11 tham số `ManagementServer` mà model C
portable có còn shell không đăng ký — đã biết, không phải lỗi).

### 17.8 Kết quả kiểm tĩnh P4a

| Kiểm | Kết quả |
|---|---|
| Đường dẫn đầy đủ vs cây shell | `WANCommonInterfaceConfig` 9/9, `WANEthernetInterfaceConfig` 8/8, `WANDSLLinkConfig` 5/5 — **22/22, dôi 0** |
| Phase 4 tổng | 22 đã port, 151 còn lại (P4b–P4e), **dôi 0** |
| Claim chồng nhau | 27 claim của 11 module đang build, **0 cặp** |
| Ngoặc `{} () []` | cân bằng |
| Symbol trong bảng | đều định nghĩa trong chính file |
| Patch `0040` | `git apply --check` và `patch -p1 --dry-run --fuzz=0` sạch; replay lên `HEAD~1` cho tree `f31952b8ba16`, **trùng HEAD thật** |
| Bundle | `78d6b8d8e5b4`, `SHA256SUMS` 376 file, `apply --dry-run` PASS trên cả hai cây SDK thật |

**Chưa compile, chưa link, chưa board.**

## 18. P4b — `WANIPConnection` bằng C, 35 param (Claude Code, 24/09)

Patch `0041`, overlay `418aedf`, file `sdk/mtk/dm098/wanip_mtk.c`. Đây là object WAN mà ACS đụng
nhiều nhất, và là chỗ **mô hình instance dễ sai nhất trong cả bản port**.

### 18.1 Instance là `id + 1`, không phải vị trí section

```sh
# functions/tr098/wan_device
wan_device_get_total_entry()     # $i:$id:$default_gw:$isWanTr069
sub_entry_wandevice_..._ip()     # object_idx=$((wan_id + 1))
```

Entry là các section ẩn danh `config entry` của UCI package `wan`, duyệt theo thứ tự file và
**dừng ở section đầu tiên không có option `id`** (đúng vòng `while :; do ... wan.@entry[$i].id`).
Số instance CWMP lấy từ chính option `id` cộng 1, **không** phải chỉ số `@entry[i]`. Nhờ vậy ACS
đã provision `WANIPConnection.3` vẫn nói chuyện với đúng entry đó sau khi một entry khác bị xoá.
Bản C giữ cả hai con số: `id` để đánh số instance, `idx` để gọi `ubus hni.wan set {"index": idx}`.

### 18.2 Một object, hai loại entry

| Loại | Điều kiện UCI | netdev | Nằm ở |
|---|---|---|---|
| IPoE định tuyến | `switch_mode=0` **và** `conn_type=0` | `network.if<id>` / `if<id>_6` | `WANIPConnection` |
| Bridge | `switch_mode=1` | `network.if_wanbr<id>`, device section `dev_wanbr<id>` | `WANIPConnection` |
| PPPoE | `switch_mode=0` **và** `conn_type=2` | — | `WANPPPConnection` (P4c) |

Entry thiếu hẳn `switch_mode` bị **cả hai** bỏ qua — so sánh chuỗi, không phải so số, nên option
không đặt thì không bằng `"0"`.

### 18.3 Bốn thứ của sản phẩm giữ nguyên

1. **Entry bridge trả hằng số cho nhóm leaf IP**: `SubnetMask`/`DefaultGateway` là `0.0.0.0`,
   `NATEnabled` false, `DNSServers` rỗng, `AddressingType` rỗng. Shell không hỏi interface, và
   template ACS đọc WAN bridge đang chờ đúng các chuỗi đó.
2. **`ExternalIPAddress` forced-inform theo từng instance**, không phải theo leaf: entry routed
   khi mang bit dịch vụ TR-069 (`service_type & 2`), entry bridge khi `id = 0` và
   `clay.opermode.mode = ap` (lúc đó nó báo địa chỉ LAN chứ không phải `0.0.0.0`). Cài bằng
   callback `get_forced_inform` của `struct dm_forced_inform_s`, đúng chỗ shell truyền tham số
   thứ sáu của `common_execute_method_param`.
3. **Ghi `X_AIS_VLAN8021P` lên entry bridge thất bại.** Shell truyền `$iface4` — biến mà hàm
   bridge **không bao giờ đặt** — nên `wan_device_set_vlan_priority()` đi tìm device tên rỗng,
   không thấy, trả internal error. Bản C trả `9002` đúng như vậy. Làm cho nó chạy được nghĩa là
   bắt đầu ghi `ingress/egress_qos_mapping` trên một đường sản phẩm chưa từng chạy.
4. **`MaxMTUSize` ngoài khoảng trả `9005`** ("invalid parameter name"), không phải `9007`. Đó là
   mã lỗi shell trả. Nhìn là biết shell nhầm hằng số, nhưng ACS đã thấy `9005` nhiều năm.

Ngoài ra ba setter `ExternalIPAddress`/`SubnetMask`/`DefaultGateway` vẫn **từ chối bằng `9001`**
khi `v4_mode = 0` (DHCP), và `DNSServers` từ chối khi `v4_static_dns = 0` — giữ nguyên.

### 18.4 Ba khác biệt cố ý, đều là *thêm*, không đổi giá trị nào

| Khác biệt | Vì sao |
|---|---|
| Instance bridge có thêm `Alias`, `X_AIS_DefaultRoute`, `X_AIS_IPMode` | Cây C tĩnh có **một** bảng leaf cho mỗi object, shell thì đăng ký tập leaf khác nhau cho từng loại entry. Getter vẫn đọc đúng option UCI đó, không bịa: `Alias` của bridge là `cpe-other`, đúng thứ `wan_device_get_alias()` trả |
| `Stats.*` là `xsd:unsignedInt` cho mọi instance | Shell truyền type **vào ô setter** (`$4`) ở nhánh routed nên chúng đi ra dưới dạng `xsd:string`, còn nhánh bridge truyền đúng ô nên là `unsignedInt`. Một leaf không thể mang hai kiểu — lấy chính tả vừa đúng TR-098 vừa đang dùng cho một nửa số instance |
| MTU / VLAN ID / VLAN priority không phải số bị từ chối `9007` | Shell viết `[ "$v" -lt 1 ]`, với chuỗi không phải số thì `[` lỗi, `if` rơi xuống nhánh else và **ghi thẳng chuỗi đó vào UCI** |

### 18.5 Hoãn có chủ ý: `X_AIS_ServiceList`

Getter đơn giản (map `service_type` → `INTERNET`/`TR069`/`INTERNET_TR069`/`OTHER`), nhưng **setter
là một máy trạng thái ~200 dòng**: bật/tắt `easycwmp.@acs[0].enablecwmp`, gọi
`wan_device_update_internet_access` (rule firewall), `wan_device_configure_easycwmpd`, và ở chế độ
bridge còn ép `OTHER`. Nó sửa chính cấu hình của client TR-069 đang chạy phiên đó. Không claim,
để `sdk/mtk/compat/` giữ, tách thành bước **P4f** riêng.

### 18.6 Object-level: add/delete viết sẵn nhưng đang ngủ

`AddObject`/`DeleteObject` trên `WANIPConnection.` vẫn do shell xử lý, vì **đường dẫn object không
được claim** — `path_match()` trả 0 cho một claim leaf khi path hết sớm hơn claim. Hai hàm C
(`add_ipconn_instance`, `del_ipconn_instance`) vẫn được viết để lúc claim cả nhánh (bỏ compat) thì
object không hụt chức năng. Ghi lại một điểm lạ để sau khỏi ngạc nhiên: **instance mà sản phẩm trả
về sau `AddObject` là SỐ LƯỢNG entry, không phải `id + 1`** — đó là thứ shell `echo` ra và là thứ
ACS đã nhận.

### 18.7 Hai lỗi của chính tôi, đã sửa trong lượt này

1. **Trùng tên bảng giữa hai file.** `wan_mtk.c` và `wanip_mtk.c` cùng đặt `tWanConnectionDeviceObj`.
   C không báo lỗi (cả hai `static`), nhưng `verify-dm-paths.py` dùng một không gian tên chung nên
   dựng sai cây và **im lặng làm biến mất cả nhánh `WANDSLLinkConfig`**. Đã đổi tên bên `wanip_mtk.c`
   thành `tWanCxDevIp*`, **và** sửa script: bảng `static` chỉ nhìn thấy trong file của nó, chỉ bảng
   không `static` (như `tIcwmpCfgParam`) mới dùng chung.
2. **Hàng bảng trải hai dòng bị bỏ qua.** Parser khớp theo từng dòng vật lý, mà hàng
   `{"WANIPConnection", ... tWanCxDevIpObj, tWanCxDevIpParam, NULL},` dài quá nên xuống dòng →
   script báo cây C có 22 param thay vì 57. Đã cho parser gom dòng tới khi ngoặc cân bằng.

Cả hai đều là lỗi **công cụ đo**, không phải lỗi code — nhưng cái thứ nhất che mất một nhánh thật,
nên đáng ghi. Bằng chứng công cụ vẫn đúng sau khi sửa: chạy lại phase đã chốt cho đúng số cũ
(P2+P3 `137/137`, thiếu 0, dôi 0).

### 18.8 Kết quả kiểm tĩnh P4b

| Kiểm | Kết quả |
|---|---|
| Đường dẫn `WANIPConnection.{i}.` | 34/34 (25 leaf + 9 `Stats`), cộng `WANIPConnectionNumberOfEntries` = **35** |
| Phase 4 tổng | 57 đã port / 116 còn lại, **dôi 0** |
| Thiếu ngoài nhánh đã hoãn | **không có** — mọi path còn thiếu đều thuộc `PortMapping`, `X_AIS_IPv6`, `WANPPPConnection` hoặc `X_AIS_ServiceList` |
| Claim chồng nhau | 54 claim của 12 module, **0 cặp** |
| Ngoặc `{} () []` | cân bằng |
| Symbol trong bảng | đều định nghĩa trong chính file |
| Patch `0041` | replay lên `HEAD~1` cho tree `2640fe21e4d9`, **trùng HEAD thật** |
| Bundle | `404cd03f4d01`, `SHA256SUMS` 378 file, `apply --dry-run` PASS trên cả hai cây SDK thật |

**Chưa compile, chưa link, chưa board.** Đây là bản người dùng sẽ mang đi build thử.

## 19. Lần build đầu: apply chết vì API Python 3.9 (Claude Code, 24/09)

Không phải lỗi biên dịch — apply dừng **trước khi ghi bất cứ file nào** trên máy build:

```
File "apply.py", line 246, in main
    if HERE == target or HERE.is_relative_to(target) or target.is_relative_to(HERE):
AttributeError: 'PosixPath' object has no attribute 'is_relative_to'
```

`Path.is_relative_to()` là API của **Python 3.9**. Máy workspace này chạy 3.10 nên toàn bộ test
nội bộ (`release/tests/verify-apply.py`, 6 case PASS) không bao giờ chạm phải. Máy build SDK cũ
hơn. Patch `0042`.

### 19.1 Vì sao test không bắt được

Test chạy cùng interpreter với code. Mọi API chỉ có ở bản mới sẽ **im lặng đi qua** ở đây và chỉ
nổ ở nơi giao hàng. Đây là loại lỗi mà thêm test case không giải quyết được — phải kiểm **mặt
bằng phiên bản**, không phải hành vi.

### 19.2 Sửa

`within()` thay cho `is_relative_to()`:

```python
def within(path, other):
    try:
        path.relative_to(other)
        return True
    except ValueError:
        return False
```

Cộng một kiểm phiên bản ở đầu file để interpreter quá cũ báo một câu đọc được thay vì traceback
giữa chừng. Quét AST cả `apply.py` xác nhận đó là **dòng duy nhất** dùng API mới hơn 3.6.
`export.py` chỉ chạy trong workspace này nên không ràng buộc.

### 19.3 Hai lớp chặn mới, đặt trong chính bộ test

| Lớp | Bắt được gì |
|---|---|
| Quét AST `apply.py` | mọi tên chỉ có từ 3.7+: `is_relative_to`, `with_stem`, `readlink`, `removeprefix`, `removesuffix`, `root_dir`, `link_to`, `missing_ok`, `dirs_exist_ok`, `capture_output`, walrus `:=`, `match` |
| Chạy dry-run cả hai fixture dưới interpreter **đã gỡ** method pathlib của 3.9 | chỗ nào thực sự gọi tới chúng lúc chạy, kể cả đường nhánh mà quét tĩnh bỏ sót |

Kiểm ngược: đặt lại dòng cũ thì gate báo
`apply.py uses APIs newer than Python 3.6: [(262, 'is_relative_to', '3.9')]`.

### 19.4 Trạng thái sau khi sửa

| Kiểm | Kết quả |
|---|---|
| `verify-apply.py` | 6/6 PASS, gồm cả hai lớp chặn mới |
| Bundle `2e351f8813d7` | `SHA256SUMS` 379 file OK |
| `apply --dry-run` hai cây SDK thật | PASS |
| Cũng vậy dưới shim gỡ method 3.9 | PASS |
| Vendor tree sau mọi dry-run | không đổi (1 và 9 file có sẵn) |
| SDK build | **chưa tới bước compile** |

Data model không bị đụng: `0040` và `0041` giữ nguyên.

## 20. Lỗi compile đầu tiên: một dấu `*/` trong comment (Claude Code, 24/09)

```
../sdk/mtk/dm098/wlan_mtk.c:181:39: error: unknown type name 'backhaul_sync_'
  181 |  * when mesh is running -- mlo_sync_*/backhaul_sync_* of the shell. */
```

`mlo_sync_*/` — **`*/` đóng block comment ngay tại đó**. Phần còn lại của dòng
(`backhaul_sync_* of the shell. */`) bị trình biên dịch đọc như code, và định nghĩa
`static void wlan_sync_option(...)` ngay dưới bị nuốt theo, nên dòng 234 báo tiếp
`implicit declaration of function 'wlan_sync_option'`. Patch `0043`, sửa chữ trong comment,
**không đổi một dòng code nào**.

### 20.1 Đọc được gì từ log này

Automake biên dịch theo thứ tự `sdk.mk`, nên dừng ở `wlan_mtk.c` nghĩa là **10 file trước nó
compile sạch**: `dmplatform_mtk`, `dmmtk`, `root`, `deviceinfo`, `time`, `managementserver` (2),
`lan`, `lanhosts`, `laneth`, `x_ais_mesh`. Đó là toàn bộ P1 và P2. Ba file sau `wlan_mtk.c`
(`wlansec`, `wan`, `wanip`) **chưa được compiler xác nhận** — lần build tới mới biết.

### 20.2 Cảnh báo `const` đi kèm, đã dọn luôn

```
wlan_mtk.c:530:29: warning: passing argument 2 of 'mtk_uci' discards 'const' qualifier
```

`radio_of()` trả `const char *`, `mtk_uci()` nhận `char *`. Không phải lỗi (không có `-Werror`),
nhưng sẽ lặp lại ở mọi module sau. Sửa tại gốc: `mtk_uci`, `mtk_varstate`, `mtk_varstate_set`,
`mtk_uci_default` nhận `const char *` và tự ép kiểu khi gọi `dmuci_*`. **Không đụng `dmuci.h`** —
đó là API dùng chung với BDK.

Trong `wanip_mtk.c` bỏ `const` ở con trỏ `struct wan_entry *` lấy từ `data`: `e->if4`/`e->dev`
đi thẳng vào `dmuci_set_value()` nên giữ `const` chỉ sinh cảnh báo và ép phải rải `(char *)`
khắp nơi.

### 20.3 Vì sao kiểm tĩnh cũ không thấy

Bộ kiểm trước đó **bỏ comment trước rồi mới đếm ngoặc** — tức là nó dùng chính quy tắc sai mà
trình biên dịch dùng đúng, nên với nó file vẫn cân bằng. Không có compiler trên máy này thì lỗi
loại "comment nuốt code" là điểm mù hoàn toàn.

Đã viết [`check-c-sanity.py`](check-c-sanity.py), chạy máy trạng thái ký tự đúng như trình biên
dịch và bắt bốn thứ:

| Bắt | Cách |
|---|---|
| `*/` đóng comment sớm | đang trong block comment mà gặp `*/` có ký tự ngay trước là chữ/số/`_`/`)`/`]` → gần như chắc là wildcard kiểu `foo_*/bar`, không phải dấu đóng cố ý |
| comment hoặc chuỗi không đóng tới hết file | trạng thái máy khi hết input |
| ngoặc `{} () []` lệch | đếm sau khi bỏ comment/chuỗi **đúng luật** |
| gọi hàm không định nghĩa, không khai báo ở header nào của cây | trừ libc/libubox/json-c, macro, và tham số con trỏ hàm |

Chạy trên 15 file MTK: **0 vấn đề**. Chạy ngược trên bản trước khi sửa thì nó chỉ đúng dòng 181.

Bản thân bộ kiểm cũng có một lỗi phải sửa khi viết: trạng thái comment `//` không có nhánh nào
tăng con trỏ → vòng lặp vô hạn trên mọi file có `//`. Đã thêm nhánh và một `i += 1` cuối thân
vòng để không bao giờ rơi ra mà quên tăng.

### 20.4 Trạng thái

| Kiểm | Kết quả |
|---|---|
| `check-c-sanity.py` | 15/15 file sạch |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| claim | 54 claim, 12 module, 0 cặp chồng |
| Bundle | `249ae7447807`, 380 file, `apply --dry-run` PASS hai cây SDK |
| SDK build | **PARTIAL** — 10 file đầu sạch, `wlansec`/`wan`/`wanip` chưa được xác nhận |

## 21. Lỗi compile thứ hai: `static get_empty` trùng khai báo của engine (Claude Code, 24/09)

```
../sdk/mtk/dm098/wlansec_mtk.c:400:12: error: static declaration of 'get_empty'
                                      follows non-static declaration
../dmtr098.h:485:5: note: previous declaration of 'get_empty' was here
```

`dmtr098.h` khai báo `int get_empty(...)` và `dmtr098.c:667` định nghĩa nó, đúng thân hàm mà tôi
viết lại (`*value = ""`). Bỏ bản `static`, dùng của engine. Patch `0044`.

**Đây là lần thứ hai.** `deviceinfo_mtk.c` đã dính đúng lỗi này và đã ghi vào mục 11 —
nhưng không có gì kiểm, nên nó quay lại ở file khác. Bài học không nằm ở chỗ "nhớ kỹ hơn".

### 21.1 Lần này build đi xa hơn

Dừng ở `wlansec_mtk.c` nghĩa là **12/15 file MTK đã compile sạch**, gồm cả `wlan_mtk.c` và
`wlanassoc_mtk.c` (P3a). Còn `wan_mtk.c` và `wanip_mtk.c` — P4a và P4b — chưa compiler nào nhìn qua.

### 21.2 Kiểm mới: `static` đụng khai báo non-static trong header ĐƯỢC include

Quét thô "tên `static` nào trùng tên hàm non-static trong bất kỳ header nào của cây" cho **10 kết
quả**, trong đó 9 là nhiễu: `tr098/landevice.h` và `sdk/bdk/dm098/landevice_bdk.h` khai báo
`get_wlan_enable`, `browseHostInst`, `browseWepKeyInst`… nhưng **không file MTK nào include chúng**,
nên không có xung đột. `wlan_mtk.c` compile sạch với 5 "va chạm" loại này là bằng chứng.

Cái lọc đúng là **bao đóng include**: chỉ tính header mà file thực sự kéo vào, kể cả gián tiếp
(`dmmtk.h` → `dmtr098.h`). Lọc theo bao đóng cho **đúng 1 kết quả**, khớp compiler.

Thêm luôn lớp thứ sáu: **hai file đang build cùng định nghĩa một hàm không `static`** — trùng
symbol lúc link, thứ chỉ nổ ở bước cuối khi mọi `.o` đã xong.

### 21.3 Bộ kiểm lại có lỗi của chính nó, lần thứ hai

`include_closure()` dùng `strip()` để bỏ comment trước khi tìm `#include`. Nhưng `strip()` thay
nội dung **mọi chuỗi** bằng rỗng, nên `#include "dmtr098.h"` thành `#include ""` — bao đóng luôn
rỗng và kiểm mới im lặng trả OK. Chỉ phát hiện vì tôi chạy kiểm ngược trên bản chưa sửa và nó
**không** báo lỗi.

Sửa: chỗ tìm `#include` chỉ bỏ comment, không đụng chuỗi.

> Luật rút ra cho mọi kiểm tĩnh viết sau: **mỗi lớp kiểm phải có một lần chạy ngược trên bản lỗi
> thật**. Kiểm "trả OK" không chứng minh gì cả nếu chưa thấy nó biết kêu.

### 21.4 `check-c-sanity.py` hiện bắt sáu lớp

| # | Lớp | Đã bắt được lỗi thật |
|---|---|---|
| 1 | `*/` đóng block comment sớm | `wlan_mtk.c:181` (0043) |
| 2 | comment / chuỗi không đóng tới hết file | — |
| 3 | ngoặc `{} () []` lệch | — |
| 4 | gọi hàm không định nghĩa, không khai báo | — |
| 5 | `static` trùng khai báo non-static trong header được include | `wlansec_mtk.c:400` (0044) |
| 6 | hai file cùng định nghĩa một hàm không `static` | — |

Chạy trước mỗi lần giao:

```sh
./check-c-sanity.py && ./verify-dm-paths.py --phase 4 && ./verify-dm-paths.py --claims
```

### 21.5 Trạng thái

| Kiểm | Kết quả |
|---|---|
| `check-c-sanity.py` | 15/15 file sạch; chạy ngược bản chưa sửa → báo đúng `get_empty` |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| claim | 54 claim, 12 module, 0 cặp chồng |
| Bundle | `8abe6a04018b`, 381 file, `apply --dry-run` PASS hai cây SDK |
| SDK build | **PARTIAL** — 12/15 sạch, `wan_mtk.c` và `wanip_mtk.c` chưa xác nhận |

## 22. `libtr098` build xong; gói app dừng ở automake (Claude Code, 24/09)

**Mốc đáng kể: thư viện data model đã build và link sạch trên SDK thật.** Cả 15 file MTK qua
compiler, gồm `wan_mtk.c` (P4a) và `wanip_mtk.c` (P4b) — 259 tham số C không còn lỗi cú pháp hay
symbol nào. Build chuyển sang gói app `icwmp_tr098` và dừng ở **automake**, chưa tới compiler.

```
sdk/bdk/sdk.mk:4: error: cannot apply '+=' because 'icwmp_tr098d_SOURCES' is not
    defined in the following conditions: ICWMP_SDK_BDK and !ICWMP_TR098
```

### 22.1 Nguyên nhân

`bin/Makefile.am` của app định nghĩa `icwmp_tr098d_SOURCES/CFLAGS/LDFLAGS/LDADD` **bên trong**
`if ICWMP_TR098`. Mỗi `sdk/<name>/sdk.mk` lại `+=` vào chúng nhưng chỉ tự bảo vệ bằng
`if ICWMP_SDK_<X>`.

automake **kiểm tĩnh trên mọi tổ hợp điều kiện**, không phải trên tổ hợp mà `configure` thực sự
chọn. Tổ hợp `ICWMP_SDK_MTK && !ICWMP_TR098` có một phép `+=` mà không có `=` nào đứng trước →
lỗi. Trên thực tế feed luôn truyền `--enable-icwmp_tr098`, nên tổ hợp đó không bao giờ xảy ra —
nhưng automake không quan tâm.

Sửa (patch `0045`): lồng `if ICWMP_SDK_<X>` vào trong `if ICWMP_TR098` ở cả ba fragment. **Không
đổi thứ gì được build**: ở nhánh `!ICWMP_TR098` app dựng `icwmpd` với bbfdm và lớp glue SDK vốn
chưa từng đóng góp gì cho nó.

### 22.2 Vì sao `libtr098` không dính

`bin/Makefile.am` của thư viện định nghĩa `libtr098_la_SOURCES =` **không điều kiện** ngay đầu
file, nên `+=` trong `sdk/mtk/sdk.mk` hợp lệ ở mọi tổ hợp. Hai cây cùng một kiểu bố cục nhưng
khác nhau đúng chỗ đó — và đó là lý do lỗi chỉ lộ ra ở gói thứ hai.

### 22.3 Kiểm mới: `check-automake-conds.py`

Máy này không có automake, nên phải mô phỏng đúng cái luật đó:

> một `VAR +=` chỉ hợp lệ nếu tập điều kiện bao quanh nó **bao hàm** tập điều kiện mà `VAR =`
> được định nghĩa.

Script đọc `bin/Makefile.am` lấy `VAR =` cùng ngăn xếp `if/else/endif` bao quanh, rồi đọc từng
`sdk/*/sdk.mk` lấy `VAR +=` cùng ngăn xếp của nó, và báo phần điều kiện còn thiếu. Chạy cho cả
hai cây (app và lib).

**Chạy ngược trên bản chưa sửa** (bắt buộc, theo luật ở mục 21.3): báo đúng
`sdk.mk:4, :8, :9  icwmp_tr098d_SOURCES/CFLAGS += thiếu điều kiện ICWMP_TR098` — trùng dòng
automake chỉ ra. Chạy trên bản đã sửa: **0 vấn đề** ở cả hai cây.

### 22.4 Trạng thái

| Kiểm | Kết quả |
|---|---|
| Gói `libtr098` trên SDK thật | **BUILD XONG** — 15/15 file compile + link |
| Gói `icwmp_tr098` | dừng ở automake (đã sửa `0045`), chưa tới compile app |
| `check-c-sanity.py` | 15/15 sạch |
| `check-automake-conds.py` | 2 cây OK; chạy ngược bản lỗi báo đúng 3 dòng |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| claim | 54 claim, 12 module, 0 cặp chồng |
| Bundle | `4ea92258d6b1`, 382 file, `apply --dry-run` PASS hai cây SDK |

---

## 23. `dm_add_end_session` — cùng một lớp lỗi, lần thứ ba, lần đầu ở cây app

Gói app qua được automake và tới compiler, dừng ở file đầu tiên dùng data model:

```
../icwmp_dm.c:139:13: error: conflicting types for 'dm_add_end_session'
  139 | static void dm_add_end_session(unsigned int flags)
.../usr/include/icwmp_dm/dmtr098.h:532:5: note: previous declaration was here
  532 | int dm_add_end_session(struct dmctx *ctx, void(*function)(...), int action, void *data);
```

### 23.1 Hai hàm khác hẳn nhau, trùng tên

| | Của ai | Làm gì |
|---|---|---|
| `dm_add_end_session(unsigned int flags)` | `icwmp_dm.c:139`, `static`, của app | Xếp **tên** các cờ end-session mà setter để lại vào mảng blobmsg của reply ubus |
| `dm_add_end_session(struct dmctx *, void(*)(...), int, void *)` | `dmtr098.c:3116`, của engine | **Xếp hàng** một hành động end-session để chạy cuối phiên |

`inc/cwmp.h:24-25` include `<icwmp_dm/dmentry.h>` và `<icwmp_dm/dmtr098.h>`, nên khai báo
non-static của engine có mặt ở **mọi** file của app. `static` đi sau khai báo non-static là lỗi,
không phải cảnh báo.

Sửa (patch `0046`): đổi tên thành `dm_add_end_session_list()`, theo đúng dáng của hai helper nằm
ngay cạnh là `dm_add_fault_list()` và `dm_add_param_list()`. App chưa bao giờ gọi hàm của engine,
nên hành vi không đổi.

### 23.2 Vì sao kiểm tĩnh không bắt được từ trước

`check-c-sanity.py` đã có lớp 5 cho đúng lỗi này từ patch `0044` — nhưng nó **chỉ chạy trên cây
`libtr098`**. Cây app có hai khác biệt mà script chưa biết:

1. App include header của thư viện bằng `<icwmp_dm/dmtr098.h>` (ngoặc nhọn, có tiền tố). Trên máy
   build đó là bản đã install vào `staging_dir`; `include_closure()` chỉ đi theo `#include "..."`
   nên không thấy gì.
2. `bin/Makefile.am` của app dựng **bốn** binary. `icwmp_xmppd`, `icwmp_twampd`,
   `icwmp_udpechoserverd` mỗi cái có bản `dmuci_set_value`/`dmuci_walk_section` riêng và không
   link với `libtr098`. Gộp chung nguồn của cả bốn thì lớp 6 báo trùng symbol nhầm — thử nghiệm
   đầu tiên ra đúng hai ca đó.

`check-c-sanity.py` nay nhận `--tree app|lib` và `--sdk mtk|bdk|uci`:

- `ANGLE_PREFIX` map `icwmp_dm/` về gốc cây `libtr098`, nên `<icwmp_dm/dmtr098.h>` tra được.
- `collect_sources()` bám theo từng khối `<var>_SOURCES = ... \` nên lọc được đúng binary
  (`icwmp_tr098d`).

### 23.3 Ba lớp phải chỉnh lại vì cây app là code upstream

Chạy lần đầu trên cây app ra **5 vấn đề, cả 5 đều sai**. Mỗi cái lộ một khiếm khuyết thật của
script, sửa tại gốc chứ không thêm ngoại lệ:

| Lớp | Báo sai ở | Vì sao | Sửa |
|---|---|---|---|
| 1 — comment đóng sớm | `/* Only One instance should run*/` | chỉ nhìn ký tự **trước** `*/` | phải dính chữ ở **cả hai** phía, đúng dáng `foo_*/bar_*` |
| 3 — lệch ngoặc | `config.c` 230 mở / 229 đóng | `#ifdef ICWMP_BDK` và `#else` mỗi nhánh mở một `while (...) {`, đóng một lần ở ngoài | `one_branch()` bỏ nhánh `#elif/#else` trước khi đếm, như compiler thấy |
| 4 — gọi hàm không khai báo | 140 dòng trên file upstream | thành viên struct (`ctx->get_permission()`), con trỏ hàm tham số (`cb`), tên nối token (`dmuci_delete_by_section_unnamed_##UCI_PATH`), thiếu tên libubox/uci | bỏ qua truy cập thành viên, bỏ qua tên còn xuất hiện **không kèm `(`**, nhận tiền tố `##`, bổ sung `EXTERNAL` |

Sau khi sửa: **lib 34 file 0 vấn đề, app 17 file 0 vấn đề** ở cả ba SDK.

### 23.4 Chạy ngược — bắt buộc, và lần này bắt được lỗi của chính bài test

Bốn bản hỏng, tái tạo đúng bốn lỗi đã từng ra tới máy build:

| Test | Bản hỏng | Script báo |
|---|---|---|
| NEG 1 | `icwmp_dm.c` với `dm_add_end_session` cũ | `:137 static dm_add_end_session trùng khai báo non-static ở libtr098/dmtr098.h` |
| NEG 2 | `wlan_mtk.c` với comment `mlo_sync_*/backhaul_sync_*` | `:181 comment đóng SỚM ở ` `_*/b` |
| NEG 3 | `wlansec_mtk.c` thêm lại `static get_empty` | `:589 static get_empty trùng khai báo non-static` |
| NEG 4 | `wlan_mtk.c` xoá định nghĩa `wlan_sync_option` | 3 dòng `gọi wlan_sync_option() mà không thấy định nghĩa` |

NEG 3 lần chạy đầu ra `OK` — tưởng là script hỏng, hoá ra **file test rỗng**: `awk 'NR==FNR{next}'`
với `/dev/null` làm file thứ nhất thì `NR==FNR` đúng cho mọi dòng của file thứ hai, nên `next` bỏ
sạch. File chỉ còn khối `get_empty` vừa nối thêm, không còn `#include "dmtr098.h"` → include
closure rỗng → không có gì để trùng.

Bài học cộng thêm vào luật ở mục 21.3: **bản hỏng dùng để chạy ngược cũng phải được kiểm là đúng
bản hỏng** — ở đây chỉ cần `wc -l` và `grep -c '#include "dmtr098.h"'`. Một test âm chạy trên file
rỗng luôn luôn "đạt" theo hướng sai.

### 23.5 Trạng thái

| Kiểm | Kết quả |
|---|---|
| Gói `libtr098` trên SDK thật | **BUILD XONG** — 15/15 file compile + link |
| Gói `icwmp_tr098` | qua automake, qua ~20 file app, dừng ở `icwmp_dm.c` (đã sửa `0046`) |
| `check-c-sanity.py --tree lib` | 34 file, 0 vấn đề |
| `check-c-sanity.py --tree app --sdk mtk\|bdk\|uci` | 17 file, 0 vấn đề, cả ba |
| Chạy ngược | 4/4 bản hỏng bị bắt đúng dòng |
| `check-automake-conds.py` | 2 cây OK |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| Bundle | `968140592ac7`, 384 file, `apply --dry-run` PASS hai cây SDK |

---

## 24. `CWMP_LOG` mang sẵn dấu `;` — lỗi mà đếm ngoặc không bao giờ thấy

```
../sdk/mtk/icwmp_mtk.c:179:4: error: expected '}' before 'else'
../sdk/mtk/icwmp_mtk.c:183:2: error: expected identifier or '(' before 'if'
../sdk/mtk/icwmp_mtk.c:185:2: error: conflicting types for 'uci_free_context'
../sdk/mtk/icwmp_mtk.c:186:2: error: expected identifier or '(' before 'return'
```

**Một lỗi, không phải năm.** Từ dòng 183 trở đi là parser đã rơi ra file scope sau khi hỏng ở
179 — `uci_free_context(c);` khi đó bị đọc như một khai báo hàm ở top level, nên mới đụng khai
báo thật trong `uci.h:84`.

### 24.1 Nguyên nhân

`inc/log.h:45` định nghĩa logger **kèm luôn dấu chấm phẩy**:

```c
#  define CWMP_LOG(SEV,MESSAGE,args...) puts_log(SEV,MESSAGE,##args);
```

Nên đoạn này:

```c
if (is_secret(m->cwmp))
        CWMP_LOG(INFO, "... (masked)", m->cwmp);
else
        CWMP_LOG(INFO, "...=%s", m->cwmp, v);
```

khai triển thành `if (x) puts_log(...); ; else ...` — câu lệnh `if` kết thúc ở dấu `;` đầu,
dấu `;` thứ hai là một câu lệnh rỗng, và `else` không còn `if` nào để gắn vào.

Upstream icwmp chưa bao giờ viết `CWMP_LOG` làm thân của một `if` **có `else`**, nên cái bẫy này
chưa từng lộ. Quét cả hai cây: chỉ 5 chỗ dùng `CWMP_LOG` làm thân không ngoặc, và chỉ đúng cặp
`if/else` này là lỗi biên dịch — ba chỗ còn lại (`external.c:284`, `xml.c:2623`,
`icwmp_mtk.c:235`) là `if` đơn, khai triển ra thêm một câu lệnh rỗng, vô hại.

Sửa (patch `0047`): bọc cả hai nhánh bằng `{}`. Bọc luôn `icwmp_platform_init()` — chỗ đó biên
dịch được ở cả hai cấu hình `WITH_CWMP_DEBUG`, nhưng là **cùng một construct**, và câu lệnh nào
thêm vào dưới nó sau này sẽ hỏng âm thầm.

### 24.2 Vì sao lớp 3 (đếm ngoặc) mù hoàn toàn

Lớp 3 đếm `{}` trên văn bản nguồn — **trước khi preprocessor chạy**. Ở đây văn bản nguồn cân
bằng tuyệt đối: lỗi chỉ xuất hiện sau khi `CWMP_LOG` nở ra thêm một dấu `;`. Cùng một hạng lỗi
với mục 20 (comment nuốt code) ở chỗ: **bộ kiểm đang nhìn một văn bản khác với văn bản mà
compiler nhìn.** Mỗi lần khoảng cách đó chưa được mô hình hoá là một lần lỗi lọt ra máy build.

### 24.3 Lớp 7

`check-c-sanity.py` thêm lớp: quét header lấy mọi macro **có tham số mà thân kết thúc bằng `;`**
(`CWMP_LOG`, `DD`, `DMFREE`), rồi báo khi một macro như vậy làm thân **không ngoặc** của `if` mà
ngay sau là `else`.

Không báo trường hợp `if` đơn — nó biên dịch được ở cả hai cấu hình, và báo nó sẽ gây nhiễu trên
code upstream. Lớp này bám đúng một thứ: dự đoán compiler sẽ hỏng ở đâu.

**Chạy ngược (NEG 5)**: bỏ ngoặc lại trong `icwmp_mtk.c` → báo
`:178 CWMP_LOG(...) không ngoặc làm thân if, ngay sau là else`. gcc chỉ dòng 179 (`else`), script
chỉ dòng 178 (thân) — cùng một chỗ.

Bốn bản hỏng cũ (NEG 1–4) chạy lại sau khi thêm lớp 7: vẫn bắt đúng dòng, không hồi quy. Cả năm
bản hỏng đều được kiểm là **đúng bản hỏng** trước khi chạy (số dòng + `grep -c` cái đặc trưng),
theo luật rút ra ở mục 23.4.

### 24.4 Trạng thái

| Kiểm | Kết quả |
|---|---|
| Gói `libtr098` | **BUILD XONG** — 15/15 file compile + link |
| Gói `icwmp_tr098` | qua automake, qua `icwmp_dm.c`, dừng ở `sdk/mtk/icwmp_mtk.c` (đã sửa `0047`) |
| `check-c-sanity.py` (7 lớp) | lib 34 file 0 vấn đề · app 17 file 0 vấn đề × 3 SDK |
| Chạy ngược | 5/5 bản hỏng bị bắt đúng dòng |
| `check-automake-conds.py` | 2 cây OK |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| Bundle | `e2a7f19e05e3`, 385 file, `apply --dry-run` PASS hai cây SDK |

---

## 25. `libz.so.1` — compile và link đã xong, chặn ở bước đóng gói

```
Package icwmp_tr098 is missing dependencies for the following libraries:
libz.so.1
```

**Mốc: toàn bộ gói app đã compile và link sạch.** Binary đã được dựng, đã `install` vào cây
ipkg; thứ duy nhất còn thiếu là **metadata của gói**, không phải code.

### 25.1 Nguyên nhân

`bin/Makefile.am:131` đưa `$(LIBZ_LIBS)` (= `-lz`, `configure.ac:86`) vào `icwmp_tr098d_LDADD`,
cho `zlib.c` (nén SOAP). `zlib.c` nằm trong `icwmp_tr098d_SOURCES` không điều kiện.

Gói `cwmpclient` cũ mà bản này thay **chưa bao giờ link zlib** —
`DEPENDS:=+libubus +libuci +libubox +libmicroxml +libjson-c +libcurl +curl`. Nên đây là
**dependency mới do iCWMP mang vào**, không phải cái tôi làm rơi mất khi port.

Sửa (patch `0048`): thêm `+zlib` vào DEPENDS của `feeds/icwmp_tr098/Makefile`. Tên gói trong SDK
này là `zlib` (`package/libs/zlib/Makefile`), đúng quy ước feed đang dùng — `stunnel` cũng viết
`+zlib`.

### 25.2 Hai mục nữa, và lý do xử lý khác nhau

| `-l` | Gói | Có trong ELF? | Quyết định |
|---|---|---|---|
| `-lubox` | `libubox` | **có** — binary NEED `libubox.so` | **thêm `+libubox`**. Nó vẫn được cài nhờ `+libblobmsg-json` (cùng source package) nên `ipkg-build` không kêu, nhưng phụ thuộc gián tiếp là may mắn, không phải thiết kế. `libtr098` đã khai báo tường minh. |
| `-lcrypto`, `-lssl` | `libopenssl` | **không** | **không thêm**. Có trên dòng link nhưng không file nào ở đây gọi OpenSSL (curl gọi), nên `--as-needed` bỏ. Thêm `+libopenssl` là kéo openssl vào image vô cớ. |

`ipkg-build` chỉ báo `libz.so.1` — nó đọc **NEEDED thật của ELF**, nên đó là bằng chứng rằng
`libubox` đã được thoả gián tiếp còn openssl thì không cần.

### 25.3 Kiểm mới: `check-pkg-deps.py`

Lớp lỗi này khác hẳn 20–24: không phải cú pháp C, mà là **metadata gói lệch với dòng link**. Mất
trọn một vòng build mới lộ, vì nó nằm sau compile và link.

Script so `<target>_LDADD` với `DEPENDS`, và **tra tên gói từ chính cây SDK** thay vì bảng đoán
sẵn: tìm Makefile nào install `lib<x>.so`, ưu tiên khối `define Package/<tên>/install` chứa nó.

Một khiếm khuyết lộ ngay khi chạy: `-lpthread` bị map thành `toolchain` (rơi về `PKG_NAME`) vì
`package/libs/toolchain/Makefile:559` viết `define` **có thụt lề**, regex neo `^define` không
khớp. Sửa regex → `libpthread`, và mục này biến mất khỏi danh sách vì DEPENDS đã có nó.

Script **không thay `ipkg-build`** và nói rõ điều đó: `--as-needed` khiến một `-l` vắng trong
DEPENDS chưa chắc đã hỏng. Vì vậy mặc định là **cảnh báo, thoát 0**; `--strict` mới thoát 1.

**Chạy ngược (NEG 6)**: đọc DEPENDS từ bản `HEAD` chưa sửa → báo đúng
`-lz -> cần +zlib (package/libs/zlib/Makefile)`, thoát 1 với `--strict`. Bản hỏng được kiểm là
đúng bản hỏng trước khi chạy (`grep -c 'DEPENDS.*zlib'` = 0).

### 25.4 Trạng thái

| Kiểm | Kết quả |
|---|---|
| Gói `libtr098` | **BUILD XONG** |
| Gói `icwmp_tr098` | **compile + link XONG**, chặn ở `ipkg-build` (đã sửa `0048`) |
| `check-c-sanity.py` (7 lớp) | lib 34 file 0 vấn đề · app 17 file 0 vấn đề × 3 SDK |
| `check-pkg-deps.py` | còn `-lcrypto`/`-lssl` (cố ý), chạy ngược báo đúng `+zlib` |
| `check-automake-conds.py` | 2 cây OK |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| Bundle | `e9ac9a691e1f`, 386 file, `apply --dry-run` PASS hai cây SDK |

---

## 26. Cùng một lỗi lần thứ hai, với cây nguồn đã đúng — `src-cpy`

Người dùng apply bản `e9ac9a691e1f` (đã có `+zlib`) rồi build lại, và nhận **đúng** thông báo cũ:

```
Package icwmp_tr098 is missing dependencies for the following libraries:
libz.so.1
make[2]: Leaving directory '.../feeds/airoha/package/airoha/apps/icwmp_tr098'
```

Patch `0048` không sai. Nó chưa bao giờ tới được build.

### 26.1 Hai đường, chỉ một được cập nhật

`feeds.conf.default` khai báo feed airoha bằng **`src-cpy`** — feed được **copy**, không symlink:

| Thứ | Đường | Ai ghi | Build đọc ở đâu |
|---|---|---|---|
| Nguồn C | `tclinux_phoenix/apps/hni/icwmp_tr098/` | `apply` | `Build/Prepare` → `$(CP) $(PKG_SOURCE)/. $(PKG_BUILD_DIR)`, tức **đọc thẳng** |
| Makefile của gói | `airoha_feeds/package/airoha/apps/icwmp_tr098/Makefile` | `apply` | **không** — build đọc bản copy ở `feeds/airoha/package/airoha/apps/icwmp_tr098/Makefile` |

Vì `PKG_SOURCE` là **đường tuyệt đối ngoài feed** (`apply.py:145` ghi Makefile, `:143` copy
nguồn), mọi patch sửa `.c` đều vào ngay — đó là lý do `0043`–`0047` đều có hiệu lực và không ai
nghi ngờ gì. Chỉ `0048` sửa **Makefile của feed**, và đúng thứ đó thì bị bản copy che mất.

Dòng `Leaving directory '.../feeds/airoha/...'` trong log đã nói rõ build đang ở cây copy.

### 26.2 Vì sao lỗi này là của tôi, không phải của người dùng

`build-commands.md` mục **1.3** đã ghi đúng từ trước, kể cả câu:

> `./apply` sửa đúng hai file Makefile này, nên **lần chạy đầu sau khi apply phải làm bước trên**.

Nhưng hai lượt trả lời gần nhất tôi đưa chuỗi lệnh rút gọn `./apply … && make
package/icwmp_tr098/{clean,compile}` và **bỏ mất bước đó**. Người dùng làm theo tin nhắn, không
theo tài liệu — đúng như mọi người vẫn làm.

`apply` cũng im lặng về nó. Lệnh build đầy đủ mà `apply` in ra thì **không** dính, vì
`airoha_script/airoha-compile.sh:113` gọi `airoha-feeds-prepare.sh`, và file đó chạy
`./scripts/feeds update airoha` (`:33-35`). Chỉ vòng lặp nhanh từng gói là thiếu.

### 26.3 Sửa

Patch `0049`: `apply` in thêm, ngay dưới danh sách lệnh build, chỉ cho SDK mtk:

```
Building one package instead of the whole image?  Refresh the
feed first -- apply rewrote two feed Makefiles and the airoha feed
is src-cpy, a copy:
cd openwrt-21.02/openwrt-21.02.1_dev
./scripts/feeds update airoha
./scripts/feeds install -p airoha -f libtr098 icwmp_tr098
make package/icwmp_tr098/{clean,compile} V=sc -j1
```

Hướng dẫn nằm **cạnh đúng những lệnh người ta copy**, chứ không chỉ trong một mục của tài liệu.
`release/tests/verify-apply.py`: 6/6 PASS, gồm cả quét nguồn Python 3.6 và chạy apply dưới shim
3.6.

Không cần sửa nguồn gì thêm: `0048` đã đúng và đã nằm trên cây của người dùng.

### 26.4 Bài học

Ba lượt trước tôi dựng ba bộ kiểm tĩnh cho ba lớp lỗi. Lỗi này **không lớp nào bắt được**, vì cây
nguồn hoàn toàn đúng — sai ở **quy trình giao hàng**, giữa "đã ghi vào đĩa" và "build thật sự
đọc".

Quy tắc rút ra: **lệnh build in cho người dùng phải là lệnh chạy được từ trạng thái sau `apply`,
không phải lệnh rút gọn cho ngắn.** Mỗi lần tôi tóm tắt lại một quy trình đã được viết đủ là một
lần có thể đánh rơi một bước, và người đọc không có cách nào biết.

### 26.5 Trạng thái

| Kiểm | Kết quả |
|---|---|
| Gói `libtr098` | **BUILD XONG** |
| Gói `icwmp_tr098` | **compile + link XONG**, chờ `feeds update` để `+zlib` có hiệu lực |
| `verify-apply.py` | 6/6 PASS |
| `check-c-sanity.py` (7 lớp) | lib 34 file 0 vấn đề · app 17 file 0 vấn đề × 3 SDK |
| `check-pkg-deps.py` | chỉ còn `-lcrypto`/`-lssl` (cố ý) |
| `check-automake-conds.py` | 2 cây OK |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| Bundle | `5d7cf30b602b`, 387 file, `apply --dry-run` PASS hai cây SDK |

---

## 27. `cp -fpR /.` — `PKG_SOURCE` rỗng, build dir phình 17 GB

```
Build/Prepare
mkdir -p .../build_dir/.../icwmp_tr098
cp -fpR /. .../build_dir/.../icwmp_tr098
cp: cannot copy a directory, '/.', into itself
```

`$(PKG_SOURCE)/.` ra `/.` nghĩa là **`PKG_SOURCE` rỗng**. `$(CP)` của OpenWrt là `cp -fpR`, nên
mỗi lần `Build/Prepare` chạy là một lần copy **toàn bộ filesystem gốc** vào build dir. Lúc phát
hiện, thư mục đã **17 GB** và chứa `boot/`, `home/`. Lần này `cp` mới dừng vì nó nhận ra đang
copy chính đích vào đích.

### 27.1 Vì sao biến rỗng

`PKG_SOURCE:=$(APP_HNI_ICWMP_TR098_DIR)`, và biến đó được export ở
`feeds/airoha/target/linux/airoha/dir.mak:894`. File `dir.mak` **chỉ được include từ Makefile của
TARGET** (`feeds/airoha/target/linux/airoha/Makefile:21`):

| Kiểu build | Có include target Makefile? | `APP_HNI_ICWMP_TR098_DIR` |
|---|---|---|
| Ảnh đầy đủ (`make -j16`) | có | đúng đường dẫn |
| Gói lẻ (`make package/icwmp_tr098/compile`) | **không** | **rỗng** |

Đây là lý do `libtr098` chưa bao giờ dính: nó dùng `$(TRUNK_DIR)/apps/hni/libicwmp_dm`, y như gói
`cwmpclient` gốc của vendor.

Dòng `PKG_SOURCE:=$(APP_HNI_ICWMP_TR098_DIR)` có từ bản baseline trước bundle này — bản
`.icwmp-backups/.../original/` trên cây người dùng cũng có, với `$(CP) $(PKG_SOURCE)/*`. Tức lỗi
đã nằm đó từ đầu, chỉ chưa lộ vì build gói lẻ luôn chạy với bản feed copy cũ (mục 26).

### 27.2 Biến thứ hai: `TRUNK_DIR` cũng không tự có

Đổi sang `$(TRUNK_DIR)` chưa đủ. `TRUNK_DIR` được ghi vào `include/ecnt-trunkdir.mk` bởi
`airoha_script/airoha-compile.sh`, nhưng **không file nào trong cây OpenWrt include file đó** —
`rules.mk`, `toplevel.mk`, `package.mk`, `target.mk`, `kernel.mk` đều không nhắc tới. Nó chỉ có
mặt khi shell gọi build đã export sẵn, tức ngay sau `airoha-compile.sh` chứ không phải trong một
terminal mới.

Nên cả hai feed Makefile nay tự kéo vào:

```make
-include $(TOPDIR)/include/ecnt-trunkdir.mk
```

`-include` để cây không có file đó vẫn parse được.

### 27.3 Lá chắn

Một đường dẫn sai không được phép tốn 17 GB. Cả hai `Build/Prepare` nay từ chối chạy khi
`PKG_SOURCE` rỗng hoặc không phải thư mục:

```make
@test -n "$(strip $(PKG_SOURCE))" -a -d "$(PKG_SOURCE)" || { \
        echo "ERROR: PKG_SOURCE is empty or not a directory: '$(PKG_SOURCE)'"; \
        echo "Without this guard the next line copies all of / into $(PKG_BUILD_DIR)."; \
        exit 1; }
```

### 27.4 Kiểm mới trong `check-pkg-deps.py`

Thêm lớp: biến mà feed Makefile **thay vào một câu lệnh** không được là biến chỉ do
`target/linux/**/*.mak` định nghĩa.

Hai tinh chỉnh phải làm ngay khi chạy thử, vì cả hai đều là khiếm khuyết thật:

- **Bỏ comment trước khi quét.** Chính comment tôi viết để giải thích lỗi có nhắc
  `$(APP_HNI_ICWMP_TR098_DIR)`, nên bản đã sửa vẫn bị báo.
- **Biến chỉ nằm trong `ifeq`/`ifdef` là cờ tính năng, không phải giá trị.**
  `ifeq ($(TCSUPPORT_VOIP),y)` không định nghĩa nghĩa là nhánh tắt — đúng như mong đợi. Chỉ biến
  được thay vào câu lệnh mới nguy hiểm.

**Chạy ngược (NEG 7)**: đọc Makefile bản `HEAD` chưa sửa → báo đúng
`BIẾN $(APP_HNI_ICWMP_TR098_DIR): chỉ định nghĩa trong target/linux/** -- build gói lẻ không
include, sẽ RỖNG`.

### 27.5 Đã làm trực tiếp trên cây build của người dùng

Người dùng cho phép thao tác trên `1_src/2025q3` để rút ngắn vòng lặp:

| Việc | Kết quả |
|---|---|
| Xóa build dir 17 GB | `build_dir/.../linux-airoha_an7583/icwmp_tr098` — đã sạch, `/home` còn trống 26 GB |
| Apply bundle `a01a585a0fb5` | 2 Makefile feed + `.icwmp-release.json`, backup `20260924-162110-p7d9ygy3` |
| Đồng bộ feed copy | copy thẳng hai Makefile sang `feeds/airoha/package/airoha/apps/` (đúng việc `feeds install` làm với `src-cpy`) |
| Xác minh | `PKG_SOURCE` → `/home/nvtu/workspace/openwrt/1_src/2025q3/tclinux_phoenix/apps/hni/icwmp_tr098`, thư mục **có thật**, chứa nguồn app |

`./scripts/feeds update` không chạy được từ phiên này (`Unsupported version of make found`) và
**build cũng không**: host này không có `make` lẫn `gcc` trong PATH. Nên bước compile vẫn do người
dùng chạy — không có kết quả build nào được khẳng định ở đây.

### 27.6 Trạng thái

| Kiểm | Kết quả |
|---|---|
| Gói `libtr098` | BUILD XONG (lần build trước) |
| Gói `icwmp_tr098` | compile + link đã xong trước đó; nay sửa `Build/Prepare`, **chưa build lại** |
| `check-pkg-deps.py` | chỉ còn `-lcrypto`/`-lssl` (cố ý); chạy ngược NEG 7 đạt |
| `check-c-sanity.py` (7 lớp) | lib 34 file 0 vấn đề · app 17 file 0 vấn đề × 3 SDK |
| `check-automake-conds.py` | 2 cây OK |
| `verify-dm-paths.py` | P2+P3 137/137, P4 57/57, dôi 0 |
| Bundle | `a01a585a0fb5`, 389 file, đã apply lên cây người dùng |

---

## 28. Cổng build ĐẠT — hai `.ipk` đã ra

Ngày 2026-09-24, trên `1_src/2025q3`:

| Gói | File | Kích thước | Giờ |
|---|---|---|---|
| `libtr098` | `libtr098_3_aarch64_cortex-a53.ipk` | 117.605 B | 17:35 |
| `icwmp_tr098` | `icwmp_tr098_3-2_aarch64_cortex-a53.ipk` | 202.786 B | 17:41 |

### 28.1 Kiểm nội dung gói

`libtr098`: `/usr/lib/libtr098.so.3.0.0` + `/usr/share/icwmp/icwmp_dm.sh` (cầu shell compat).

`icwmp_tr098`: 85 file. Binary `/usr/sbin/icwmp_tr098d` **214.488 B**, init `icwmpd` và
`easycwmpd`, `/etc/config/cwmp` + `/etc/config/easycwmp`, thư viện hàm easycwmp dưới
`/usr/share/easycwmp/functions/`.

`Depends:` của gói xác nhận hai patch cuối đã có tác dụng thật:

```
Depends: libc, libubus20210630, libuci20130104, libubox20210516, libcurl,
         libmicroxml, libjson-c5, libblobmsg-json20210516, libpthread,
         zlib, libtr098
Conflicts: cwmpclient
```

`zlib` và `libubox` có mặt (patch `0048`), `Conflicts: cwmpclient` giữ đúng ý thay thế client cũ.

### 28.2 Chuỗi lỗi đã đi qua

| Patch | Chặn ở | Lớp lỗi |
|---|---|---|
| `0043` | compile `libtr098` | comment nuốt code |
| `0044` | compile `libtr098` | `static` trùng khai báo engine |
| `0045` | automake gói app | `+=` ngoài điều kiện của `=` |
| `0046` | compile app | `static` trùng khai báo engine (cây app) |
| `0047` | compile app | macro mang sẵn `;` làm `else` mồ côi |
| `0048` | `ipkg-build` | DEPENDS thiếu `+zlib` |
| `0049` | — | apply không nói bước `feeds update` (mất một vòng build) |
| `0050`+`0051` | `Build/Prepare` | `PKG_SOURCE` rỗng → `cp -fpR /.` |

Sáu lỗi biên dịch/đóng gói, hai lỗi quy trình. Ba bộ kiểm tĩnh bắt trước được `0043`, `0044`,
`0045`, `0046`, `0047`, `0048`, `0050`; hai lỗi quy trình (`0049`) thì không lớp nào bắt được vì
cây nguồn luôn đúng.

### 28.3 Còn lại

Cổng build xong không có nghĩa là chạy được. Tiếp theo là **gate board 1**: cài `.ipk` lên
HP2236B, `icwmpd` lên được và `tr069 dm` đọc ra tham số. Rồi gate 2 (so giá trị với client cũ,
phiên ACS) và gate 3 (Connection Request / Download / Upload / reboot / factory reset).

---

## 29. P4c — `WANPPPConnection.{i}`, 42 tham số

Nguồn: `sub_entry_wandevice_wanconnectiondevice_ppp()` (dòng 323 của
`functions/tr098/wan_device`), `wan_device_browse_instances_wancxdev_ppp()` (dòng 533) và nửa PPP
của `wan_device_get_total_entry()`.

Data model C: **259 → 301 / 783 tham số (38,4%)**.

### 29.1 Vì sao thêm vào `wanip_mtk.c` chứ không tạo file mới

Quy ước của bản port là một file một nhánh object. Ở đây phá lệ, có lý do:

- Shell phục vụ **cả hai** object bằng một cặp hàm, chỉ khác tham số
  `$targe_conn_type` (0 cho IPoE, 2 cho PPPoE).
- Entry PPP gọi **đúng những `wan_device_get_*` mà entry IP gọi** cho 30 trên 42 leaf.

Tách file thì phải hoặc export 30 helper ra header kèm tiền tố (đổi tên 30 hàm trong một file
vừa build sạch trên SDK thật), hoặc nhân bản chúng và để hai bản trôi khỏi nhau. Cả hai đều tệ
hơn việc để chung. File thành 1755 dòng.

### 29.2 Instance — giống P4b, chỉ khác bộ lọc

`wan_entries()` thành `wan_entries_kind(out, max, kind)`:

| kind | Lọc | Object |
|---|---|---|
| `WAN_KIND_IP` | `switch_mode==1`, hoặc `switch_mode==0 && conn_type==0` | `WANIPConnection` |
| `WAN_KIND_PPP` | `switch_mode==0 && conn_type==2` | `WANPPPConnection` |

Instance vẫn là `id + 1`, vẫn dừng ở section đầu tiên không có `id`. **Mỗi entry thuộc đúng một
trong hai object**, nên `WANIPConnection.2` và `WANPPPConnection.2` là hai entry khác nhau — đúng
như sản phẩm vẫn báo.

### 29.3 12 leaf không dùng chung

| Leaf | Nguồn |
|---|---|
| `TransportType` | hằng `PPPoE` |
| `PossibleConnectionTypes`, `ConnectionType` (get) | hằng `IP_Routed` — **khác** object IP (`IP_Routed,IP_Bridged`) |
| `Username` | `wan.@entry[i].ppp_username`, set ghi thêm `network.if<id>.username` |
| `Password` | xem 29.4 |
| `RemoteIPAddress` | ubus `network.interface.if<id> status` → `$["ipv4-address"][0].ptpaddress` |
| `MaxMRUSize` | `network.if<id>.mtu`, mặc định `1492` khi chưa đặt |
| `CurrentMRUSize` | `/sys/class/net/<l3_device>/mtu` |
| `MaxMTUSize` | xem 29.5 |
| `Reset` | xem 29.4 |
| `Stats.*` | **`l3_device`** từ ubus, không phải `network.if<id>.device` |

`Stats` là chỗ dễ bỏ sót nhất: `wan_device_get_eth_stats()` có **ba** nhánh, và nhánh PPP đọc
netdev mà ppp daemon tạo ra, không phải ethernet bên dưới. `stat_of()` nay phân ba nhánh theo
`e->ppp` / `e->bridge`.

Quyền cũng không giống object IP: `DefaultGateway` và `RemoteIPAddress` **chỉ đọc**, còn
`ExternalIPAddress` và `DNSEnabled` nhận setter `"true"` của shell — chấp nhận lệnh ghi rồi không
làm gì.

### 29.4 Hai khác biệt cố ý

**`Password` đọc ra rỗng.** Shell trả về `uci get wan.@entry[i].ppp_password`, tức giao mật khẩu
PPP cho bất cứ ai hỏi. TR-098 quy định tham số này đọc ra chuỗi rỗng, và ACS không bao giờ cần
đọc lại giá trị nó vừa ghi. Ghi vẫn nguyên vẹn.

**`Reset` xếp hàng thay vì chạy ngay.** Shell chạy `ifdown; sleep 1; ifup` **inline**. Trên đúng
WAN đang mang phiên CWMP, nó cắt kết nối **trước khi** response kịp gửi — ACS thấy timeout chứ
không thấy `SetParameterValuesResponse`. Ở đây lệnh vào danh sách apply-service, chạy sau khi
phiên kết thúc, cùng chỗ với mọi thay đổi khác của object này.

Cả hai đều là thay đổi hành vi. Nếu muốn giống sản phẩm tuyệt đối thì nói, sửa lại mất hai dòng.

### 29.5 Một bug được chép lại nguyên

`MaxMTUSize` của nhánh PPP gọi `wan_device_get_mtu $iface` và `wan_device_set_br_mtu $device`
trong một hàm **không hề đặt** hai biến đó (locals chỉ có `iface4`, `iface6`, `ifaceName`...).
Hệ quả thật trên sản phẩm:

- getter đọc `uci get network..device` → rỗng → `cat /sys/class/net//mtu` → **trả rỗng**,
- setter kiểm tra range rồi `uci set network..mtu=...` → path không hợp lệ → **báo thành công,
  không đổi gì**.

Chép lại y nguyên, cùng lý do đã ghi cho `X_AIS_VLAN8021P` của entry bridge ở mục 18: làm cho nó
chạy tức là bắt đầu đổi MTU của một link PPP mà sản phẩm chưa từng đụng tới.

### 29.6 Kiểm

| Kiểm | Kết quả |
|---|---|
| `verify-dm-paths.py --phase 4` | 99/173 param, **dôi 0**; dưới `WANPPPConnection.{i}` không còn thiếu gì ngoài P4d/P4e/P4f |
| `verify-dm-paths.py --phase 2/3` | thiếu 0 |
| `--claims` | 88 claim, 12 module, **0 cặp chồng** |
| `check-c-sanity.py` | lib 34 file 0 vấn đề, app 17 file 0 vấn đề |
| `check-automake-conds.py` | 0 vấn đề |
| `check-pkg-deps.py` | không có biến nào sai phạm vi |
| Bundle | `a9dd185b8c7f`, 390 file, `apply --dry-run` PASS hai cây SDK |

**CHƯA BUILD-TEST**: host này không có compiler. Gói đã build sạch ở vòng trước nên rủi ro thấp,
nhưng vẫn phải build lại.

---

## 30. P4d — `X_AIS_IPv6` của cả hai object WAN, 46 tham số

Nguồn: `wan_device_v6_*` cho nhánh `X_AIS_IPv6.`, `wan_device_{get,set}_x_ais_ipv6_*` cho các
leaf phẳng, cộng `json_get_gw()` và `is_ipv6_public()` của `functions/common/common`.

Data model C: **301 → 347 / 783 (44,3%)**.

### 30.1 File mới, và một header dùng chung

Lần này **tách file** (`wanipv6_mtk.c`, 1025 dòng): 46 tham số này không dùng chung getter nào
với các leaf IPv4 — chúng đọc `wan.@entry[i].v6_*` và `ubus network.interface.if<id>_6`.

Thứ duy nhất dùng chung là **mô hình entry**. Nên `struct wan_entry` cùng phép duyệt chuyển sang
`wanconn_mtk.h`, và `wanip_mtk.c` export 8 helper. Bốn cái tên quá chung được đổi ra khỏi vùng
dễ đụng độ:

| Cũ (static) | Mới (export) |
|---|---|
| `sect_opt` | `wan_sect_opt` |
| `entry_opt` | `wan_entry_opt` |
| `str_is_uint` | `wan_str_is_uint` |
| `iface_status` | `wan_iface_status` |

Bốn cái còn lại đã có tiền tố `wan_` nên giữ nguyên tên, chỉ bỏ `static`. P4e và P4f sẽ dùng
đúng header này — đó là lý do bỏ công tách bây giờ thay vì để `wanip_mtk.c` phình tới 3000 dòng.

Sau refactor: `check-c-sanity` 34 file 0 vấn đề, `verify-dm-paths` phase 4 vẫn 99/0 — không hồi quy.

### 30.2 Hai hình dạng, và chúng KHÔNG cùng một tập

| | `X_AIS_IPv6.` (nhánh) | `X_AIS_IPv6<Name>` (phẳng) |
|---|---|---|
| `WANIPConnection` | 13 leaf | **12** leaf, status tên `X_AIS_IPv6ConnStatus` |
| `WANPPPConnection` | 13 leaf, **giống hệt** | **8** leaf, status tên `X_AIS_IPv6ConnectionStatus` |

Object PPP **không có** `GatewayType`, `GatewayAddress`, `DNSType`, `PrefixDelegationType`,
`GUAFromPrefixEnable`. Giữ nguyên bất đối xứng đó: một template ACS viết cho object này không
được đột nhiên thấy tham số mới ở object kia.

Nhánh giống hệt nhau nên **một cặp bảng phục vụ cả hai**, `dm_registry` merge theo tên object.

### 30.3 Chỗ trùng tên nhưng không phải alias

| Cặp | Khác nhau ở đâu |
|---|---|
| `X_AIS_IPv6.AddressingType` vs `X_AIS_IPv6AddressingType` | leaf nhánh đọc `v6_mode` một mình; leaf phẳng đọc `v6_active` trước và trả `"None"` khi IPv6 tắt |
| `X_AIS_IPv6AutoModeEnable` | `true` → DHCP (1), `false` → Static (2). **Không bao giờ** đưa entry về SLAAC được — chỉ `AddressingType` làm được |
| `X_AIS_IPv6.DefaultGateway` vs `X_AIS_IPv6GatewayAddress` | leaf nhánh lấy nexthop của route `::` (và đọc nhánh `inactive` khi `defaultroute=0`); leaf phẳng lấy thẳng `route[0].nexthop`. Setter leaf nhánh chặn nếu không Static, leaf phẳng **không chặn gì** |

### 30.4 Guard rail giữ nguyên từng cái

Đây là phần ACS sẽ đụng thật:

| Leaf | Điều kiện | Fault |
|---|---|---|
| `IPAddress`, `PrefixLength`, `DefaultGateway`, `ExternalAddress` | phải đang Static | `9001` |
| `Pd.Enable`, `X_AIS_IPv6PdEnable` | **không** được Static | `9001` |
| `DNSServers` | `v6_static_dns` phải khác `0` | `9001` |
| `ManualDNS` | không tắt được khi đang Static | `9007` |
| `PrefixLength` | `> 128` | `9005` (đúng mã shell trả) |

### 30.5 Hai chi tiết triển khai

**`/proc/net/if_inet6` thay cho `ip -6 addr show`.** `wan_device_v6_get_ipaddr()` có đường lùi:
khi ubus không báo `ipv6-address`, shell chạy `ip -6 addr show dev <l3_device> | ... | head -1`.
Ở đây đọc `/proc/net/if_inet6` (32 hex + ifindex + prefixlen hex + scope + flags + tên dev) rồi
`inet_ntop()` về đúng dạng rút gọn mà `ip` in ra — cùng nguồn dữ liệu, không phải fork process.

**`X_AIS_IPv6DNSServers1/2` không theo entry.** Chúng đọc và ghi danh sách cách nhau bằng dấu
cách `dhcp.lan.dns` — DNS mà LAN phát ra. Mọi instance của mọi object đều thấy cùng một cặp, và
ghi một cái vẫn giữ các phần tử khác của danh sách. Đó là hành vi sản phẩm, và là lý do hai leaf
này không nằm trong nhánh `X_AIS_IPv6.`.

### 30.6 Một khác biệt cố ý

`X_AIS_IPv6ConnStatus` **ghi được** trong sản phẩm: kiểm enum rồi `ifup`/`ifdown`. Shell chạy
inline — cùng lỗi với `WANPPPConnection.Reset` ở mục 29.4: cắt phiên đang trả lời. Ở đây xếp vào
apply-service, chạy sau khi phiên kết thúc.

### 30.7 Kiểm

| Kiểm | Kết quả |
|---|---|
| `verify-dm-paths --phase 4` | **145/173**, dôi **0**; 28 còn lại đúng bằng PortMapping 26 + X_AIS_ServiceList 2 |
| `--phase 2/3` | thiếu 0 |
| `--claims` | **111 claim**, 13 module, **0 cặp chồng** |
| `check-c-sanity.py` | lib **35** file 0 vấn đề, app 17 file 0 vấn đề |
| `check-automake-conds.py` | 0 vấn đề |
| Bundle | `c1c470a0651e`, 393 file, `apply --dry-run` PASS hai cây SDK |

**CHƯA BUILD-TEST.**

---

## 31. P4e — `PortMapping` dưới cả hai object WAN, 26 tham số

Nguồn: `sub_entry_port_mapping_wanconnectiondevice()`, `port_mapping_browse_instances()`,
`wan_device_get_port_mapping_number()`, `port_mapping_add_entry()`,
`port_mapping_delete_entry()` và 12 cặp `port_mapping_{get,set}_*`.

Data model C: **347 → 373 / 783 (47,6%)**. Phase 4 còn **2** — chỉ `X_AIS_ServiceList`.

### 31.1 Rule nằm ở một chỗ, chia theo `interface`

Rule là section `config port_forwarding` của gói UCI **`firewall_clay`**, dùng chung cho mọi
WAN connection. Thứ chia chúng về từng instance là option `interface`, và tên đem so **không
phải netdev**:

| Loại entry | Tên đem so | Nguồn |
|---|---|---|
| Routed IPoE | `pon` hoặc `pon.<vlan_id>` | `get_ipoe_interface_name()`: `vlan_active==1` và có `vlan_id` thì mới có hậu tố |
| PPPoE | `pppoe-if<id>` | `"$protocol-$iface4"` |
| Bridged | *(không có)* | nhánh bridge chưa bao giờ đăng ký `PortMapping` |

### 31.2 Instance là VỊ TRÍ, và nó không ổn định

Số instance = **thứ tự trong danh sách rule khớp**, đếm từ 1, theo thứ tự file UCI. Không phải
chỉ số section. **Xoá rule 2 trong 3 rule thì rule thứ ba thành số 2.** Đó đúng là biến
`rule_index` của shell, và ACS duyệt lại object mỗi phiên vẫn thấy y như trước.

Khác hẳn `WANIPConnection`/`WANPPPConnection`, nơi instance là `id + 1` và **ổn định**. Hai quy
tắc đánh số khác nhau nằm cạnh nhau trong cùng một cây — ghi rõ ở đây vì đây là chỗ dễ sửa nhầm
cho "nhất quán".

### 31.3 Chép nguyên, từng cái một

1. **`X_AIS_Name` và `PortMappingDescription` là CÙNG một option** `service_type`. Ghi cái này
   đổi cái kia. Chỉ khác khi option rỗng: `Name` trả `"-"`, `Description` trả `""`. Và khác giới
   hạn độ dài: 128 với `Name`, 256 với `Description`.
2. **Ghi `RemoteHost` hoặc `X_AIS_RemoteHostEndRange` bằng giá trị RỖNG xoá CẢ HAI đầu**
   (`remote_start_ip` và `remote_end_ip`). Chỉ trường hợp rỗng mới đối xứng như vậy; một địa chỉ
   thật chỉ ghi option của chính nó.
3. **`udp/tcp` và `both` đều lưu thành `tcp/udp`**, và enum nhận không phân biệt hoa thường
   (`validate_protocol()`).
4. **Mọi setter xếp hàng** `ubus call hni.service commit '{"param":"PortForwarding","action":"ApplyRule"}'`
   vào apply-service. Không có gì được áp dụng giữa phiên.
5. **AddObject trả về TỔNG số section `port_forwarding`** — không phải vị trí rule mới, cũng
   không phải số rule của connection này. Chép nguyên: đó là số instance ACS vẫn được nhận.

### 31.4 Một khác biệt, mang tính bổ sung

Shell chỉ đăng ký `PortMapping` khi `ip route` có default gateway — một điều kiện **toàn cục**,
giống nhau cho mọi entry, tính một lần mỗi phiên. Cây C tĩnh không thể làm object hiện ra rồi
biến mất, nên ở đây nó **luôn có mặt**.

Không giá trị nào đổi: rule đến từ `firewall_clay`, không đến từ bảng định tuyến. Chỉ khác ở chỗ
`GetParameterNames` nay liệt kê object trên một máy không có default route, nơi shell bỏ qua nó.

Object path `PortMapping.` **được claim** — khác các object connection phía trên: `AddObject` và
`DeleteObject` của một port mapping nay do module này xử lý, không còn về `sdk/mtk/compat/`.

### 31.5 Lớp kiểm 8 — `NULL` vào out-parameter

Bản nháp đầu gọi:

```c
dmuci_add_section(PM_PACKAGE, PM_TYPE, &added, NULL);
```

`dmuci.c` ghi `*value` trên **mọi** đường ra, kể cả hai đường lỗi:

```c
if (dmuci_lookup_ptr(...)) { *value = ""; return -1; }
```

Compiler hoàn toàn hài lòng — `NULL` là `char **` hợp lệ. Trên thiết bị đó là segfault ngay lần
`AddObject` đầu tiên.

Lớp 8 **tự suy ra** danh sách out-parameter thay vì dùng bảng liệt kê sẵn: quét mọi định nghĩa
hàm trong cây, tham số nào có dạng `T **p` mà thân hàm có `*p = ...` thì đánh dấu; rồi báo mọi
lời gọi truyền `NULL` đúng vị trí đó.

**Chạy ngược (NEG 8)** trên bản nháp: `:393 dmuci_add_section() nhận NULL ở tham số 4, nhưng hàm
ghi *p = ... qua nó -- segfault lúc chạy`. Trên cây đã sửa: 36 file, 0 vấn đề. Quét toàn cây
không tìm thấy call site nào khác cùng lỗi.

### 31.6 Kiểm

| Kiểm | Kết quả |
|---|---|
| `verify-dm-paths --phase 4` | **171/173**, dôi **0**; còn đúng `X_AIS_ServiceList` × 2 |
| `--phase 2/3` | thiếu 0 |
| `--claims` | **115 claim**, 14 module, **0 cặp chồng** |
| `check-c-sanity.py` (8 lớp) | lib **36** file 0 vấn đề, app 17 file 0 vấn đề × 3 SDK |
| `check-automake-conds.py` | 0 vấn đề |
| Bundle | `1c4bddb90f9e`, 395 file, `apply --dry-run` PASS hai cây SDK |

**CHƯA BUILD-TEST.**

---

## 32. P4f — `X_AIS_ServiceList`, và PHASE 4 ĐÓNG

Nguồn: `wan_device_get_x_ais_service_list()`, `wan_device_set_x_ais_service_list()`,
`wan_device_update_internet_access()`, `wan_device_configure_easycwmpd()`,
`wan_device_get_physical_interface()`.

Data model C: **373 → 375 / 783 (47,9%)**. **Phase 4: 173/173, thiếu 0, dôi 0.**

Hai tham số, và là hai tham số nặng nhất cả nhánh WANDevice: setter quyết định một WAN mang lưu
lượng khách, mang quản lý TR-069, hay cả hai — và trên đường đi nó viết lại firewall cùng chỗ
bind của chính client CWMP. Để cuối cùng là có lý do.

### 32.1 Setter đụng vào những gì

| Nơi | Khi nào |
|---|---|
| `wan.@entry[i].service_type` | luôn luôn |
| `easycwmp.@acs[0].enablecwmp` | các chuyển đổi cắt qua ranh giới TR-069 |
| `easycwmp.@local[0].interface` / `.network` | bind/unbind client vào một WAN |
| Rule firewall `tr069_block_<wan_if>` + `iptables FORWARD ... -j DROP` | chế độ TR069-only chặn LAN |

### 32.2 Hai cái bẫy trong bản gốc, chép lại cả hai

1. **Chế độ bridge chỉ nhận `OTHER`.** `INTERNET`, `TR069`, `INTERNET_TR069` đều trả `9007`.
   Getter cũng luôn trả `OTHER` cho entry bridge, bất kể `service_type` ghi gì.
2. **Ở chế độ router, `OTHER` được lưu thành `INTERNET`** (`num=1`). ACS ghi `OTHER` rồi đọc lại
   ra `INTERNET` — đã như vậy từ trước tới nay.

### 32.3 Và một cái bản nháp đầu làm sai

Mọi nhánh của shell là `case "$old_service_type" in 3|2|1|4)` — **không có arm mặc định**. Một
entry chưa từng đặt `service_type` rơi khỏi tất cả: giá trị được ghi lại và **không một side
effect nào chạy**.

Bản nháp đầu của tôi chạy chúng. Hậu quả nếu lọt: lần đầu ACS ghi `X_AIS_ServiceList` lên một
entry mới, firewall và chỗ bind của client CWMP bị viết lại — trên một entry mà sản phẩm chưa
từng chạm vào. Đã sửa bằng `old_known`, tái tạo đúng fall-through.

Không lớp kiểm tĩnh nào bắt được loại này. Nó lộ ra vì **đọc lại từng nhánh của shell đối chiếu
với code vừa viết** trước khi commit.

### 32.4 Khác biệt duy nhất, và là khác biệt bắt buộc

Shell chạy nửa gây gián đoạn **INLINE** — `iptables`, `/etc/init.d/firewall reload &`,
`/etc/init.d/easycwmpd restart &` — ngay trong `SetParameterValues` mà nó đang trả lời. Khởi động
lại client CWMP giữa phiên nghĩa là **ACS không bao giờ nhận được response của chính lệnh vừa
gửi**.

Ở đây: nửa UCI chạy ngay, cùng transaction với phần còn lại của phiên; chỉ nửa mệnh lệnh mới xếp
vào apply-service, chạy sau khi phiên đóng. Lệnh xếp hàng là **lệnh của chính shell, nguyên văn**.

`/etc/init.d/easycwmpd restart` đúng trong bản build này chứ không phải sót lại:
`sdk/mtk/files/easycwmpd` là shim một dòng `exec /etc/init.d/icwmpd "$@"`.

### 32.5 Một lỗ hổng đã biết, cố ý để nguyên

`easycwmp.@acs[0].enablecwmp` được ghi y như trước, nhưng bảng mirror easycwmp → cwmp trong
`apps/icwmp/sdk/mtk/icwmp_mtk.c` **không mang option đó**. Nên trong bản build này `icwmpd`
không bị nó gate như `easycwmpd` trước kia.

Thêm nó vào mirror sẽ cho phép ACS **tắt hẳn client CWMP** bằng cách ghi `X_AIS_ServiceList` —
đó là quyết định của sản phẩm, không phải của bản port. Ghi lại ở đây để người quyết định có đủ
dữ kiện.

### 32.6 Phase 4 khép lại

| Phase | Param | Patch |
|---|---|---|
| P4a khung `WANDevice` | 22 | `0040` |
| P4b `WANIPConnection` | 35 | `0041` |
| P4c `WANPPPConnection` | 42 | `0052` |
| P4d `X_AIS_IPv6` | 46 | `0053` |
| P4e `PortMapping` | 26 | `0054` |
| P4f `X_AIS_ServiceList` | 2 | `0055` |
| **Tổng** | **173** | |

| Kiểm | Kết quả |
|---|---|
| `verify-dm-paths --phase 4` | **173/173, thiếu 0, dôi 0** |
| `--phase 2/3` | thiếu 0 |
| `--claims` | **117 claim**, 15 module, **0 cặp chồng** |
| `check-c-sanity.py` (8 lớp) | lib **37** file 0 vấn đề, app 17 file 0 vấn đề × 3 SDK |
| `check-automake-conds.py` | 0 vấn đề |
| Bundle | `ef3bd5c483f1`, 397 file, `apply --dry-run` PASS hai cây SDK |

**CHƯA BUILD-TEST.** Bốn phase (P4c–P4f, 116 tham số, 3 file mới + 1 header) chưa qua compiler
lần nào — đây là khối code lớn nhất chưa build kể từ đầu bản port.

## 33. Build P4c–P4f ĐẠT, cổng compile thật, và P5a — `IPPingDiagnostics` + `TraceRouteDiagnostics`

### 33.1 Build P4c–P4f — ĐẠT (Verified)

Người dùng build lại với bundle `ef3bd5c483f1`. Kiểm trên cây `1_src` (25/09):

| Bằng chứng | Giá trị |
|---|---|
| `libtr098_3_aarch64_cortex-a53.ipk` | 128.195 B, 13:40 (bản 24/09: 117.605 B) |
| `icwmp_tr098_3-2_aarch64_cortex-a53.ipk` | 202.856 B, 13:47 |
| object của P4c–P4f trong `build_dir/.../libtr098/sdk/mtk/dm098/` | `wanip_mtk.o`, `wanipv6_mtk.o`, `portmapping_mtk.o`, `servicelist_mtk.o` — 13:40 |
| symbol trong `libtr098.so.3.0.0` | `browsePortMappingInst`, `svc_acs_enablecwmp`, `svc_configure_cwmpd`, `svc_internet_access`, ... |

116 tham số, 3 file mới + 1 header đã qua compiler và linker. Board: chưa chạy.

### 33.2 Cổng compile thật — `check-cc-syntax.py`

Cây build của người dùng có sẵn cross-gcc của SDK
(`staging_dir/toolchain-aarch64_cortex-a53_gcc-10.2.0_musl`). `check-cc-syntax.py` chạy nó với
`-fsyntax-only` trên **đúng** danh sách file mà `libtr098` và `icwmp_tr098d` build, lấy nguồn từ
overlay, header `icwmp_dm/` dựng tạm trỏ về nguồn hiện tại (không dùng bản cũ trong
`staging_dir`), không ghi gì vào cây SDK. Cảnh báo gây lỗi lúc chạy được nâng thành lỗi:
`implicit-function-declaration`, `int-conversion`, `incompatible-pointer-types`, `return-type`,
`implicit-int`, `format-security`. 53 file, khoảng 1 giây.

Lần chạy đầu bắt được **hai lỗi mà 8 lớp của `check-c-sanity.py` đều cho qua**:

| Lỗi | Ở đâu | Hậu quả |
|---|---|---|
| định danh `D` chưa khai báo | `ipping_mtk.c` bản nháp — `sed` đổi `(D, ` nhưng sót `(D);` | không build được, bắt trước khi giao |
| `__dmjson_get_value_in_array_idx` **không có prototype** | macro trong `dmjson.h:38`, hàm định nghĩa ở `dmjson.c:210` | gcc 10 coi nó trả `int` và **chỉ cảnh báo** — build SDK vẫn ra `.ipk`. Trên aarch64 con trỏ `char *` mất 32 bit cao |

Lỗi thứ hai là **lỗi chạy thật trong bản đã build**:

- `wanipv6_mtk.c:455-456` (P4d, `X_AIS_IPv6...DNSServers`) gọi macro đó. Chuỗi trả về nằm trong
  heap json-c hoặc `.rodata` của thư viện — trên aarch64 đều ở trên 4 GB, nên con trỏ bị cắt trỏ
  vào vùng không map. **Conditional**: bố cục địa chỉ của tiến trình; chưa chạy trên board.
- `tr098/deviceinfo.c:698-703` (upstream) cũng gọi, và ép `(char *)` nên **che cả cảnh báo**.
  File này không build trên MTK.

Sửa: khai báo `____dmjson_get_value_in_array_idx()` và `__dmjson_get_value_in_array_idx()` trong
`dmjson.h`, đúng chữ ký ở `dmjson.c:181,210` (patch `0056`).

Cùng lần chạy, ba chỗ không đổi hành vi, sửa để cổng về 0:

| File | Sửa |
|---|---|
| `dmuci.h` | prototype `dmuci_delete_by_section_unnamed_tr098` — sinh bởi `NEW_UCI_PATH`, `dmcommon.c:1087` gọi không khai báo (trả `int`, vô hại) |
| `dmentry.c` | `#include <ctype.h>` cho `isdigit()` |
| `netlink.c:247` | `bind()` nhận `struct sockaddr *`, không phải `sockaddr_in6 *` |

`collect_sources()` (dùng chung cho cả hai checker) nay hiểu khối `if/else/endif` của automake.
Trước đó nó kiểm cả 4 file `upnp/*` — **không** build trên SDK nào (`UPNP_TR064` chỉ bật bằng
`--enable-tr064`, không feed nào truyền; `config.log`: `UPNP_TR064_TRUE='#'`). Danh sách file giờ
khớp **tuyệt đối** với object trong `build_dir`: lib 36 (= 33 đã build + 3 file mới của P5a), app
17 = 17.

**Bài học:** kiểm tĩnh bằng regex không thay được compiler. Có compiler trong tầm tay thì chạy nó
trước khi giao — ghi nhận "không có compiler trên máy workspace" của các lượt trước đúng với
`gcc` hệ thống, nhưng cross-gcc của SDK thì luôn có trên máy build.

### 33.3 P5a — hai diagnostic, 22 tham số

Nguồn: `functions/tr098/ipping_diagnostic`, `functions/tr098/traceroute_diagnostic`,
`functions/common/{ipping,traceroute}_launch`.

**Cơ chế của shell (Verified):** diagnostic là một mặt tiền mỏng trước một launcher.

```
SPV DiagnosticsState=Requested
  -> <x>_stop_diagnostic ; DiagnosticsState=Requested (uci -P <dir>)
  -> common_execute_command_in_apply_service "/bin/sh $FUNCTION_PATH/<x>_launch run &"
hết phiên -> apply-service chạy launcher nền
  -> launcher đo, ghi kết quả vào cùng <dir>
  -> ubus -t 1 call tr069 inform '{"event":"8 DIAGNOSTICS COMPLETE"}'   (thử lại tới 200 lần)
```

Hợp đồng với `icwmpd` (Verified): `ubus.c` `cwmp_handle_inform` → `cwmp_get_int_event_code()`
(`cwmp.c:66`) nhận ký tự đầu `'8'` → `EVENT_IDX_8DIAGNOSTICS_COMPLETE` → event container → phiên
mới. Cả 6 launcher đã nằm trong `.ipk` tại `/usr/share/easycwmp/functions/`. **Launcher giữ nguyên
là shell** — chúng là bộ đo, không phải data model.

**Mỗi diagnostic một kho riêng** — điểm dễ sai nhất:

| Shell | Kho |
|---|---|
| `ipping_diagnostic` | `uci -P /var/state` |
| `traceroute_diagnostic` | `uci -P /var/state/traceroute` |
| `nslookup_diagnostic` | `uci -P /var/state/nslookup` (+ `nslookup_result`) |
| `dns_diagnostics` | `uci -P /var/state/dnsDiagnostics` (+ `dnsDiagnostics_result`) |
| `tr143/download_diagnostic` | `uci -P /var/state/downloadDiag` |
| `tr143/upload_diagnostic` | `uci -P /var/state/uploadDiag` |

Cùng tên option `easycwmp.@local[0].DiagnosticsState` nhưng là sáu giá trị khác nhau.
`mtk_varstate()` có sẵn đi qua context dùng chung đã gắn `/var/state` làm delta path — đọc kho
traceroute qua nó sẽ **lẫn delta của ping vào**. Vì vậy thêm `mtk_state(dir, ...)`: context mới
mỗi lần gọi, đúng những gì `uci -P <dir>` làm (`uci_add_delta_path(savedir)` rồi
`uci_set_savedir(dir)`).

**Mã mới:**

| File | Nội dung |
|---|---|
| `dmmtk.[ch]` | `mtk_state()/mtk_state_set()`, `mtk_kill_cmdline()` (`pgrep -f \| kill -9`, trừ chính mình), `mtk_netdev_exists()`, `mtk_ere_match()` (ERE, `REG_NEWLINE` như grep) |
| `dm098/diag_mtk.[ch]` | khung chung cho cả 7 diagnostic: bảng kho, `diag_get` (`${val:-def}`), `diag_stop`, `diag_store_value` (đuôi chung của mọi setter), `diag_request`, `diag_check_uint`, `diag_host_valid` |
| `dm098/ipping_mtk.c` | 12 tham số |
| `dm098/traceroute_mtk.c` | 10 tham số + `RouteHops.` |

**Quirk của vendor, chép nguyên và ghi trong file:**

1. `IPPingDiagnostics.Interface` nhận **đường dẫn TR-098** và dịch bằng bảng cứng
   (`resolve_trpath_to_ifname`): LAN IPInterface → `network.lan.ifname`, mọi `WANPPPConnection` →
   `network.if0.ifname`, mọi `WANIPConnection` → **`eth0.1` viết cứng**.
2. Setter đó gọi **`nslookup_stop_diagnostic`** chứ không phải của ping — lỗi copy-paste. Ghi
   `Interface` sẽ **giết một NSLookup đang chạy** và reset state của nó, còn ping đang chạy thì để
   nguyên.
3. `TraceRouteDiagnostics.Interface` lại nhận **tên thiết bị mạng**, mặc định `default`. Shell
   chấp nhận khi `$(ifconfig $2)` (không nháy) in ra gì đó: `""` → liệt kê interface → nhận;
   `-a` → nhận; hai từ → `ifconfig` cố **cấu hình** → rỗng → từ chối. Giữ kết quả chấp nhận/từ
   chối, **không** giữ tác dụng phụ (`eth0 down` từng thật sự tắt `eth0`).
4. `IPPingDiagnostics.DSCP` không kiểm gì và kiểu string; của TraceRoute thì `0..63`, unsignedInt.
5. Kiểm Host tìm một dotted quad **ở bất cứ đâu** trong chuỗi (`grep -o` không neo): `x1.2.3.4y`
   được nhận.
6. Số quá lớn so với `[` của busybox được **nhận**: `[` in `out of range`, trả 2, `if` hiểu là
   sai, kiểm khoảng không bao giờ chạy.
7. `RouteHops.` **luôn rỗng**: hàm browse ở `functions/tr181/traceroute_routehops`, mà
   `cwmpclient/Makefile:104` comment dòng cài `tr181/*`. Chỉ `RouteHopsNumberOfEntries` mang số.

**Khác biệt có chủ ý duy nhất:** shell kiểm và ghi trong cùng một lần gọi; ở đây VALUECHECK kiểm cả
SPV trước, VALUESET mới ghi. Một SPV có một giá trị sai không còn để lại nửa kia đã ghi vào
`/var/state` — như mọi phase trước.

### 33.4 Tiến trình nền không còn dùng chung stdio của `icwmpd`

P5 là lần đầu hàng đợi apply-service chở **tiến trình nền sống lâu** (traceroute, TR-143). Rà lại
hai đường chạy hàng đợi:

| Đường | Trước | Rủi ro | Sửa |
|---|---|---|---|
| không-compat `mtk_run_apply_service()` | `mtk_exec()` đọc stdout tới EOF rồi `waitpid` | launcher `... &` kế thừa pipe → **hết phiên phải chờ diagnostic chạy xong**, trong khi launcher đang `ubus call tr069 inform` vào chính agent này | `exec </dev/null >/dev/null 2>&1;` trước mỗi dòng |
| compat `icwmp_dm.sh apply_service` (đang build) | `/bin/sh $apply_service_tmp_file` với stdio của coprocess | coprocess sống suốt đời icwmpd, stdin là **pipe request**, stdout là **pipe reply**; `dmscript.c` giữ buffer đọc qua các lần gọi và **không xả dữ liệu cũ** (`drain_lines_locked`) | `</dev/null >/dev/null 2>&1` |

Mức rủi ro hôm nay (Verified từ code): thấp. Dòng không phải object JSON bị bỏ qua kèm log, và
`ubus call` in JSON thụt lề nhiều dòng — `{`, `"status": 1,`, `}` — nên từng dòng đều bị bỏ qua
(`ubus-2021-06-30-4fc532c8/cli.c:93`: `blobmsg_format_json_indent(msg, true, simple_output ? -1 : 0)`,
chỉ `-S` mới in một dòng).
Không launcher nào đọc stdin. Nhưng đó là **may mắn định dạng**, không phải thiết kế: một
`ubus -S` hay một `read` thêm vào sau này sẽ làm lệch giao thức với coprocess.

### 33.5 Kiểm

| Kiểm | Kết quả |
|---|---|
| `check-cc-syntax.py` (gcc thật) | lib **36**/0 lỗi · app **17**/0 lỗi |
| `check-c-sanity.py` 8 lớp | lib/mtk 36/0 · app 17/0 × 3 SDK |
| `check-automake-conds.py` | OK |
| `verify-dm-paths --phase 5` | C **22** param, **3** object, **dôi 0**; thiếu 66 = P5b–P5e |
| `--phase 2/3/4` | thiếu 0 (không lùi) |
| `--claims` | **119** claim, **17** module, **0** cặp chồng |
| bundle | `ed530b239da7`, 402 file `SHA256SUMS` OK, `apply --dry-run` exit 0 trên `1_src` và `2_src` |

Patch: `0056` (dmjson prototype + 3 sửa compile), `0057` (P5a). Data model C: **397/783**.

**Build:** chưa với `0056`/`0057`, nhưng mọi file đã qua cross-gcc của chính SDK đó. **Board:**
chưa.

### 33.6 Còn lại của P5

| Bước | Nội dung | Param |
|---|---|---|
| P5b | `DNSDiagnostics` + `NSLookupDiagnostics` (có `Result.{i}` từ kho `_result` riêng) | 25 |
| P5c | `DownloadDiagnostics` + `UploadDiagnostics` (TR-143) | 26 |
| P5d | `Layer3Forwarding` (605 dòng shell, đồng bộ, route) | 12 |
| P5e | `SelfTestDiagnostics` + `WiFi.NeighboringWiFiDiagnostic` (hằng số ở `root`) | 3 |

## 34. P5b — `NSLookupDiagnostics` + `DNSDiagnostics`, 20 tham số; ma trận có 5 lá "ma"

Nguồn: `functions/tr098/nslookup_diagnostic`, `functions/tr098/dns_diagnostics`,
`functions/common/{nslookup,dnsDiagnostics}_launch`. Code: `sdk/mtk/dm098/lookupdiag_mtk.c`
(một module, hai object), khung chung mở rộng ở `diag_mtk.[ch]`. Patch `0058`.

### 34.1 `DNSDiagnostics` trên sản phẩm chỉ có 7 tham số (Verified)

`dns_diagnostics` comment ba dòng đăng ký: `DNSServer` (:15), `ResultNumberOfEntries` (:18) và
object `Result.` (:21). Hàm `sub_entry_dnsdiag_dnslookupresult` và nửa ghi kết quả của launcher
vẫn còn, nhưng không đường nào tới được — `Result.{i}` chưa bao giờ xuất hiện với ACS.

Ma trận phủ (`gen-coverage-matrix.py`) đọc văn bản, bỏ dòng `#` nhưng vẫn thu 5 lá trong hàm
`sub_entry_*`, nên liệt kê `DNSDiagnostics.Result.{i}.*` mà **không** có dòng object chứa
`DNSDiagnostics.Result.`. Quét toàn bộ 783 dòng theo quy tắc "cây con `X.{i}.` chỉ tồn tại khi
object chứa `X.` được đăng ký": **đây là trường hợp duy nhất**, cả ở cấp `{i}` lồng nhau.

Xử lý: giữ nguyên ma trận (kiểm kê văn bản, số 783 được trích ở nhiều nơi), `verify-dm-paths.py`
tách 5 lá đó thành mục **"không tới được trên sản phẩm"** thay vì "thiếu". Số tham số tới được
trên sản phẩm là **778**; P5 là **83**, không phải 88.

### 34.2 `Result.{i}` của NSLookup — một giả thuyết đã bị bác bỏ

Launcher ghi kết quả bằng `uci -P /var/state/nslookup_result add easycwmp local` rồi
`uci -P ... commit easycwmp`. Giả thuyết ban đầu: `commit` ghi thẳng vào `/etc/config/easycwmp`
(flash), mỗi lần chạy nối thêm section vĩnh viễn, và `Result.1` mãi là kết quả lần đầu.

**Bác bỏ** bằng source libuci đúng bản SDK dùng (`uci-2020-10-06-52bbc99f`):

- `cli.c:749-752`: `-P` làm `uci_add_delta_path(savedir)`, `uci_set_savedir(dir)` và **bật
  `CLI_FLAG_NOCOMMIT`**;
- `cli.c:330-333`: `CMD_COMMIT` với cờ đó → `ret = 0; goto out` — không làm gì.

Vậy kết quả chỉ nằm ở delta `/var/state/nslookup_result/easycwmp`, bị `rm` đầu mỗi lần chạy
(`nslookup_process_result`). `Result.<n>` = `easycwmp.@local[<n>]` của kho đó: `@local[0]` là
section có sẵn trong `/etc/config/easycwmp`, `@local[1..N]` là các section launcher vừa thêm. Đếm
từ `ResultNumberOfEntries` của **kho tham số** (`/var/state/nslookup`), đọc từ **kho kết quả** — C
làm y như vậy (`diag_result_get`, `browseNSLookupResultInst`).

### 34.3 Quirk giữ nguyên

| Quirk | Hệ quả |
|---|---|
| `Timeout`, `NumberOfRepetitions` đi qua `nslookup_set`/`dnslookup_set` | **không kiểm gì** (kiểu unsignedInt nhưng `abc` vẫn được lưu) và **không dừng** lookup đang chạy — khác mọi lá ghi được khác |
| `DNSServer` dùng chung kiểm Host với `HostName` | giá trị rỗng ("dùng resolver hệ thống", launcher có hỗ trợ) bị từ chối — đã đặt thì không trả về rỗng được |
| `Interface` nhận tên thiết bị mạng | cùng quy tắc `$(ifconfig $2)` với TraceRoute, nay dùng chung `diag_ifconfig_prints()` |

**Khác biệt có chủ ý:** `Result.{i}` dừng ở 256 instance. Shell chạy `seq 1 $ResultNumberOfEntries`
trên bất cứ giá trị nào kho đang giữ; launcher ghi số dòng `Address` của một lần nslookup.

### 34.4 Kiểm

| Kiểm | Kết quả |
|---|---|
| `check-cc-syntax.py` | lib **37**/0 lỗi (không cảnh báo mới ở file P5) · app 17/0 |
| `check-c-sanity.py` | lib/mtk 37/0 · app 17/0 × 3 SDK |
| `verify-dm-paths --phase 5` | C **42** param, 7 object, **dôi 0**; không tới được 5; thiếu 41 = TR-143 26 + `Layer3Forwarding` 12 + P5e 3 |
| `--claims` | **121** claim, **18** module, 0 cặp chồng |
| bundle | `31b9a874d55a`, 404 file, `apply --dry-run` exit 0 trên `1_src` và `2_src` |

Data model C: **417/783** (417/778 tham số tới được). Build SDK: chưa với `0056`–`0058`; mọi file
đã qua cross-gcc của SDK. Board: chưa.

## 35. P5c — TR-143 `DownloadDiagnostics` + `UploadDiagnostics`, 26 tham số

Nguồn: `functions/tr143/{download,upload}_diagnostic`,
`functions/common/{Download,Upload}Diagnostics_launch`. Code: `sdk/mtk/dm098/tr143diag_mtk.c`.
Kho: `-P /var/state/downloadDiag`, `-P /var/state/uploadDiag`. Patch `0059`.

### 35.1 TR-143 dừng theo cách khác hẳn (Verified)

Các diagnostic khác giết launcher ngay trong setter (`pgrep -f | kill -9`). TR-143 thì **không**:

```
downloadDiag_stop_diagnostic():
    [ DiagnosticsState == Requested ] && queue "/bin/sh .../DownloadDiagnostics_launch stop"
```

Launcher `stop` giết mọi tiến trình `DownloadDiagnostics` đang chạy (trừ chính nó) rồi reset state
và kết quả. Vì vậy **thứ tự hàng đợi apply-service quyết định cái gì chạy sau phiên**. Ví dụ một SPV
`{DiagnosticsState=Requested, DownloadURL=...}` xếp `run &` rồi `stop` → test vừa yêu cầu bị giết
ngay khi bắt đầu. Thứ tự ngược lại (`DownloadURL` trước) thì chạy bình thường.

C giữ đúng từng lời gọi ở đúng vị trí shell đặt. Điều kiện để thứ tự trùng: engine áp VALUESET theo
thứ tự tham số của SPV — **Verified**: `add_set_list_tmp()` dùng `list_add_tail`
(`dmtr098.c:716`), VALUESET duyệt `set_list_tmp` FIFO (`dmentry.c:347`).

### 35.2 Lỗi vendor: Upload không bao giờ dừng (Verified)

```sh
uploadDiag_stop_diagnostic() {
	if [ "`$UCI_SET_VARSTATE_UPLOAD easycwmp.@local[0].DiagnosticsState`" == "Requested" ]; then
```

Dùng **set** thay cho get. `uci -q -P ... set x.y.z` không có `=value`: `cli.c` gọi `uci_set()`,
`list.c:702` `UCI_ASSERT(ctx, ptr->value)` → lỗi, không đổi gì, `-q` nuốt thông báo, stdout rỗng.
`"" == "Requested"` không bao giờ đúng → setter của `UploadDiagnostics` **không bao giờ xếp stop**.
Đổi `UploadURL` khi đang upload thì upload cũ cứ chạy tiếp. Giữ nguyên.

### 35.3 Quirk khác, giữ nguyên

| Quirk | Chi tiết |
|---|---|
| URL | chỉ `http://`, `ftp://` (không `https`); giá trị phải **bằng đúng** output của `grep -E -o` → **`""` được chấp nhận** (grep không in gì, `"" == ""`), launcher sau đó báo `Error_InitConnectionFailed` |
| `Interface` | tên thiết bị, quy tắc `$(ifconfig $2)`; đọc ra `""` khi chưa đặt (không phải `default` như các diagnostic khác) |
| `EthernetPriority` | lưu ở option `EthPriority`, 0..7 |
| `TestFileLength` | chỉ kiểm chữ số, không giới hạn trên |
| `*Time` | kiểu string (không phải dateTime), `0000-00-00T00:00:00.000000` trước lần chạy đầu |

`mtk_grep_o()` mới trong `dmmtk.c` mô phỏng `$(echo "$s" | grep -E -o RE)`: từng dòng, mọi lần
khớp không rỗng, nối bằng xuống dòng, cắt newline cuối như command substitution. Regex biên dịch
bằng `regcomp` của musl — **cùng thư viện với busybox grep trên board**, nên các chỗ tối nghĩa của
POSIX (`\]` ngoài ngoặc, `\` trong ngoặc) được xử lý giống hệt.

**Chưa chứng minh được bằng chạy thật:** máy workspace không có gcc host lẫn qemu-aarch64. Danh sách
URL để so trên board (busybox `grep -E -o` với `icwmpd` qua SPV): `http://a.com/x`, `https://a.com`,
`ftp://1.2.3.4:21/f`, `xhttp://a`, `http://a b`, `http://a]`, `http://a.com/<x>`, `""`,
`HTTP://a.com`, `ftp://u:p@h/f;type=i`.

### 35.4 Kiểm

| Kiểm | Kết quả |
|---|---|
| `check-cc-syntax.py` | lib **38**/0 lỗi, không cảnh báo mới · app 17/0 |
| `check-c-sanity.py` | lib/mtk 38/0 · app 17/0 × 3 SDK |
| `verify-dm-paths --phase 5` | C **68** param, 9 object, **dôi 0**; thiếu 15 = `Layer3Forwarding` 12 + P5e 3 |
| `--claims` | **123** claim, **19** module, 0 cặp chồng |
| bundle | `d55c46cc0e27`, 406 file, `apply --dry-run` exit 0 trên `1_src` và `2_src` |

Data model C: **443/783** (443/778 tới được).

## 36. P5d + P5e — `Layer3Forwarding` và object ẩn của root; PHASE 5 ĐÓNG

Patch `0060`. **Phase 5: 83/83, thiếu 0, dôi 0** (cộng 5 lá "không tới được", §34.1).

### 36.1 Path của vendor, không phải TR-098 — và engine phải biết lá cấp container

`functions/tr098/layer3_forwarding` đăng ký `Enable`, `ForwardNumberOfEntries`,
`DefaultConnectionService` ở **`Layer3Forwarding.Forwarding.`** — ngay trên object nhiều
instance, cạnh `Forwarding.{i}.`. TR-098 đặt hai cái sau ở `Layer3Forwarding.`. ACS đã luôn thấy
path của vendor → giữ nguyên.

Engine không diễn đạt được (Verified, `dmtr098.c:278` `dm_browse`): object có `browseinstobj` thì
`continue` sau browse, lá của nó chỉ gắn vào từng instance. Hai cách vá tạm đều hỏng:

| Cách | Vì sao bỏ |
|---|---|
| hai mục `Forwarding` cùng tên | registry gộp object cùng tên (`merge_obj`) → chỉ sống sót khi cha được nối nguyên khối; GPN in object hai lần |
| lá tên có dấu chấm `"Forwarding.Enable"` dưới `Layer3Forwarding` | GPN next-level sai tầng ở cả hai phía (`plugin_leaf_nextlevel_match` xét node chứa lá) |

Sửa: **`DMOBJ.container_leaf`** — thành viên cuối, mọi bộ khởi tạo theo vị trí hiện có để NULL.
`dm_browse` duyệt nó trên node container **trước** các instance, qua cùng `checkleaf` như lá của
object đơn; `merge_entry` gộp nó. `verify-dm-paths.py` đọc trường thứ 11.

### 36.2 Hai setter đường dẫn WAN — busybox thật sự kiểm gì (Verified từ source)

```sh
if [[ ! "$node" =~ ^WAN[A-Za-z0-9_]+$ || ! is_integer "$index" ]]; then return 9007
if [ "$index" -le 1 ] && [[ "$node" != "WANIPConnection" ] || [ "$node" != "WANPPPConnection" ]]; then return 9007
```

busybox 1.33.1 của SDK (`CONFIG_ASH_BASH_COMPAT=y`, `[[` là builtin `testcmd`):

- `ash.c:11822-11829`: trong `[[ ]]`, ash **đưa `&&`/`||` vào test như đối số chữ**, không tách
  thành toán tử shell. (Giả thuyết đầu của tôi — ash tách `||` — **sai**, source bác bỏ.)
- dòng 1: `test` đọc `is_integer` là một chuỗi không rỗng (không gọi hàm), `$index` thừa ra →
  `test_main` (`test.c`) báo "unknown operand", exit **2 với mọi giá trị** → `if` sai → **không
  bao giờ từ chối**. Kiểm regex `^WAN…` là code chết.
- dòng 2: `[[ … ] || [ … ]]` cũng thừa operand → exit 2 → không bao giờ từ chối.

Cái thật sự quyết định:

1. prefix `InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1.`;
2. `awk -F '.' '{ print $(NF-2) "." $(NF-1) }'` — FS một ký tự là **chữ** (`awk.c:1693`), trường âm
   là lỗi và không in gì: node = trường NF-2, index = trường NF-1 (NF=2 thì cả hai là trường đầu);
3. `wan_if = $((index - 1))` — số học ash: `0x` hex, `0` đầu là bát phân, `08` lỗi;
4. entry có `id == wan_if` và `conn_type` = 0 (WANIPConnection) / 2 (WANPPPConnection) / chính
   `node` nếu node khác.

**Not established, xấp xỉ:** ash đọc `index` không phải số như **tên biến** (đệ quy); C đọc là 0
(biến chưa đặt).

### 36.3 `hniwan` không tồn tại

`Forwarding.{i}.Interface` đọc/ghi route WAN qua UCI package **`hniwan`** (`@wan[]`). Quét toàn SDK
(`grep -rIl hniwan`, trừ build/staging/dl): chỉ có chính file shell và **một comment** trong
`hal_unify/src/hal_network.c`. Trên sản phẩm: getter đọc `""` cho route WAN, setter chỉ nhận
`InternetGatewayDevice.LANDevice.`. Port nguyên văn.

### 36.4 Object ẩn của root — P5e

`entry_execute_method_root` là một `case` theo **đường dẫn được hỏi**: toàn cây (`""`,
`InternetGatewayDevice.`) chỉ vào nhánh đầu. `SelfTestDiagnostics.`, `WiFi.`, `FaultMgmt.`,
`BulkData.`, `CaptivePortal.`, `FAP.GPS.`, `User.`… chỉ trả lời khi được hỏi đúng tên — không có
trong GPN/GPV toàn cây, inform, notification.

**`DMOBJ.addressed_only`**: `dm_browse` bỏ qua object trừ khi `in_param` nằm dưới nó. Dùng lại
được cho các object ẩn của P6/P7 (`sdk/mtk/dm098/root_hidden_mtk.c`).

`SelfTestDiagnostics.DiagnosticsState` ghi được nhưng shell không có setter →
`common_set_value_check_param` trả **9008**; engine làm y vậy với lá `DMWRITE` + setter NULL.

## 37. Hợp đồng input của shell — lỗ hổng xuyên suốt P1–P5

Patch `0061`. **Phát hiện khi làm P5e, ảnh hưởng mọi tham số ghi được đã port sang C.**

### 37.1 Shell kiểm gì trước MỌI setter (Verified, `functions/common/common`)

`common_set_value_check_param()`:

1. `is_safe_input "$val"` → 9007 nếu: không dòng nào là ASCII in được; **bất kỳ dòng nào rỗng/toàn
   dấu cách** (nên `""` luôn bị từ chối); bất kỳ dòng nào chứa `# ; & | < > \` $ \ ' "`;
2. tên / quyền: ghi được mà setter rỗng → 9008;
3. theo **kiểu shell**, phân biệt hoa thường: `xsd:unsignedInt` (`-gt -1 && -lt 4294967296`),
   `xsd:int`, `xsd:boolean` (`true/1/false/0`), `xsd:dateTime` (`date -d`),
   `xsd:IPv4Address` (một dotted quad ở đâu đó, `grep -o`), `xsd:IPv6Address` (`is_valid_ipv6`).
   `xsd:Int`, `xsd:unsignedint`, `""` rơi vào `*)` — **không kiểm**.

`is_safe_input` là **bộ lọc chống chèn lệnh** của sản phẩm: giá trị vào UCI rồi vào các script shell
(`hni_wan_reload.sh`, firewall, launcher diagnostic).

### 37.2 C không có lớp này (Verified)

`mparam_set_value` (`dmtr098.c:1556`) gọi thẳng setter; hook MTK `dm_platform_param_method` trả 0
ngay với path native (`dmplatform_mtk.c:609`). → **mọi tham số ghi được port sang C ở P1–P5 nhận giá
trị sản phẩm cũ từ chối**, kể cả `;`, `` ` ``, `$`.

### 37.3 Sửa — một chỗ, cho mọi path native

| Thành phần | Nội dung |
|---|---|
| `sdk/mtk/input_contract_mtk.c` | `mtk_shell_safe_input()` (từng dòng như grep thấy `echo "$v"`), kiểm theo kiểu: số đọc như `[` của busybox (`FEATURE_TEST_64=y`), `is_valid_ipv4`/`is_valid_ipv6` port nguyên văn |
| `sdk/mtk/shelltypes_mtk.h` | **SINH** từ ma trận bằng `gen-shell-types.py`: 186 tham số ghi được có kiểu shell được kiểm (94 boolean, 68 unsignedInt, 9 IPv6, 8 IPv4, 6 int, 1 dateTime) |
| `dm_platform_param_method` | áp ở VALUECHECK; bản compat: chỉ path native (path còn qua shell có bản kiểm của shell); bản all-C: mọi path |

Kiểu C trong `DMLEAF` **không thay được** bảng sinh: shell chỉ kiểm sáu chuỗi chính xác, và kiểm
IPv4/IPv6 mà C ghi là string.

**Not emulated:** `xsd:dateTime` (`busybox date -d`). Tham số ghi được duy nhất,
`ManagementServer.PeriodicInformTime`, là của engine (`tr098/managementserver.c`) và tự kiểm.

Áp cả cho tham số icwmp tự thêm mà sản phẩm cũ không có (phần dôi của P1) — cùng bộ lọc, cùng lý do.

### 37.4 Đính chính §34.3 và §35.3

| Đã viết | Đúng trên sản phẩm (và nay trong C) |
|---|---|
| §34.3: `Timeout`/`NumberOfRepetitions` "không kiểm gì, `abc` vẫn được lưu" | setter không kiểm, nhưng hợp đồng `xsd:unsignedInt` từ chối `abc`; phần "không dừng lookup" vẫn đúng |
| §35.3: URL `""` được chấp nhận | `is_safe_input` từ chối `""` trước; URL có query `?a=1&b=2` hay `#frag` cũng **chưa bao giờ** được nhận |
| §36 (bản code đầu): `Forwarding.Enable` nhận `yes/on/True` | hợp đồng `xsd:boolean` chỉ cho `true/1/false/0` tới setter |
| — | Dest/Mask/Gateway phải có dotted quad (`xsd:IPv4Address`), không phải "chuỗi không rỗng bất kỳ" |

### 37.5 Kiểm

| Kiểm | Kết quả |
|---|---|
| `check-cc-syntax.py` | lib **41**/0 lỗi · app 17/0; thêm compile `dmplatform_mtk.c` + `input_contract_mtk.c` bản **all-C** (không `DM_MTK_SCRIPT_COMPAT`): sạch |
| `check-c-sanity.py` | lib/mtk 41/0 · app 17/0 × 3 SDK |
| `verify-dm-paths` | phase 1–5 thiếu 0; **phase 5 83/83 dôi 0**; 126 claim, 21 module, 0 chồng |
| bundle | `33477b15bf88`, 412 file, `apply --dry-run` exit 0 trên `1_src` và `2_src` |

Data model C: **458/783** (458/778 tới được). Build SDK: chưa với `0056`–`0061`. Board: chưa.
**Chưa chạy thử được** `mtk_shell_safe_input`/`shell_ipv6` (không gcc host, không qemu) — test trên
board: SPV với `a;b`, `""`, `true`/`yes` vào một boolean, `1.2.3.4` vào IPv4.

## 38. Board: `/etc/init.d/icwmpd` treo — `value_monitoring` giữ flock fd 1000

Board HP2236B, 2026-09-26. Hiện tượng: `/etc/init.d/icwmpd restart` không thoát, không có
`icwmp_tr098d`. `S99icwmpd boot` và `icwmpd running` đều đứng ở tiến trình con `flock 1000`.

- **Verified (source):** `procd_lock()` (`package/system/procd/files/procd.sh:48-58`, snapshot
  `src/2025q3`) mở `/var/lock/procd_<service>.lock` trên fd 1000 và `flock` chờ vô hạn.
- **Verified (board):** `/proc/5171/fd/1000 -> /tmp/lock/procd_icwmpd.lock`, pid 5171 là
  `/usr/sbin/value_monitoring 30`, được `start_service` chạy nền bằng `&` và thừa kế fd 1000.
- **Fix:** overlay `9692cf8`, `sdk/mtk/files/icwmpd.init` — chạy `value_monitoring` với
  `</dev/null >/dev/null 2>&1 1000>&-`. Board sửa tay cùng dòng: start chạy hết, `icwmp_tr098d`
  lên (pid 20474), `restart` thoát bình thường. **Chưa vào bundle `33477b15bf88`.**
- Bài học: mọi tiến trình nền do init script procd sinh ra phải đóng fd 1000; tiến trình sống lâu
  nên để procd quản (`procd_open_instance`) thay vì `&`.

## 39. Board: `icwmp_tr098d` chết trước `STARTING ICWMP` — thêm trace khởi động (`0063`, debug)

Sau §38, daemon được procd chạy nhưng không còn sống: `pidof` rỗng, `ubus list` không có
`tr069`, log chỉ có một dòng `sync … log_severity=DEBUG` (dòng này chỉ in khi config đổi, nên các
lần respawn sau không để lại gì). Trước dòng `STARTING ICWMP`, upstream có 4 đường chết **không
log**: khóa `/var/run/icwmpd.pid` bận (`exit 0`), `global_env_init`/`global_conf_init` trả lỗi,
`cwmp_init_backup_session`/`cwmp_root_cause_events` trả lỗi, và crash. `ubus_connect` lỗi trong
thread ubus cũng im lặng.

Overlay `01a1884` (`0063-icwmp-startup-trace-crash-report-debug.patch`):

- `icwmp_boot_trace()`: mỗi bước khởi động ghi ra stderr (procd `stderr 1` → `logread`) và
  **`/tmp/icwmpd_boot.log`** (append, cắt ở 64 KiB), không phụ thuộc `cwmp.cpe.log_*`.
- Handler SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT: `sig`, `addr`, `pc/lr/sp`, `tid`, rồi chép
  `/proc/self/maps` vào file và raise lại (core dump vẫn được). BDK vẫn dùng handler riêng
  (`sdk/bdk/icwmp_bdk.c`), đăng ký sau nên thay handler này.
- Init MTK thêm `procd_set_param stdout 1`.

Cổng: cross-gcc SDK app 17/0 lỗi, `check-c-sanity` app mtk/bdk/uci 0, automake 0. Bundle
`65a09f005ee7` (414 file, gồm `0062` = §38 và `0063`), dry-run exit 0 trên `1_src`, `2_src`.
**Chưa build, chưa chạy trên board.** Đọc kết quả: `debug-commands.md` mục trace khởi động.

## 40. Crash khởi động = `browseAssocInst` duyệt mảng `infor` như object (`0065`)

Trace `0063` trên board: mọi lần respawn chết ở `dm_entry_load_enabled_notify` — lần đầu duyệt
**toàn cây** — với `SIGSEGV addr=0x8`, `pc = lr = libtr098.so + 0x4b810` (giống nhau ở 3 lần).

- **Verified (addr2line trên bản build `1_src`, 2026-09-26 15:51):** `0x4b810` thuộc
  `browseAssocInst` (`sdk/mtk/dm098/wlanassoc_mtk.c`). `objdump`: `bl json_object_get_object`
  rồi `ldr x0, [x0, #8]` — tức `json_object_get_object(infor)->head` của macro
  `json_object_object_foreach`, với kết quả NULL.
- **Verified (source ubusmon):** `get_wlan_device_list` (`tclinux_phoenix/apps/hni/ubusmon/ubus.c`)
  tạo `infor` bằng `blobmsg_open_array` — **mảng** các table, kèm `number_client`. Shell
  (`functions/tr098/lan_device`, `assoc_build_cache`) duyệt bằng `json_get_keys` theo chỉ số và bỏ
  qua khi `number_client` không phải số. Bản port P3 viết như `infor` là object — sai kiểu dữ liệu,
  chưa từng chạy trên board trước hôm nay.
- **Fix (overlay `b068392`):** duyệt mảng theo chỉ số, bỏ phần tử không phải table, kiểm
  `number_client` như shell; `dmjson_get_var()` chặn cùng kiểu lỗi với dòng lạc của shell compat.
- **Lớp kiểm mới** `check-c-sanity.py` → `check_json_foreach`: trong `sdk/`, mọi
  `json_object_object_foreach(X…)` phải kiểm `X` là `json_type_object` trong 40 dòng trước. Chạy
  ngược trên bản lỗi: bắt đúng 2 dòng (81, 142); bản sửa: 0.

Cổng: cross-gcc lib 41/0 lỗi, sanity lib/mtk 0. Bundle `c776a013dcfa` (416 file), dry-run exit 0 trên
`1_src`, `2_src`. **Chưa build, chưa chạy board.** Sau crash này, cây data model mới được duyệt
toàn phần lần đầu — crash tiếp theo (nếu có) sẽ lại hiện trong `/tmp/icwmpd_boot.log`.

**Kết quả board (2026-09-26 16:43, bundle `c776a013dcfa`):** `dm_entry_load_enabled_notify done`,
`ubus object tr069 registered`; `ubus call tr069 status` → `cwmp up`, phiên đầu tới
`http://172.16.0.15:7547` **success** (1 success / 0 failure), phiên định kỳ kế tiếp 19:00:01 +07
(interval 43200 s căn theo `0001-01-01T00:00:00Z` = 12:00 UTC). **Gate board 1 (khởi động + Inform)
ĐẠT.** Chưa kiểm: GPV/SPV từ ACS, Connection Request, hợp đồng input (§37.5).

## 41. `ubus call tr069 dm get` treo 300 s — deadlock `mutex_session_send` ở đường notify (`0066`)

Board 2026-09-27: `ubus -t 300 call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.",…}'`
→ `Request timed out`. Chụp lúc treo: daemon sống, `icwmp_dm.sh` **không có lệnh con**, 11 thread đều
`futex_wait_queue_me` (1 thread `inet_csk_accept`). Log dừng ở `ubus dm get InternetGatewayDevice.`
→ không phải getter treo mà là thread uloop chờ mutex.

- **Verified (source):** `cwmp_add_notification()` (`event.c`, thread handle_notify, được đánh thức
  bởi `ubus call tr069 notify` của `value_monitoring` mỗi 30 s) lock `mutex_session_send` rồi
  `fopen(DM_ENABLED_NOTIFY)`; `NULL` → `return` **không unlock**. Bug upstream icwmp.
- **Verified (source):** `DM_ENABLED_NOTIFY = /etc/tr098/.dm_enabled_notify` (`dmtr098.h:39`);
  không script/Makefile/C nào của MTK tạo `/etc/tr098` → file không bao giờ có → notify đầu tiên
  (~30 s sau start) giữ mutex vĩnh viễn. Hệ quả: `ubus dm` treo, **thread session cũng kẹt** (không
  còn phiên nào sau Inform đầu — phiên 19:00 sẽ không chạy), value change notification chưa từng
  hoạt động trên MTK.
- **Conditional:** trên board, chuỗi trên khớp với mọi quan sát nhưng chưa đọc trực tiếp owner của
  mutex; xác nhận bằng: `ls /etc/tr098` (không có) và hết treo sau fix.
- Phát hiện kèm: `dmplatform_mtk.c` `rename(/tmp/… , /etc/tr098/…)` khác filesystem → `EXDEV`,
  file bị xóa mà không thay.
- **Fix (overlay `b01ec72`):** `event.c` unlock + clean ctx trên đường lỗi (và clean ctx ở
  `cwmp_add_notification_min`); MTK platform init tạo thư mục của `DM_ENABLED_NOTIFY`;
  `dmplatform_mtk.c` copy thay rename.
- **Workaround không cần build:** `mkdir -p /etc/tr098 && /etc/init.d/icwmpd restart`.

Cổng: cross-gcc lib 41/0, app 17/0; sanity lib/mtk, app mtk/bdk/uci 0. Bundle `d6e8e4aedd99`
(417 file), dry-run exit 0 trên `1_src`, `2_src`. **Chưa build, chưa chạy board.**

## 42. Agent sau controller (NAT): icwmpd dùng được không — STUN, `cr_host`, gap của bản port

Câu hỏi 2026-09-27. Snapshot `src_bk/2025q3` + overlay `b01ec72`.

- **Hướng đi ra (Inform, phiên do CPE mở):** HTTP ra ACS qua NAT của controller — không cần gì thêm
  ngoài route/DNS. **Conditional:** init chỉ chạy khi có IP trên `easycwmp.@local[0].network`
  (mặc định `if0`, WAN PPPoE); agent không có `if0` → kẹt "no WAN IP yet". Phải đặt `network` về
  interface có IP trên agent (thường `lan`). Init và `stuncd.init` đều **không chạy khi
  `clay.opermode.mode=auto`** — giá trị opmode trên agent: **Not established**, cần đọc trên board.
- **Hướng đi vào (Connection Request):** ACS không tới được `http://<IP LAN agent>:7547`. Ba đường:
  1. **STUN (TR-069 Annex G)** bằng `stuncd` (`/usr/sbin/stun-client`, config `stun.@stun[0]`) có sẵn
     của sản phẩm. **Verified:** khi nhận UDP CR nó chạy
     `ubus call tr069 inform '{"event":"6 connection request"}'` (`stunclient/src/stun.cxx:306`) —
     icwmpd có method `inform`, `'6'` → `EVENT_IDX_6CONNECTION_REQUEST` (`ubus.c`
     `cwmp_handle_inform`). Khi địa chỉ NAT đổi, nó ghi `stun.@stun[0].udpcontnreqaddr` và gửi
     `inform "4 value change"`.
  2. **Port-forward trên controller + `cwmp.cpe.cr_host`/`cr_port`**: bản MTK đã dùng hai option
     này để dựng `ConnectionRequestURL` (`dmplatform_mtk.c`).
  3. XMPP (Annex K): có source trong icwmp, **không build** (`--enable-icwmp_xmpp`), cần ACS hỗ trợ.
- **Gap của bản port (Verified):** shell gốc (`functions/common/management_server:39-47`) map
  `UDPConnectionRequestAddress`, `STUNEnable/ServerAddress/ServerPort/Username/Password/
  Min/MaximumKeepAlivePeriod`, `NATDetected` vào `stun.@stun[0].*` và đặt cờ
  `/tmp/stunclient_reload_needed`. Bản C (P1) phục vụ `ManagementServer.` bằng module dùng chung
  `tr098/managementserver.c`, đọc `cwmp_stun.stun.*` và bật/tắt `/etc/init.d/icwmp_stund` — config
  và dịch vụ **không có** trên sản phẩm. Hệ quả: ACS đọc `UDPConnectionRequestAddress` rỗng, bật STUN
  từ ACS không có tác dụng. `stuncd` tự chạy theo `stun.@stun[0].stun_enable` thì vẫn nhận CR và
  đánh thức icwmpd được, nhưng ACS không biết địa chỉ UDP để gửi. Cần một module MTK override các
  leaf STUN (chưa làm). Comment đầu `dmplatform_mtk.c` ("ManagementServer.* is served by the
  script") đã cũ.

> **Cập nhật 04/10 (sau đồng bộ dev):** gap data model STUN ở trên đã sửa ở `0078`
> (K2, `managementserver_mtk.c` đọc/ghi `stun.@stun[0]`, host PASS), còn G7 lifecycle `stuncd`.
> Hai điểm init của chế độ agent (network `if0`, cổng `opermode=auto`) chưa có trong K1–K14,
> ghi thành **K15** ở §44.

## 43. Đồng bộ branch dev — 04/10/2026

Baseline `icwmpMultiSdk/dev` **6352623**, clean và khớp origin/dev sau fetch. `main`
4965f5d là ancestor, dev thêm 22 commit (sửa source 0067–0079 + host test/docs).
Review caller/callee và kết quả static/coverage/checksum ở
[dev-sync-review.md](dev-sync-review.md).

P1–P5 vẫn 458 parameter sản phẩm bằng C, thêm 11 leaf icwmp. Source neutral/SDK
registry đã có, resolver model/service contracts/full-C/single-model release
vẫn chưa hoàn tất. PH0–PH8 thay thứ tự A2–A6/P6 trực tiếp của handoff cũ.

Phát hiện bổ sung: **K13** URL validation/defaults side effect và STUN address
validation chưa tương đương shell SDK hiện có; **K14** dateTime chỉ kiểm shape,
calendar/timezone chưa đầy đủ. Consumer flag STUN cũ đã xác định tại xml.c:1232,
gọi restart và xóa flag, khác queue reload của native mới. G7 cần kiểm procd thực tế.

Static/checksum đã tự chạy lại đạt. Host PASS là báo cáo của repo dev, chưa chạy
lại lượt này. Board G1 ở §40 chỉ gắn với 0065. Source SDK vendor/overlay cũ chỉ đọc,
không merge dev/main hoặc đưa dev ngược vào overlay cũ trong lượt đồng bộ.

## 44. Claude Code nhận lại việc sau đồng bộ dev — xác minh và bổ sung (04/10/2026)

Xác minh bằng lệnh, không dựa vào handoff:

- `icwmpMultiSdk` branch `dev` = `6352623`, worktree sạch; `main` = `4965f5d` = merge-base;
  `main..dev` 22 commit, 13 commit source `0067`–`0079`.
- `git archive main userspace/public` so với overlay cũ `b01ec72` (`git archive HEAD public`):
  `diff -rq` **không khác byte nào** — `main` mang đúng `0034`–`0066`, overlay cũ chỉ còn là lịch sử.
- **Cổng compile SDK lần đầu cho `0067`–`0079`**: `check-cc-syntax.py` (cross-gcc aarch64 gcc 10.2
  musl của `1_src`, `-fsyntax-only` + `-Werror=implicit-function-declaration,int-conversion,
  incompatible-pointer-types,return-type`) trên `icwmpMultiSdk/userspace`: lib/mtk 41 file 0 lỗi,
  app/mtk 17 file 0 lỗi. Đã kiểm module `ccs` trỏ `USERSPACE` = repo dev. Đây **không** phải
  build/link gói — `sdk_build` cho HEAD dev vẫn chưa có.
- `check-c-sanity` lib/mtk 41/0, app mtk/bdk/uci 17/0; `verify-dm-paths --claims` 126 claim,
  21 module, 0 chồng — khớp số Codex báo.

Bổ sung:

- **K15 (MEDIUM, OPEN_SOURCE_VERIFIED)** — chế độ agent/AP sau controller.
  `sdk/mtk/files/icwmpd.init` trên dev: `:125` không chạy khi `clay.opermode.mode=auto`;
  `:139-140` network mặc định `if0`; `:167` không có IP → chỉ chạy `wan_interface_up` và chờ.
  `stunclient/files/stuncd.init:26` có cùng cổng `opmode != auto` (khác nhau khi opmode rỗng, §45). Inform đi ra qua NAT controller
  được khi `easycwmp.@local[0].network` trỏ interface có IP (thường `lan`); Connection Request đi
  vào cần STUN (K2/G7) hoặc port-forward trên controller + `cwmp.cpe.cr_host`/`cr_port`.
  Giá trị `opermode` thật trên agent: **Not established** — đọc trên board. Cần quyết định sản
  phẩm: init tự chọn network theo opmode hay để cấu hình.
- Đã thêm K15 và `validation.sdk_syntax` vào `implementation-status.json` của workspace.
  **Chưa ghi vào `icwmpMultiSdk/docs/plan/sync-main-dev.md`** — repo có remote và đang được phát
  triển trên `origin/dev`, commit local sẽ lệch nhánh; chờ người dùng quyết định nơi ghi.


## 45. Cổng `opermode` của `icwmpd.init` và `stuncd.init` — giống ở đâu, khác ở đâu (K15)

Snapshot: `icwmpMultiSdk/dev` `6352623` (`sdk/mtk/files/icwmpd.init`), `stunclient/files/stuncd.init`
md5 `728c081c200f` giống nhau ở `src_bk` và `1_src`, busybox 1.33.1 của `1_src/build_dir`.

| Script | Dòng | Điều kiện |
|---|---|---|
| [icwmpd.init:125](../../userspace/public/apps/icwmp/icwmp/sdk/mtk/files/icwmpd.init#L125) | `:125` | `[ "$opmode" = "auto" ] && return` — biến **có nháy** |
| [stuncd.init:26](../../src/2025q3/tclinux_phoenix/apps/hni/stunclient/files/stuncd.init#L26) | `:26` | `if [ $stun_enable == "1" ] && [ $opmode != "auto" ]` — biến **không nháy** |

**Verified (busybox `coreutils/test.c`):** biến rỗng không nháy làm mất một đối số. `[ != auto ]` còn
hai đối số `!=` `auto`; không rơi vào nhánh 3 đối số, `primary()` đi tới `check_emptiness:` (`:890`)
trả true cho chuỗi `!=`, rồi `test_main` thấy còn `auto` dư → `"auto: unknown operand"` và
`res = 2` (`:1016-1024`). Exit 2 = false. Tương tự `[ == 1 ]` khi `stun_enable` rỗng.

| `clay.opermode.mode` | icwmpd | stuncd (`stun_enable=1`) |
|---|---|---|
| `auto` | không chạy | không chạy — **giống**, nhất quán: không có TR-069 nửa vời |
| `router` / `ap` / giá trị khác | chạy (nếu `enable`, `EnableCWMP`) | chạy |
| rỗng / option không có | **chạy** | **không chạy**, in `[: auto: unknown operand` — **khác** |

Hệ quả khi hai cổng **khác** nhau:

1. Opmode rỗng: icwmpd chạy, Inform ra được, nhưng không có STUN → ACS không gửi được UDP
   Connection Request; data model vẫn đọc `STUNEnable=1` từ `stun.@stun[0]` (0078) trong khi
   `stuncd` không chạy.
2. Khi sửa K15 cho icwmpd chạy ở agent mà **chỉ sửa `icwmpd.init`**: agent Inform được nhưng sau NAT
   controller không nhận Connection Request qua STUN. **Conditional:** `stuncd.init:23` ghi
   `udpcontnreqaddr=<easycwmp.@local[0].ip>:<port>` (không commit) **trước** cổng `:26`, nên
   `UDPConnectionRequestAddress` có thể mang địa chỉ LAN dù `stuncd` không chạy — phụ thuộc libuci
   của icwmpd có đọc delta `/tmp/.uci` hay không, chưa kiểm trên board.

Quy tắc khi sửa K15: đổi cổng **ở cả hai script cùng lúc** và bọc nháy biến. `stuncd.init` là file
vendor của gói `stunclient`, **không nằm trong repo icwmpMultiSdk** — phải chọn: apply sửa file vendor
(như hai feed Makefile) hoặc để `icwmpd.init` điều khiển `stuncd` theo opmode. Quyết định sản phẩm.

## 46. STUN cho thiết bị agent: viết trong icwmp hay dùng `stunclient` của vendor (04/10/2026)

Câu hỏi của user (chatlog mục 60). Snapshot `icwmpMultiSdk/dev` `77c9207`, `src_bk/2025q3`.

**Plan hiện có:** chưa có plan cho STUN ở agent. Thiết kế v2 chỉ có cờ `ICWMP_STUN` và hợp đồng
`stun_get_state/validate/stage` (PH3); PH0 "0.6 STUN parity" giữ `stuncd`, đã làm ở 0078. K15 (§44,
§45) là lần đầu agent được ghi thành việc.

**Sản phẩm đã có đường TR-069 cho thiết bị con — ở `opermode=ap`, không phải `auto` (Verified):**
[hmx_network.c `ap_mode_update`](../../src/2025q3/tclinux_phoenix/apps/hni/svcboot/hmx_network.c#L80)
xóa mọi WAN và tạo WAN bridge giả `BRIDGE` (VLAN 999) "on the sub-router ... so the ACS reports it as
the device MAC"; `svcboot_update_opermode` không làm gì cho `OPERMODE_AUTO`
([:124](../../src/2025q3/tclinux_phoenix/apps/hni/svcboot/hmx_network.c#L124)); data model báo
`ExternalIPAddress` bằng IP LAN khi `ap` (§18.3). `auto` bị cả icwmpd lẫn stuncd tắt có chủ ý.
**Not established:** agent mesh thật chạy `ap` hay `auto`; ở `ap`, `easycwmp.@local[0].network`
(`if0` = WAN bridge giả) có IP không — không thấy code nào đổi network theo opmode.

| Tiêu chí | `stunclient` vendor (`stuncd`) | `icwmp_stund` (có sẵn trong repo) |
|---|---|---|
| Trạng thái | trong image: `CONFIG_PACKAGE_stunclient=y`, `/usr/sbin/stun-client` | có source, **không build** (`--enable-icwmp_stun` không có trong feed Makefile) |
| Code | Vovida stund 0.96 + sửa HNI, C++ (uClibc++), [stun.cxx](../../src/2025q3/tclinux_phoenix/apps/hni/stunclient/src/stun.cxx) 2733 dòng | C, [stun/](../../userspace/public/apps/icwmp/icwmp/stun/stun.c) ~1400 dòng, uloop/ubus/uci/openssl |
| Annex G binding | `CONNECTION-REQUEST-BINDING`, `BINDING-CHANGE` ([stun.cxx:1906-1920](../../src/2025q3/tclinux_phoenix/apps/hni/stunclient/src/stun.cxx#L1906)) | có, `USERNAME` + `MESSAGE-INTEGRITY` ([stun.c:287](../../userspace/public/apps/icwmp/icwmp/stun/stun.c#L287)) |
| Kiểm chữ ký UDP CR | HMAC-SHA1 với **`stun.@stun[0].username/password`** ([stun.cxx:284-305](../../src/2025q3/tclinux_phoenix/apps/hni/stunclient/src/stun.cxx#L284)) — TR-069 dùng ConnectionRequestUsername/Password; khác chuẩn nhưng là hành vi sản phẩm đang chạy với ACS (TATAPF-219) | đọc `cwmp.cpe.password` ([stun.c:439](../../userspace/public/apps/icwmp/icwmp/stun/stun.c#L439)) nhưng icwmp lưu **`cwmp.cpe.passwd`** ([cwmp.h:69](../../userspace/public/apps/icwmp/icwmp/inc/cwmp.h#L69)) → mật khẩu rỗng → **bỏ qua kiểm chữ ký, nhận mọi UDP CR** (K16) |
| Chống replay | chỉ từ chối khi cùng `id` và `ts` cũ hơn ([stun.cxx:277](../../src/2025q3/tclinux_phoenix/apps/hni/stunclient/src/stun.cxx#L277)) | `id` khác lần trước **và** `ts` mới hơn ([stun.c:409](../../userspace/public/apps/icwmp/icwmp/stun/stun.c#L409)) |
| Đánh thức icwmpd | `system("ubus call tr069 inform ... 6 ...")` | ubus API, retry 1 s ([stun.c:104](../../userspace/public/apps/icwmp/icwmp/stun/stun.c#L104)) |
| Tích hợp sản phẩm | `stun.@stun[0]` (0078 đã map), hal_gateway restart 3 chỗ, ubusmon reload khi interface up, `wan_interface_up` | `cwmp_stun` riêng, không ai trong sản phẩm gọi; phải đổi map 0078 hoặc sửa daemon đọc `stun.@stun[0]` |
| Đa SDK | chỉ MTK (`HalConfig`, hal_unify) | mọi nơi có uci/ubus |
| Agent | cổng `opmode` ở `stuncd.init:26` (K15) | init riêng, không cổng opmode |

**Đánh giá:**

1. **Không viết STUN trong process icwmpd.** Lợi duy nhất là chung vòng đời (một cổng thay vì hai),
   đạt được rẻ hơn bằng init script. Cái giá: thêm socket/timer vào thread uloop đang phục vụ ubus,
   lỗi STUN kéo sập agent, đi ngược upstream (tách daemon).
2. **MTK: giữ `stunclient` vendor làm backend.** Đã trong image, đã chạy với ACS operator, 0078 đã
   map data model. Việc của agent không nằm trong giao thức STUN mà ở **khởi động**: chọn opmode, IP
   cho Connection Request, cổng `opmode` hai script (K15) — vài dòng shell + quyết định sản phẩm.
3. **`icwmp_stund` để dành cho SDK không có STUN** (BDK/uci) sau PH3 (`stun_*` contract), và phải
   sửa K16 trước khi bật. Chuyển MTK sang nó chỉ đáng khi `stunclient` có lỗi chặn được chứng minh
   trên board (vd. HMAC không khớp ACS, không bind được IP agent).
4. Câu hỏi sản phẩm phải chốt trước code: agent có được ACS quản trực tiếp không (vendor tắt ở
   `auto`); chữ ký UDP CR dùng credential STUN (như hiện tại) hay ConnectionRequest (chuẩn).

**Board để chốt (agent thật):** `uci get clay.opermode.mode`, `uci get easycwmp.@local[0].network`,
`ubus call network.interface.$(uci get easycwmp.@local[0].network) status`, `ip -4 addr show br-lan`,
`pidof stun-client icwmp_tr098d`, `uci show stun`, `logread | grep -i stun`.

## 47. Board agent 05/10: `opermode=ap`, network `lan` — K15 thu hẹp lại

Output board: [logs/20261005_agent_opmode_stun_board.txt](../../logs/20261005_agent_opmode_stun_board.txt).

| Quan sát | Giá trị | Ý nghĩa |
|---|---|---|
| `clay.opermode.mode` | `ap` | **Verified:** agent này chạy `ap`, không phải `auto` → qua cổng [icwmpd.init:125](../../userspace/public/apps/icwmp/icwmp/sdk/mtk/files/icwmpd.init#L125) và cổng opmode của [stuncd.init:26](../../src/2025q3/tclinux_phoenix/apps/hni/stunclient/files/stuncd.init#L26). Cổng `auto` của K15 không chặn agent ở `ap` |
| `easycwmp.@local[0].network` | `lan` | IP `192.168.1.104/24` trên `br-lan` → icwmpd có IP để chạy. **Conditional:** sản phẩm chỉ ghi `if<id>`/`notused` ([hal_gateway.c:2128 `HalGateway_setInterfaceCwmp`](../../src/2025q3/tclinux_phoenix/apps/hni/hal_unify/src/hal_gateway.c#L2128)), config mặc định `if0` ([config/easycwmp:6](../../src/2025q3/tclinux_phoenix/apps/hni/cwmpclient/ext/openwrt/config/easycwmp#L6)) → `lan` gần như chắc là đặt tay (hướng dẫn 27/09). Ở `ap` thuần sản phẩm, network là `if0` = WAN bridge giả (`ap_mode_update`), có IP hay không: **Not established** |
| `pidof stun-client icwmp_tr098d` | một pid `12711` | **Conditional:** gần chắc là icwmpd, vì `stun_enable=0` → `stuncd.init:26` false ở vế `stun_enable` |
| `stun.@stun[0]` | `stun_enable=0`, `serveraddress=0.0.0.0` | STUN chưa cấu hình trên agent này |
| `udpcontnreqaddr` | `192.168.1.104:3478` | **Verified trên board:** giá trị do [stuncd.init:23](../../src/2025q3/tclinux_phoenix/apps/hni/stunclient/files/stuncd.init#L23) ghi **trước** cổng `:26`, có cả khi `stuncd` không chạy → `UDPConnectionRequestAddress` (0078 đọc option này, active notify) báo địa chỉ LAN riêng. Giống hành vi shell cũ (cùng option), không phải lỗi mới của port |

Hệ quả cho K15: với agent `ap`, phần còn lại là (1) network cho icwmpd — sản phẩm không tự đặt `lan`,
(2) Connection Request vào từ ACS — STUN chưa bật. Bước kế tiếp là **G7 trên chính agent này**: bật
STUN qua data model (0078) với STUN server của ACS, kiểm `stuncd` khởi động, binding, và UDP CR.

## 48. Board MTK gate PH0 lần 1 (05/10 21:01) — kết quả và K17 (`0080`)

Build: image từ source `dev` `6352623` (PH0.1 MTK PASS, runbook §0.1). Log:
[tunv_log/USB5_2236B_2026-10-05_21-01-23.log](tunv_log/USB5_2236B_2026-10-05_21-01-23.log).

| Gate | Kết quả | Bằng chứng (dòng log) |
|---|---|---|
| G1 | **PASS** | `:3-17` boot log đi hết tới `ubus object tr069 registered`, không `CRASH` trong 15 dòng cuối; `:20` `status up`; `:40` `/etc/tr098/.dm_enabled_notify` 575 byte (0066). Chưa đếm `CRASH` trên cả file |
| G2 | **PASS phần định kỳ**, CR NOT RUN | `:35-37` 6/6 phiên success, 0 failure với chu kỳ 120 s; `:69` đúng 1 `icwmp_dm.sh`. `:70` "1 zombie" là chính dòng `grep` (lệnh runbook sai, đã sửa) |
| G3 | **PASS phần `dm`/notify**, value change NOT RUN | `:76` 16 lần notify; `:80-87` GPV toàn cây `fault 0`, **1768** tham số, **15,62 s** (trước 0066 treo 300 s, §41) |
| G4 | **PASS GPV từng nhánh**, phần ACS NOT RUN | `:132-137` 6 nhánh `rc=0`, ≤1 s mỗi nhánh (LANDevice 803 dòng) |
| G5 | **PASS** chuỗi/boolean/unsignedInt; IPv4 NOT RUN | `:152-159` `''`, khoảng trắng, `;&\|`, `` ` ``, `$` → 9007, `g5ok` OK; `:161-165` `yes`/`on` → 9007, `true/0/1` OK; `:192-195` `abc`/`-1`/`4294967296` → 9007, `43200` OK |
| G6 | phần mirror PASS (đường `tr069 dm`), đường ACS + reboot NOT RUN | `:214-216` `easycwmp` và `cwmp` cùng 43200 |
| G7 | hoãn (agent, §47) | — |
| G8, G9 | NOT RUN | — |

**K17 — lỗi mới do board bắt được (Verified chuỗi source, khớp board):** `:163-174` ngay sau
`set PeriodicInformEnable` (`reloaded: true`), 4 lệnh ubus trả `Command failed: Not found` → object
`tr069` tạm mất; boot log `:3` có start `-g` lúc 20:54:36. Chuỗi: setter ManagementServer (0078) ghi
`easycwmp` → engine commit trong process ([dmentry.c:378](../../userspace/public/libs/libicwmp_dm/src/dmentry.c#L378))
→ [`dm_platform_restart_services`](../../userspace/public/libs/libicwmp_dm/src/sdk/mtk/dmplatform_mtk.c#L1059)
chạy thêm `ubus call uci commit easycwmp` → rpcd phát `config.change`
([uci.c:1350](/home/nvtu/workspace/openwrt/1_src/2025q3/openwrt-21.02/openwrt-21.02.1_dev/build_dir/target-aarch64_cortex-a53_musl/rpcd-2021-03-11-ccb75178/uci.c#L1350))
→ reload trigger [icwmpd.init:220](../../userspace/public/apps/icwmp/icwmp/sdk/mtk/files/icwmpd.init#L220)
→ `reload_service` stop rồi start `-g`. Shell cũ **bỏ** `easycwmp` đúng chỗ này
([common:218,224](../../src/2025q3/tclinux_phoenix/apps/hni/cwmpclient/ext/openwrt/scripts/functions/common/common#L218))
và bật `config_load` ([:189](../../src/2025q3/tclinux_phoenix/apps/hni/cwmpclient/ext/openwrt/scripts/functions/common/common#L189))
→ lỗi của bản port, không phải kế thừa. Hệ quả: mỗi SPV ManagementServer (ACS hay `tr069 dm`) làm
daemon bị kill/start lại ~1 s sau phiên, thêm một phiên GetRPCMethods, mất ubus trong lúc đó.

**Sửa `0080`** (repo `dev` `311ad55`): bỏ `easycwmp` khỏi vòng `ubus call uci commit` — dữ liệu đã
commit bằng `dmuci_commit()`, reload trong process bằng `END_SESSION_RELOAD`. Trong rootfs chỉ
`/etc/init.d/icwmpd` có trigger trên `easycwmp`. Cổng: cross-gcc lib/mtk 41/0, sanity 0, claims 0,
automake 0, sums `--staged`. **Không có test host** (harness không có rpcd/procd). **Chưa build, chưa
board** — kiểm bằng G5 lần 2: không còn `Not found`, không có dòng `==== start` mới.

Lỗi quy trình của runbook (đã sửa): lẫn câu hướng dẫn vào ô lệnh (`đổi: not found`, `từ: not found`),
lệnh đếm zombie tự đếm chính nó.

## 49. Claude Code kiểm trực tiếp board MTK qua SSH (05/10 21:16–21:25)

User cho SSH từ máy build (`Dell-Slim`, cổng `enx…` `192.168.1.141` → board `192.168.1.1`). Không lưu
mật khẩu ở đâu: `SSH_ASKPASS` đọc biến môi trường, control socket đóng khi xong; credential CR/STUN
đọc từ board và đưa thẳng vào HMAC/curl, không in. Image vẫn là source `6352623` (chưa có 0080):
`libtr098.so.3.0.0` md5 `35534133…`, `icwmp_tr098d` `0a7065f9…` = gói build 19:49.

| Kiểm | Kết quả |
|---|---|
| CRASH toàn boot log | **0** |
| K17 trên board | 6 lần start: 40 s `-g`, 60 s `-b` (boot), 2814 s, 2972 s, **3551,3 s**, **3575,0 s** `-g`. Boot = 20:54:36 − 2972,585 s = 20:05:03 → hai lần cuối là **21:04:14,7** và **21:04:38,4**, đúng ngay sau `set PeriodicInformEnable` (21:04:13, kèm `Not found`) và `set PeriodicInformInterval` (21:04:37). **Verified** |
| Tiến trình | đúng 1 `icwmp_tr098d`, 1 `icwmp_dm.sh`, 1 `value_monitoring`, 0 zombie — không sót con sau 4 lần restart |
| **G3 value change** | **PASS**: `uci set easycwmp.@local[0].provisioning_code=g3test` (CLI, không `config.change`) lúc 21:18:11 → Inform `4 VALUE CHANGE` lúc 21:18:39 mang `DeviceInfo.ProvisioningCode=g3test`, phiên success, không restart. Đã trả về `g5ok` |
| **G2 Connection Request HTTP** | **PASS**: `curl --digest` tới `192.168.1.1:7547/` → HTTP 200, phiên `6 CONNECTION REQUEST` success (21:24:34); sai mật khẩu / không auth → 401, không có phiên |
| **G7 phần đọc (board router)** | **PASS**: STUN đang bật trên board router, `stun-client 172.16.0.15 -p 19302 -i 30.1.1.153`; binding học được `172.16.0.78:19302`, `natdetect=1`; GPV `ManagementServer.` (0078) trả `STUNEnable=1`, `STUNServerAddress=172.16.0.15`, `STUNServerPort=19302`, `STUNUsername=itms`, `NATDetected=1`, `UDPConnectionRequestAddress=172.16.0.78:19302` — khớp `stun.@stun[0]` |
| **G7 UDP CR (đánh thức)** | **PASS phía CPE**: gói `GET …?ts&id&un&cn&sig HTTP/1.1` ký HMAC-SHA1 bằng credential `stun.@stun[0]`, gửi từ chính board (lua `nixio`) tới socket `30.1.1.153:19302` → `stun-client` → `ubus call tr069 inform 6` → phiên `6 CONNECTION REQUEST` success trong cùng giây (21:23:32). Gói **sai chữ ký** → không có phiên. **Chưa test**: ACS gửi UDP CR qua NAT tới `172.16.0.78:19302` |
| Init script trên board | **Phát hiện:** `/overlay/upper/etc/init.d/icwmpd` (sửa tay 26/09) che `/rom/etc/init.d/icwmpd` của image (`37c11abd…`): khác comment và thiếu `procd_set_param stdout 1`; dòng `1000>&-` vẫn có. Không ảnh hưởng chức năng nhưng mọi sửa init sau này sẽ **không có hiệu lực** trên board này tới khi bỏ bản overlay |

Ghi chú môi trường: máy build route `30.1.1.0/24` qua `enp1s0` (mạng khác), nên gói gửi từ máy build
tới IP WAN của board không qua board — phải gửi từ chính board.

Còn lại cho PH0: build + flash 0080 rồi chạy lại G5 (không `Not found`, không start mới), G4/G6 phần
ACS + reboot, G5 IPv4, G8, G9; G7 UDP CR từ ACS qua NAT.

## 50. Build 0080 trên cây `1_src/2025q3`, image dev-access, và đường nạp FW của WebUI (05/10 21:40–)

User cho Claude Code apply + build trong cây build (chatlog mục 65), và chốt quy tắc (mục 66): thay đổi
ngoài repo `icwmpMultiSdk` phải làm ở **project src** (`src/2025q3` = `src_bk/2025q3`, git) → patch →
`git apply` vào cây build, không tạo/sửa thẳng trong cây build.

**Apply + build** (tmux `tunv1` → docker `nvtu-openwrt`; log `1_src/2025q3/.icwmp-build-logs/`):

| Bước | Kết quả |
|---|---|
| So repo `dev` `0ff05dc` với cây build trước apply | chỉ khác `libicwmp_dm/sdk/mtk/dmplatform_mtk.c` (0080); `icwmp_tr098` giống hệt |
| `apply.py --sdk mtk` | backup `.icwmp-backups/20261005-214332-qxnbgifs`, `Applied: libicwmp_dm`; sau apply `diff -rq` 0 file, 2 feed Makefile cùng nội dung |
| `make package/libtr098/{clean,compile}` + `icwmp_tr098` | `ICWMP_BUILD_RC=0`; `libtr098_3` 21:44:53, `icwmp_tr098_3-2` 21:45:16 (`20261005-pkg-0080.log`) |
| Binary | `libtr098.so.3.0.0` md5 **`3f6265bf…`** (bản trên board: `35534133…`); `icwmp_tr098d` vẫn `0a7065f9…` (0080 chỉ sửa lib). `root-airoha` cùng md5 với gói |
| Image lần 1 `make -j16 MSDK=1` | `ICWMP_IMAGE_RC=0`, `tclinux.bin` 21:55:28, md5 `762da99c…`, **không** có dev-access (`20261005-image-0080.log`) |
| Image lần 2, sau khi apply dev-access | `ICWMP_IMAGE_RC=0`, log có `Enabling dev_access`; `tclinux.bin` **22:02:37**, md5 **`13e99856…`**, model `HP-2236B`, magic `d00dfeed`. `unsquashfs -l` của `root.squashfs` 22:02:15 có `etc/init.d/dev_access`, `etc/rc.d/S11dev_access`, `usr/lib/libtr098.so.3.0.0` (md5 trong `root-airoha` = `3f6265bf…`) (`20261005-image-0080-devaccess.log`). **Đây là image để nạp** |

Kết luận: **SDK_BUILD_PASS (MTK)** cho source `dev` `0ff05dc` (code `311ad55`, 0080). Board chưa chạy.

**Image dev-access** (v0 `0001-`, nay ở [patches/20261005_board_dev_access_feature](../../patches/20261005_board_dev_access_feature/README.md)). Init script `S11dev_access` bật
`account.ssh` và `account.telnet` với account cố định. Nó cũng đổi hai rule `firewall_clay`
`AIS_Default_SSH` và `AIS_Default_TELNET` (`action block`, `interface all`) sang `interface wan`, nên
LAN mở còn WAN vẫn bị chặn. Rule được tìm theo tên, vì chỉ số `@packetfilter[33]` user dùng không khớp
file mặc định. Ở file mặc định, `[33]` là `AIS_Default_ICMP`; SSH là `[45]` và `[46]`. Chuỗi xử lý:

- `account` S50 ([init.d/account](../../src/2025q3/airoha_feeds/airoha_build/profile/HP2236B/target/linux/airoha/an7583/base-files/etc/init.d/account))
  tạo user uid 0 cùng hash trong `/etc/shadow`, và ghi `/etc/tty_pwd` cho telnet. **Verified** (source).
- Packet filter thành iptables `HMX_PACKET_FILTER`: interface `wan` cho ra PPP + PON, `all` cho thêm
  LAN ([hal_security.c:1339-1354](../../src/2025q3/tclinux_phoenix/apps/hni/hal_unify/src/hal_security.c#L1339-L1354)).
  **Verified** (source).
- WebUI login so với UCI `account.admin` và `account.root`, không đọc `/etc/shadow` (`verifyAdmin`,
  `verifyRoot` trong FalGateway.cpp). **Verified** theo tìm kiếm: backend và hal_unify không có
  `getspnam`, `/etc/shadow`.
- Chạy thật trên board: **Not established**. Máy build không có `uci` host, cũng không có qemu aarch64.

**Đường nạp FW của WebUI** (để trả lời "Claude tự nạp có giống WebUI không"):

1. [HalGateway_doFirmwareUpdate](../../src/2025q3/tclinux_phoenix/apps/hni/hal_unify/src/hal_gateway.c#L1375)
   nhận file ở `/var/tmp/tclinux.bin` ([hal_gateway.h:37](../../src/2025q3/tclinux_phoenix/apps/hni/hal_unify/include/hal_gateway.h#L37)).
2. [isFirmwareValid](../../src/2025q3/tclinux_phoenix/apps/hni/hal_unify/src/hal_gateway.c#L454) chạy hai kiểm tra:
   - [hni_validate_image.sh](../../src/2025q3/airoha_feeds/target/linux/airoha/base-files/userfs/bin/hni_validate_image.sh)
     so 16 byte model ở offset 10 MiB − 64 với `/etc/fw_info/model_name`. Image này có `HP-2236B`.
   - `/usr/libexec/validate_firmware_image` phải trả JSON `valid=true`.
3. Gọi `/sbin/sysupgrade /var/tmp/tclinux.bin` ([hal_gateway.c:1401](../../src/2025q3/tclinux_phoenix/apps/hni/hal_unify/src/hal_gateway.c#L1401)).
4. [platform_check_image](../../src/2025q3/airoha_feeds/target/linux/airoha/base-files/lib/upgrade/platform.sh#L53):
   magic `d00dfeed` (FIT, đúng với image này) đi tiếp tới `blapi_cmd system image_check`.
5. [default_do_upgrade](../../src/2025q3/openwrt-21.02/openwrt-21.02.1_dev/package/base-files/files/lib/upgrade/common.sh#L405)
   gọi `get_current_partition`:
   - đang chạy bank 1 thì ghi `tclinux`, nếu không thì ghi `tclinux_slave`. Nói cách khác, luôn ghi
     **bank không chạy**;
   - ghi lỗi thì `exit 1` ([:418](../../src/2025q3/openwrt-21.02/openwrt-21.02.1_dev/package/base-files/files/lib/upgrade/common.sh#L418))
     trước khi đổi `bootflag` ([:446](../../src/2025q3/openwrt-21.02/openwrt-21.02.1_dev/package/base-files/files/lib/upgrade/common.sh#L446)).

`common.sh` và `platform.sh` của project src giống hệt bản trong rootfs đã build (`cmp`). Vì vậy
`scp` image vào `/var/tmp/tclinux.bin`, chạy hai script kiểm tra rồi `sysupgrade` là **đúng đường ghi
flash của WebUI**. Chỉ thiếu phần trạng thái UCI (`upgrade_fw_status`, `3rdpartyagent upgrade_state`)
và nhánh `/tmp/maintenance_services`. **Not established**:

- bootloader có tự quay về bank cũ khi bank mới không boot được hay không. Nếu không, phải đổi
  `bootflag` qua console;
- `sysupgrade` giữ cấu hình bằng cơ chế nào. Bằng chứng duy nhất: file overlay `/etc/init.d/icwmpd`
  ngày 26/09 vẫn còn sau lần nạp WebUI 05/10 (§49).

### 50.1 Claude Code nạp image 22:02 qua SSH (05/10 22:07–22:16, user yêu cầu, chatlog mục 67)

Trước khi nạp: board `HP-2236B`, rootfs đang chạy là dm-verity trên `PARTLABEL=filesystem_slave`, ART
bootflag=1, nên `sysupgrade` ghi `kernel` + `filesystem` (bank không chạy) rồi đặt bootflag=0.
Bootflag nằm trong partition `art`, `ecnt_sys` ghi vào đó; WebUI nạp cũng vậy. RAM còn 133 MB,
`/tmp` còn 180 MB. Image cũ trước khi nạp: CRASH 0, 6 dòng `==== start` (không có lần mới sau
21:04), 5 phiên success, 0 failure.

Các bước, đúng đường WebUI:

1. `cat > /var/tmp/tclinux.bin`, md5 hai đầu đều `13e99856…`.
2. `hni_validate_image.sh`: `Model validation successful: HP-2236B`.
3. `validate_firmware_image`: `"valid": true`.
4. `sysupgrade -T` rc=0.
5. `/sbin/sysupgrade /var/tmp/tclinux.bin`:
   - 22:09:24 `Saving config files… Commencing upgrade`;
   - board mất mạng 22:10:01, có ping lại 22:11:11.

Lúc 22:14:34 và 22:16:29: các cổng 80, 443 và 7547 (CR icwmp) mở, còn **22 và 23 đóng**. Vì không có
shell nên **chưa xác minh được image đang chạy** (bank và md5).

**Vì sao dev-access v0 (`0001`, S11) không mở được SSH.** **Verified** từ source:

- `/etc/init.d/svcboot` (S70, procd) chạy `/userfs/bin/svcboot`. Handler đầu tiên
  ([svcboot.c:9](../../src/2025q3/tclinux_phoenix/apps/hni/svcboot/svcboot.c#L9)) là
  [svcboot_default](../../src/2025q3/tclinux_phoenix/apps/hni/svcboot/hmx_services.c#L59), gọi
  `HalSecurity_initAwnSecurity()`.
- Hàm đó gọi `setTelnetUserActive(false)` và `setSshActive(false)`
  ([hal_security.c:2320-2328](../../src/2025q3/tclinux_phoenix/apps/hni/hal_unify/src/hal_security.c#L2320-L2328)).
  Kết quả: `account.telnet.enabled=0` và `account.ssh.enabled=0`, whitelist SSH tắt, xoá rule
  `ACCEPT br+ 22`.

Việc này xảy ra ở **mọi lần boot**, sau S11. Đây cũng là lý do user phải mở lại SSH bằng console sau
mỗi lần reboot. API bật lại của vendor: `ubus call hni setSshAccess '{"enabled":true}'` và
`setTelnetAccess`
([ubusmon/ubus.c](../../src/2025q3/tclinux_phoenix/apps/hni/ubusmon/ubus.c#L1046)). Lưu ý `dropbear`
chỉ có trigger reload trên config `dropbear`, không trên `account`, nên vẫn phải reload dropbear
bằng tay như lệnh console của user.

Hướng sửa (cơ chế khác, sẽ là `1000-`): script chạy lại **sau** khi `svcboot_default` đã chạy. Cụ thể:
chờ `account.ssh.enabled` từ 1 (do S11 đặt) về 0 hoặc tới timeout, rồi chạy chuỗi lệnh console đã
chạy được của user. Phải thử trên board trước khi làm patch.

### 50.2 Image 0080 trên board: G5 lần 2, dev-access v1, nạp lần 2 (05/10 22:20–22:40)

User mở SSH bằng console (chatlog mục 69), rồi yêu cầu: làm patch mở mặc định SSH/telnet, dùng lại được
cho các task khác của project, sau đó kiểm tiếp.

**Image 0080 đang chạy** (lần nạp 1): `libtr098.so` `3f6265bf…`, rootfs `filesystem`, bootflag=0.

**G5 lần 2** (runbook §3), 22:27:53–22:27:55:

| Kiểm | Kết quả |
|---|---|
| Trước gate | CRASH 0, 2 dòng `==== start` (`-g` 41 s, `-b` 61 s), 3 phiên success, 1 `icwmp_dm.sh`, 0 zombie |
| Chuỗi `''`, `'   '`, `a;b`, `a&b`, `a\|b`, `` a`b ``, `a$b` | 9007 cả 7; `g5ok` OK — **PASS** |
| Boolean `yes`, `on` → 9007; `true`, `0`, `1` OK | **PASS** |
| unsignedInt `abc`, `-1`, `4294967296` → 9007; `43200` OK | **PASS** |
| **K17 sau 0080** | 4 lệnh set ManagementServer trả `reloaded: true`. **Không** còn `Command failed: Not found`; pid giữ `10396`; vẫn 2 dòng start; `ubus list tr069` còn. Log chỉ có `deviceid: reading identity …`: daemon đọc lại cấu hình trong tiến trình. **PASS, K17 đã sửa trên board** |
| Mirror | `easycwmp.@acs[0].periodic_interval` = `cwmp.acs.periodic_inform_interval` = 43200; `provisioning_code` = `g5ok` |
| IPv4 | `add Layer3Forwarding.Forwarding.` → **9002**: thiếu `network.routev4Common.max_rules` (board chỉ có `route4_common`). Shell `static_route_add_rule` ([layer3_forwarding:132-136](../../src/2025q3/tclinux_phoenix/apps/hni/cwmpclient/ext/openwrt/scripts/functions/tr098/layer3_forwarding#L132-L136)) cũng trả `E_INTERNAL_ERROR`, nên đây là hành vi giữ nguyên ([layer3forwarding_mtk.c:398-400](../../userspace/public/libs/libicwmp_dm/src/sdk/mtk/dm098/layer3forwarding_mtk.c#L398-L400)), không phải lỗi port. Thay bằng `LANHostConfigManagement.MinAddress`: `1.2.3`, `abc`, `1.2.3.4.5` → 9007, không có `uci changes`. Ghi giá trị đúng: **NOT RUN** (sẽ commit `network` và kích netifd) |

**Dev-access v1** ([patches/20261005_board_dev_access_feature](../../patches/20261005_board_dev_access_feature/README.md),
[guide](../../docs/board_dev_access_guide.md)):

- Trên board, firewall không chặn SSH: chain `HMX_PACKET_FILTER` không tồn tại, svcboot log
  `Failed to append tcp rule for interface br+`. `[33]` là `AIS_Default_ICMP`; rule SSH là `[44]`, `[45]`.
- Thử trước khi làm patch, chạy từ `/tmp`, không ghi overlay:
  - `start`: 22 và 23 LISTEN, login telnet `admin` ra shell root.
  - `boot` cộng `ubus call hni setTelnetAccess/setSshAccess false` (đúng hàm svcboot gọi): watcher log
    `svcboot turned SSH off after 6s`, bật lại cả hai, `admin` login lại được.
  - Hàm HAL ngắt luôn phiên SSH đang mở và chuyển `admin` sang `/bin/false`. Watcher vẫn chạy tiếp.
- Patch `1000-`: gỡ `0001-` rồi apply ở project src trước, cây build sau. Ba bản `dev_access` cùng md5
  `23b1e4c6…`: project src, cây build, bản đã thử.
- Image `tclinux.bin` **22:32:28**, md5 **`49539b28…`**, `ICWMP_IMAGE_RC=0`
  (`20261005-image-0080-devaccess-v1.log`). Squashfs có `dev_access` 1953 byte, `S11dev_access`,
  `libtr098.so.3.0.0`.

**Nạp lần 2** (22:34): kiểm như lần 1, md5 khớp, model đúng, `valid: true`, `-T` đạt. `sysupgrade` ghi
bank slave. Kiểm một lần lúc 22:38:44 (chờ đủ theo lời user dặn):

| Kiểm | Kết quả |
|---|---|
| Bank | rootfs `filesystem_slave`, bootflag=1; `dev_access` `23b1e4c6…`, `libtr098` `3f6265bf…` |
| Boot thật | S50 `Enable admin` → `svcboot_default` → `dev_access: … (boot, svcboot turned SSH off after 16s)` → `SSH is enabled`, `Telnet is enabled` |
| Truy cập | 22 và 23 LISTEN; SSH `admin` vào được; telnet `admin` ra `uid=0(root)`. **Không cần console — PASS** |
| icwmp | CRASH 0, 2 dòng start bình thường, phiên boot success, 1 `icwmp_dm.sh`, 0 zombie |
| Overlay init | `/etc/init.d/icwmpd` vẫn là bản overlay 26/09 (`541750ed…`), khác bản ROM `37c11abd…` — chờ user quyết |

Còn lại cho PH0:
- G4 và G6 phần ACS: GPN, SPV nhiều tham số có rollback, reboot, K10;
- G5 ghi IPv4 đúng (cần route thật);
- G8, G9;
- G7 UDP CR từ ACS qua NAT.

## 51. PH0 còn lại: tự đánh giá được gì, phải test thật cái gì (05/10 23:05)

User hỏi (chatlog mục 70): phần còn lại của PH0 có tự rà soát và xác nhận đạt được không, hay phải test
thật. Cơ sở là [sync-main-dev.md §4–§5](../../docs/plan/sync-main-dev.md): trạng thái **không được
cao hơn bằng chứng thấp nhất**. Gate board chỉ đóng bằng chạy trên board; đọc source hay test host chỉ
dùng để đánh giá rủi ro.

| Hạng mục | Hiện có | Tự xác nhận được? | Cần gì |
|---|---|---|---|
| PH0.1 MTK build | SDK_BUILD_PASS cho 0080 (§50) | xong | — |
| PH0.1 BDK build | user báo build OK | **không**: cây BDK trên máy này chưa build (`targets/MO77300EB/fs.install` không có) | user chạy runbook §0.2 trên máy BDK, gửi output |
| G1, G2, G3, G5 | PASS (§48, §49, §50.2) | xong | G5 ghi IPv4 đúng NOT RUN vì board thiếu `routev4Common.max_rules`, giống shell |
| G4 GPV từng nhánh | PASS | xong | — |
| G4 GPN, SPV qua SOAP | lớp dm đã chạy trên board qua `ubus dm` (cùng engine); lớp SOAP (`xml.c`) không phụ thuộc SDK, host PASS | **không** đủ cho mức BOARD_GATE | một phiên ACS: GPN, SPV mỗi nhánh |
| G4 SPV nhiều param, một param lỗi → rollback | không có ca host riêng cho rollback; chưa chạy board | **không**, và nên test thật: rollback đi qua `dmuci` + end session, đúng vùng 0080 sửa | một SPV 2 param (runbook G4) |
| G6 sau phiên + mirror | PASS qua `ubus dm` (§50.2) | phần reboot: **tự làm được** qua SSH (ghi, reboot, đọc lại); WebUI: không xem được | reboot test (sau G9), user nhìn WebUI |
| K10 PeriodicInformTime | host `ptime` PASS | **tự làm được**: `dm set` PeriodicInformTime + Interval, đọc `next_session` | — |
| G7 | phía router PASS (§49) | UDP CR từ ACS qua NAT: **không** | ACS gửi UDP CR |
| G8 Download/ScheduleDownload sai | host `rpc` 5/5 (cùng code `cwmp.c`/`xml.c`) | **không** cho BOARD_GATE | ACS gửi 2 RPC; hoặc dùng `tests/host/acs.py` trỏ vào board — nhưng đổi ACS URL làm icwmp gửi `0 BOOTSTRAP` ([event.c:794-816](../../userspace/public/apps/icwmp/icwmp/event.c#L794-L816)) cho ACS thật khi trả URL về, nên cần user đồng ý |
| G9 soak 24 h | sampler chạy từ 23:03 (`/tmp/g9.sh`, `/tmp/g9.csv`, mỗi 300 s + một `dm get` toàn cây) | **tự làm được**, mốc 24 h là 06/10 23:03 | không reboot board trong thời gian này |
| K13, K14 | OPEN, cần code + test host (§7.4 của plan) | việc tiếp theo | — |
| K15 | cần quyết định sản phẩm | không | user |

Kết luận: PH0 **chưa đóng được chỉ bằng tự đánh giá**. Ba ca cần SOAP thật từ một ACS (G4 GPN/SPV
rollback, G8, G7 qua NAT), BDK cần output từ máy BDK, và còn K13/K14. Những phần tự làm được (K10, G6
reboot, G9) sẽ làm tiếp. Sau 3h30 (chatlog mục 70), việc tiếp theo là K14 rồi K13 trên `dev` kèm test host.

## 52. 06/10: G9 giữa chừng, K10 trên board, K14 (`0081`) và K13 (`0082`)

Làm tiếp theo hẹn 3h30 (chatlog mục 70) và lượt "làm tiếp" (mục 71).

**G9, đọc lúc 02:33** (sampler chạy từ 05/10 23:03, mỗi 300 s một mẫu kèm một `dm get` toàn cây):

| Cột | min → max (40 mẫu, 3h25) |
|---|---|
| pid `icwmp_tr098d` | 10407 (không đổi) |
| VmRSS | 5344 → 5344 kB |
| fd / thread | 14–15 / 11 |
| VmRSS `icwmp_dm.sh` | 3924 → 3928 kB |
| MemAvailable | 133 788 – 134 824 kB, không giảm dần |
| `dm get` toàn cây | 15–16 s, 1740 dòng mỗi lần |

Phẳng. Lưu ý: trong cửa sổ này **không có phiên ACS** (chu kỳ 12 h), nên mới thử đường dm/ubus, chưa
thử đường session. Sampler vẫn chạy tới mốc 24 h.

**K10 trên board, PASS.** Lúc 06:10:31 `dm set` PeriodicInformTime=`2026-10-05T23:20:31Z` và
Interval=3600:
- `next_session` = 06:20:31+07, đúng mốc;
- phiên định kỳ chạy lúc 06:20:31, success (GenieACS 1.2.9 trả 204);
- `next_session` sau đó = 07:20:31;
- không restart (pid giữ, 2 dòng start).

Đã trả giá trị gốc `0001-01-01T00:00:00Z` / 43200.

**Shell gốc kiểm `xsd:dateTime` thế nào.** [common:842](../../src/2025q3/tclinux_phoenix/apps/hni/cwmpclient/ext/openwrt/scripts/functions/common/common#L842)
chạy `date -d` của busybox sau khi đổi `T` thành dấu cách; riêng đúng chuỗi unknown time được nhận.
Thử trên board:

| Giá trị | busybox `date -d` |
|---|---|
| có hậu tố `Z`, mọi trường hợp | **từ chối**, kể cả `2026-10-05T23:20:31Z` và `2024-02-29T00:00:00Z` |
| không có zone, `+07:00` | nhận (`+07:00` còn bị đọc sai thành `23:20:00`) |
| `+25:00` | **nhận** |
| ngày hoặc giờ sai | từ chối |

Shell sai ở cả hai chiều, nên K14 kiểm theo đúng `xsd:dateTime` chứ không chép shell.

**`0081` K14** (`5b57deb`):
- `ms_valid_datetime` (setter) và `periodic_time_value` (reader của icwmpd) kiểm lịch: tháng, ngày theo
  tháng và năm nhuận, hh ≤ 23, mm/ss ≤ 59, zone `Z` hoặc `±hh:mm` ≤ 14:00, không có gì phía sau.
- Setter sai trả 9007 và giữ giá trị cũ; reader sai trả 0 (không căn mốc).
- Hai hàm, tách khỏi source và build bằng gcc 7.5 trong container: 31 case đúng, epoch khớp `fromisoformat`
  của Python.
- `run.sh ptime` có thêm 9 ca SPV (7 bị từ chối, 2 được nhận).
- Build SDK hai gói đạt lúc 06:18.

**`0082` K13** (`0add366`):
- URL: `ms_valid_url` theo `grep "[a-zA-Z0-9_]://.*"`.
- STUNServerAddress: `mtk_shell_valid_host` = bản dịch mới của `is_valid_domain` + `is_valid_ipv4`/`ipv6`
  (đã có).
- **Đối chiếu với chính hàm shell trên board: 35/35 giống**, gồm 27 host và 8 URL, kể cả các điểm lạ của
  shell: `01.2.3.4` và `1.2.3.a` được nhận, `a_b.example.net` bị từ chối, tên có dấu chấm cuối được nhận.
- Side effect ghi `/usr/share/easycwmp/defaults` **không port**. Chạy đúng lệnh `sed` của shell trên bản sao
  file của board thì gặp `sed: bad option in substitution expression`, vì URL luôn chứa `/`; file không đổi,
  tức side effect này chưa bao giờ xảy ra trên sản phẩm.
- STUNServerPort và keepalive giữ range TR-098 của 0078: phép kiểm range của shell không bao giờ trả sai
  (`-lt 1 && -gt 65535`, biến trong nháy đơn).

**Cổng §6.3:**
- Đạt: `check-cc-syntax` (lib 41/0, app 17/0 lỗi; cảnh báo giữ 15/27, không cái nào ở file đã sửa),
  `check-c-sanity` 0, dm paths thiếu 0 / dôi 11, claims 0 cặp chồng, automake 0, sums OK.
- **`run.sh all` không chạy hợp lệ được trên máy này.** Không vào được GitHub và Docker Hub, chỉ có Ubuntu
  archive. Đã thử container dùng một lần `icwmp-hosttest-20261006`:
  - image `openwrt-an75xx` (Ubuntu 18.04), apt `python3.8 libjson-c-dev libcurl4-openssl-dev busybox valgrind`;
  - libubox/uci/ubus/json-c 0.15 từ `dl/` của SDK (json-c `-nodoc` cần thư mục `doc/` rỗng);
  - `build.sh` chạy trên bản chép, bỏ ba lệnh `dep` clone GitHub.

  Build đạt, nhưng agent chết ngay phiên đầu (`Segmentation fault`). **Bản HEAD `7121259`, không có
  K13/K14, chết y hệt**, nên đây là môi trường. Harness `unit` thì không tìm thấy `libjson-c.so.5` vì không
  có rpath.

  Muốn có HOST_VERIFIED thì cần một container Ubuntu mới hơn (json-c 0.15+, Python ≥ 3.10) có mạng tới
  GitHub, hoặc chép sẵn repo deps theo rev đã pin.

## 53. Image 0083 trên board: K13/K14 PASS, G9 trên 0080, G9 mới (06/10 07:47–08:00)

User yêu cầu nạp và kiểm (chatlog mục 73). Từ mục này nhật ký bằng chứng ghi trong repo.

**G9 trên image 0080** (sampler từ 05/10 23:03, dừng trước khi nạp; CSV
[evidence/20261006_g9_soak_image0080.csv](evidence/20261006_g9_soak_image0080.csv)):

| Cột | min → max, 100 mẫu, 8 h 41 |
|---|---|
| pid `icwmp_tr098d` | 10407 (không đổi) |
| VmRSS | 5344 → 5352 kB |
| fd / thread | 14–15 / 11 |
| VmRSS `icwmp_dm.sh` | 3924 → 3928 kB |
| MemAvailable | 131 288 – 134 824 kB |
| phiên | success 3 → 4 (một phiên định kỳ K10), failure 0 |
| `dm get` toàn cây | 15–18 s, 1740 dòng |

Phẳng, CRASH 0, vẫn 2 dòng start. Chưa đủ 24 h vì phải nạp image cuối của PH0.

**Image 0083:**
- Build: `make -j16 MSDK=1` trên cây đã apply `--sdk-only`, `ICWMP_IMAGE_RC=0`. `tclinux.bin` 07:52:30, md5
  `d495699f…`; `libtr098.so.3.0.0` `e6994037…`, `icwmp_tr098d` `cd10c7bf…`, có `S11dev_access`.
- Nạp như mọi lần: md5 hai đầu khớp, model `HP-2236B`, `valid: true`, `sysupgrade -T` rc 0. `sysupgrade`
  lúc 07:53:40 ghi bank A (board đang chạy `filesystem_slave`).
- Kiểm một lần lúc 07:58:24: 22/23/80/7547 mở. Rootfs `filesystem`, đúng md5.
  `dev_access: … svcboot turned SSH off after 16s`. CRASH 0, 2 dòng start, phiên boot success lúc 07:55:57.

**K13/K14 trên board, PASS** (`ubus call tr069 dm set`):

| Tham số | Giá trị | Kết quả |
|---|---|---|
| PeriodicInformTime | `2026-02-29T00:00:00Z`, `2026-04-31…`, `…T24:00:00Z`, `…T23:60:00Z`, `…T23:59:60Z`, `…+15:00`, `…+0730` | 9007 cả 7 |
| URL | `acs.example.net:7547/acs`, `://acs.example.net/acs` | 9007 cả 2 |
| STUNServerAddress | `localhost`, `-stun.example.net`, `stun..example.net`, `192.0.2.256` | 9007 cả 4 |
| sau 13 lần bị từ chối | `uci changes` = 0; `periodic_time`, `url`, `serveraddress` giữ nguyên | — |
| PeriodicInformTime | `2028-02-29T00:17:00Z` (ngày nhuận) | 0, lưu đúng; trả về `0001-01-01T00:00:00Z` |
| STUNServerAddress | `172.16.0.15` (giá trị hiện tại, IPv4) | 0 |

pid giữ `11018`, vẫn 2 dòng start, CRASH 0. Sau khi ghi lại STUN server (có reload `stuncd`), binding học lại
`172.16.0.78:19302`, nhưng `natdetect` vẫn 0 (ngày 05/10 là 1). Cần xem lại khi kiểm G7.

**G9 mới trên image 0083**: sampler như cũ, chạy từ 07:58:58 (epoch 1791248338; pid 11018, VmRSS 5392 kB, fd 15,
thread 11). Mốc 24 h là 07/10 07:59.

**Đường tới ACS (để kiểm G4/G6/G7/G8 qua ACS):**
- Máy build không có route tới `172.16.0.15` (đi `enp1s0` sang mạng khác, mọi cổng đều đóng).
- Từ board (lua `nixio`, chỉ TCP connect), GenieACS mở 7547 (CWMP), 7557 (NBI), 7567 (FS), 3000 (UI).
- `nc` của busybox báo "closed" sai cả với 7547, không dùng được để kiểm cổng.
- Chưa gọi API nào của ACS.

## 54. G4 và G6 qua ACS (GenieACS NBI), image 0083 (06/10 08:05–08:15)

User chọn "chỉ NBI, bỏ G8" (chatlog mục 74). Điều kiện: không ảnh hưởng ACS, hệ thống liên quan và các
thiết bị khác; test xong trả lại như cũ.

**Cách làm:**
- **Đường vào:** NBI GenieACS `172.16.0.15:7557` qua tunnel SSH của board
  (`ssh -O forward -L 127.0.0.1:17557:172.16.0.15:7557`), vì máy build không có route tới ACS. Không vào UI,
  Mongo hay docker.
- **Phạm vi:** chỉ thao tác trên device `000378-HP%2D2236B%2DMain-OANH00000001`. Trong URL, id phải encode
  thêm một lần thành `%252D`; nếu không, NBI trả 404 `No such device` và không tạo gì.
- **Chụp trước khi test:** device có 0 task, 0 fault; 4 preset đều là preset mặc định (`default`, `inform`,
  `getrpcmethods`, `bootstrap`). Không đọc hay sửa provision, virtual parameter, file, config.
- Mọi task dùng `?connection_request`.

| Gate | Task NBI | Kết quả |
|---|---|---|
| G4a GPV | `getParameterValues` SoftwareVersion, PeriodicInformInterval, ProvisioningCode | **PASS**: HTTP 200 trong khoảng 1 s; board log CR 401 rồi `success authentication`, phiên chạy `GetParameterValues` |
| G4b GPN | `refreshObject` `InternetGatewayDevice.DeviceInfo` | **PASS**: 10 GetParameterNames + 1 GetParameterValues, ACS có 23 phần tử con của DeviceInfo. Lần đầu tôi để `objectName` có dấu chấm cuối: script `refresh` của GenieACS báo `Invalid parameter path` (lỗi phía ACS, board không nhận RPC nào). Đã xoá đúng task và fault đó |
| G4c SPV nhiều param có một param sai | `setParameterValues` `ProvisioningCode=g4ok` + `PeriodicInformInterval=abc` | **PASS**: fault `cwmp.9003` kèm `9007` đúng ở `PeriodicInformInterval`. Board giữ `g5ok`/43200, `uci changes` 0, pid không đổi. Đã xoá task và fault |
| G6 ACS ghi, sau phiên | `setParameterValues` `ProvisioningCode=g6test` + `PeriodicInformInterval=3600` | **PASS**: 200; `easycwmp` = g6test/3600, mirror `cwmp` = 3600, `next_session` = +3600; không restart (pid 11018, 2 dòng start), không có `Command failed` |
| G6 sau reboot | `reboot` lúc 08:09, kiểm lúc 08:13 | **PASS**: vẫn g6test/3600 ở `easycwmp`, `cwmp` và GPV; phiên BOOT success; `next_session` = boot + 3600; CRASH 0, 2 dòng start; ACS thấy `_lastBoot` mới, Interval 3600, ProvisioningCode g6test |
| Trả lại | `setParameterValues` `ProvisioningCode=g5ok` + `PeriodicInformInterval=43200` | 200; ACS và board đều về `g5ok`/43200; `next_session` về chu kỳ 12 h |
| Sau cùng | — | Device còn 0 task, 0 fault trên ACS; tunnel đã đóng |

**Chưa làm:** WebUI hiển thị giá trị (G6), vì không có tài khoản WebUI. G8 không làm, theo lựa chọn của user.

**G7:** GenieACS tới thẳng được `30.1.1.153:7547` bằng HTTP CR. Gói UDP thấy trên WAN lúc 08:07:15 là phản
hồi STUN (`172.16.0.15:19302`), không phải UDP CR, nên đường UDP CR qua NAT không cần và chưa được thử bằng
ACS trong topology này. Sau khi nạp, `UDPConnectionRequestAddress` = `172.16.0.78:19302`, khác IP WAN,
nhưng `NATDetected` = 0 (ngày 05/10 là 1). Giá trị này do `stun-client` của vendor ghi vào `stun.@stun[0]`,
module 0078 chỉ đọc. **Not established**: vì sao vendor ghi 0.

**G9** chạy lại sau reboot từ 08:14:19 (epoch 1791249259, pid 10691, VmRSS 4964 kB, fd 15, thread 11),
mốc 24 h là 07/10 08:15.

## 55. Test host chạy lại trên máy build; bundle MTK thiếu microxml (`0087`) (06/10 10:41–10:56)

**Sửa ghi chú §52.** Ở §52 tôi ghi "máy build không vào được GitHub và Docker Hub". Ghi chú đó sai:

- Trên host, `git ls-remote https://github.com/openwrt/libubox.git` báo `server certificate verification failed.
  CAfile: none`. Nhưng `curl https://github.com/` trả 200 (`ssl_verify_result` 0), và
  `git -c http.sslCAInfo=/etc/ssl/certs/ca-certificates.crt ls-remote …` trả `e7608b69…`, đúng `LIBUBOX_REV` đã pin.
  Vậy chỉ git trên host thiếu đường tới CA bundle. Trong container có gói `ca-certificates` thì `git clone` chạy bình thường.
- `docker pull ubuntu:24.04` thành công.

**Môi trường:** container dùng một lần `icwmp-hosttest-20261006b`:

- image `ubuntu:24.04`: json-c 0.17, gcc 13, Python 3.12.3, valgrind 3.22, busybox 1.36.1;
- gói như [tests/host/README.md](../../tests/host/README.md), thêm `ca-certificates`;
- repo mount `/repo:ro`, mọi file tạm nằm trong container;
- libubox, uci, ubus clone theo rev đã pin.

**`run.sh all` trên repo `97fa36f`** (code giống 0086; các commit sau đó không đổi lib/app): **rc 0**.

| Nhóm | Kết quả |
|---|---|
| unit | 3/3. GPV root: 417 getter / 9 request (`*_list`), 73 request (không list), 417 param. `dmcmd` 0 B…3 MB |
| smoke | 5 phiên, 75 RPC, 15 fault theo kế hoạch, agent còn sống |
| notify | 100/100 thay đổi giữ và gửi trong Inform |
| rpc | 5/5: ScheduleDownload hợp lệ không fault; 3 window, Download/Upload/ScheduleDownload thiếu FileType đều fault |
| msrv, stun | PASS (K1, K2, K13) |
| ptime | 2/2: Inform kế tiếp đúng `:17:00` (K10); thời điểm không có thật bị fault, giữ giá trị cũ (K14) |
| valgrind | 12 phiên, Download, tải ubus: definitely 0, indirectly 0, ERROR SUMMARY 0 |

Valgrind còn ghi `possibly lost: 3,520 bytes in 10 blocks`. Cổng không tính loại này. **Verified** từ stack trong `vg.log`:
đó là 10 block × 352 B mà `calloc` ← `allocate_dtv` ← `_dl_allocate_tls` ← `pthread_create` cấp cho mỗi thread
`main` tạo lúc khởi động (`cwmp.c:984`, 989, …, 1029, mỗi dòng một thread). Các thread còn sống khi nhận SIGTERM;
số block cố định, không tăng theo số phiên. Không phải leak.
Dòng PASS của valgrind in hai dòng `DONE` (12 rồi 13 phiên) vì `grep DONE acs.log` khớp cả hai. Đây chỉ là
chuyện hiển thị, không ảnh hưởng tiêu chí.

**Segfault ở §52:** cùng code này chạy sạch trên 24.04, nên kết luận "do môi trường" nay có bằng chứng.
Nguyên nhân cụ thể trên 18.04 thì **Not established**. Môi trường đó ghép json-c 0.15 tự build với
libubox/uci/ubus lấy từ `dl/` của SDK, không phải các rev đã pin.

**Lỗi của bản giao MTK.** `export.py --sdk mtk` (0085) bỏ cả `userspace/public/libs/microxml/` vì coi đó là
thành phần của BDK. Nhưng bundle MTK có mang `tests/host`, và `tests/host/build.sh` build microxml từ chính source
đó (`env.sh` `MICROXML_SRC`). Bằng chứng: trong cùng container, giải nén bundle `97fa36f` và dùng
`ICWMP_HOST_WORK` mới. `build.sh` build xong libubox/uci/ubus, rồi dừng ở
`cp: cannot stat '…/libs/microxml/microxml': No such file or directory`, rc 1. Người nhận bản
`a7549e7` cũng sẽ gặp lỗi y như vậy. Lỗi này không ảnh hưởng `apply` và build SDK: trên MTK, gói
`libmicroxml` lấy từ SDK, apply không cài microxml của repo.

**0087** (`e273359`): bundle MTK giữ `microxml/microxml/` (22 file) và chỉ bỏ glue BDK của nó (`autodetect`,
`Bcmbuild.mk`, `Makefile`, `Manifest.brcmoss`), giống cách đã làm với `libicwmp_dm` và `icwmp`.

**Bundle `e273359`:**

| Kiểm | Kết quả |
|---|---|
| Export | 327 file, sha256 `e693ae29f8d8a6f44a215655dceb4ca268adffbcc3da6c12da0b39b6f1878f4a`, export hai lần ra cùng sha256 |
| `apply --sdk mtk --dry-run` trên `1_src/2025q3` | Release `e273359`, layout `sdk-only`, đúng 6 path quản lý như trước (không có microxml), không ghi gì, không để lại `icwmp-apply-*` |
| So với bản đang cài trên `1_src` (`a7549e7`) | `diff -rq` lib và app: 0 khác; hai feed Makefile giống. Code giống từng byte, nên kết quả build gói + image rc 0 của `a7549e7` (06/10 08:42) đúng cho bundle này. Không build lại |
| Giải nén trong container, `sha256sum -c SHA256SUMS` | OK |
| `build.sh` → `setup.sh --yes` → `run.sh all` từ bundle, `ICWMP_HOST_WORK` mới | **rc 0**: build cả microxml; unit 3/3 (417 getter / 9 và 73 request), smoke 5, notify 100/100, rpc 5/5, msrv, stun, ptime 2/2, valgrind 12 phiên 0 lost 0 lỗi (possibly lost cũng 10 × 352 B như trên) |

**Chưa làm:** nạp image của bundle này lên board (board vẫn chạy 0083, chỉ khác ở 27 hàm không ai gọi).

## 56. BDK: apply + build image tại `9b75ed9` (K9), bundle một SDK (06/10 11:37–11:52)

**Môi trường:**
- Máy `192.168.100.38` (`network2`), cây `/home/vtanh/workspaceBRCM/tunv/2_src/bcm963xx`, profile `MO77300EB`.
- Build trong container `vtanh-brcm` qua tmux `bdk1` (user `vtanh`). Toolchain
  `crosstools-aarch64-gcc-13.2-linux-5.15-glibc-2.38`, Python 3.10.
- Trước lượt này, cây đang cài bản apply ngày 05/10 20:29. Marker của bản đó ghi `b01ec72`, nhưng đó là lỗi K19;
  thực tế là `dev` khoảng 0079. Bản đó chỉ build component, chưa build image.

**Bundle:** `./export.py --sdk bdk` tại `9b75ed9` (0088): 329 file, sha256 `4f7746f118bc…`, chỉ có `sdk/bdk` ở lib
(20 file) và ở app (6 file). Đặt tạm ở `tunv/icwmp_bdk_stage_20261006/`. `sha256sum -c` OK.
Lúc export, tôi thấy bản export trước đó (tại `e5e297f`) mang `tests/__pycache__/verify-apply.cpython-310.pyc`.
File này bị commit từ 0042 và cũng có trong bản giao `release/mtk-20261006`. 0088 gỡ nó.

| Bước | Kết quả |
|---|---|
| `apply --sdk bdk --dry-run .` | rc 0. Release `9b75ed9`, layout `sdk-only`, 8 path quản lý |
| `apply --sdk bdk .` | rc 0. Backup `.icwmp-backups/20261006-044322-1vd31o5d`. Applied: `microxml`, `uci`, `libicwmp_dm`, `icwmp`, marker. `make.common`, `comp_tr69_md.c` và profile đã đúng từ 05/10 nên không đổi |
| 6 lệnh build component (theo `apply`) | rc 0, 41 s. `sdk-scan: SDKs = bdk`. Cảnh báo đều là loại có từ trước (biến không dùng trong `xml.c` upstream, `/*` trong comment của `sdk.h`, `CWMP_BKP_FILE` redefined); không có lỗi |
| `make PROFILE=MO77300EB` | Lần 1 dừng ở `profile_saved_check` của vendor: profile (20:29 ngày 05/10, thêm `BUILD_ICWMP=y`) mới hơn `.last_profile` (19:46). Lần 2 với `FORCE=1`: rule touch cookie nhưng vẫn `exit 1` (do vendor viết vậy). Lần 3: **rc 0**, 04:46:08–04:49:23 UTC, log 26.474 dòng, 0 cảnh báo ở lib/app icwmp |

Vì sao `FORCE=1` mà không `make clean`:
- **Verified:** so với profile trong backup 05/10, profile hiện tại chỉ thêm đúng `BUILD_ICWMP=y`.
- **Verified:** `BUILD_ICWMP` chỉ được `make.common` (khối tích hợp của icwmp) và `apps/icwmp` (`Bcmbuild.mk`,
  `autodetect`) đọc. Nó thêm `-DSUPPORT_ICWMP`, và chỉ `comp_tr69_md.c` dùng define này.
- **Verified:** sau build, `cms_dmp_flags.h` đã có `SUPPORT_ICWMP`, và `comp_tr69_md.o` mới hơn cookie.

**Kiểm sau build (runbook §0.2):**
- Source đã cài khớp bundle. `diff -rq` của 4 component chỉ còn `Only in` phía cây: `autom4te.cache`, `.libs`,
  `Makefile`, … sinh ra lúc build.
- `.icwmp-release.json` ghi `9b75ed9`, `sdk-only`. `BUILD_ICWMP=y` ở dòng 804 của profile.
- `fs.install`:
  - `libtr098.so.3.0.0` 895.760 B, 11:48:23 (bản 05/10 là 924.216 B);
  - `icwmpd` 721.176 B, 11:48:28;
  - `libmicroxml.so.1.0` và `libuci.so` cùng được build lại.
- `nm -D libtr098`: có `dm_entry_prefetch_values` và `dm_platform_prefetch_values`; import `posix_spawnp` và
  `waitpid` (`dmcmd`).
- `icwmpd` cần `libtr098.so.3`, `libmicroxml.so.1`, `libuci.so`. `icwmpd` không import `posix_spawnp`/`waitpid`,
  vì BDK dùng `sdk/bdk/external_bdk.c` thay cho `external.c`. Dòng kiểm cũ của runbook đếm hai symbol này ở
  `icwmpd` là đặt sai chỗ, nay đã chuyển sang lib.
- Image: "Done! Image MO77300EB has been built". Các `*.pkgtb` nằm trong `targets/MO77300EB/`, không nằm trong
  `images/`, ví dụ `bcmMO77300EB_emmc_squashfs_update.pkgtb` 56.892.624 B, md5 `0c5664468dd4…`.

**K9:** BDK build đạt với 0067–0088, từ bundle một SDK. Tag `release/bdk-20261006` → `9b75ed9`; export lại tại tag cho
đúng file đã build (cmp giống nhau). Bundle lưu ở `release/icwmp_bdk_9b75ed9.tar.gz` của issue workspace. **Chưa làm:** nạp image lên board BDK và smoke (runbook §5,
thuộc PH7).

## 57. Soak host 300 phiên tại 0088; G9 board hoãn (07/10 15:34–16:05)

**G9 trên board chưa đọc được.** Từ 15:34, SSH (22) và telnet (23) của HP2236B đóng; ping và web 80/443 vẫn có.
DNS của board trả địa chỉ giả cho mọi tên (A `123.0.0.2`, AAAA `fc00::1`). User xác nhận đã rút PON nên board
mất WAN. G9 24 h trên image 0083 (sampler chạy từ 06/10 08:14) vì vậy chưa có kết quả. Theo user, test để sau:
G9 và PH0.5 (tag baseline, fast-forward `main`) làm khi board có lại WAN và SSH.

**Soak host tại `775fee2`** (code 0088), cho thêm bằng chứng ổn định của 0080–0088:

- Môi trường: container `ubuntu:24.04` dùng một lần, tạo với `--dns 192.168.100.4 --dns 8.8.8.8`, vì DNS của
  board đang trap làm apt hỏng.
- Lệnh: `build.sh` → `setup.sh --yes` → `run.sh soak 300`. Tải ubus mỗi 2 s: `notify`, `status`,
  `dm get/names/inform`.

| Chỉ số | Kết quả |
|---|---|
| Phiên | 300/300, 4.500 RPC, 900 fault theo kế hoạch ACS; 08:53:07–08:59:41 UTC (6,5 phút) |
| Agent | còn sống tới cuối, `PASS soak` |
| VmRSS | 12.496 kB ở mẫu đầu (khởi động); sau đó 13.172–14.332 kB, mẫu cuối 13.340 kB, không có xu hướng tăng |
| fd | 14–18 |
| Thread | 11 suốt thời gian chạy |

Mẫu đầy đủ (13 mẫu, mỗi 30 s): [evidence/20261007_host_soak300_0088.txt](evidence/20261007_host_soak300_0088.txt).
Host soak không thay được G9: board có shell data model thật, procd và thời gian 24 h.

## 58. P6a–d: phần P6 trừ Firewall, cùng LTE của P8, sang C (`0089`, `0090`, 07/10 16:05–)

**Vì sao làm bây giờ:** ngày 07/10 user chốt ưu tiên: icwmp chạy ổn định và hỗ trợ đủ tham số theo kế hoạch. Board
đang mất WAN, nên phần test board để sau. Thứ tự làm: hết P6 (cây UserInterface, các object lẻ ở root, Firewall), rồi P7,
P8. Mỗi object port từ đúng file `functions/tr098/*` của sản phẩm trong `src/2025q3`.

| Phần | File C | Tham số | Nguồn shell | Ghi chú |
|---|---|---|---|---|
| P6a | `root_hidden_mtk.c` (mở rộng) | 10 | `tr098/root` | `DeviceSummary` (lá ở gốc, forced inform) và các object chỉ trả lời khi được hỏi đúng path: FaultMgmt, BulkData, SoftwareModules, Layer2Bridging, USBHosts, CaptivePortal, FAP.GPS, User (`addressed_only`). Tất cả là hằng |
| P6a | `account_mtk.c` | 1 | `tr098/account` | `SessionMaxTime` = `hmxwslbackend.@hmxwslbackend[0].SessionTimeOut`, 300..3600; restart wsl xếp hàng |
| P6b | `x_ais_carrierlocking_mtk.c` | 7 | `tr098/X_AIS_CarrierLocking` | `isplocking.@isplocking[0]`, thêm section khi thiếu; LockingEnable chỉ nhận `0`/`1`; bốn bộ đếm chỉ nhận chữ số |
| P6c | `x_ais_webuserinfo_mtk.c` | 13 | `tr098/X_AIS_WebUserInfo` | remoteaccess, account.admin/root, clay captcha/language, whitelist qua `ubus call hni setAISWhiteList`, WebIp |
| P6d | `xmpp_mtk.c` | 15 | `tr098/xmpp` | XMPP.Connection.1/Server.1, hằng; setter nhận rồi bỏ |
| P8 | `xmpp_mtk.c` | 12 | `tr098/xmpp` | LTE, hằng chỉ đọc |

**Khác shell, có chủ đích** (ghi ở đầu từng file):
- Restart dịch vụ và WebIp xếp hàng tới cuối phiên, sau khi engine commit. Shell commit và restart ngay trong từng setter,
  nên một lá khác trong cùng SPV lỗi cũng không hoàn tác được.
- Không chép mật khẩu mặc định của nhà máy vào C. Shell so mật khẩu mới với mặc định để bỏ qua lần ghi; ở đây, khi option
  chưa có, giá trị được ghi luôn. Kết quả đăng nhập như nhau.
- Thiết bị của default route lấy từ `/proc/net/route`. `awk '{print $5}'` của shell lấy nhầm `link` khi route không có gateway.
- `FAP.` được trả lời khi hỏi đúng `FAP.`. Shell chỉ khớp `FAP.GPS.*` nên báo 9005.
- `setAISWhiteList`: không có reply, hoặc reply không phải JSON, thì báo 9007 như khi ubus lỗi. Shell cho reply rỗng của
  một lần gọi thành công thành 9002.

**Sửa công cụ (0089):**
- `verify-dm-paths.py` đọc thêm `.params` (lá ở gốc). Trước đó `DeviceSummary` bị đếm là thiếu.
- `check-c-sanity.py` biết thêm `json_object_new_boolean` và `json_object_to_json_string_ext`.
- `acs.py` ghi `fault <mã> <tham số>=<mã>`.

**Test host mới `run.sh p6`, đã đưa vào `all`:**
- Ghi trên config của sản phẩm, kiểm mỗi dịch vụ restart đúng một lần (init script dạng stub).
- 8 giá trị bị từ chối và không ghi gì: SessionMaxTime 299/3601, LockingEnable `true`, RoundNum `5a`, CurrentLanguage
  ngoài danh sách, SuperAdminEnable `2`, Captcha `TRUE` (bị kiểm boolean phía trước chặn, đúng như shell), mật khẩu 33
  ký tự.
- Object ẩn không xuất hiện khi lấy cả cây; XMPP/LTE thì có (27 dòng).

Dòng fault của ACS đã giúp tìm ra một kỳ vọng sai của chính test: tôi tưởng `Captcha_enable=TRUE` đi tới được setter.

**Kiểm:**
- `verify-dm-paths --phase 6`: 46 C, thiếu 51 (chỉ còn Firewall), dôi 0. Phase 1–5 không đổi (thiếu 0, dôi 11). Phase 8:
  thiếu 132/144. Claims 0 cặp chồng.
- `check-c-sanity` lib/app 0 vấn đề; cross-gcc SDK MTK 45 file, 0 lỗi.
- `run.sh all` rc 0 (ubuntu:24.04). `unit`: số getter mà script phải chạy cho một GPV gốc giảm từ 417 còn 359, đúng bằng
  58 lá đã port; request 9 (list), 66 (nolist). Các nhóm còn lại: smoke, notify, rpc 5/5, msrv, stun, ptime, p6, valgrind
  0 lost 0 lỗi.
- **MTK SDK build** (07/10 16:31–16:38):
  - export `--sdk mtk` tại `8858816` → apply vào `1_src/2025q3`, backup `.icwmp-backups/20261007-163156-phy7syj5`;
  - build trong container `nvtu-openwrt` bằng `docker exec`, vì pane `tunv1` đang được user dùng;
  - `libtr098` + `icwmp_tr098` clean/compile rc 0, image `make -j16 MSDK=1` rc 0 (`tclinux.bin` 16:37:57,
    md5 `41a40eea9ca2…`);
  - 0 cảnh báo ở 5 file mới. Log: `1_src/2025q3/.icwmp-build-logs/20261007-p6-8858816.log`.
- **Tổng tham số bằng C: 516/783** (P1–P5 458 + P6 46 + LTE 12).

**Chưa làm:** Firewall (51 tham số: DisablePort, IPFilter, ServiceControl, có Add/Delete); nạp image lên board (board đang mất WAN).

## 59. P6e Firewall (`0092`), K20 lỗi fault ở VALUESET, K21 SPA lên object C (`0091`), K22 (07/10 16:45–17:30)

**P6e:** `firewall_mtk.c` port `functions/tr098/firewall` (51 tham số), dữ liệu nằm trong `firewall_clay`.

| Nhánh | Section UCI | Ghi chú |
|---|---|---|
| `Config`, `Enable` | — | hằng `High` / `true` |
| `X_AIS_DisablePort.{i}` | `disable_port` (tối đa 32) | Interface đổi thì xếp hàng `hni.service set ruleIdx` |
| `X_AIS_ServiceControl.IPV4ServiceControl/IPV6ServiceControl.{i}` | `packetfilter`, lọc theo `ipversion` (tối đa 64 mỗi loại) | ánh xạ Ingress/ServiceType/OtherPort/OtherProtocol, điền `-` cho IPStart/IPEnd |
| `X_AIS_IPFilter.{i}` | `ipfilter2` (tối đa 20) | 27 lá, giữ giá trị mặc định và ánh xạ của shell |

- Instance theo vị trí như shell. AddObject ghi giá trị mặc định của shell (IPFilter lấy priority trống nhỏ nhất).
  DeleteObject xoá section, các instance sau dồn số.
- Kiểm địa chỉ và mask của IPFilter theo `IPVersion`, và kiểm `Order` trùng, đặt ở VALUESET. Shell chạy setter theo
  thứ tự request, nên `IPVersion=6` + `SourceIP` IPv6 trong cùng SPV là hợp lệ.
- **P6 xong 97/97** (`verify-dm-paths --phase 6`: thiếu 0, dôi 0). **Tổng tham số bằng C: 567/783.**

**K20 (HIGH, Verified, đã sửa 0091):** `mparam_set_value()` (`dmtr098.c`) gọi setter ở VALUESET rồi **bỏ giá trị trả về**.
Setter từ chối ở bước đó thì SPV vẫn báo thành công mà không ghi gì.
- Bị ảnh hưởng từ trước: `X_AIS_Mesh.MeshEnabled` ngoài chế độ router (9001).
- Bị ảnh hưởng trong code hôm nay: CarrierLocking khi không có package `isplocking` (9002), whitelist của hni, WebIp khi
  không có WAN.
- Lộ ra khi test firewall: các ca VALUESET trả "thành công", còn `IPVersion` lẽ ra phải hoàn tác thì vẫn ở lại.
- Sửa:
  - fault được trả về `dm_entry_apply()`, hàm này vốn đã revert UCI, platform và hành động cuối phiên;
  - handler SPV gửi fault đó dưới mã 9003 kèm mã của từng tham số, giống fault ở VALUECHECK (cũng áp dụng cho
    batch BDK có ghi tên tham số);
  - MTK: hàng đợi `apply_service` được cắt về kích thước lúc VALUECHECK cuối cùng khi batch bị revert, nên SPV bị từ
    chối không còn làm restart dịch vụ.
- Test: `run.sh p6` (9003 kèm 9002 khi thiếu `isplocking`), `run.sh fw` (SPV có lá thứ hai lỗi ở VALUESET, lá thứ nhất
  không được ghi).

**K21 (MEDIUM, Verified, đã sửa 0091):** cả 114 dòng object trong `sdk/mtk/dm098` đều đặt `&DMNONE` ở cột notification.
- Engine chỉ đọc cột này để từ chối SPA lên đúng path object bằng 9009 (`mobj_set_notification_in_obj`); GPA không dùng
  (`mobj_get_notification` trả 0).
- Shell của sản phẩm nhận SPA lên object. Vì vậy từ khi P1–P5 chạy bằng C, ACS đặt attribute cho `IGD.LANDevice.` sẽ
  nhận 9009.
- Lộ ra khi smoke có thêm một fault 9009 mỗi phiên cho `SPA IGD.Firewall.`.
- Sửa: đặt NULL ở cột đó. Code portable vẫn giữ `&DMNONE` cho 27 object nó cố ý không cho đặt notification.

**Test host phải sửa theo:**
- `notify` từng dựa vào 100 tham số Firewall do `fake_dm` phục vụ. Nay chuyển sang `IGD.Device.` (vẫn là shell, 128
  tham số), số kỳ vọng đọc từ `fake_dm`.
- `smoke` có `SPV Firewall.Config=x`. Lá này chỉ đọc cả trong shell, nên 9008 bây giờ là đúng (trước đây `fake_dm` nhận nhầm).

**K22 (LOW, Not established):** trong một lần `run.sh all`, agent chạy dưới valgrind không thoát hẳn sau SIGTERM.
- Luồng chính thoát; còn một luồng chờ futex (`wchan futex_wait_queue`, trạng thái `Zl`).
- Luồng đó vẫn giữ `/var/run/icwmpd.pid`, nên agent sau thoát ngay ("is locked by another process") và không có dòng
  tổng kết của valgrind.
- Chạy lại valgrind 3 phiên, 12 phiên và `all` hai lần: đều thoát sạch.
- Chưa biết luồng nào và kẹt ở mutex nào. Trên board, procd sẽ `kill -9` sau timeout nếu chuyện này xảy ra. Cần stack
  (gdb attach) nếu gặp lại.

**Kiểm (tại `e576b0b`):**
- `run.sh all` rc 0 hai lần (ubuntu:24.04): unit (script getter GPV gốc 259 / 9 request), smoke 5 phiên (20 fault theo
  kế hoạch), notify 128/128, rpc 5/5, msrv, stun, ptime, p6, fw, valgrind 12 phiên 0 lost 0 lỗi.
- Cổng tĩnh: phase 1–6 thiếu 0; claims 0 chồng; sanity 0; cross-gcc lib 46 file và app 17 file, 0 lỗi, không có cảnh
  báo mới.
- MTK SDK build (17:29–17:35):
  - export `--sdk mtk` tại `e576b0b` → apply `1_src` (lib + app, backup `.icwmp-backups/20261007-172910-7verbfh_`);
  - `libtr098` + `icwmp_tr098` rc 0, image rc 0 (`tclinux.bin` 17:35:01, md5 `7474f123a5b4…`);
  - cảnh báo duy nhất ở các file đã sửa là `dmtr098.c:536` (`-Wpointer-to-int-cast`), đã có từ bản build trước.
  - Log: `1_src/2025q3/.icwmp-build-logs/20261007-p6e-e576b0b.log`.

**Chưa làm:** nạp image lên board (board mất WAN và SSH từ 15:34).

## 60. P7: 79 tham số `X_AIS_*` của operator sang C (`0093`–`0095`, 07/10 18:05–)

**Kết luận:** P7 xong **79/79** (`verify-dm-paths --phase 7`: thiếu 0, dôi 0). Tổng tham số bằng C: **646/783**
(P1–P5 458, P6 97, P7 79, LTE của P8 12). Còn 132 tham số P8 qua compat shell.

**Repo GitHub `tunguyenvanbn94/icwmpMultiSdk` là public** (API GitHub trả `"visibility": "public"`, 07/10 18:10).
- Khóa AES-256 mà shell `X_AIS_CPEagent` dùng để mã hoá `SecretKey` khi đọc **không** được đưa vào source.
- Lúc build, `tools/mtk-cpeagent-key.sh` lấy khóa từ file shell của sản phẩm trong cây SDK (`Build/Prepare` của
  `feeds/libtr098`) và sinh `cpeagent_key_mtk.h`. File này có trong `.gitignore`, và khóa không xuất hiện trong log build.
- Build không có header (gate tĩnh, test host không có khóa test) thì `SecretKey` đọc ra `""`, đúng như shell khi
  `openssl` lỗi. Test host dùng khóa test cố định (`tests/host/env.sh`).
- Repo public thì cũng công khai code port từ shell của sản phẩm và các ghi chú trong `docs/`. Đây là quyết định của
  chủ repo, chưa đổi gì.

**0093, lỗi công cụ:** `gen-coverage-matrix.py` không nối dòng tiếp `\`.
- `X_AIS_Conf` viết mỗi lời gọi trên hai dòng, nên 8 tham số có perm `\`, không có type/getter/setter.
- Bảng kiểu của input contract (`shelltypes_mtk.h`, sinh từ ma trận) vì thế thiếu kiểu boolean/int của Conf.
- Ma trận cũng cũ so với commit vendor `a920c7fe8` (HP2236BVA-384, 23/09). Commit đó đổi
  `X_AIS_SSH.Enable`, `X_AIS_Telnet.Enable` và `UserInterface.X_AIS_WebUserInfo.SuperAdminSecurity` sang
  `xsd:boolean`.
- Hệ quả từ 0090: input contract của C không chặn `TRUE` cho `SuperAdminSecurity` như shell chặn.
- Sửa:
  - nối dòng `\` như shell;
  - sinh lại TSV (vẫn 783 tham số), sinh lại `shelltypes_mtk.h` (186 → 193 dòng).

**Object và nơi lưu (giống shell, trừ phần ghi ở mục khác biệt):**

| Object | Tham số | Nơi lưu | Áp dụng |
|---|---|---|---|
| `X_AIS_UPnP` | 1 | `upnpd.config.enabled` | `miniupnpd reload` |
| `X_AIS_3rdAgent` | 3 | `3rdpartyagent.3rdpartyagent` (AP mode: URL mới thì xoá `client_id`) | `3rdpartyagent restart` |
| `X_AIS_CPEagent` | 2 | `secret_key` (≤256, đọc ra bản mã), `secret_key_version` (≤64) | `3rdpartyagent restart` |
| `X_AIS_AutoWifiScan` | 3 | `autowifiscan.@autowifiscan[0]`, mặc định 1/300/120 | Enable=1: `running \|\| start` |
| `X_AIS_DHCPClient` | 3 | `lanhost.common.total_hosts` (đọc); Session/Clean chỉ nhận 1 | Clean: dnsmasq stop, xoá lease, start |
| `X_AIS_DnsLandingPage` | 1 | `landingpage.@landingpage[0]` (thêm section khi thiếu) | `landingpage restart` |
| `X_AIS_Isolation` | 1 | `dhcp.lan.isolation` (thêm `dhcp.lan` khi thiếu) | `ubus call hni doLanIsolation` |
| `X_AIS_SSH`, `X_AIS_Telnet` | 1 + 3 | `account.ssh/telnet`; Enable qua `hni setSshAccess/setTelnetAccess`; user/pass ghi cả telnet và ssh | account reload, telnet restart, dropbear killclients + reload |
| `X_AIS_MeshAPI` | 3 | `meshapi.meshapi` | `meshapi restart` |
| `X_AIS_DDNS` | 5 | `ddns.service` (Provider DynDNS ↔ dyndns.org, No-IP ↔ no-ip.com) | `ddns restart` |
| `X_AIS_Conf` | 8 | `aisbackup.params` (thêm khi thiếu), cờ lưu `true`/`false` đọc `1`/`0` | không |
| `X_AIS_Logging` | 24 | `system.syslog`; level là danh sách `\|`, rỗng là `none` | `log restart`; TFTP/Clean làm trong setter |
| `X_AIS_UplinkSetup` | 15 | `dualuplink`; mode/uplink/vlan qua `ubus call hni.dualuplink set` | timer đổi: `killall -USR1 dualuplink` |
| `X_AIS_WiFiStatus` | 4 | file trạng thái/JSON dưới `/tmp` | quét `mwctl`, `iw station dump` |
| `X_AIS_MLO` | 2 | `wireless.apmld1` + ra5/rai5, `apmld2` + ra4/rai4 (backhaul tắt vẫn để rai4 bật) | `wifi reload`, mapd khi mesh bật |

**Khác shell, có chủ đích:**
- Restart dịch vụ và lời gọi hni không cần kết quả (SSH/Telnet Enable, `doLanIsolation`, flush lease DHCP) được xếp
  hàng cuối phiên, sau commit của engine, như các phase trước. SPV bị từ chối thì không chạy (K20).
- **UplinkSetup** gọi `hni.dualuplink set` ngay trong setter ở VALUESET, vì fault (9002) phụ thuộc vào reply.
  - hni tự ghi và commit `dualuplink`. Vì vậy giá trị "hiện tại" để so sánh được đọc bằng `uci -q get` từ file, như
    shell, chứ không đọc từ bản package của phiên.
  - Bản của phiên đã cũ nếu hni vừa ghi trong cùng SPV. Ví dụ đặt `mode3.BackupUplink=lan1` rồi
    `mode3.MainUplink=lan4` khi backup đang là lan4: đọc bản cũ sẽ báo trùng (9007) sai.
- **Logging:** `TFTPUploadResponse=1` (tar + `tftp -p`) và `CleanLogging=1` làm ngay trong setter như shell, rồi lưu 2 hoặc 3.
  - Lá sau trong SPV bị lỗi thì hoàn tác trạng thái, không hoàn tác việc upload.
  - Shell xoá `/backup/log/messages` sau khi tar, kể cả khi `ln -s` thất bại vì ở đó có file thật. C chỉ xoá link do
    chính nó tạo.
- **WiFiStatus:**
  - quét neighbour trong setter như shell (7–21 giây);
  - danh sách station chạy trong setter thay vì nền (`&`), vì chỉ mất vài ms;
  - escape `"` và `\` trong SSID.
- **MeshAPI:** thiếu `meshapi.meshapi` thì thêm section có tên đó, như config mặc định của sản phẩm.
- **Conf:** kiểm giá trị trước khi thêm section. Shell thêm section trước, nên giá trị bị từ chối vẫn để lại section rỗng.

**Lỗi của shell sản phẩm (đang chạy trên board hiện tại):**
- `X_AIS_WiFiStatus.X_AIS_WiFiClientResponse` luôn trả `Hostname` và `IP` rỗng.
  - awk thứ hai chạy với `-F'|'`, và `getline < /tmp/dhcp.leases` tách dòng lease theo `|`, nên `$2` không bao giờ là MAC.
  - Kiểm bằng chính hàm shell dưới busybox trong container với cùng đầu vào: cả ba station ra `"Hostname":"","IP":""`.
  - C tra lease theo MAC như shell định làm.
- `X_AIS_MeshAPI` khi thiếu `meshapi.meshapi`: `uci add meshapi meshapi` tạo section vô danh, sau đó
  `uci set meshapi.meshapi.<opt>` hỏng. Giá trị mất nhưng setter vẫn báo thành công.
- `sort -nr` đặt dòng `"NO"` (không có RSSI, giá trị 0) lên **đầu** danh sách neighbour. C giữ y như vậy (không phải
  lỗi crash, chỉ là thứ tự lạ).

**Helper mới:**
- `dmmtk.c`: `mtk_uci_ensure_section()` (ensure_*_section của shell, -1 thì 9002) và `mtk_run()` (lấy exit status).
- `input_contract_mtk.c`: `mtk_shell_getn()` (toán hạng số của `test` busybox).
- `wlan_mtk.c`: `wlan_mesh_enabled()`.

**Kiểm (dev, chưa commit lúc chạy):**
- Gate tĩnh: phase 7 thiếu 0, dôi 0; claims 158, 0 chồng; check-c-sanity 60 file, 0 vấn đề (thêm `mkstemp`, `symlink`,
  `localtime_r`, `qsort` vào whitelist libc); cross-gcc lib 60 file và app 17 file 0 lỗi, 14 cảnh báo thường như trước.
- Test host (ubuntu:24.04, container dùng một lần):
  - `run.sh p7`: ghi đúng option, mỗi restart/lời gọi hni đúng một lần, giá trị không đổi thì không ghi, 18 giá trị
    shell từ chối đều fault;
  - `SecretKey` trùng từng ký tự với pipeline `printf | dd | openssl enc | openssl base64 | cut` của shell (khóa test);
  - upload TFTP qua `tftp` giả: archive có `messages`, link và tar.gz được xoá;
  - `run.sh p7c`: stub `hni.dualuplink` tự commit như hni; JSON neighbour/client khớp kết quả hàm shell dưới busybox
    (trừ hai khác biệt nêu trên); 9 fault, trong đó 2 ở VALUESET tới ACS là 9003 kèm 9007/9002;
  - `run.sh all` tại 0093 + P7a: rc 0, valgrind 12 phiên 0 lost, 0 lỗi.
  - `run.sh all` trên cây cuối (0095): mọi test PASS, gồm unit, smoke, notify, rpc, msrv, stun, ptime, p6, fw, p7, p7c.
    Riêng bước valgrind **không có tổng kết**: K22 lặp lại (lần thứ hai), xem bên dưới.

**K22 lặp lại, đã bắt được trạng thái (07/10 18:53).** Agent dưới valgrind (pid 32120) sau SIGTERM còn `Zl`, 2 luồng:
- Luồng chính là zombie, có `SigPnd` bit 63 (tín hiệu valgrind dùng để "giết" luồng khác khi tiến trình thoát).
- Luồng 32146 ngủ trong `futex(0xcad6fd0, FUTEX_WAIT_PRIVATE, 2)`, đúng chữ ký `lll_lock_wait` của glibc (một lock
  đang bị tranh chấp).
- SIGTERM thứ hai còn pending và bị chặn.
- Tiến trình còn giữ `/var/run/icwmpd.pid`, nên lần chạy lại test valgrind cũng hỏng theo.

Diễn giải:
- Handler SIGTERM của agent chỉ `close()` + `_exit()` ([cwmp.c](../../userspace/public/apps/icwmp/icwmp/cwmp.c),
  `signal_handler`). Trên kernel thật đó là `exit_group`: mọi luồng chết ngay, không có trạng thái này.
- Dưới valgrind, luồng nhận tín hiệu (32146) giết luồng chính rồi chạy phần dọn cuối của valgrind (mặc định có
  `__libc_freeres` của glibc trong chính tiến trình). Nó kẹt ở một lock glibc mà luồng vừa bị giết còn giữ.

Phân loại:
- **Conditional (chỉ dưới valgrind)**: Verified ở mức trạng thái luồng và tham số futex.
- **Not established**: hàm glibc nào đang chờ. `vgdb` cần ptrace, container test không có `CAP_SYS_PTRACE`.
- Không phải lỗi của agent trên board. Cần xác minh bằng stack (container có `--cap-add SYS_PTRACE`) và cách tránh trong
  test (`--run-libc-freeres=no`).

**K22 — nguyên nhân đã xác minh (07/10 19:00, container có `--cap-add SYS_PTRACE`).**
- Tái hiện ngay lượt 1 của `run.sh valgrind 6`: pid 2510 ở `Zl`, luồng 2536 trong `futex(0x64882c0, FUTEX_WAIT_PRIVATE, 2)`.
- `vgdb` vẫn không attach được (luồng chính đã zombie), và memcheck đã bị strip nên gdb chỉ thấy địa chỉ. Vì vậy đọc
  thẳng bộ nhớ qua `/proc/2536/mem`:
  - tại `futex − 0xe0` là một glibc `FILE` (`_flags` = `0xfbad3c84`, magic `0xfbad`) có con trỏ `_lock` trỏ đúng vào từ
    futex. Một FILE mở bằng `fopen` có lock đặt ngay sau `_IO_FILE_plus`;
  - `_fileno` = 14, và `/proc/2536/fd/14` là `/etc/tr098/.dm_enabled_notify`;
  - lock word = 2 (đang bị tranh chấp); owner `0x5d52440` nằm trong vùng TLS của luồng chính.
- Chuỗi sự kiện:
  1. Luồng chính đang đọc file trong vòng value-change (`dm_entry_enabled_notify_check_value_change`, `fgets`).
  2. Luồng khác nhận SIGTERM; handler `close()` + `_exit()`.
  3. Valgrind giết các luồng còn lại, rồi chạy `__libc_freeres` của glibc trong luồng đang thoát.
  4. `_IO_cleanup` khóa từng FILE còn mở và chờ mãi lock mà luồng đã chết còn giữ.
- **Kết luận:**
  - Verified: chỉ xảy ra dưới valgrind. `_exit` trên kernel thật không chạy freeres hay `_IO_cleanup`, nên agent trên
    board không bị.
  - Test sửa bằng `--run-libc-freeres=no` (chỉ bỏ phần glibc tự dọn khi thoát, không ảnh hưởng việc đếm leak của agent).
  - Nếu agent dưới valgrind vẫn không thoát, `do_valgrind` in dòng `K22: ...` và kill nó (`ICWMP_KEEP_STUCK=1` để giữ lại
    cho gdb). Mục đích: tiến trình kẹt không giữ `/var/run/icwmpd.pid` làm hỏng các lượt sau.
- Ghi chú phụ (Not established là có hại): `.dm_enabled_notify` được dựng lại bằng `remove()` rồi nhiều lần
  `fopen("a")`. SIGTERM đến giữa lúc dựng lại sẽ để lại file thiếu dòng. Đây là thiết kế upstream; chưa kiểm agent có
  dựng lại file này khi khởi động hay không.

**MTK SDK build tại `7e7f9c6` (07/10 18:56–19:09):**
- Export `--sdk mtk`, apply vào `1_src` (backup `.icwmp-backups/20261007-185618-4r8it51i`).
- Lượt 1 (`20261007-p7-7e7f9c6.log`): gói rc 0, image rc 0, **nhưng `libtr098` không có khóa CPEagent**.
  - OpenWrt build gói từ `openwrt-21.02.1_dev/feeds/airoha/`, là bản **copy** (`src-cpy`, ngày 25/09) của
    `airoha_feeds/`. `apply` chỉ ghi vào `airoha_feeds/`.
  - `apply` có in sẵn vòng `cmp || cp` để làm mới hai feed Makefile khi build gói lẻ. Script build của phiên này bỏ sót
    vòng đó. Các lần build trước không bị gì vì feed Makefile không đổi.
  - Bài học: build gói lẻ sau khi feed Makefile đổi thì phải chạy vòng đó trước.
- Lượt 2 (`20261007-p7-7e7f9c6-key.log`):
  - làm mới `feeds/airoha/.../libtr098/Makefile`;
  - Build/Prepare sinh `cpeagent_key_mtk.h`; hash khóa trùng với khóa trong shell sản phẩm, và khóa không có trong log build;
  - gói rc 0, không cảnh báo nào ở file P7, image rc 0;
  - `tclinux.bin` 19:08:49, md5 `f303253d1baefee3c2a95a2ef69d2eba`;
  - `libtr098.so.3.0.0` trong `root.squashfs` (19:08:25) có md5 `2c401049…`, trùng bản trong `root-airoha`, và có các
    module P7.
- Chưa nạp board (SSH vẫn đóng).

## 61. P8: 132 tham số còn lại sang C, toàn cây TR-098 bằng C (`0097`–`0099`, 07/10 22:35–)

**Kết luận:** `verify-dm-paths --phase 1..8` đều **thiếu 0**: cả **783/783** tham số của cây sản phẩm đã do C phục vụ.
Shell compat (`sdk/mtk/compat`) không còn tham số nào để phục vụ. Gỡ hẳn nó (`--disable-dm-script-compat`) là việc
riêng, cần test board trước.

| Commit | Nhánh | Tham số | File |
|---|---|---|---|
| `0097` | `Device.IP` (biến toàn cục, `Interface.{i}` + Stats, Add/Delete), `Device.IP.Diagnostics.TraceRoute` + RouteHops, `Device.DHCPv6.Server.Pool`, DOCSIS | 68 | `device_ip_mtk.c`, `device_traceroute_mtk.c`, `device_dhcpv6_mtk.c`, `docsis_mtk.c` |
| `0098` | `Device.PPP.Interface`, `Device.DynamicDNS`, `Device.RouterAdvertisement.InterfaceSetting` | 30 | `device_ppp_mtk.c`, `device_ddns_mtk.c`, `device_ra_mtk.c` |
| `0099` | `Services.StorageService` (+ LogicalVolume), `Services.STBService` | 34 | `services_mtk.c` |

**Đánh số instance khi GET:**
- Shell gán số instance ngay lúc trả lời GET rồi `uci commit` luôn:
  - `network.<sec>.ip_int_instance`;
  - `dhcp.<sec>.dhcpv6_int_instance` và `ra_int_instance`;
  - `wan.@entry[n].ppp_int_instance`;
  - và cả `ra_alias`.
- Nhờ vậy số mà ACS đã thấy không bao giờ đổi.
- `mtk_uci_set_persist()` làm y như vậy: ghi vào bản của phiên để getter cùng RPC thấy, và commit riêng option đó bằng
  một context UCI mới, không commit kèm các thay đổi SPV đang dở.
- Cách cấp số:
  - IP/DHCPv6/RA: số trống nhỏ nhất;
  - PPP: số lớn nhất cộng 1, như shell.

**Lỗi shell không mang sang (sửa có chủ đích):**
- `Device.PPP.Interface.` AddObject: shell lấy output của `uci add wan entry` làm chỉ số, nhưng đó là **tên section**
  (`cfgXXXXXX`).
  - Mọi `uci set wan.@entry[cfgXXXXXX]...` hỏng: entry rỗng, không phải PPP; sinh ra `network.ifcfgXXXXXX`; instance trả
    cho ACS không tồn tại.
  - C ghi đúng ý định: `id`/`name if<n>` theo chỉ số thật, PPPoE, số tiếp theo.
- `Device.DynamicDNS.Client.` AddObject: shell luôn trả instance **"1"**, nên ACS đặt `Client.1.*` sẽ sửa nhầm client có
  sẵn. C trả số thật của client mới.

**Khác shell, có chủ đích:**
- `ifdown`/`ifup`, flush IPv4 và launcher traceroute được xếp hàng cuối phiên. Shell chạy chúng trong setter (có
  `sleep 1`), có thể cắt WAN đang mang chính phiên CWMP.
- 12 bộ đếm `xsd:unsignedLong` ra dây là `xsd:string`, như khi đi qua shell bridge (engine không có kiểu này, §17.5).

**Giữ nguyên quirk của shell:**
- `Device.DynamicDNS.Client.{i}.Interface` đọc ra `Device.IP.Interface.<n>`, thiếu `InternetGatewayDevice.` ở đầu. Khi
  set, chỉ nhận đường dẫn mà trường thứ 4 (tách theo dấu chấm) là số. Vì vậy đường dẫn đầy đủ bị 9007, như trên shell.
- RouteHops chỉ liệt kê khi request trỏ vào dưới `RouteHops.`.
- StorageService có một instance 1 "không có đĩa" khi không có disk.
- `FolderNumberOfEntries` có thể ra -1.
- Engine trả tên tham số theo thứ tự tên (cả object C), không theo thứ tự config như shell. Thứ tự trong RPC không mang
  nghĩa.

**Hệ quả cho test host:**
- `notify` từng dựa vào nhánh shell giả: `IGD.Device.` cho tới 0096, rồi `IGD.Services.` ở 0097–0098.
- Từ 0099 test chạy trên tham số C: passive notification trên `IGD.X_AIS_Logging.`, đổi cả 24 option của
  `system.syslog` giữa hai phiên. Kết quả: 24/24 giá trị mới nằm trong `.dm_enabled_notify` và trong Inform.
- Test mới: `p8` (0097), `p8b` (0098), `p8c` (0099).
- `p8c` không mount được trong container. Kỳ vọng tính từ `/sys` của chính host như shell làm; đường mount/umount và đổi
  nhãn để board.

**Kiểm (host, ubuntu:24.04):**
- `run.sh all` tại 0097: rc 0 (notify 34/34 trên `Services.`).
- `run.sh all` trên cây cuối (0099):
  - mọi test PASS: smoke, notify 24/24 trên C, rpc, msrv, stun, ptime, p6, fw, p7, p7c, p8, p8b, p8c, valgrind 12 phiên
    0 lost 0 lỗi;
  - riêng `unit` lần đầu FAIL vì tham số forced-inform giả của harness (có instance) nằm dưới `IGD.Services.`, nay đã
    do C claim nên bridge bỏ qua. Đã chuyển sang `IGD.X_HNI_FakeShell.`, chạy lại `unit` PASS.
- Gate tĩnh: cross-gcc lib 68 file 0 lỗi, 14 cảnh báo thường như trước; check-c-sanity 0 (thêm `scandir`, `alphasort`,
  `fscanf`); claims 0 chồng.

**MTK SDK build tại `2ea7c00` (07/10 23:14–23:21):**
- Export `--sdk mtk` rồi apply vào `1_src` (backup `.icwmp-backups/20261007-231444-_qkszu1c`).
- Feed Makefile đã cùng nội dung với bản copy `feeds/airoha`, nên vòng `cmp || cp` không phải chép gì.
- Gói: rc 0, không cảnh báo nào ở file P8. Image: rc 0, log có `Enabling dev_access`.
- `tclinux.bin` 23:20:34, md5 `4626ae1ebaf98a385aa043a8b24e65af`, chép ra
  `1_src/2025q3/.icwmp-images/tclinux_p8_2ea7c00_devaccess.bin`.
- `root.squashfs` (23:20:11):
  - có `etc/init.d/dev_access` và `etc/rc.d/S11dev_access`, tức SSH/telnet tự mở sau boot (patch dev-access v1);
  - `libtr098.so.3.0.0` md5 `20914d44…`, trùng bản trong `root-airoha`, có các module P8 và khóa CPEagent.
- Chưa nạp board (SSH vẫn đóng).

## 62. K8: AddObject/DeleteObject của WAN connection sang C, lỗi merge quyền ghi trong registry (`0100`, 07/10 23:37–)

**Kết luận:**
- AddObject/DeleteObject của `WANIPConnection.` và `WANPPPConnection.` giờ do `wanip_mtk.c` trả lời, không qua compat
  shell nữa. **K8 đóng (host).**
- Rà cả 30 object có Add/Delete trong ma trận: 26 object khác đã đi C từ trước, 4 path còn lại đều thuộc K8. Sau `0100`
  không còn RPC nào của cây TR-098 cần đến shell. Compat chỉ còn trả các hàng container (`InternetGatewayDevice.`,
  `LANDevice.`, `Device.`…), mà C cũng trả các hàng này.
- **Lỗi engine tìm thấy khi làm:** `merge_entry()` (`dm_registry.c`) luôn lấy `permission` của module merge sau, vì
  trường này là con trỏ nên không bao giờ NULL.
  - Các module chỉ thêm nhánh con (`mtk-wanipv6`, xếp sau `mtk-wanip` theo tên) khai `&DMREAD` làm chỗ trống, nên
    `WANIPConnection.` / `WANPPPConnection.` thành read-only trong cây C. AddObject trên đường C trả 9005.
  - Trước đây lỗi không lộ: hai object chưa được claim nên shell trả lời, và trong GPN dòng của shell đến trước (writable 1).
  - **Sửa:** permission thuộc module browse/add/delete instance; module chỉ mở rộng thì không đổi được.
  - **Kiểm:** so cờ writable của mọi object trong cây C với cột perm của shell. Sau khi sửa chỉ còn `WLANConfiguration`
    khác, và đó là khác biệt có chủ đích (xem dưới).

| File | Thay đổi |
|---|---|
| `wan_mtk.c` | Claim cả nhánh `InternetGatewayDevice.WANDevice.` (một claim thay cho 92 claim lá/object cũ) |
| `wanip_mtk.c`, `wanipv6_mtk.c`, `portmapping_mtk.c`, `servicelist_mtk.c` | Bỏ `.paths`, cây merge vào claim trên (pattern của `managementserver_core_mtk.c`) |
| `wanip_mtk.c` | Hai hàm add dùng chung `add_conn_instance()`; số entry đếm trên một context UCI mới |
| `dm_registry.c`, `dm_registry.h` | Quy tắc permission khi merge |
| `wlan_mtk.c` | Header ghi lý do `WLANConfiguration` read-only; bỏ câu cũ "P3b còn ở shell" |

**Hành vi, so với shell (`functions/tr098/wan_device`):**
- **AddObject:**
  - gọi `ubus call hni.wan set {"action":"add","param":"IP"|"PPP"}`;
  - nếu `result` là `SUCCESS`, trả số entry của `wan`, đếm như `uci show wan | grep -c '=entry$'` của
    `wan_device_add_instance_ip/_ppp`, và không reload;
  - nếu không, trả 9002.
  - Đếm trên context mới vì entry do hni (một process khác) ghi; context của engine có thể còn giữ package từ trước lúc add.
- **DeleteObject:**
  - tìm theo instance (`id + 1`), rồi gọi `hni.wan set {"index":<vị trí>,"action":"delete"}`;
  - nếu `SUCCESS`, xếp `hni_wan_reload.sh` vào cuối phiên (`wan_device_del_instance`);
  - không có instance thì 9005, "xoá tất cả" cũng 9005 (shell không có thao tác này).
- **Hệ quả phụ:** compat walk không đi xuống `WANDevice.` nữa. GPV toàn cây không còn hỏi shell bất cứ gì của nhánh WAN.

**`WLANConfiguration` read-only, khác biệt có chủ đích:** có từ P3, đến giờ mới ghi lại. Chi tiết ở `other-findings`
mục 5 của issue trong workspace.
- Shell đăng ký object writable, với `lan_device_add_wlan_iface` / `lan_device_delete_wlan_iface`. Cả hai hỏng trên sản phẩm này.
- **Add:**
  - tính `max instance` qua `wireless.@wifi-iface[N].instance`, không khớp section có tên (`wireless.ra0`…), nên lần
    đầu trả instance 1, trùng `ra0`;
  - để lại một `wifi-iface` vô danh trên `wl0`, radio không có trên board;
  - browse bảng cố định 12 interface nên section mới không thành instance.
- **Delete:** chạy `uci delete wireless.<ra0…rai5>`, xoá một interface thật.
- **Bản C:** AddObject/DeleteObject trả 9005, GPN báo `writable 0` (shell báo 1).
- **Phạm vi:** image từ P3 tới nay đã trả như vậy, vì object được claim từ 0039.

**Test host:**
- **`run.sh wan` (mới)** dùng một stub `hni.wan`, kiểm:
  - add IP/PPP trả `3`/`4`;
  - entry mới đúng `conn_type`;
  - số entry và danh sách instance;
  - add không reload;
  - delete theo instance xoá đúng vị trí, mỗi delete một reload;
  - hni `FAIL` → 9002, instance không có → 9005;
  - shell giả không nhận lệnh `add`/`delete` nào và không bị hỏi path nào dưới `WANDevice.`;
  - cờ writable của mọi object khớp shell, trừ `WLANConfiguration`.
- **`fake_dm.py`** ghi thêm path của mỗi lệnh vào `FAKE_DM_LOG`.
- **`smoke`:** fault từ 20 lên 25, thêm 1 mỗi phiên. Một phiên `smoke 1` cho 5 fault: GPV `Nope.X` 9005, SPV k1 9003
  (`Firewall.Config` 9008), SPV k3 9003, AddObject 9002, DeleteObject 9005.
  - `DeleteObject WANIPConnection.2.` của plan nay vào C, mà `wan` của host không có instance 2, nên 9005. Trước đây
    shell giả nhận mọi path.
  - `AddObject` vốn đã 9002 từ trước (shell giả trả status mà không có instance); nay 9002 vì host không có `hni.wan`.
- **`run.sh all` tại cây cuối (container `ubuntu:24.04`, xong 08/10 00:06):** EXIT 0, mọi test PASS:
  - unit 3/3 (GPV gốc: 0 getter shell);
  - smoke 5 phiên, notify 24/24, rpc 5/5, msrv, stun, ptime 2/2;
  - p6, fw, p7, p7c, p8, p8b, p8c, wan;
  - valgrind 12 phiên, 196 RPC, 0 lost, 0 lỗi.

**Gate tĩnh:**
- verify-dm-paths: thiếu 0, dôi 19 (`X_HNI_Icwmp`, như trước);
- claims: 82 claim, 45 module, 0 cặp chồng (trước 173 claim);
- check-c-sanity lib 68 file 0 vấn đề; check-automake-conds 0;
- cross-gcc SDK: lib 68 + app 17 file, 0 lỗi; cảnh báo chỉ ở file cũ (`dmcommon.c`, `dmjson.c`, `dmtr098.c`, `sdk.h`,
  `softwaremodules.c`), không có ở file sửa.
- Quét mọi SDK: 32 dòng object `&DMWRITE` đều có browse/add/delete. Không module nào dựa vào luật merge cũ để làm object
  writable, nên luật mới chỉ trả lại quyền của module chủ. BDK chưa build lại.

**MTK SDK build tại `d3f7459` (08/10 00:07–00:13):**
- Export `--sdk mtk` (356 file, sha256 `d1cb2612…`) rồi apply vào `1_src` (backup `.icwmp-backups/20261008-000738-jvfc05b2`).
- Gói và image: rc 0. Log có `Enabling dev_access`; không có cảnh báo nào ở các file sửa.
- `tclinux.bin` 00:13:26, md5 `bc032690e2e4e883bfd4e4a7ef33859b`, chép ra
  `1_src/2025q3/.icwmp-images/tclinux_k8_d3f7459_devaccess.bin`.
- `root.squashfs`:
  - có `etc/init.d/dev_access` và `etc/rc.d/S11dev_access`;
  - `libtr098.so.3.0.0` md5 `27ee9edd…`, trùng bản trong `root-airoha`;
  - có chuỗi claim `InternetGatewayDevice.WANDevice.`, không còn claim cũ `WANDevice.{i}.WANCommonInterfaceConfig.`.
- Image này thay `tclinux_p8_2ea7c00_devaccess.bin` để nạp: cùng P8, thêm 0100.

**Board:** chưa. SSH vẫn đóng (chưa nạp image nào có dev-access).

## 63. Board với image `d3f7459`: so toàn cây C với shell sản phẩm, K8 và SPV trên board, sửa `0101` (08/10 09:37–)

User nạp `tclinux_k8_d3f7459_devaccess.bin` qua WebUI và cho phép Claude tự nạp FW trong phiên này (chatlog mục 85).

**Board sau khi nạp:**
- SSH mở (dev-access v1, account dev lấy từ patch, chỉ giữ trong biến môi trường);
- `libtr098.so.3.0.0` md5 `27ee9edd…`, đúng bản trong image;
- 3 phiên ACS đều `success`, 0 failure.

**GPV toàn cây:** C 4 s qua `ubus tr069 dm`, shell sản phẩm 23 s qua `icwmp_dm.sh`. Image 0083 còn compat là 15–16 s.

### 63.1 So từng tham số C với shell trên cùng board

`tests/board/parity_dump.sh` (chạy trên board) dump GPV + GPN toàn cây từ hai phía, và `tests/board/parity.py`
(chạy trên host) phân loại mọi khác biệt. Trên image `d3f7459`:

| Lớp | Số tham số | Ghi chú |
|---|---|---|
| Bằng nhau | 1468 | |
| Bộ đếm, đồng hồ | 97 | hai lần đọc cách nhau ~20 s |
| Khác biệt đã biết | 16 | CR URL và ParameterKey của icwmpd, PPP Password đọc ra rỗng, rate 5 GHz, DUID |
| Nháy của shell | 31 | getter `"echo \"\""` chạy qua `$(...)` không eval: shell trả `""`, `"Synchronized"` |
| **Không giải thích được** | **24** | sửa ở `0101`, xem 63.2 |

Tập tham số: C 1769, shell 1752, chung 1751.
- Chỉ ở C: 11 lá ManagementServer (K3), `X_HNI_Icwmp`, và `Account.Web.SessionMaxTime`. Shell thiếu nhánh `"$DMROOT."`
  trong `entry_execute_method_root_Account_Web`, nên chỉ trả lá này khi hỏi đúng `Account.Web.`.
- Chỉ ở shell: `ManagementServer.ConnReqXMPPConnection`.

GPN: cờ writable chỉ khác ở `WLANConfiguration` (K24).

Kiểu khác ở 664 tham số. cwmpclient gửi `xsd:string` khi shell không khai kiểu, và gửi nguyên chữ khi có kiểu, kể cả
`xsd:IPv4Address`, `xsd:unsignedint`. Giá trị y hệt; engine không phát được kiểu ngoài chuẩn. **Chấp nhận**, không sửa.

### 63.2 Lệch thật, sửa ở `0101`

| Nhóm | Phase | Lỗi | Sửa |
|---|---|---|---|
| Chữ boolean, 21 mẫu | P1 Time, P2 LAN/Hosts/DHCP, P3 WLAN + AssociatedDevice | C trả `1`/`0`, shell (cả 2 giá trị, đọc getter) `true`/`false`. Trái §17.4. Có từ 0037–0039, đã chạy trên mọi image | getter trả `true`/`false`; Time so `"1"` đúng như shell |
| `LANHostConfigManagement.MACAddress` | P2 | C đọc sysfs (chữ thường), shell `ifconfig` (chữ hoa) | đổi sang chữ hoa |
| `LANEthernetInterfaceConfig.Stats` | P2 | `gsw_sum()` dừng ở key đầu tiên trên một dòng, mà `/proc/tc3162/gsw_stats` in hai bộ đếm một dòng: PacketsReceived/Sent thiếu multicast, DiscardPacketsReceived thiếu `Rx ING Drop`/`Rx FILTER Drop`. Board port 3: shell 1977 = 1870 + 95 + 12, C ~1884 | cộng mọi key trên dòng, như `LANEthernet_Stats_sum_keys` |
| `ManagementServer.ConnReqXMPPConnection` | P1 | lá portable chỉ có dưới `#ifdef XMPP_ENABLE`, build MTK không bật | thêm lá read-only vào `managementserver_mtk.c`, trả `""` (shell trả nháy) |
| `verify-dm-paths.py` | công cụ | không xét `#ifdef` nên báo "thiếu 0" sai | bỏ nhánh `#ifdef`/`#ifndef` không build theo macro của SDK; chạy với code cũ báo đúng thiếu 1 |

Bộ đếm Stats của LANEthernet nằm trong lớp "bộ đếm" của parity.py, nên công cụ không tự bắt được lỗi `gsw_sum`. Lỗi
này tìm ra bằng cách hỏi riêng từng nhánh `.Stats.`; GET cả object qua shell trả 0 ở mọi bộ đếm, cũng là lỗi của
đường bulk bên shell.

**Lỗi shell, C giữ giá trị đúng (khác biệt có chủ đích):**
- `BasicDataTransmitRates` / `OperationalDataTransmitRates`: `case "$iface" in ra*)` khớp cả `rai*`, nên shell báo
  rate 2.4 GHz cho cả 6 interface 5 GHz. C trả `6,12,24` như shell định viết.
- `Device.DHCPv6.Server.Pool.{i}.DUID`: shell định viết chữ hoa (`tr '[:lower:]' '[:upper:]'`), nhưng busybox `tr`
  của board đổi theo từng ký tự (`echo lower | tr ...` ra `upper`), nên ACS thấy chữ thường. C giữ chữ hoa;
  `parity.py` so không phân biệt hoa thường.
- Nháy thừa (`""`, `"Synchronized"`): C trả giá trị shell định viết.

### 63.3 K8 trên board (`hni.wan` thật, qua `ubus tr069 dm`, cùng đường code với RPC)

Sao lưu `/etc/config/wan` và `network` trước khi test. Board có một entry WAN (PPPoE id 0).

| Bước | Kết quả |
|---|---|
| GPN `WANConnectionDevice.1.` | `WANIPConnection.` và `WANPPPConnection.` đều `writable 1` (K23 trên board) |
| add `WANIPConnection.` | instance `2`; entry id 1 `conn_type 0`, `active 0`; ParameterKey `k8a`; không reload; `if0` vẫn up |
| add `WANPPPConnection.` | instance `3`; entry id 2 `conn_type 2`; số entry IP 1, PPP 2; instance IP 2, PPP 1, 3 |
| del `WANIPConnection.2.` | `hni.wan` xoá vị trí 1; `if0` uptime 8 s lúc 10:00:38 (reload cuối phiên đã chạy), up lại |
| del `WANPPPConnection.3.` | xoá, reload, `if0` up; `WANIPConnection.9.` → 9005 |

**Sau test:** `wan` về một entry như cũ. Có hai thay đổi do `hni.wan`, không do C:
- `wan.pending.wan_id '0'` đã được reload xử lý;
- `network.lan.ip6class` còn `if1_6` và `if2_6` của hai entry đã xoá. Đây là lỗi của `hni.wan`
  (other-findings của issue trong workspace); đã gỡ bằng `uci del_list` và commit.

ACS vẫn ping được.

### 63.4 SPV trên board (ghi, đọc lại, trả về)

Ghi rồi trả về đều fault 0, đọc lại đúng:
- **P6:** `CarrierLocking.X_AIS_RoundNum`, `X_AIS_WebUserInfo.RemoteAccessTimeout`;
- **P7:** `X_AIS_Logging.DebugEnable`, `X_AIS_UPnP.Enable`, `X_AIS_MeshAPI.Delay_time`, `X_AIS_Conf.auto_upload_delay`;
- **P8:** `RouterAdvertisement.InterfaceSetting.1.MaxRtrAdvInterval`, `DynamicDNS.Client.1.Server`.

`CurrentLanguage=th` → 9007. Đúng: `clay.language.available` của board chỉ có `en`, và shell (`set_CurrentLanguage`)
cũng từ chối. Giá trị sai `abc` và `a;b` → 9007, giá trị cũ giữ nguyên.

### 63.5 Nạp `0fa9d31` (0101) qua SSH, parity lại, G9

**Build** tại `0fa9d31`:
- export 359 file, sha256 `5f7d3c99…`; apply vào `1_src`, backup `.icwmp-backups/20261008-101029-jhzg9h2p`;
- gói và image rc 0, có `Enabling dev_access`, không cảnh báo ở file sửa;
- `tclinux.bin` md5 `727f3862a785f5368358a2d04360fbac`, chép ra `1_src/2025q3/.icwmp-images/tclinux_parity_0fa9d31_devaccess.bin`;
- `libtr098` md5 `ca8dffc5…`, có `ConnReqXMPPConnection`.

**Nạp** (Claude, user cho phép trong phiên):
- Các bước như §50.1: md5 hai đầu khớp, `Model validation successful: HP-2236B`, `"valid": true`, `sysupgrade -T` rc 0.
- **Lần 1 (10:17) không chạy.** Board không có `setsid` (cũng không có `nohup`), và output bị đẩy vào `/dev/null` nên
  không thấy lỗi `setsid: not found`. `/tmp/sysupgrade.meta` là do bước `-T` tạo. Không phải thiếu RAM: MemAvailable
  77 MB khi image 58 MB đang nằm trong `/var/tmp` (Shmem 67 MB), bằng mức của lần nạp đạt ngày 05/10.
- **Lần 2 (10:23:40):** `start-stop-daemon -S -b -m -p /tmp/sysupg.pid -x /bin/sh -- -c "sysupgrade … > log"`. Cần
  pidfile riêng, nếu không nó báo `/bin/sh is already running`. procd đóng SSH sau ~4 s; board lên lại, kiểm lúc 10:26:52.

**Sau khi nạp:**
- uptime 2 phút; `libtr098` `ca8dffc5…`; 2 dòng start, không crash; 6 phiên ACS success, 0 failure; `if0` up;
- cấu hình giữ qua `sysupgrade`.

**Parity trên `0fa9d31`:**
- 1724 tham số chung: 1579 bằng, 100 động, 15 đã biết, 29 nháy;
- mục mới duy nhất là `UDPConnectionRequestAddress`: port 2314 → 2315 giữa hai lần đọc vì STUN client vendor ánh xạ
  lại, đưa vào lớp động;
- **PASS**: 24 mục của 63.1 đã hết.
- `LANEthernet` Stats port 3: C `PacketsReceived` 389 = unicast 329 + multicast 50 + broadcast 10, khớp cách shell cộng;
  port 1 `DiscardPacketsReceived` 303 bằng shell.

**G9 (soak 24 h)** chạy lại trên image này từ 10:27:56:
- `/tmp/g9.sh` lấy mẫu mỗi 600 s × 150 mẫu vào `/tmp/g9.csv` (chạy bằng `start-stop-daemon`);
- mẫu đầu: pid 10704, VmRSS 5960 kB, fd 14, thread 11, 10 phiên success, 0 failure, MemAvailable 134296 kB;
- mốc 24 h: 09/10 10:28.

## 64. PH0.5: đóng băng baseline, G9 không còn chặn (08/10 10:36–)

**Quyết định của user (08/10 10:36, chatlog 87):** G9 không chặn tiến độ nữa. Trong lúc phát triển, board sẽ được nạp
lại liên tục nên khó giữ uptime 24 h. Board chạy được đủ lâu thì kiểm; không thì ghi lại bằng chứng đã có. PH0.5
(đã duyệt 07/10) làm ngay, không chờ G9.

**Bằng chứng soak đã có:**

| Nơi | Image / code | Thời gian | Kết quả |
|---|---|---|---|
| Board | 0080 (05–06/10, §52) | 8 h 41, 100 mẫu | VmRSS 5344→5352 kB, fd 14–15, thread 11, MemAvailable không giảm |
| Host | 0088 (07/10, §57) | 300 phiên | VmRSS 13,2–14,3 MB không tăng, fd 14–18, thread 11 |
| Host | 0100 (08/10, §62) | valgrind 12 phiên, 196 RPC | 0 lost, 0 lỗi |
| Board | `0fa9d31` (0101) | 10:27:56–10:37:56, 2 mẫu | pid 10704 không đổi, VmRSS 5960→5748 kB, fd 14, thread 11, 50 phiên success, 0 failure, start vẫn 2 |

G9 24 h trên image cuối: **chưa có**. Rủi ro còn lại là rò rỉ chậm chỉ lộ sau nhiều giờ trên board thật. Hai lần
trước trên board (0080, 8 h 41) và soak host không thấy dấu hiệu nào.

**Baseline:**
- Tag `baseline/ph0-mtk-tr098-20261008` trên `dev` tại commit tài liệu của mục này; `main` fast-forward tới đó.
- Code: 0101 (`9d342c5`) + 0102 (`b433d29`, chỉ công cụ test).
- Board: image `0fa9d31` (`tclinux.bin` md5 `727f3862…`, `libtr098` md5 `ca8dffc5…`), build compat-on: shell vẫn được
  cài nhưng không còn tham số hay RPC nào đi qua nó (§62).

**Đã qua:** G1–G7 phía router (§48–§54), parity toàn cây với shell sản phẩm (§63), K8 qua `hni.wan` thật (§63.3).

**Mở, mang sang sau:**
- G9 24 h trên board: kiểm khi có dịp.
- G6: xem WebUI (cần tài khoản).
- G8: Download sai trên board (user bỏ khi test bằng NBI).
- K15: hoãn theo quyết định 07/10.

Mọi thay đổi kiến trúc từ đây so với baseline này. Bước kế tiếp là PH5 (tắt compat), kéo lên trước PH1–PH3 vì PH4
(P6–P8) đã xong theo cách làm thực tế (0089–0101). Tắt compat là cách duy nhất chứng minh "783/783 bằng C" trên
board: khi compat còn bật, parity không bắt được tham số C thiếu, vì shell trả lời thay ở cả hai bản dump.

## 65. PH5: build MTK không còn shell bridge (`0103`, 08/10 10:45–)

**Vì sao làm ngay:** từ 0099/0100 không còn tham số hay RPC nào đi qua shell, nhưng bridge vẫn được build và
`icwmp_dm.sh` vẫn được cài. Khi bridge còn, một path cây C thiếu vẫn được shell trả lời. Vì vậy cả test host lẫn parity
board (§63) đều không chứng minh được cây C đủ. Build compat-off là phép thử thật.

**Thay đổi (`aab44c6`, 0103):**
- `feeds/libtr098`: configure `--disable-dm-script-compat`, không cài `icwmp_dm.sh`. Thư viện hàm easycwmp vẫn cài:
  diagnostics C chạy `*_launch` của nó (`diag_mtk.h` `DIAG_FUNCTION_PATH`), `stuncd` của sản phẩm cũng dùng. Rollback:
  bỏ cờ, trả hai dòng cài lại (ghi sẵn trong file).
- `tests/host/build.sh`: configure libtr098 như sản phẩm; `ICWMP_HOST_DM_COMPAT=1` build lại có bridge. Đổi biến thể
  thì build lại từ sạch: cờ là `-D` trên dòng lệnh, không có `config.h`.
- `run.sh full` (mới):
  - `X_HNI_Icwmp.DataModelBackend` = `mtk-c`;
  - một phiên ACS và GPV/GPN toàn cây không khởi động shell lần nào (`fake_dm.py` ghi mọi lệnh nhận được);
  - mọi tên trong cây thuộc ma trận coverage, object của icwmpd hoặc 11 lá K3.
- `run.sh wan`: với compat-off, shell không được chạy lần nào. Bản cũ đòi shell phải được hỏi khi duyệt `IGD.`.
- `parity_dump.sh`: `DM_SH=` trỏ tới bản `icwmp_dm.sh` chép vào board.
- `verify-dm-paths.py`: macro build MTK bỏ `DM_MTK_SCRIPT_COMPAT`. Kết quả không đổi: thiếu 0, dôi 17, claim chồng 0.

**Host (container `ubuntu:24.04`):**
- `run.sh all` trên build compat-off: mọi test PASS, gồm valgrind 12 phiên / 196 RPC. Lần chạy đầu `wan` FAIL vì đúng
  giả định cũ nói trên; sửa test rồi chạy lại thì PASS.
- `full`: 1204 giá trị, 1481 tên, 0 tên ngoài ma trận. 603/967 mẫu path của ma trận có mặt; phần còn lại là object
  không có instance trên host, phần đó do `verify-dm-paths.py` kiểm tĩnh.
- `smoke`: 25 fault, bằng bản compat-on (các fault cố ý của `acs.py`).
- `ICWMP_HOST_DM_COMPAT=1`: `wan` và `smoke` PASS, `full` FAIL đúng thiết kế (`DataModelBackend` = `mtk-c+script`).

**MTK SDK build tại `2cca863` (11:00–11:06):**
- Export sha256 `00031d97…`, apply vào `1_src` (backup `.icwmp-backups/20261008-110023-z8q5as1q`).
- Gói rc 0, image rc 0, có `Enabling dev_access`; configure có `--disable-dm-script-compat`; không cảnh báo mới ở file
  icwmp.
- `root.squashfs`:
  - không còn `usr/share/icwmp/`;
  - có `dev_access`, `stuncd`, `easycwmp/functions/ipping_launch`;
  - `libtr098.so.3.0.0` 777 680 B, md5 `8287bba1…`, trùng `root-airoha`. Không còn chuỗi `icwmp_dm.sh`, `set_apply`,
    `mtk-c+script`; có `mtk-c`.
- `tclinux.bin` md5 `4ff59ae971e12d67366c8be43e28b363`, chép ra
  `1_src/2025q3/.icwmp-images/tclinux_ph5_2cca863_devaccess.bin`.

**G9 trên `0fa9d31` trước khi nạp đè (§64):**
- 4 mẫu, 10:27:56–10:57:57;
- pid 10704 không đổi, VmRSS 5748–6000 kB, fd 14, thread 11;
- 130 phiên success, 0 failure, start vẫn 2.

**Nạp (Claude, user cho phép trong phiên):**
- md5 hai đầu khớp, `Model validation successful: HP-2236B`, `"valid": true`, `sysupgrade -T` rc 0;
- `start-stop-daemon … sysupgrade` 11:07:39; board vào `Commencing upgrade` 11:07:41.

**Board với image `2cca863` (kiểm 11:10:59, uptime 153 s):**
- `libtr098` md5 `8287bba1…` đúng image; không có `/usr/share/icwmp`; không có tiến trình shell nào;
- `X_HNI_Icwmp.DataModelBackend` = `mtk-c`; 7 phiên ACS success, 0 failure; 2 dòng start, không crash.

**Parity compat-off** (`DM_SH=/tmp/icwmp_dm.sh`, `icwmp_dm.sh` chép từ repo):
- Thời gian: GPV toàn cây C **1 s** (compat-on 3–4 s), GPN 1 s; shell 23 s.
- **Tên: không có tên nào chỉ có ở shell.** Cây C đủ trên board thật, lần đầu chứng minh được khi không còn shell trả
  lời thay. Tên chỉ có ở C là những tên đã biết (K3, `X_HNI_Icwmp`); writable lệch 13 tên, đều là `WLANConfiguration`
  (K24).
- Giá trị, 1724 tham số chung: 1577 bằng, 99 động, 15 đã biết, 29 nháy. Còn lại:
  - `TemperatureSensor.1.Value`: C 56, shell 57, đọc cách nhau vài giây → đưa vào lớp động của `parity.py`;
  - `Time.NTPServer1`/`2` rỗng, `NTPServer3` = `h\x8f\u0016fU`: **K28**, dưới đây. `parity.py` dừng vì byte không phải
    UTF-8; nay đọc bằng `errors="replace"` để báo thành UNEXPECTED.
- Stderr của shell sản phẩm (`sh_get.err`): `wan_common_get_access_type: not found`,
  `traceroute_routehops_browse_instances: not found`, 12 lần `Failed to parse json data`. Đây là lỗi sẵn có của thư viện
  shell, không thuộc C.

### 65.1 K28: giá trị `NTPServer` đọc sau khi bộ nhớ UCI bị giải phóng

**Hiện tượng:** trên board, GPV toàn cây trả `NTPServer3` là rác, `NTPServer1/2` rỗng. Thử lại 5 lần (3 lần nhánh
`Time.`, 2 lần toàn cây) không tái lập: lỗi chập chờn.

**Cơ chế (Verified trên host dưới valgrind):**
- `ntp_server_get()` (`time_mtk.c`, có từ P1) trả `e->name`, tức con trỏ vào phần tử list `system.ntp.server` trong
  package UCI đã nạp.
- `add_list_paramameter()` (`dmtr098.c:686`) giữ nguyên con trỏ giá trị; quy ước của engine là getter tự cấp phát
  (`dmstrdup`).
- Sau khi duyệt xong, `dm_entry_param_method()` gọi `dmuci_commit()` (`dmentry.c:333`). Hàm này commit mọi package trong
  `/etc/config`. Package nào có delta chưa commit (tiến trình khác vừa `uci set`) thì `uci_file_commit` của libuci giải
  phóng rồi nạp lại.
- Reply được ghi sau đó (`icwmp_dm.c:258`, cũng như XML của phiên CWMP) và đọc vào vùng đã giải phóng.
- Valgrind trên host: tạo `system` có 3 server cộng một delta chưa commit, rồi GPV toàn cây.
  - `Invalid read` trong `dm_add_param_list`.
  - Khối 14 byte (`"time.nist.gov\0"`) bị giải phóng bởi `uci_free_package` ← `uci_file_commit` ← `dmuci_commit` ←
    `dm_entry_param_method`.
- Trên board, delta của `system` do tiến trình sản phẩm để lại; tiến trình nào thì **chưa xác định**.

**Ảnh hưởng:** GPV của ACS (phiên CWMP đi cùng đường) có thể nhận rác hoặc chuỗi rỗng ở `NTPServer1..5`. Chưa thấy crash,
vì vùng đọc vẫn nằm trong heap.

**Sửa (0104):**
- `ntp_server_get()` trả `dmstrdup(e->name)`.
- Rà mọi getter MTK: `time_mtk.c` là nơi duy nhất trả `e->name`. `mtk_uci`, `mtk_state`, `mtk_uci_default` đã sao chép.
  `mtk_varstate` thì không: `dmuci_get_varstate_string` trả `v.string` của `uci_varstate_ctx`, chỉ hỏng nếu trong cùng
  request có ghi varstate (một getter dùng, chưa thấy lỗi). Sửa luôn cho sao chép.

**Test hồi quy:** `run.sh valgrind` dựng `system` có NTP server. Mỗi vòng tải ubus để lại một `uci set system…` chưa
commit rồi GET `Time.`.
- Code cũ: FAIL, 1560 lỗi valgrind.
- Code sửa: PASS, 0 lost, 0 lỗi.

**Phát hiện phụ:** `dmuci_commit()` sau mọi lệnh, kể cả GET, commit luôn thay đổi chưa commit của tiến trình khác (hành vi
upstream). Ghi ở `other-findings/icwmp-get-commits-other-uci-changes.md` của workspace, không sửa trong phạm vi này.

### 65.2 Image `9f393e4` (0104) trên board, parity PASS

**Host tại 0104:** `run.sh all` 24/24 PASS, EXIT 0, gồm `full` và valgrind có tải K28.

**Build** (11:30–11:36):
- export sha256 `b3f7e533…`, backup `.icwmp-backups/20261008-113026-rxbozm_4`;
- gói rc 0, image rc 0, có dev_access, không có `usr/share/icwmp/`;
- `libtr098` md5 `5d73ec37…`, trùng `root-airoha`, 0 chuỗi của shell bridge;
- `tclinux.bin` md5 `6fef74bbd7fc901d65c75d853c03f1a1` → `.icwmp-images/tclinux_k28_9f393e4_devaccess.bin`.

**Phiên lỗi trên `2cca863`:** trước khi nạp đè, `tr069 status` báo 113 success, **1 failure** (11:09–11:33).
- Log đã xoay vòng, phần còn lại (từ 11:33) không có dòng ERROR/WARNING.
- GenieACS NBI (chỉ đọc `GET /faults`, lọc theo OUI `000378` + serial): không có fault nào của board.
- Nguyên nhân: **chưa xác định**. Một khả năng là rác của K28 trong XML gửi ACS; chưa chứng minh.
- Image `0fa9d31` (compat-on) trước đó: 130/0.
- Theo dõi bằng G9 trên image mới.

**Nạp** 11:38:11–11:38:14 (cùng các bước), kiểm 11:41:18.

**Board sau khi nạp:**
- uptime 146 s, `libtr098` `5d73ec37…`, không có `/usr/share/icwmp`;
- 6 phiên success, 0 failure; 2 dòng start, không crash.
- `/tmp/.uci/` có file delta của 8 package, nhưng đều rỗng (0 dòng, chỉ còn vỏ sau commit): không có thay đổi chưa
  commit nào.

**Parity compat-off: PASS.**
- 1724 tham số chung: 1583 bằng, 97 động, 15 đã biết, 29 nháy.
- Không tên nào chỉ có ở shell; writable lệch 13 tên, đều thuộc K24.
- `NTPServer1..3` = `time.nist.gov`, `2.th.pool.ntp.org`, `3.asia.pool.ntp.org` như config, 4 và 5 rỗng.
- GPV/GPN toàn cây C 1 s mỗi lệnh, shell 23 s.
- Sau dump: 11 phiên success, 0 failure.

**G9 trên image cuối** chạy lại từ 11:42:44 (`/tmp/g9.sh 600 150`):
- mẫu đầu: pid 10252, VmRSS 6096 kB, **fd 12**, thread 11;
- fd 12, giảm 2 so với 14 của bản compat-on, vì hai pipe tới tiến trình shell không còn.

**Kết luận PH5:** build sản phẩm MTK không còn shell. Cây TR-098 C đủ trên board thật, đã đối chiếu với shell sản phẩm.
Source `sdk/mtk/compat/` vẫn giữ để rollback và làm driver cho parity; xoá hẳn (K7) sau một bản giao không cần rollback.

## 66. Bản giao đầu tiên MTK full C: `release/mtk-20261008` (08/10 11:53–)

**Yêu cầu của user (08/10 11:53, chatlog 88):** push; bàn giao code MTK full C, đủ tham số, không còn chạy `.sh`, làm bản
base và release đầu. Sau đó viết tài liệu từng bước (máy nào, lệnh gì, để làm gì, kết quả gì) để tự chạy lại và học.

**Push 11:56:** `dev` `5df1675..c386821`, `main` `4965f5d..c022de1`, tag `baseline/ph0-mtk-tr098-20261008`. Trước khi push
đã rà bí mật: khóa CPEagent 0 trong cây và lịch sử, mật khẩu dev 0.

**"Không còn chạy `.sh`" nghĩa là gì (Verified từ source 0104):**
- Đường GET/SET/ADD/DEL/notify/Inform của data model là C. `icwmp_dm.sh` không build, không cài.
- Shell còn chạy là hành động của sản phẩm, firmware cũ cũng gọi y hệt:
  - restart dịch vụ sau SET: init script, `hni_wan_reload.sh`, `start_wsl.sh`;
  - launcher chẩn đoán `/usr/share/easycwmp/functions/*_launch` khi `DiagnosticsState=Requested`;
  - `/usr/sbin/icwmp` cho Download/Upload/nạp firmware/Reboot/FactoryReset;
  - `sh -c` một dòng để ghi output của `mwctl`/`iw` (`X_AIS_WiFiStatus`).
- Bỏ nốt phần này nghĩa là port launcher chẩn đoán và hành động sang C. Bản giao này chưa làm; tài liệu hướng dẫn mục 2
  ghi rõ.

**Code của bản giao** = 0104 + 0105 (`tests/board/soak_sample.sh`, chỉ là công cụ test).
- Apply bundle vào `1_src` (backup `.icwmp-backups/20261008-120159-6jo_3lt1`) chỉ ghi `.icwmp-release.json`.
- Nghĩa là `libicwmp_dm`, `icwmp_tr098` và feed Makefile trùng từng byte với bản đã build thành image `9f393e4`
  đang chạy trên board (§65.2). Không cần build lại.

**Kiểm bundle** (thư mục `icwmp_mtk_26580fe0d1c3`, 361 file, gồm `docs/`, `tests/host`, `tests/board`):
- `sha256sum -c SHA256SUMS` sạch.
- `apply --dry-run` rồi apply thật: như trên.
- Test host chạy từ chính bundle (container `ubuntu:24.04` mount bundle, đúng các lệnh C1–C6 của tài liệu):
  `apt=0`, `build.sh` ok, `setup.sh` ok, `run.sh full` PASS, `run.sh all` exit 0, 24 PASS, 0 FAIL.
- Các lệnh E1, E2 (dạng không tương tác), E6 và G1–G13, H1–H2 của tài liệu cũng đã chạy thật ngày 08/10.
- G9 có chuỗi rỗng bị 9007: đó là hợp đồng input của sản phẩm, nên tài liệu dùng ví dụ đổi rồi trả lại `NTPServer3`.

**Tài liệu:** [handover/icwmp_mtk_build_verify_guide.md](../handover/icwmp_mtk_build_verify_guide.md). Các bước A–H và
rollback, mỗi bước một bảng gồm máy, lệnh, mục đích, kết quả đạt.

**Tag:** `release/mtk-20261008` (annotated) ở commit tài liệu chứa mục này. Message của tag ghi sha256 của
`./export.py --sdk mtk` tại tag. Bundle chép ra workspace `issues/20260922_icwmp_multiplatform_tr098/release/`.

**Còn mở:**
- G9 24 h: đang chạy trên `9f393e4` từ 11:42:44; 2 mẫu đầu phẳng (fd 12, 52 phiên, 0 lỗi).
- G6: xem WebUI.
- K15: hoãn.
- 1 phiên lỗi trên image `2cca863`: chưa rõ nguyên nhân.
- Image giao khách phải build **không** có patch dev-access.

## 67. TR-181 trên MTK, branch `dev_181`: T0 nền và T1 object hệ thống (08/10 12:44–)

**Quyết định của user:**
- **Chatlog 89:** bản giao TR-098 đạt yêu cầu, vì phần tham số/xử lý đã là C; chẩn đoán và script hành động giữ
  shell. Làm TR-181 trên branch mới `dev_181`, tách từ `dev` tại `dc3d7f7`.
- **Chatlog 90:** phạm vi trước hết là TR-181 tương đương 783 tham số TR-098. Sau đó tham khảo TR-181 của BDK, chỉ
  thêm tham số cần dùng.

Thiết kế: [../plan/tr181_mtk_design.md](../plan/tr181_mtk_design.md).

**Nguồn TR-181 sẵn có của sản phẩm (Verified):**
- easycwmp có `functions/tr181`: 18 file, 6336 dòng, 297 tham số / 86 object.
- Package `cwmpclient` **không cài** thư mục đó: dòng cài bị comment trong Makefile, sản phẩm chỉ ship TR-098.
- Các file này là bản upstream PIVA. Không có tham chiếu HNI nào (`hni`, `mwctl`, `wan.@entry` đều 0), trong khi 32/60
  file `tr098` đã được sửa cho sản phẩm. Vì vậy chúng không dùng làm chuẩn hành vi được.
- Thiếu Time, PPP, Firewall, DHCPv6 và mọi `X_AIS_*`.

**Tham chiếu dùng được:**
- Ma trận TR-098 783 tham số.
- Ánh xạ TR-098 ↔ TR-181 đã làm cho BDK (`projects/brcm_ap_wifi7_mvn/docs/icwmp_tr098_bdk_mapping_matrix.md`).
- Schema TR-181 của BDK, qua `docs/issue/tr181-schema.py`: chỉ lấy tên `specSource="TR181"`, đọc lúc chạy, không chép
  vào repo public. Bảng tra có 512 object, 4206 tham số.
- Cây TR-098 của sản phẩm đã có nhánh `InternetGatewayDevice.Device.*` viết theo TR-181 (IP.Interface trên mọi
  interface, đánh số bền bằng `ip_int_instance`; PPP, DHCPv6, DynamicDNS, RouterAdvertisement, TraceRoute).

**T0 (`4cd9b1d`, `[icwmp tr181-0001]`):**
- Model được chốt từ `cwmp.cpe.datamodel` lúc khởi động và lúc reload config (`dm_entry_load_model()`), nên ACS ghi
  `DataModel` thì có hiệu lực ở phiên sau, không đổi giữa phiên. Hàm dùng context UCI riêng, không mở dm context.
- MTK đổi root trong `dm_platform_select_root`; BDK giữ cơ chế riêng, không đổi.
- `sdk/mtk/dm181/root181_mtk.c`: `Device.RootDataModelVersion` = `2.19` (forced inform) và `Device.X_HNI_Icwmp.`.
- `verify-dm-paths.py --model`, `tr181-schema.py`, `acs.py --walk`, `run.sh tr181`.
- `run.sh all` PASS sau khi thêm stub cho harness (lần chạy đầu `unit` không link được).

**T1 (object hệ thống):**
- **Cách đặt bảng:** bảng TR-181 nằm ngay trong file backend của domain (`sdk/mtk/dm098/*_mtk.c`), cạnh bảng TR-098.
  Getter/setter vẫn `static`, một bản dùng cho cả hai model.
- **Dùng lại nguyên bảng (A/C):** 16 module `X_AIS_*`/UserInterface, Account, Services, ManagementServer (core +
  MTK), XMPP (không gồm LTE).
- **Nhánh `Device.*` của sản phẩm:** 6 module (IP, PPP, DynamicDNS, DHCPv6, RouterAdvertisement, TraceRoute) đặt ở
  root. Tham chiếu `…IP.Interface.<n>` của DHCPv6 và RA, cùng đường `RouteHops`, nay theo root của context
  (`mtk_dev_prefix()`, `mtk_ipif_prefix()` trong `dmmtk.c`).
- **Bảng TR-181 riêng:**
  - `DeviceInfo`: không có `DeviceLog`.
  - `Time`: `LocalTimeZone` là chuỗi TZ POSIX, ghi được nếu là TZ của một thành phố trong bảng sản phẩm, đi qua đúng
    setter của `LocalTimeZoneName`. Không có `LocalTimeZoneName`, `DaylightSavings*`.
  - Placeholder ở root: SelfTest, FaultMgmt, BulkData, SoftwareModules, `USB.USBHosts`, CaptivePortal, FAP,
    `Users.User`.
- **Hai tên sai so với TR-181, bắt được nhờ đối chiếu bảng tra:**
  - nhánh TraceRoute sản phẩm ghép vào dùng tên lá TR-098 (`HopHost`, `HopHostAddress`, `HopErrorCode`, `HopRTTTimes`).
    Bảng TR-181 dùng `Host`, `HostAddress`, `ErrorCode`, `RTTimes`;
  - `CaptivePortal.CaptivePortalURL` → `URL`.
- **Còn lại 58 tên không có trong bảng tra BDK:** đều là tên BBF hợp lệ mà XML Broadcom không mang (TR-135/140
  Services, DynamicDNS, FAP.GPS, SelfTestDiagnostics, lá LWN và HTTPCompression của ManagementServer,
  `XMPP...ServerConnectAttempts`), cộng `Account.Web.SessionMaxTime` của sản phẩm.

**Ánh xạ và bằng chứng:**
- `docs/issue/tr181_mapping.tsv`: quy tắc prefix/leaf/new, loại A/B/C/D, các nhánh chờ phase sau.
- `docs/issue/tr181-map.py`:
  - `check`: TR-181 mong đợi 318 = cây C 318, thiếu 0, không tên nào thiếu quy tắc.
  - Nguồn: ma trận + tên chỉ có ở C TR-098. Theo loại: A 236, B 1, C 80, D 24; chờ: T2 63, T3 75, T4 185, T5 136.
  - `equiv`: so giá trị từng cặp trên hai bản dump thật.
- **Host `run.sh tr181`:**
  - Inform root `Device.`, duyệt toàn cây 400 tên, 0 fault, đường `InternetGatewayDevice.` trả 9005.
  - So cặp: **306 bằng**, 4 động, 2 loại B có mặt (`LocalTimeZone`, `DataModel`), 0 tên TR-181 thiếu cặp.
  - Hai lệch ban đầu (`ParameterKey`, `DataModel`) do chính lệnh đổi model của test. Một lệch `UsedSpace` do đĩa host
    thay đổi giữa hai lần dump; đã xếp vào lớp động.
- **Cổng của commit T1 (`d01373d`):** `run.sh all` 25/25 PASS (24 nhóm của bản giao + `tr181`), TR-098 không đổi
  (`verify-dm-paths.py --model tr098` thiếu 0). Chỉ chạy host; chưa build SDK, chưa chạy board.

**Để cải tiến ở T7 (giữ tương đương TR-098 lúc này):** vài tham chiếu của sản phẩm là tên thiết bị Linux chứ không
phải path TR-181: `IP.Diagnostics.TraceRoute.Interface`, `IP.Interface.{i}.LowerLayers`.

## 68. TR-181 trên MTK: T2 LAN (`tr181-0003`) và hợp đồng input cho tên TR-181 (`tr181-0004`) (08/10 16:06–)

**Trước khi làm (chatlog 91):** rà các tài liệu trạng thái so với kết quả T1. Có 9 chỗ lệch, đã sửa ở `ca71bf8`:
header progress matrix (còn ghi 06/10, 0083), dòng test host, `docs/README.md`, kiến trúc §1.2/§2/§8, design,
JSON (P6–P8 ghi "Board chưa" dù parity toàn cây đã PASS ở §63/§65), README issue ở workspace. Ngoài ra
`progress.py` ngoài repo vẫn đọc JSON đóng băng ngày 06/10; nay nó đọc JSON của repo.

**T2 (`a56e442`, `[icwmp tr181-0003]`):** 63 tham số LAN của cây TR-098 sản phẩm. Bảng TR-181 nằm cạnh bảng TR-098,
dùng cùng getter/setter, nên cùng option UCI, cùng lệnh reload và cùng fault.

| TR-098 | TR-181 | File |
|---|---|---|
| `LANHostConfigManagement.` | `DHCPv4.Server.Pool.1.` (`DHCPServerEnable` → `Enable`, `DHCPLeaseTime` → `LeaseTime`); mới: `Server.PoolNumberOfEntries`, `Pool.1.Interface` | `lan_mtk.c` |
| `LANHostConfigManagement.IPInterface.1.` | `IP.Interface.{lan}.IPv4Address.1.` (`IPAddress`, `SubnetMask`, `AddressingType`) | `lan_mtk.c` |
| `LANEthernetInterfaceConfig.{i}` (+`Stats`) | `Ethernet.Interface.{i}` (+`Stats`), 4 cổng cùng thứ tự; mới: `Upstream` = false | `laneth_mtk.c` |
| `Hosts.Host.{i}` | `Hosts.Host.{i}` (`MACAddress` → `PhysAddress`, `Layer2Interface` → `Layer1Interface`); mới: `Layer3Interface` | `lanhosts_mtk.c` |

- **`IPv4Address.1`:** chỉ có dưới instance `IP.Interface` của `network.lan` (tìm bằng `dip_section_of_instance`).
  Object `Interface` vẫn do `device_ip_mtk.c` duyệt; `IPv4Address` gộp vào bằng registry merge, không claim path.
  IPv4Address của WAN để T4.
- **Giá trị viết theo TR-181 (loại B, 6):**
  - `Ethernet.Interface.Status`: TR-098 sản phẩm trả `NoLink`/`Disable`, TR-181 trả `Down`.
  - `MaxBitRate`: `-1` = Auto, dịch cả chiều ghi.
  - `InterfaceNumberOfEntries`: TR-181 đếm số instance (4); TR-098 đếm cổng `eth` trong `br-lan`.
  - `IPv4Address.Enable`: TR-181 trả `true`; TR-098 trả `false` cố định (giá trị giữ chỗ của sản phẩm).
  - `Layer1Interface`: path TR-181 (`LAN3` → `Device.Ethernet.Interface.3`, `SSID2` → `Device.WiFi.SSID.2`).
- **Không có tương ứng (D, 13):**
  - Của `LANHostConfigManagement`: `MACAddress` (TR-181 đặt ở `Ethernet.Link`, chưa dựng), `DHCPServerConfigurable`,
    `DHCPRelay`, `UseAllocatedWAN`, `AssociatedConnection`, `Passthrough*`, `AllowedMACAddresses`, hai lá đếm.
  - Còn lại: `MACAddressControlEnabled`, `LANUSBInterfaceNumberOfEntries` (`Device.USB` là object ẩn của sản phẩm),
    `Hosts.Host.InterfaceType`.
  - `Layer2Bridging`: sản phẩm chỉ có object, không có tham số.
- **Instance:** browse TR-181 dùng cấp instance 1 (`Device.Hosts.Host.{i}`, `Device.Ethernet.Interface.{i}`); TR-098
  là cấp 2. Cấp này chỉ dùng khi đánh địa chỉ theo alias (`AliasBasedAddressing`, đang tắt).

**Công cụ:**
- `tr181_mapping.tsv` có quy tắc T2 (A 44, B 6, D 13). Ký hiệu `{lan}` là instance `IP.Interface` của LAN.
- `tr181-map.py` thay `{lan}` bằng instance có `Name` = `lan` trong bản dump TR-181.
- `equiv` coi tham chiếu `InternetGatewayDevice.Device.X` bằng `Device.X` ("equal ref"). Hai lá T1
  (`DHCPv6.Server.Pool.1.Interface`, `RouterAdvertisement.InterfaceSetting.1.Interface`) chỉ có giá trị khi có LAN,
  và lộ ra khi fixture có LAN. Chúng đúng theo thiết kế: tham chiếu đi theo root.

**Hợp đồng input cho tên TR-181 (`[icwmp tr181-0004]`, Verified trên host):**
- **Lỗ hổng:**
  - `shelltypes_mtk.h` giữ kiểu shell (`xsd:int`, `unsignedInt`, `boolean`, IPv4/IPv6) theo path TR-098.
  - `mtk_input_contract()` ([input_contract_mtk.c](../../userspace/public/libs/libicwmp_dm/src/sdk/mtk/input_contract_mtk.c),
    hàm `mtk_input_contract`) tra bảng theo path. Path `Device.*` không có dòng nào nên chỉ qua `is_safe_input`.
- **Bằng chứng trên build `a56e442`:** `Device.X_AIS_Conf.auto_upload_delay=abc` được nhận (fault 0). Setter của nó
  dựa vào kiểm int đứng trước, nên qua tên TR-098 thì giá trị này bị 9007.
- **Sửa:** `gen-shell-types.py` sinh thêm tên TR-181 của mọi cặp loại A/C theo `tr181_mapping.tsv`, cùng kiểu (88 dòng;
  bảng 193 → 281). Loại B không đưa vào, vì setter TR-181 của chúng tự dịch và kiểm.
- **Sau sửa:** `abc` → 9007. `Time.Enable=maybe` và `PeriodicInformInterval=-5` đã trả 9007 từ trước, vì setter của
  chúng tự kiểm.

**Kiểm (host, container `ubuntu:24.04`):**
- `tr181-map.py check`: TR-181 mong đợi 372 = cây C 372, thiếu 0. Nguồn theo loại: A 280, B 7, C 80, D 37; chờ:
  T3 75, T4 185, T5 136.
- `run.sh tr181`: 627 tên. So cặp: 482 bằng, 2 bằng theo tham chiếu, B 14, D 41, động 4, 0 tên TR-181 thiếu cặp.
- Các kiểm T2 trên fixture `network`/`dhcp`/`lanhost`:
  - giá trị TR-181 và tham chiếu đúng;
  - ghi `Pool.1.MinAddress`/`LeaseTime`, `IPv4Address.1.SubnetMask`, `Ethernet.Interface.3.MaxBitRate` 1000 rồi `-1`
    vào đúng option;
  - `MaxBitRate=abc` và `10` trên cổng 2.5G trả 9007.
- Cổng tĩnh tại `tr181-0003`: `run.sh all` 25/25 PASS; check-c-sanity lib 69/0; verify-dm-paths tr098 thiếu 0;
  claims 0 cặp chồng; automake 0; cross-gcc SDK lib 69 file 0 lỗi, không cảnh báo ở ba file đã sửa.
- Chưa build SDK, chưa chạy board.

**Chưa chứng minh được:**
- `Layer1Interface` của host Wi-Fi là `Device.WiFi.SSID.<n>`, giả định T3 giữ số `WLANConfiguration` cho `WiFi.SSID`.
  Chốt ở T3.
- Số `IP.Interface` của LAN trên board: do `ip_int_instance` đã gán sẵn trên board quyết định (§61). Test host chỉ
  thấy số của fixture.

## 69. TR-181 trên MTK: T3 Wi-Fi (`tr181-0005`) (08/10 16:36–)

**Phạm vi:** 75 tham số Wi-Fi của cây TR-098 sản phẩm: `WLANConfiguration.{1..12}` (+`AssociatedDevice`, `Stats`, `WPS`,
`PreSharedKey`, `WEPKey`), `LANWLANConfigurationNumberOfEntries`, `X_AIS_Mesh`, hai lá công suất `X-AIS_*`,
`WiFi.NeighboringWiFiDiagnostic`. Bảng TR-181 nằm cạnh bảng TR-098 trong `wlan_mtk.c`, `wlansec_mtk.c`,
`wlanassoc_mtk.c`, `x_ais_mesh_mtk.c`, `lan_mtk.c`, `root_hidden_mtk.c`, dùng cùng getter/setter.

**Cách tách (Verified bằng đọc getter, rồi so cặp trên host):**
- `WLANConfiguration.{i}` thành `WiFi.SSID.{i}` và `WiFi.AccessPoint.{i}`, giữ đúng 12 số của bảng cố định
  (ra0..ra3, rai0..rai3, rai4, ra4, ra5, rai5). Nhờ vậy `Hosts.Host.Layer1Interface` = `Device.WiFi.SSID.<n>` của T2
  trỏ đúng (mục chưa chứng minh ở §68 đã đóng).
- `WiFi.Radio.1` (2.4 GHz) và `.2` (5 GHz):
  - Lá theo radio (`Channel`, `AutoChannelEnable`, `ChannelsInUse`, `PossibleChannels`, `TransmitPower`,
    `TransmitPowerSupported`, `RegulatoryDomain`, hai lá tốc độ) đọc section radio qua interface được giao. Mọi
    interface cùng băng đọc ra cùng giá trị, nên gộp N:1 là đúng.
  - Instance Radio được giao interface đầu của băng (ra0, rai0).
  - Bảng ánh xạ dùng `{radio:i2}`, tra từ `SSID.{i}.LowerLayers` lúc so cặp.
- **Lá theo interface, không theo radio:**
  - `RadioEnabled` của sản phẩm là cờ `wireless.<iface>.disabled`, cùng cờ với `Enable`. Vì vậy nó là
    `AccessPoint.{i}.Enable`, không phải `Radio.Enable`. `Radio.Enable` mới đọc `wireless.<radio>.disabled`.
  - `X_AIS_WlanStandard` chỉ có giá trị ở ra0/rai0, nên ở lại `SSID.{i}`, loại C.
- **Bảo mật:** `AccessPoint.{i}.Security.ModeEnabled` là một enumeration TR-181 dựng từ `wireless.<iface>.encryption`:
  - `none` → None
  - `wep+shared+64` → WEP-64, `wep+shared+128` → WEP-128
  - `psk` → WPA-Personal, `psk2*` → WPA2-Personal, `psk-mixed*` → WPA-WPA2-Personal
  - `sae` → WPA3-Personal, `sae-mixed` → WPA3-Personal-Transition
  - Ghi theo đúng đường của `BeaconType`: option, bản sao của mapd (`mapd_security`), `wifi reload`.
  - `KeyPassphrase` và `PreSharedKey` giữ setter TR-098. `WEPKey` ghi vào slot của key index.
  - Sáu lá mode của TR-098, `WEPEncryptionLevel`, `WEPKeyIndex` và `PreSharedKey.{i}.KeyPassphrase` là D, vì đã gộp
    vào `ModeEnabled`/`KeyPassphrase`/`WEPKey`.
- **Loại B (6), giá trị viết theo TR-181:**
  - `SSID.Status`: Disabled → Down.
  - `Radio.OperatingStandards`: danh sách chữ, chính là giá trị `X_AIS_WlanStandard` của băng; ghi cũng chọn htmode
    như cũ.
  - `Radio.SupportedStandards`.
  - `Security.ModeEnabled`, `Security.WEPKey`.
  - `SSIDNumberOfEntries`: TR-181 đếm 12 instance; TR-098 đếm interface AP đang bật.
- **Không có tương ứng (D, 17):** ngoài nhóm bảo mật ở trên còn:
  - `MaxBitRate` (sản phẩm trả "Auto"), `BeaconAdvertisementEnabled`;
  - `MruEnable`: tên của sản phẩm không có tiền tố vendor, object chuẩn TR-181 không mang được; cần thống nhất tên
    với nhà mạng ở T7;
  - của `AssociatedDevice`: `AssociatedDeviceIPAddress`, `RSSI`, `Stats.ErrorsReceived`/`TxDropCount`/`RxDropCount`.
- **Operator (C, 8):** `Device.WiFi.X_AIS_Mesh.*` (cùng bảng, kể cả forced inform), `Device.WiFi.X-AIS_2-4GHzTransmitPower`,
  `X-AIS_5GHzTransmitPower`, `SSID.{i}.X_AIS_APModuleEnable`/`X_AIS_WlanStandard`.
- **`NeighboringWiFiDiagnostic`:** TR-098 để cả `WiFi.` ẩn. TR-181 để `Device.WiFi` hiện (vì có Radio/SSID/
  AccessPoint) và chỉ ẩn object con này (`addressed_only`).
- **Instance:** browse TR-181 dùng cấp 1 (SSID/AccessPoint/Radio) và cấp 2 (`AssociatedDevice`). `browseAssocInst`
  của TR-098 nay gọi `assoc_browse(level)` chung.

**Công cụ:**
- `tr181_mapping.tsv` có quy tắc T3. `tr181-map.py` hiểu `{radio:iN}`.
- `tr181-schema.py` bỏ qua tên `X-` như `X_`; tên `X-AIS_` có dấu `-` trước đây bị cắt thành `Device.WiFi.X`.
- `shelltypes_mtk.h` sinh lại theo mapping mới: 297 dòng, 104 tên TR-181.

**Kiểm (host):**
- `tr181-map.py check`: TR-181 mong đợi 441 = cây C 441, thiếu 0. Nguồn theo loại: A 324, B 13, C 88, D 54; chờ:
  T4 185, T5 136.
- `run.sh tr181` trên fixture `wireless` (2 radio, 12 interface, mỗi kiểu encryption một interface):
  - duyệt 1129 tên;
  - so cặp **884 bằng** + 2 bằng theo tham chiếu, B 111, D 185, 0 tên TR-181 thiếu cặp.
- **Kiểm riêng T3:**
  - các lá đếm 2/12/12;
  - Radio 1: kênh 6, công suất 60, chuẩn `b,g,n,ax,be`; Radio 2: auto, 5GHz, `TH `;
  - SSID 2 Down; SSID 9 = `rai4` trên Radio 2;
  - `ModeEnabled` của 7 interface đúng bảng.
- **Ghi qua tên TR-181, đọc lại UCI:**
  - `ModeEnabled` WPA3-Personal → `sae`;
  - `Radio.1.Channel` 11; `OperatingStandards` `b,g,n,ax` → `HE40`;
  - `SSID.1.SSID`; `AccessPoint.3.Enable` false → `ra2.disabled` 1.
- **Từ chối 9007:** `ModeEnabled` sai, `Radio.2.Channel` 7, `Radio.1.TransmitPower` abc.
- **Cổng tĩnh:** check-c-sanity lib 69/0; verify-dm-paths tr098 thiếu 0; claims 142, 0 cặp chồng; automake 0;
  cross-gcc SDK lib 69 file 0 lỗi, không cảnh báo ở file đã sửa.
- Chưa build SDK, chưa chạy board.

**Chưa chứng minh được:**
- `ChannelsInUse`, `PossibleChannels` và `AssociatedDevice` đọc `ubus hni` (getCurrentChannel, getChannelList,
  getWlanDeviceList). Host không có `hni`, nên chỉ thấy giá trị rỗng/0 và không có station. Cần board (T6).
- `Radio.Enable` đọc `wireless.<radio>.disabled`. Sản phẩm có dùng option này để tắt radio không thì chưa kiểm trên
  board.
