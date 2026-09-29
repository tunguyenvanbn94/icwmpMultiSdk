# 20260922 — icwmp + libtr098 chạy nhiều SDK (Broadcom BDK + MTK OpenWrt), data model TR-098 bằng C

| Mục | Giá trị |
|---|---|
| Trạng thái | **A1 + R6/R7 + P2 + P3 + P4 + P5 TRỌN VẸN** đã implement, **+ hợp đồng input của shell** (`0061`). **BUILD P4c–P4f ĐẠT** trên SDK thật (25/09). Overlay `1cde441`, patch `0034`–`0061`. Data model C: **458/783 param** (458/778 tới được trên sản phẩm). **`0056` sửa lỗi chạy thật trong bản đã build** (con trỏ `char *` bị cắt 32 bit ở `X_AIS_IPv6...DNSServers`) — build lại trước khi lên board. Việc tiếp theo: build lại + gate board 1; code tiếp P6 (Firewall/UI/root còn lại). |
| Xem tiến độ | `./projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098/progress.py` — phase nào xong, phase nào chưa, việc kế tiếp |
| Build nhanh từng gói | [build-commands.md](build-commands.md) — lệnh build riêng `libtr098`/`icwmp_tr098` (MTK) và `libicwmp_dm`/`icwmp` (BDK) để bắt lỗi compile trong vài phút |
| Ngày mở | 2026-09-22 |
| Cập nhật lần cuối | 2026-09-25 15:50 |
| Project | `mtk_openwrt_wifi7` (SDK Airoha 2025Q3, board HP2236B AN7583) |
| Source dùng chung | overlay repo `brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace` (HEAD **`1cde441`**, sạch) |
| Baseline cũ giữ nguyên | `icwmp_mtk_port.tar.gz` (sha256 `841b582c0f531b28`) + `install-mtk.sh`; tarball BDK `icwmp_bdk_port_overlay.tar.gz` (`c014ec8e0032adb4`) |
| Patch overlay | `0032`/`0033` (nền tảng mtk, tách SDK, registry) · `0034` rename A1 · `0035` bộ apply · **`0036` R6+R7** · **`0037` P2 LAN bằng C** · **`0038` P3a Wi-Fi** · **`0039` P3b bảo mật Wi-Fi** · **`0040` P4a khung WANDevice** · **`0041` P4b WANIPConnection** · **`0042` apply chạy được Python 3.6** · **`0043` sửa comment nuốt code ở `wlan_mtk.c`** · **`0044` bỏ `static get_empty` trùng engine ở `wlansec_mtk.c`** · **`0045` điều kiện automake của fragment SDK trong gói app** · **`0046` đổi tên `static dm_add_end_session` trùng engine ở `icwmp_dm.c`** · **`0047` bọc ngoặc nhánh `CWMP_LOG` — macro mang sẵn `;`** · **`0048` `+zlib`/`+libubox` vào DEPENDS của feed** · **`0049` apply in bước `feeds update` cho build gói lẻ** · **`0050` `PKG_SOURCE` từ `TRUNK_DIR` + lá chắn chống `cp /.`** · **`0051` feed tự include `ecnt-trunkdir.mk`** · **`0052` P4c `WANPPPConnection` bằng C** · **`0053` P4d `X_AIS_IPv6` bằng C + tách `wanconn_mtk.h`** · **`0054` P4e `PortMapping` bằng C** · **`0055` P4f `X_AIS_ServiceList` — đóng phase 4** · **`0056` khai báo getter mảng của `dmjson` — `char *` bị trả về như `int`** · **`0057` P5a `IPPingDiagnostics` + `TraceRouteDiagnostics` bằng C** · **`0058` P5b `NSLookupDiagnostics` + `DNSDiagnostics` bằng C** · **`0059` P5c TR-143 `DownloadDiagnostics` + `UploadDiagnostics` bằng C** · **`0060` P5d `Layer3Forwarding` + P5e object ẩn của root; engine thêm `container_leaf`, `addressed_only` — đóng phase 5** · **`0061` hợp đồng input của shell (`is_safe_input` + kiểm theo kiểu shell) trước mọi setter C** |
| Giao một lệnh | `icwmp_multiplatform_port.tar.gz` sha256 `d6e8e4aedd99` — giải nén, `./apply --sdk mtk|bdk <SDK root>` |

## Đã bắt đầu thực thi — A1

- Source đã đổi sang `public/libs/libicwmp_dm/src/`, app include `<icwmp_dm/...>`, wrapper/feed/
  installer đã đồng bộ. ABI/package vẫn `libtr098`, không thay logic getter/setter.
- [Hướng dẫn A1 + build commands](a1-implementation.md), [kết quả kiểm](a1-verification.md).
  Bundle mới `icwmp_a1_port.tar.gz`, patch `0034-icwmp-dm-source-layout.patch`, hash trong
  `SHA256SUMS-a1`. Không dùng tarball 0033 cũ để kiểm code A1.
- Còn A2–A6/P2–P8. Gate kế tiếp là clean SDK build A1 cho MTK/BDK trước thay model/transaction.

```sh
./projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098/progress.py --watch
# bỏ --watch để xem một lần, --json để xuất trạng thái
```

Command đọc `implementation-status.json`, phân biệt code/static/build/board, không phải
heartbeat của AI. Phiên được giữ PAUSED khi chờ kết quả SDK build, không tự coi phase sau đã xong.

## Lượt 25/09 (22) — PHASE 5 ĐÓNG; hợp đồng input của shell — lỗ hổng xuyên suốt P1–P5 (Claude Code)

Data model C: **443 → 458 / 783**. **Phase 5: 83/83, thiếu 0, dôi 0.** Patch `0060`, `0061`,
bundle `33477b15bf88`.

**`0060` — `Layer3Forwarding` (12) + object ẩn của root (3):**

- Path của vendor giữ nguyên: `Enable`/`ForwardNumberOfEntries`/`DefaultConnectionService` nằm
  **ngay trên** object nhiều instance `Forwarding.`. Engine không có chỗ cho lá cấp container →
  thêm **`DMOBJ.container_leaf`** (thành viên cuối, bảng cũ không đổi).
- Hai setter đường dẫn WAN: đọc source busybox 1.33.1 của SDK thì cả hai dòng `[[ … || … ]]`
  **exit 2 với mọi giá trị → không bao giờ từ chối**; cái quyết định là prefix, `awk -F .`, số học
  ash và việc tìm entry. `Forwarding.{i}.Interface` đọc package **`hniwan` — không tồn tại** trong SDK.
- `SelfTestDiagnostics.`, `WiFi.` là object **ẩn**: root shell là `case` theo đường dẫn được hỏi,
  chỉ trả lời khi hỏi đúng tên. Thêm **`DMOBJ.addressed_only`**.

**`0061` — lỗ hổng tìm thấy khi làm P5e, ảnh hưởng mọi tham số ghi được đã port:**

> Trên sản phẩm cũ, **mọi SPV** đi qua `common_set_value_check_param()` **trước** setter:
> `is_safe_input` (từ chối `""`, dòng trống, ký tự ngoài ASCII in được, và `# ; & | < > \` $ \ ' "`)
> rồi kiểm theo **kiểu shell**. C chỉ port setter — engine gọi thẳng setter. Tức là mọi tham số C
> từ P1 tới P5 **nhận cả ký tự chèn lệnh** vào UCI mà script shell sau đó đọc.

Sửa một chỗ: `input_contract_mtk.c` + bảng kiểu **sinh từ ma trận** (`gen-shell-types.py`, 186
dòng), áp ở VALUECHECK cho mọi path native. Kéo theo đính chính P5b/P5c: `Timeout` bị kiểm khoảng,
URL `""` và URL có `&`/`#` chưa bao giờ được nhận.

Kiểm: `check-cc-syntax` lib 41/0 · app 17/0 (+ bản all-C của hook); `check-c-sanity` 0 × 4 cây;
phase 1–5 thiếu 0; 126 claim, 0 chồng. **Chưa chạy thử được** code kiểm input (không gcc host) —
danh sách thử trên board ở [analysis.md §37](analysis.md).

## Lượt 25/09 (21) — P5c TR-143 `DownloadDiagnostics` + `UploadDiagnostics`, 26 tham số (Claude Code)

Data model C: **417 → 443 / 783**. Patch `0059`, bundle `d55c46cc0e27`.

- **TR-143 không giết launcher từ data model**: setter chỉ xếp `<launcher> stop` vào apply-service
  khi state là `Requested`. Thứ tự hàng đợi quyết định test có chạy: SPV
  `{Requested, DownloadURL}` xếp `run &` rồi `stop` → test vừa yêu cầu bị giết. Giữ nguyên; engine
  áp SPV theo thứ tự (Verified, `set_list_tmp` FIFO).
- **Lỗi vendor: Upload không bao giờ dừng** — `uploadDiag_stop_diagnostic` đọc state bằng
  `uci set` thay cho `get`; `uci set` thiếu giá trị là lỗi im lặng (`list.c:702`). Giữ nguyên.
- `DownloadURL`/`UploadURL` chỉ nhận http/ftp, phải bằng output `grep -o`. *(Đính chính lượt 22: `""` **không** được nhận — `is_safe_input` của tầng chung từ chối trước.)*
- `mtk_grep_o()` mới mô phỏng `$(echo | grep -E -o)`; chạy trên musl như busybox grep của board.
  **Chưa chạy thử được** (không có gcc host / qemu) — danh sách URL để so trên board ở analysis.

Kiểm: `check-cc-syntax` lib 38/0 · app 17/0; `check-c-sanity` 0 × 4 cây; phase 5 C 68 dôi 0; 123
claim, 0 chồng. Chi tiết: [analysis.md §35](analysis.md).

## Lượt 25/09 (20) — P5b `NSLookupDiagnostics` + `DNSDiagnostics`, 20 tham số (Claude Code)

Data model C: **397 → 417 / 783**. Patch `0058`, bundle `31b9a874d55a`.

- **`DNSDiagnostics` trên sản phẩm chỉ có 7 tham số**: shell comment `DNSServer`,
  `ResultNumberOfEntries` và object `Result.`. Ma trận vẫn liệt kê 5 lá `Result.{i}.*` vì đọc văn
  bản. Quét cả 783 dòng: **đây là cây con "ma" duy nhất**. `verify-dm-paths.py` nay báo chúng là
  *không tới được* thay vì *thiếu* → P5 thật là **83**, tổng tới được là **778**.
- `NSLookupDiagnostics.Result.{i}` đọc `@local[<n>]` của kho `/var/state/nslookup_result`. Tôi
  từng nghi launcher `commit` kết quả vào `/etc/config` (ghi flash mỗi lần chạy) — **bác bỏ** bằng
  source libuci của SDK: `-P` bật `CLI_FLAG_NOCOMMIT`, `commit` thành no-op (`cli.c:331`).
- Quirk giữ nguyên: `Timeout`/`NumberOfRepetitions` lưu không kiểm, không dừng lookup đang chạy;
  `DNSServer` đã đặt thì không trả về rỗng được.

Kiểm: `check-cc-syntax` lib 37/0 · app 17/0; `check-c-sanity` 0 vấn đề × 4 cây; phase 5 C 42
param dôi 0; 121 claim, 0 chồng. Chi tiết: [analysis.md §34](analysis.md).

## Lượt 25/09 (19) — build P4c–P4f ĐẠT, cổng compile thật, P5a hai diagnostic (Claude Code)

**Build P4c–P4f ĐẠT** trên SDK thật: `libtr098` 128.195 B, `icwmp_tr098` 202.856 B, bốn object
mới đã link vào `libtr098.so.3.0.0`. Data model C: **375 → 397 / 783 (50,7%)**.

**Cổng mới: [check-cc-syntax.py](check-cc-syntax.py)** — chạy cross-gcc của chính SDK
(`-fsyntax-only`, không ghi gì vào cây) trên đúng danh sách file lib/app build, 53 file trong
khoảng 1 giây. Lần đầu chạy bắt **hai lỗi mà 8 lớp kiểm tĩnh cho qua**, trong đó một là **lỗi chạy
thật trong bản bạn vừa build**:

> `dmjson.h` dùng `__dmjson_get_value_in_array_idx()` trong macro nhưng không khai báo nó. gcc 10
> coi nó trả `int` và **chỉ cảnh báo**, nên `.ipk` vẫn ra. Trên aarch64, `char *` trả về mất 32
> bit cao. Chỗ gọi: `X_AIS_IPv6...DNSServers` (P4d) — đọc tham số này là segfault. Sửa ở `0056`.

**Build lại với bundle mới trước khi lên board.**

P5a — `IPPingDiagnostics` (12) + `TraceRouteDiagnostics` (10), patch `0057`:

- Launcher shell giữ nguyên, báo xong bằng `ubus call tr069 inform "8 DIAGNOSTICS COMPLETE"` —
  `icwmpd` nhận đúng hợp đồng đó (`cwmp.c:66`).
- **Mỗi diagnostic một kho `uci -P <dir>` riêng** — `DiagnosticsState` của ping và traceroute là
  hai giá trị khác nhau. Thêm `mtk_state(dir, ...)` mở context riêng mỗi lần gọi; context dùng
  chung sẽ lẫn delta của `/var/state` vào.
- Quirk giữ nguyên: ghi `IPPingDiagnostics.Interface` **giết NSLookup đang chạy** (vendor gọi nhầm
  `nslookup_stop_diagnostic`); `WANIPConnection` → `eth0.1` viết cứng; `RouteHops.` luôn rỗng.
- Tiến trình nền của hàng đợi apply-service **không còn dùng chung stdio với icwmpd** — cả đường
  compat (coprocess sống suốt đời, stdin/stdout là pipe request/reply) lẫn không-compat (đọc tới
  EOF → hết phiên phải chờ traceroute chạy xong).

| Kiểm | Kết quả |
|---|---|
| `check-cc-syntax.py` | lib 36/0 · app 17/0 |
| `check-c-sanity.py` | lib/mtk 36/0 · app 17/0 × 3 SDK |
| `verify-dm-paths --phase 5` | 22 param, dôi 0 · thiếu 66 = P5b–P5e |
| `--claims` | 119 claim, 17 module, 0 chồng |
| bundle `ed530b239da7` | 402 file, dry-run exit 0 trên `1_src` và `2_src` |

Chi tiết: [analysis.md §33](analysis.md).

## Lượt 25/09 (18) — P4f `X_AIS_ServiceList`, **PHASE 4 ĐÓNG** (Claude Code)

Data model C: **373 → 375 / 783 (47,9%)**. **Phase 4: 173/173, thiếu 0, dôi 0.**

Hai tham số nặng nhất cả nhánh WANDevice: setter quyết định một WAN mang lưu lượng khách, mang
quản lý TR-069, hay cả hai — và trên đường đi viết lại firewall cùng chỗ bind của chính client
CWMP. Để cuối là có lý do.

**Hai cái bẫy trong bản gốc, chép lại cả hai:** chế độ bridge **chỉ nhận `OTHER`** (còn lại
`9007`, và getter luôn trả `OTHER` cho entry bridge); ở chế độ router **`OTHER` được lưu thành
`INTERNET`** — ACS ghi `OTHER` rồi đọc lại ra `INTERNET`, đã vậy từ trước tới nay.

**Và một cái bản nháp đầu làm sai.** Mọi nhánh của shell là `case $old_service_type in 3|2|1|4)`
**không có arm mặc định**, nên entry chưa từng đặt `service_type` rơi khỏi tất cả: giá trị được
ghi lại và **không side effect nào chạy**. Bản nháp của tôi chạy chúng — lần đầu ACS ghi lên một
entry mới sẽ viết lại firewall và chỗ bind của client. Đã sửa bằng `old_known`. **Không lớp kiểm
tĩnh nào bắt được loại này** — nó lộ ra vì đọc lại từng nhánh shell đối chiếu code trước khi commit.

**Khác biệt duy nhất, bắt buộc:** shell chạy nửa gây gián đoạn (`iptables`,
`/etc/init.d/firewall reload &`, `/etc/init.d/easycwmpd restart &`) **inline**, ngay trong
`SetParameterValues` nó đang trả lời — ACS không bao giờ nhận được response của chính lệnh vừa
gửi. Ở đây nửa UCI chạy ngay, nửa mệnh lệnh xếp vào apply-service với **lệnh nguyên văn của
shell**. `/etc/init.d/easycwmpd restart` đúng trong build này: `sdk/mtk/files/easycwmpd` là shim
một dòng `exec /etc/init.d/icwmpd "$@"`.

**Lỗ hổng đã biết, cố ý để nguyên:** `easycwmp.@acs[0].enablecwmp` được ghi y như trước, nhưng
bảng mirror trong `icwmp_mtk.c` không mang option đó, nên `icwmpd` không bị nó gate như
`easycwmpd` trước kia. Thêm vào mirror sẽ cho phép ACS **tắt hẳn client CWMP** bằng cách ghi
`X_AIS_ServiceList` — quyết định của sản phẩm, không phải của bản port.

| Phase 4 | Param | Patch |
|---|---|---|
| P4a khung `WANDevice` | 22 | `0040` |
| P4b `WANIPConnection` | 35 | `0041` |
| P4c `WANPPPConnection` | 42 | `0052` |
| P4d `X_AIS_IPv6` | 46 | `0053` |
| P4e `PortMapping` | 26 | `0054` |
| P4f `X_AIS_ServiceList` | 2 | `0055` |
| **Tổng** | **173** | |

Bundle `ef3bd5c483f1` (397 file). **CHƯA build-test** — P4c–P4f là 116 tham số, 3 file mới + 1
header, chưa qua compiler lần nào. Đây là khối code lớn nhất chưa build kể từ đầu bản port.

## Lượt 25/09 (17) — P4e: `PortMapping` cả hai object WAN, 26 tham số (Claude Code)

Data model C: **347 → 373 / 783 (47,6%)**. Phase 4 còn **2** — chỉ `X_AIS_ServiceList`.

Rule là section `config port_forwarding` của gói UCI **`firewall_clay`**, dùng chung cho mọi WAN
connection. Chia về từng instance bằng option `interface`, và tên đem so **không phải netdev**:
`pon` / `pon.<vlan_id>` cho routed IPoE, `pppoe-if<id>` cho PPPoE, bridged thì **không có** —
nhánh bridge chưa bao giờ đăng ký `PortMapping`.

**Instance là VỊ TRÍ, và không ổn định**: thứ tự trong danh sách rule khớp, đếm từ 1 theo thứ tự
file UCI. Xoá rule 2 trong 3 thì rule thứ ba thành số 2. Khác hẳn object connection phía trên,
nơi instance là `id + 1` và ổn định — hai quy tắc đánh số khác nhau cạnh nhau trong cùng cây.

Chép nguyên: `X_AIS_Name` và `PortMappingDescription` là **cùng** option `service_type` (ghi cái
này đổi cái kia, chỉ khác khi rỗng: `"-"` vs `""`, và giới hạn 128 vs 256); ghi `RemoteHost` hoặc
`X_AIS_RemoteHostEndRange` bằng **giá trị rỗng xoá cả hai đầu**; `udp/tcp` và `both` đều lưu
thành `tcp/udp`; `AddObject` trả về **tổng** số section, không phải vị trí rule mới.

Một khác biệt bổ sung: shell chỉ đăng ký `PortMapping` khi `ip route` có default gateway — điều
kiện toàn cục. Cây C tĩnh không làm object hiện/biến được nên nó luôn có mặt. Không giá trị nào
đổi; chỉ `GetParameterNames` liệt kê thêm trên máy không có default route.

**Lớp kiểm 8 ra đời từ một lỗi suýt lọt.** Bản nháp gọi
`dmuci_add_section(pkg, type, &s, NULL)` — `dmuci.c` ghi `*value` trên **mọi** đường ra kể cả
đường lỗi, nên đó là segfault ngay lần `AddObject` đầu tiên, còn compiler thì hoàn toàn hài lòng.
Lớp 8 **tự suy ra** out-parameter từ định nghĩa hàm (`T **p` có `*p = ...` trong thân) rồi báo
mọi lời gọi truyền `NULL` đúng vị trí đó. Chạy ngược trên bản nháp: chỉ đúng dòng và đúng vị trí
tham số.

Bundle `1c4bddb90f9e` (395 file). **CHƯA build-test.**

## Lượt 25/09 (16) — P4d: `X_AIS_IPv6` cả hai object WAN, 46 tham số (Claude Code)

Data model C: **301 → 347 / 783 (44,3%)**. Phase 4 còn **28**, đúng bằng PortMapping (P4e) +
X_AIS_ServiceList (P4f).

**File mới `wanipv6_mtk.c`, và một header dùng chung.** 46 tham số này không dùng chung getter
nào với leaf IPv4 — chúng đọc `wan.@entry[i].v6_*` và `ubus network.interface.if<id>_6`. Thứ duy
nhất dùng chung là **mô hình entry**, nên `struct wan_entry` + phép duyệt chuyển sang
`wanconn_mtk.h`; `wanip_mtk.c` export 8 helper, bốn cái tên quá chung được đổi ra khỏi vùng dễ
đụng độ (`sect_opt` → `wan_sect_opt`, tương tự `entry_opt`, `str_is_uint`, `iface_status`).
P4e/P4f sẽ dùng đúng header này.

**Hai hình dạng, không cùng một tập:**

| | `X_AIS_IPv6.` (nhánh) | `X_AIS_IPv6<Name>` (phẳng) |
|---|---|---|
| `WANIPConnection` | 13 leaf | **12**, status tên `X_AIS_IPv6ConnStatus` |
| `WANPPPConnection` | 13 leaf, giống hệt | **8**, status tên `X_AIS_IPv6ConnectionStatus` |

Object PPP không có `GatewayType`/`GatewayAddress`/`DNSType`/`PrefixDelegationType`/
`GUAFromPrefixEnable`. Giữ nguyên bất đối xứng đó.

**Trùng tên nhưng không phải alias**: `X_AIS_IPv6.AddressingType` đọc `v6_mode` một mình, còn
`X_AIS_IPv6AddressingType` đọc `v6_active` trước và trả `"None"` khi IPv6 tắt.
`X_AIS_IPv6AutoModeEnable` chỉ đưa entry giữa DHCP và Static, **không bao giờ** về SLAAC được.

Guard rail giữ nguyên từng cái: địa chỉ/prefix/gateway chặn `9001` nếu không Static, `Pd.Enable`
chặn `9001` nếu **đang** Static, `DNSServers` chặn khi `v6_static_dns=0`, `ManualDNS` không tắt
được khi Static (`9007`).

Hai chi tiết triển khai: đường lùi `ip -6 addr show` đọc `/proc/net/if_inet6` rồi `inet_ntop()`
thay vì fork process; `X_AIS_IPv6DNSServers1/2` **không theo entry** — chúng đọc/ghi
`dhcp.lan.dns`, nên mọi instance thấy cùng một cặp (đúng hành vi sản phẩm).

Một khác biệt cố ý: `X_AIS_IPv6ConnStatus` ghi được, shell chạy `ifup`/`ifdown` **inline** cắt
luôn phiên đang trả lời — ở đây xếp vào apply-service.

Bundle `c1c470a0651e` (393 file). **CHƯA build-test.**

## Lượt 24/09 (15) — P4c: `WANPPPConnection.{i}`, 42 tham số (Claude Code)

Data model C: **259 → 301 / 783 (38,4%)**. Dưới `WANPPPConnection.{i}` không còn thiếu gì ngoài
ba nhánh để dành: PortMapping (P4e), X_AIS_IPv6 (P4d), X_AIS_ServiceList (P4f).

**Thêm vào `wanip_mtk.c`, không tạo file mới.** Shell phục vụ cả hai object bằng một cặp hàm chỉ
khác tham số `$targe_conn_type`, và entry PPP gọi đúng những helper mà entry IP gọi cho **30 trên
42** leaf. Tách file thì phải export 30 helper ra header kèm tiền tố — đổi tên 30 hàm trong file
vừa build sạch trên SDK thật — hoặc nhân bản để hai bản trôi khỏi nhau.

`wan_entries()` thành `wan_entries_kind(out, max, kind)`. Bộ lọc là khác biệt duy nhất:
`switch_mode==0 && conn_type==2`. Instance vẫn `id + 1`. Mỗi entry thuộc **đúng một** object, nên
`WANIPConnection.2` và `WANPPPConnection.2` là hai entry khác nhau.

12 leaf không dùng chung. Chỗ dễ sót nhất là **`Stats`**: `wan_device_get_eth_stats()` có **ba**
nhánh, nhánh PPP đọc `l3_device` từ ubus — netdev mà ppp daemon tạo, không phải ethernet bên dưới.
Quyền cũng khác object IP: `DefaultGateway`/`RemoteIPAddress` chỉ đọc, `ExternalIPAddress`/
`DNSEnabled` nhận setter `"true"` của shell (chấp nhận rồi không làm gì).

**Hai khác biệt cố ý, cần bạn duyệt** (đổi lại mất hai dòng):

- `Password` **đọc ra rỗng**. Shell trả về mật khẩu PPP cho bất cứ ai hỏi; TR-098 quy định tham
  số này đọc ra chuỗi rỗng. Ghi vẫn nguyên.
- `Reset` **xếp hàng** thay vì chạy ngay. Shell chạy `ifdown; sleep 1; ifup` inline — trên đúng
  WAN đang mang phiên CWMP thì nó cắt kết nối trước khi response kịp gửi, ACS thấy timeout.

Một bug **chép lại nguyên**: `MaxMTUSize` của nhánh PPP truyền `$iface`/`$device` trong hàm không
hề đặt hai biến đó → getter trả rỗng, setter báo thành công mà không đổi gì.

Bundle `a9dd185b8c7f` (390 file). **CHƯA build-test.**

## Lượt 24/09 (14) — **CỔNG BUILD ĐẠT**, hai `.ipk` đã ra (Claude Code)

| Gói | File | Kích thước | Giờ |
|---|---|---|---|
| `libtr098` | `libtr098_3_aarch64_cortex-a53.ipk` | 117.605 B | 17:35 |
| `icwmp_tr098` | `icwmp_tr098_3-2_aarch64_cortex-a53.ipk` | 202.786 B | 17:41 |

`icwmp_tr098` có 85 file: binary `/usr/sbin/icwmp_tr098d` **214 KB**, init `icwmpd` và
`easycwmpd`, `/etc/config/{cwmp,easycwmp}`, thư viện hàm easycwmp dưới `/usr/share/easycwmp/`.
`libtr098` có `libtr098.so.3.0.0` + cầu shell `icwmp_dm.sh`. `Depends:` xác nhận hai patch cuối
có tác dụng thật — có `zlib`, `libubox`, `libtr098`, và `Conflicts: cwmpclient`.

Đi qua **tám** lỗi để tới đây: `0043` comment nuốt code · `0044`/`0046` `static` trùng engine ·
`0045` điều kiện automake · `0047` macro mang sẵn `;` · `0048` DEPENDS thiếu `+zlib` · `0049`
apply không nói bước `feeds update` · `0050`+`0051` `PKG_SOURCE` rỗng làm `cp -fpR /.`.
Kiểm tĩnh bắt trước được bảy; hai lỗi quy trình thì không, vì cây nguồn luôn đúng.

**Tiếp theo là gate board 1**, không còn là build.

Lỗi `hostapd` cùng lượt (`patches/addr`) **không liên quan icwmp** — ghi ở
[other-findings/hostapd-patches-addr.md](other-findings/hostapd-patches-addr.md).

## Lượt 24/09 (13) — `PKG_SOURCE` rỗng, `cp -fpR /.` làm build dir phình 17 GB (Claude Code)

```
cp -fpR /. .../build_dir/.../icwmp_tr098
cp: cannot copy a directory, '/.', into itself
```

`$(PKG_SOURCE)/.` ra `/.` = **biến rỗng**. `$(CP)` là `cp -fpR`, nên mỗi lần `Build/Prepare` chạy
là một lần copy **cả filesystem gốc** vào build dir — lúc phát hiện đã **17 GB**, chứa `boot/` và
`home/`. Lần này `cp` mới dừng vì nhận ra đang copy đích vào chính đích.

**Hai biến, cùng một kiểu lỗi: định nghĩa ở nơi build gói lẻ không nhìn thấy.**

| Biến | Định nghĩa ở | Build ảnh đầy đủ | Build gói lẻ |
|---|---|---|---|
| `APP_HNI_ICWMP_TR098_DIR` | `feeds/airoha/target/linux/airoha/dir.mak:894`, chỉ target Makefile include | thấy | **rỗng** |
| `TRUNK_DIR` | `include/ecnt-trunkdir.mk` — **không file nào include** | thấy (shell đã export sau `airoha-compile.sh`) | **rỗng trong terminal mới** |

`libtr098` chưa bao giờ dính vì nó dùng `$(TRUNK_DIR)/apps/hni/libicwmp_dm`, y như gói
`cwmpclient` gốc của vendor. Dòng `APP_HNI_ICWMP_TR098_DIR` có từ bản baseline trước bundle này —
bản `.icwmp-backups/.../original/` trên cây người dùng cũng có.

Patch `0050` + `0051`: `PKG_SOURCE` lấy từ `$(TRUNK_DIR)`, cả hai feed Makefile tự
`-include $(TOPDIR)/include/ecnt-trunkdir.mk`, và `Build/Prepare` **từ chối chạy** khi
`PKG_SOURCE` rỗng hoặc không phải thư mục — một đường dẫn sai không được phép tốn 17 GB.

`check-pkg-deps.py` thêm lớp: biến được **thay vào câu lệnh** không được là biến chỉ do
`target/linux/**/*.mak` định nghĩa. Chạy ngược trên bản chưa sửa: báo đúng.

**Đã làm trực tiếp trên cây build** (người dùng cho phép): xóa build dir 17 GB, apply bundle
`a01a585a0fb5`, đồng bộ hai Makefile sang `feeds/airoha/`, xác minh `PKG_SOURCE` trỏ vào thư mục
có thật. **Không build được từ đây** — host này không có `make` lẫn `gcc` trong PATH.

## Lượt 24/09 (12) — `+zlib` đúng nhưng chưa tới được build: feed `src-cpy` (Claude Code)

Cùng một lỗi `libz.so.1`, với cây nguồn **đã đúng**. Patch `0048` không sai — nó chưa bao giờ
tới được build.

`feeds.conf.default` khai báo feed airoha bằng **`src-cpy`**: feed được **copy**, không symlink.

| Thứ | `apply` ghi vào | Build đọc ở đâu |
|---|---|---|
| Nguồn C | `tclinux_phoenix/apps/hni/icwmp_tr098/` | đọc thẳng (`PKG_SOURCE` là đường tuyệt đối ngoài feed) |
| Makefile của gói | `airoha_feeds/package/.../Makefile` | **bản copy** ở `feeds/airoha/package/.../Makefile` |

Nên `0043`–`0047` (đều sửa `.c`) vào ngay, còn `0048` sửa **Makefile của feed** thì bị bản copy
che. Dòng `Leaving directory '.../feeds/airoha/...'` trong log đã chỉ đúng chỗ.

**Lỗi này là của tôi.** [build-commands.md](build-commands.md) mục 1.3 đã ghi đúng từ trước, kể
cả câu *"`./apply` sửa đúng hai file Makefile này, nên lần chạy đầu sau khi apply phải làm bước
trên"*. Nhưng hai lượt trả lời gần nhất tôi đưa chuỗi lệnh rút gọn và **bỏ mất bước đó**. Lệnh
build đầy đủ mà `apply` in ra thì không dính, vì `airoha-compile.sh` gọi `airoha-feeds-prepare.sh`
và file đó chạy `feeds update airoha` — chỉ vòng lặp nhanh từng gói là thiếu.

Patch `0049`: `apply` in luôn bước refresh feed ngay dưới danh sách lệnh build, **cạnh đúng những
lệnh người ta copy**. `verify-apply.py` 6/6 PASS.

Không lớp kiểm tĩnh nào bắt được lỗi này — cây nguồn hoàn toàn đúng, sai ở **quy trình giao
hàng**, giữa "đã ghi vào đĩa" và "build thật sự đọc". Quy tắc rút ra: **lệnh build in cho người
dùng phải chạy được từ trạng thái sau `apply`, không phải lệnh rút gọn cho ngắn.**

Bundle `5d7cf30b602b` (387 file).

## Lượt 24/09 (11) — app COMPILE + LINK XONG, chặn ở đóng gói: thiếu `+zlib` (Claude Code)

```
Package icwmp_tr098 is missing dependencies for the following libraries:
libz.so.1
```

**Mốc: toàn bộ gói app đã compile và link sạch.** Binary đã dựng xong và đã `install` vào cây
ipkg — thứ duy nhất còn thiếu là **metadata của gói**, không phải code.

`bin/Makefile.am:131` đưa `$(LIBZ_LIBS)` vào `icwmp_tr098d_LDADD` cho `zlib.c` (nén SOAP). Gói
`cwmpclient` cũ mà bản này thay **chưa bao giờ link zlib**, nên đây là dependency **mới do iCWMP
mang vào**, không phải cái bị làm rơi khi port.

Patch `0048` thêm `+zlib`, và thêm `+libubox` — binary NEED `libubox.so` thật, hiện được cài nhờ
`+libblobmsg-json` (cùng source package) nên `ipkg-build` không kêu, nhưng phụ thuộc gián tiếp là
may mắn chứ không phải thiết kế; `libtr098` đã khai báo tường minh. **Không** thêm `+libopenssl`:
`-lcrypto`/`-lssl` có trên dòng link nhưng không file nào ở đây gọi OpenSSL (curl gọi), nên
`--as-needed` bỏ và ELF không có NEEDED — thêm vào là kéo openssl vào image vô cớ.

Lớp lỗi này khác hẳn 5 lượt trước: không phải cú pháp C mà là **metadata gói lệch với dòng
link**, và nó nằm *sau* compile lẫn link nên mất trọn một vòng build mới lộ.
`check-pkg-deps.py` so `LDADD` với `DEPENDS`, **tra tên gói từ chính cây SDK** (tìm Makefile nào
install `lib<x>.so`) thay vì bảng đoán sẵn. Chạy ngược trên bản chưa sửa: báo đúng
`-lz -> cần +zlib`.

Bundle `e9ac9a691e1f` (386 file).

## Lượt 24/09 (10) — `CWMP_LOG` mang sẵn `;`, `else` mồ côi (Claude Code)

`icwmp_dm.c` qua, dừng ở `sdk/mtk/icwmp_mtk.c`:

```
../sdk/mtk/icwmp_mtk.c:179:4: error: expected '}' before 'else'
```

**Một lỗi, không phải năm** — từ dòng 183 trở đi là parser đã rơi ra file scope, nên
`uci_free_context(c);` bị đọc như khai báo hàm top level và đụng khai báo thật trong `uci.h`.

`inc/log.h:45` định nghĩa logger kèm luôn dấu chấm phẩy:

```c
#  define CWMP_LOG(SEV,MESSAGE,args...) puts_log(SEV,MESSAGE,##args);
```

nên `if (x) CWMP_LOG(...); else ...` nở ra `if (x) puts_log(...); ; else` — `if` kết thúc ở dấu
`;` đầu, dấu thứ hai là câu lệnh rỗng, `else` mất chỗ bám. Upstream chưa bao giờ viết `CWMP_LOG`
làm thân của một `if` **có `else`**, nên bẫy này chưa từng lộ.

Patch `0047` bọc cả hai nhánh bằng `{}`, và bọc luôn `icwmp_platform_init()` — chỗ đó biên dịch
được ở cả hai cấu hình `WITH_CWMP_DEBUG` nhưng là cùng construct, câu lệnh thêm vào sau sẽ hỏng
âm thầm. Quét cả hai cây: chỉ 5 chỗ dùng kiểu này, ba chỗ còn lại là `if` đơn, vô hại.

**Đếm ngoặc mù hoàn toàn với lỗi này**: văn bản nguồn cân bằng tuyệt đối, lỗi chỉ sinh ra sau khi
macro nở. Cùng hạng với lỗi comment nuốt code — **bộ kiểm đang nhìn một văn bản khác với văn bản
compiler nhìn**. Lớp 7 mới quét macro có tham số mà thân kết thúc bằng `;` (`CWMP_LOG`, `DD`,
`DMFREE`) rồi báo khi nó làm thân không ngoặc của `if` có `else` đi sau.

**Chạy ngược 5/5 bản hỏng bắt đúng dòng**, và cả năm đều được kiểm là đúng bản hỏng trước khi
chạy. Bundle `e2a7f19e05e3` (385 file).

## Lượt 24/09 (9) — gói app tới compiler; `dm_add_end_session` trùng tên engine (Claude Code)

Gói `icwmp_tr098` qua automake, qua ~20 file của app, dừng ở file đầu tiên dùng data model:

```
../icwmp_dm.c:139:13: error: conflicting types for 'dm_add_end_session'
.../usr/include/icwmp_dm/dmtr098.h:532:5: note: previous declaration was here
```

Hai hàm khác hẳn nhau nhưng trùng tên: bản `static` của app xếp **tên** các cờ end-session vào
reply ubus, bản của engine **xếp hàng** một hành động end-session để chạy cuối phiên. `inc/cwmp.h`
kéo `<icwmp_dm/dmtr098.h>` vào mọi file của app, nên `static` đi sau khai báo non-static là lỗi.

Patch `0046` đổi tên thành `dm_add_end_session_list()`, theo dáng `dm_add_fault_list()` /
`dm_add_param_list()` nằm ngay cạnh. App chưa bao giờ gọi hàm của engine — hành vi không đổi.

**Lần thứ ba của cùng một lớp lỗi** (`deviceinfo_mtk.c`, `wlansec_mtk.c`, nay `icwmp_dm.c`), và
lần đầu ở cây app. `check-c-sanity.py` đã có lớp kiểm cho đúng lỗi này từ `0044` nhưng chỉ chạy
trên cây `libtr098`. Nay nhận `--tree app|lib` và `--sdk mtk|bdk|uci`:

- map `<icwmp_dm/...>` về gốc cây `libtr098` (trên máy build là bản trong `staging_dir`)
- lọc nguồn theo đúng binary `icwmp_tr098d` — `bin/Makefile.am` của app dựng bốn binary, ba cái
  kia có bản `dmuci_*` riêng và không link `libtr098`

Ba lớp kiểm phải sửa tại gốc vì cây app là code upstream: comment đóng sớm phải dính chữ ở **cả
hai** phía, đếm ngoặc phải bỏ nhánh `#else` như compiler thấy, và lớp "gọi hàm không khai báo"
phải bỏ qua thành viên struct, con trỏ hàm và tên nối token `##`. Từ 140 dòng báo sai xuống 0.

**Chạy ngược 4 bản hỏng, cả 4 bị bắt đúng dòng.** NEG 3 lần đầu ra `OK` — hoá ra file test bị
`awk` làm rỗng, không phải script sai. Luật cộng thêm: bản hỏng dùng chạy ngược **cũng phải được
kiểm là đúng bản hỏng**.

Bundle mới `968140592ac7` (384 file). `libtr098` không cần build lại — chỉ
`make package/icwmp_tr098/{clean,compile}`.

## Lượt 24/09 (8) — `libtr098` BUILD XONG; gói app dừng ở automake (Claude Code)

**Mốc đáng kể: thư viện data model đã build và link sạch trên SDK thật.** Cả **15/15 file MTK**
qua compiler, gồm `wan_mtk.c` (P4a) và `wanip_mtk.c` (P4b) — 259 tham số C không còn lỗi cú pháp
hay symbol nào.

Build chuyển sang gói app `icwmp_tr098` và dừng ở **automake**, chưa tới compiler:

```
sdk/bdk/sdk.mk:4: error: cannot apply '+=' because 'icwmp_tr098d_SOURCES' is not
    defined in the following conditions: ICWMP_SDK_BDK and !ICWMP_TR098
```

`bin/Makefile.am` của app định nghĩa `icwmp_tr098d_*` **bên trong** `if ICWMP_TR098`, còn mỗi
`sdk/<name>/sdk.mk` `+=` vào chúng nhưng chỉ tự bảo vệ bằng `if ICWMP_SDK_<X>`. automake kiểm
**tĩnh trên mọi tổ hợp điều kiện**, không phải tổ hợp mà `configure` thực sự chọn — và tổ hợp
`ICWMP_SDK_MTK && !ICWMP_TR098` có `+=` mà không có `=` đứng trước.

Patch `0045` lồng `if ICWMP_SDK_<X>` vào trong `if ICWMP_TR098` ở cả ba fragment. **Không đổi thứ
gì được build**: nhánh `!ICWMP_TR098` dựng `icwmpd` với bbfdm và lớp glue SDK vốn chưa từng đóng
góp gì cho nó.

**Vì sao `libtr098` không dính:** `bin/Makefile.am` của thư viện định nghĩa `libtr098_la_SOURCES =`
**không điều kiện**, nên `+=` hợp lệ ở mọi tổ hợp. Hai cây cùng bố cục nhưng khác đúng chỗ đó.

**Kiểm mới** [check-automake-conds.py](check-automake-conds.py) — mô phỏng đúng luật của automake
(tập điều kiện của `+=` phải bao hàm tập điều kiện của `=`), chạy cho cả hai cây. Chạy ngược trên
bản chưa sửa: báo đúng ba dòng automake chỉ ra. Chạy trên bản đã sửa: 0 vấn đề.

```sh
./check-c-sanity.py && ./check-automake-conds.py && ./verify-dm-paths.py --claims
```

Bundle mới **`4ea92258d6b1`** (382 file). Lỗi tiếp theo sửa ở patch `0046`.

## Lượt 24/09 (7) — lỗi compile thứ hai: `static get_empty` trùng engine (Claude Code)

```
wlansec_mtk.c:400: error: static declaration of 'get_empty' follows non-static declaration
dmtr098.h:485:     note: previous declaration of 'get_empty' was here
```

Engine đã có `get_empty()` với **đúng thân hàm** tôi viết lại (`*value = ""`). Bỏ bản `static`,
dùng của engine. Patch `0044`.

**Đây là lần thứ hai** — `deviceinfo_mtk.c` đã dính đúng lỗi này và đã được ghi vào analysis mục
11, nhưng không có gì kiểm nên nó quay lại ở file khác. Bài học không nằm ở chỗ "nhớ kỹ hơn".

**Build đi xa hơn:** dừng ở `wlansec_mtk.c` nghĩa là **12/15 file MTK compile sạch**, gồm cả
`wlan_mtk.c` và `wlanassoc_mtk.c`. Còn `wan_mtk.c` và `wanip_mtk.c` (P4a, P4b) chưa xác nhận.

**Kiểm mới, và cái bẫy khi viết nó.** Quét thô "tên `static` trùng hàm non-static ở bất kỳ header
nào" cho 10 kết quả, **9 là nhiễu** — `tr098/landevice.h` khai báo `get_wlan_enable` nhưng không
file MTK nào include nó. Lọc đúng là **bao đóng include** (kể cả gián tiếp `dmmtk.h` → `dmtr098.h`):
còn đúng 1, khớp compiler.

Bản thân `include_closure()` lúc đầu **sai và im lặng trả OK**: nó dùng `strip()` để bỏ comment,
mà `strip()` xoá nội dung mọi chuỗi, nên `#include "dmtr098.h"` thành `#include ""`. Chỉ lộ ra vì
tôi chạy kiểm ngược trên bản chưa sửa và nó không kêu.

> Luật cho mọi kiểm tĩnh viết sau: **mỗi lớp kiểm phải có một lần chạy ngược trên bản lỗi thật.**
> Kiểm trả OK không chứng minh gì nếu chưa thấy nó biết kêu.

[check-c-sanity.py](check-c-sanity.py) nay bắt sáu lớp — hai trong số đó đã bắt được lỗi thật
(`0043` và `0044`). Chi tiết: [analysis.md mục 21](analysis.md).

Bundle mới **`8abe6a04018b`** (381 file).

## Lượt 24/09 (6) — lỗi compile đầu tiên: một dấu `*/` trong comment (Claude Code)

```
../sdk/mtk/dm098/wlan_mtk.c:181:39: error: unknown type name 'backhaul_sync_'
  181 |  * when mesh is running -- mlo_sync_*/backhaul_sync_* of the shell. */
```

`mlo_sync_*/` — **`*/` đóng block comment ngay tại đó**. Phần còn lại của dòng bị đọc như code,
và định nghĩa `wlan_sync_option()` ngay dưới bị nuốt theo (đó là nguồn của dòng warning thứ hai,
`implicit declaration`). Patch `0043` sửa chữ trong comment, **không đổi một dòng code nào**.

**Đọc được gì từ log:** automake biên dịch theo thứ tự `sdk.mk`, dừng ở `wlan_mtk.c` nghĩa là
**10 file trước nó compile sạch** — toàn bộ P1 và P2. Ba file sau (`wlansec`, `wan`, `wanip`)
chưa được compiler xác nhận.

**Cảnh báo `const` đi kèm cũng dọn luôn:** `mtk_uci`/`mtk_varstate`/`mtk_varstate_set`/
`mtk_uci_default` nhận `const char *` và tự ép kiểu khi gọi `dmuci_*`; `wanip_mtk.c` bỏ `const`
ở con trỏ `struct wan_entry`. Không đụng `dmuci.h` — API dùng chung với BDK.

**Vì sao kiểm tĩnh cũ không thấy:** nó bỏ comment rồi mới đếm ngoặc, tức dùng đúng cái quy tắc
sai mà compiler dùng đúng. Đã viết [check-c-sanity.py](check-c-sanity.py) — máy trạng thái ký tự
đúng luật, bắt `*/` đóng sớm, comment/chuỗi không đóng, ngoặc lệch, và hàm gọi mà không có định
nghĩa lẫn khai báo. Chạy 15 file MTK: **0 vấn đề**; chạy ngược trên bản chưa sửa thì nó chỉ đúng
dòng 181.

```sh
./check-c-sanity.py            # trước mỗi lần giao, cùng với verify-dm-paths.py
```

Bundle mới **`249ae7447807`** (380 file). Build lại bằng lệnh trong [build-commands.md](build-commands.md);
lỗi tiếp theo sửa ở patch `0044`.

## Lượt 24/09 (5) — apply chết trên máy build vì API Python 3.9 (Claude Code)

Lần chạy đầu trên máy build dừng **trước khi ghi bất cứ thứ gì**:

```
File "apply.py", line 246, in main
    if HERE == target or HERE.is_relative_to(target) or target.is_relative_to(HERE):
AttributeError: 'PosixPath' object has no attribute 'is_relative_to'
```

`Path.is_relative_to()` có từ **Python 3.9**. Máy workspace này chạy 3.10 nên test nội bộ không
bao giờ chạm phải, máy build SDK cũ hơn. Đây là đúng một dòng, và là **dòng duy nhất** trong
`apply.py` dùng API mới hơn 3.6 — đã quét AST cả file để chắc.

Patch `0042` thay bằng `within()` (`relative_to()` trong `try`) và thêm kiểm phiên bản để
interpreter quá cũ báo một câu rõ ràng thay vì crash giữa chừng.

**Hai lớp chặn mới trong `release/tests/verify-apply.py`** để lỗi loại này không lặp lại:

1. quét AST `apply.py` tìm mọi tên chỉ có từ 3.7/3.8/3.9/3.10 (`is_relative_to`, `removeprefix`,
   `dirs_exist_ok`, `capture_output`, walrus, `match`…),
2. chạy dry-run cả hai fixture SDK bằng interpreter đã **bị gỡ** các method pathlib của 3.9 —
   mô phỏng đúng máy build.

Đã thử ngược lại: đặt lại dòng cũ thì gate báo `apply.py uses APIs newer than Python 3.6:
[(262, 'is_relative_to', '3.9')]`. Bản mới chạy dry-run PASS trên cả hai cây SDK thật, cả khi
chạy dưới shim gỡ method 3.9.

Không đụng gì tới data model — `0040`/`0041` giữ nguyên.

## Lượt 24/09 (4) — P4b `WANIPConnection`, bản mang đi build (Claude Code)

[`sdk/mtk/dm098/wanip_mtk.c`](../../../brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libicwmp_dm/src/sdk/mtk/dm098/wanip_mtk.c),
patch `0041`: **35 param** — 25 leaf kết nối + 9 `Stats` + `WANIPConnectionNumberOfEntries`.
Một object gánh **cả entry routed lẫn entry bridge**, đúng như cây shell.

**Chỗ dễ sai nhất và cũng là chỗ quan trọng nhất:** số instance là option `wan.@entry[].id` **cộng
1**, không phải vị trí section. Nhờ vậy ACS đã provision `WANIPConnection.3` vẫn nói chuyện đúng
entry đó sau khi một entry khác bị xoá. Bản C giữ cả hai: `id` để đánh số, chỉ số section để gọi
`ubus hni.wan set {"index": …}`.

**Bốn thứ của sản phẩm giữ nguyên:** entry bridge trả hằng số cho nhóm leaf IP · `ExternalIPAddress`
forced-inform **theo từng instance** (routed có bit TR-069, hoặc bridge `id=0` khi opermode là AP) ·
ghi `X_AIS_VLAN8021P` lên entry bridge **vẫn thất bại** (shell truyền một biến nó không bao giờ đặt) ·
`MaxMTUSize` sai khoảng vẫn trả `9005` chứ không `9007`.

**Ba khác biệt cố ý, đều là thêm chứ không đổi giá trị:** instance bridge có thêm `Alias`,
`X_AIS_DefaultRoute`, `X_AIS_IPMode` (cây C tĩnh có một bảng leaf cho mỗi object) · `Stats.*` là
`xsd:unsignedInt` cho mọi instance (shell truyền type nhầm vào ô setter ở nhánh routed nên chúng
đang đi ra dưới dạng string) · MTU/VLAN không phải số bị từ chối thay vì ghi thẳng vào UCI.

**Hoãn có chủ ý:** `X_AIS_ServiceList` — getter đơn giản nhưng setter là máy trạng thái ~200 dòng
sửa `easycwmp.@acs[0].enablecwmp`, rule firewall và cấu hình easycwmpd, tức là sửa chính client
TR-069 đang chạy phiên đó. Không claim, tách thành bước **P4f**.

| Kiểm | Kết quả |
|---|---|
| Đường dẫn P4a+P4b vs cây shell | **57/57**, dôi 0 |
| Thiếu ngoài nhánh đã hoãn | không có |
| Claim chồng nhau | 54 claim, 12 module, **0 cặp** |
| Patch `0041` | replay lên `HEAD~1` ra tree `2640fe21e4d9` — **trùng HEAD thật** |
| Bundle | `404cd03f4d01`, 378 file, `apply --dry-run` PASS trên cả hai cây SDK thật |
| SDK build / board | **NOT_RUN** — bước kế tiếp của người dùng |

Trong lượt này công cụ `verify-dm-paths.py` bị **hai lỗi của chính nó** và đã sửa: bảng `static`
trùng tên giữa hai file làm mất hẳn một nhánh khi dựng cây, và hàng bảng trải hai dòng bị bỏ qua.
Chi tiết ở [analysis.md mục 18.7](analysis.md). Code không sai, nhưng lỗi thứ nhất che mất một nhánh
thật nên đáng ghi.

### Build thử — chạy gì, gửi lại gì

```sh
python3 -V                       # apply cần >= 3.6, không cần 3.9
tar -xzf icwmp_multiplatform_port.tar.gz
cd icwmp_port && sha256sum -c SHA256SUMS
./apply --sdk mtk <SDK root>     # thêm --dry-run để xem trước
```

Giải nén **ngoài** thư mục SDK — apply từ chối nếu bundle nằm trong cây đích.

Apply in ra lệnh build ngay sau khi chạy xong. Lỗi compile **đầu tiên** gửi lại kèm ~20 dòng
ngữ cảnh → sửa ở patch `0042`.

Sau lần apply đầu tiên, feed của MTK phải đồng bộ lại trước khi build (feed dùng `src-cpy`,
tức copy chứ không symlink):

```sh
cd <SDK>/openwrt-21.02/openwrt-21.02.1_dev
./scripts/feeds update airoha && ./scripts/feeds install -p airoha -f libtr098 icwmp_tr098
```

Từ đó chỉ cần build hai gói để bắt lỗi compile, không phải dựng cả image — xem
[build-commands.md](build-commands.md).

Rollback: apply **tự phục hồi** nếu nó lỗi giữa chừng (ghi theo staging + `journal.json`, khôi
phục mọi file nó đã đụng rồi đặt `state: rolled_back`). Muốn quay lại thủ công sau khi apply đã
thành công thì chép ngược từ thư mục backup nó in ra — `<SDK>/.icwmp-backups/<timestamp>/original/`.
Không có cờ `--apply --rollback`, apply chỉ nhận `--sdk`, `--profile`, `--dry-run`.

## Lượt 24/09 (3) — P4a, khung `WANDevice` (Claude Code)

Bắt đầu phase lớn nhất (173 param) bằng phần không phụ thuộc entry WAN nào —
[`sdk/mtk/dm098/wan_mtk.c`](../../../brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libicwmp_dm/src/sdk/mtk/dm098/wan_mtk.c)
+ [`wan_mtk.h`](../../../brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libicwmp_dm/src/sdk/mtk/dm098/wan_mtk.h),
patch `0040`: `WANCommonInterfaceConfig` 9, `WANEthernetInterfaceConfig` + `Stats` 8,
`WANDSLLinkConfig` 5 — **22 param, 8 object**.

`WANIPConnection` / `WANPPPConnection` vẫn do `sdk/mtk/compat/` trả lời, nên chỉ claim ba nhánh đã
port chứ không claim cả `WANDevice.`.

**Một lỗi có thật của sản phẩm, giữ nguyên chứ không sửa:** `WANAccessType` trả **chuỗi rỗng**.
Getter `wan_common_get_access_type` được đăng ký ở `functions/tr098/wan_device:3095` nhưng **không
định nghĩa ở đâu trong cây `ext/`**, nên shell chạy một lệnh không tồn tại và trả rỗng. TR-098
định nghĩa tham số này là enum `DSL | Ethernet | POTS`. Điền giá trị vào là **đổi hành vi sản
phẩm** — ACS đã đọc rỗng suốt vòng đời máy. Chi tiết và chỗ sửa một dòng: [analysis.md mục 17.1](analysis.md).

Hai thứ khác cũng giữ nguyên: `WANEthernetInterfaceConfig`/`WANDSLLinkConfig` là hằng số (máy
PON/Ethernet, không có DSL — chính shell đặt tên hàm là `get_fake_*`), và các leaf ghi được nhưng
không có setter vẫn **nhận rồi bỏ** thay vì trả `9008`.

**Công cụ mới** [`verify-dm-paths.py`](verify-dm-paths.py) — từ lượt này so **đường dẫn đầy đủ**
chứ không chỉ tên leaf: dựng lại cây từ bảng `DMOBJ`/`DMLEAF` của đúng danh sách nguồn đang build,
gộp theo tên object như `dm_registry` làm, rồi đối chiếu `tr098_coverage_matrix.tsv`.
Chạy lại trên phase đã chốt bằng tay cho đúng số cũ (P2+P3 `137/137`, dôi 0) — đó là bằng chứng
script đúng.

| Kiểm | Kết quả |
|---|---|
| Đường dẫn P4a vs cây shell | **22/22**, thiếu 0, dôi 0 |
| Phase 4 tổng | 22 xong / 151 còn (P4b–P4e) |
| Claim chồng nhau | 27 claim, 11 module, **0 cặp** |
| Patch `0040` | replay lên `HEAD~1` ra tree `f31952b8ba16` — **trùng HEAD thật** |
| Bundle | `78d6b8d8e5b4`, 376 file, `apply --dry-run` PASS trên cả hai cây SDK thật |
| SDK build / board | **NOT_RUN** |

## Lượt 24/09 (2) — P3b, xong cả phase P3 (Claude Code)

13 leaf bảo mật cuối cùng của `WLANConfiguration` sang C —
[`sdk/mtk/dm098/wlansec_mtk.c`](../../../brcm_ap_wifi7_mvn/issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libicwmp_dm/src/sdk/mtk/dm098/wlansec_mtk.c):
`BeaconType`, ba cặp `Basic`/`WPA`/`IEEE11i` auth + encryption, `KeyPassphrase`,
`PreSharedKey.1.*`, `WEPKeyIndex`, `WEPEncryptionLevel`, `WEPKey.{i}.WEPKey`.

Tất cả treo trên một option `wireless.<iface>.encryption`. **Hai cái bẫy của sản phẩm được giữ
nguyên chứ không dọn:**

- cùng option `.key` là **passphrase** với WPA nhưng là **số thứ tự key** với WEP — đúng thứ
  `set_wep_key_index()` ghi vào;
- chỉ `BeaconType`, `BasicAuthenticationMode` và `WEPEncryptionLevel` đẩy `authmode`/`EncryptType`
  sang mapd. Setter WPA và IEEE11i **chưa từng** làm thế, nên vẫn không làm.

`KeyPassphrase` và `PreSharedKey.1.*` trông giống nhau nhưng **không phải một**: cái đầu từ chối
mọi `wep+` và đồng bộ cặp MLO, cái sau ép độ dài 8–63 và ghi thêm `key1` (5 ký tự đầu, WebUI hiển
thị). Giữ cả hai.

Một điểm **cố ý khác** shell: `11i`/`WPAand11i` giữ nguyên cấu hình SAE đang có thay vì hạ xuống
`psk2`, để ACS ghi lại đúng `BeaconType` nó vừa đọc không âm thầm hạ cấp một BSS WPA3.

Cả object giờ là C nên claim gộp lại: `wlan_mtk.c` bỏ 30 dòng claim từng leaf, còn **một** claim
nhánh; hai module anh em không claim gì. Kiểm lại toàn bộ: **16 claim của 7 module MTK, 0 cặp chồng nhau.**

## Lượt 24/09 — P3a Wi-Fi (Claude Code)

`WLANConfiguration.{i}` **54/67 tham số** sang C, chia hai module:

| File | Nội dung |
|---|---|
| `sdk/mtk/dm098/wlan_mtk.c` | radio/identity, kênh + auto channel, công suất phát, chuẩn và tốc độ, MU-OFDMA, bộ đếm, WPS |
| `sdk/mtk/dm098/wlanassoc_mtk.c` | `AssociatedDevice.{i}` + `Stats`, đọc một lần `ubus call hni getWlanDeviceList` cho mỗi interface |

Bản đồ instance của sản phẩm giữ **nguyên xi** (1–4 `ra0..ra3`, 5–8 `rai0..rai3`, 9 `rai4`,
10 `ra4`, 11 `ra5`, 12 `rai5`) — ACS đã provision theo số này. Setter giữ đồng bộ hai cặp
MLO (`ra5`/`rai5` + `apmld1`, `ra4`/`rai4` + `apmld2`) và ghi sang node `mapd.<n>` khi mesh
đang bật, vì mapd ghi đè `wireless` từ config của nó ở lần reload sau.

**Còn lại của P3 là P3b: 13 leaf bảo mật** (`BeaconType`, `Basic/WPA/IEEE11i` auth + encryption,
`KeyPassphrase`, `PreSharedKey`, `WEP*`) — vẫn do cầu nối shell trả lời. Vì thế module này claim
**từng leaf** thay vì cả nhánh; `dm_registry` được bổ sung khả năng khớp path theo segment với
wildcard `{i}`, và claim không có dấu chấm cuối giờ là **một leaf đúng nghĩa** (trước đây
`IGD.Foo.Bar` nuốt cả `IGD.Foo.BarBaz`).

## Lượt 23/09 tối — R6, R7 và P2 (Claude Code)

| Việc | Kết quả |
|---|---|
| **R6** registry báo trùng chủ sở hữu path | `dm_registry.c`: `check_claims()` báo từng cặp `.paths` chồng nhau lúc build, `dm_registry_conflicts()` trả số. Mở rộng object của module khác vẫn làm bằng cách **không khai `.paths`** |
| **R7** transaction cho hàng đợi action cuối phiên | `dm_end_session_mark()`/`dm_end_session_rollback()` trong `dmtr098.c`, gọi ở mọi nhánh fault của `dm_entry_apply`. Trước đây SPV lỗi vẫn để lại reboot/factory-reset đã xếp hàng. Thêm `dm_platform_revert()` vào nhánh commit hỏng (trước chỉ có `dmuci_revert()`) |
| **P2** LAN bằng C | 4 module, **70/70 tham số khớp tên** cây shell: `lan_mtk.c`, `lanhosts_mtk.c`, `laneth_mtk.c`, `x_ais_mesh_mtk.c` |
| **Sửa extractor inventory** | bảng cũ vừa **bịa 29 path** vừa **mất 61 path** — xem [tr098_c_port_phases.md §0b](tr098_c_port_phases.md). Tổng đúng: **783 param / 184 object** |

Khác biệt có chủ ý duy nhất của P2: `X_AIS_Mesh.MeshEnabled`/`MeshMode` khi thiếu option UCI thì
trả giá trị mặc định mà **không** phát fault — bản shell trả 9002, và một fault giữa GPV toàn cây
sẽ làm hỏng cả RPC. Mọi fault code khác giữ nguyên (`dhcp.lan.configurable=0` → 9002, riêng
`MaxAddress` → 9007, đúng như shell).

## Review kiến trúc 23/09 — đọc trước khi bàn giao code

**Hướng tách SDK đúng, nhưng `0033` chưa phải app unify hoàn chỉnh và chưa xóa riêng TR-098/TR-181
để build được.** Phân tích source với 8 findings tại [analysis §10](analysis.md#10-review-kiến-trúc-2309--codex-snapshot-d3c82a4).
Thiết kế đích, source layout, options/profile, ma trận release và gate kiểm tại
[design §10–17](../../docs/icwmp_multiplatform_tr098_design.md#10-kết-quả-review-và-mục-tiêu-kiến-trúc).
Các options model/profile mới vẫn **Proposed, chưa implement**. Riêng rename source/include A1 đã thực thi.

- `--enable-icwmp_tr098` chọn **engine libtr098**, engine đó phục vụ cả TR-181 trên BDK.
  Không tắt cờ này để tạo bản TR-181-only, vì nhánh còn lại dùng bbfdm upstream.
- TR-181 đang dùng code trong `tr098/` và `sdk/bdk/dm098/`. Phải tách shared services, schema
  theo model và backend trước khi xóa. **BDK TR-098-only vẫn cần backend MDM TR-181**.
- ~~Compat-off còn lời gọi `dmscript_request` ngoài guard~~ → **đã sửa** (R3, xem Hiện trạng bên
  dưới). Package vẫn cài script vô điều kiện — đúng cho bản đang ship vì feed không truyền
  `--disable-dm-script-compat`. Registry chưa kiểm duplicate owner, model runtime chưa kiểm tập
  model thực có trong binary (R6, R4 — còn mở).
- Đề xuất ba trục **SDK × model ACS × product profile**, layers app → DM engine/facade →
  service contracts → SDK backend. Common có thể giữ, source/link/install/runtime của phần
  tắt phải cùng theo profile. Action queue cần gắn transaction để bỏ action của failed RPC.
- Ưu tiên profile/model separation và transaction/registry trước khi mở rộng P2–P8.
  Lượt review chỉ sửa tài liệu, không thay source/patch/tarball. PDF design cũ chưa cập nhật.

## Hiện trạng đồng bộ — 2026-09-23 21:00

Issue có hai phần công việc: source đang chờ build, phần thiết kế/plan đã hoàn thiện:

| Luồng | Ai | Sản phẩm | Trạng thái |
|---|---|---|---|
| **Source** — tách SDK, registry, data model C | Claude Code | overlay `cd93685`, patch `0032`+`0033`, tarball, feed, installer | code xong, **chưa build** |
| **Thiết kế đích** — app unify theo layer, profile, single-model release | Codex | `analysis.md` §10 (R1–R8), `docs/..._design.md` §10–18, `docs/..._flow.md`, `tr098_c_port_phases.md` §6 (A0–A6) | **Tài liệu hoàn thiện, code Proposed chưa implement** |

Bảng dưới mô tả baseline 0033 trước A1. Source `libicwmp_dm/src` nay đã có, còn `services/`,
`--enable-model-*`, `EXPOSE_DM_STUBS` vẫn là đề xuất. Current execution status nằm ở đầu README.

### Đã có trong source (patch `0033`, commit `cd93685`)

| Hạng mục | Bằng chứng |
|---|---|
| Một thư mục một SDK, `sdk/enabled.*` sinh tự động, `./sdk-prune.sh <name>` | prune thử trên bản copy: source còn đủ, không sót tên SDK khác |
| Registry module data model, gộp đệ quy theo tên object | thay `tEntry098Obj`/`tEntry181Obj`/`mtk_native_objs[]` |
| P1 bằng C: DeviceInfo, Time, ManagementServer | 65/65 tên tham số khớp cây shell cũ |
| Compat-off không còn tham chiếu lớp transport trong phần source đã strip | bỏ mọi khối `DM_MTK_SCRIPT_COMPAT` còn **196 dòng**, **0** tham chiếu symbol compat |
| BDK giữ logic getter/setter ở mức source | so nội dung 15 file BDK trước/sau: chỉ đổi `#include` + cách dựng root |

### Ba lỗi build do refactor, đã sửa trong `0033`

| Lỗi | Hậu quả | Sửa |
|---|---|---|
| `tr098_bdk_register_all()` thành `static` nhưng `dmplatform_bdk.c:427` vẫn gọi | BDK **lỗi link** | trả lại non-static, registry gọi qua `.init`, cờ `done` chặn chạy hai lần |
| `static get_empty()` trong `deviceinfo_mtk.c` trùng hàm engine (`dmtr098.h:485`) | MTK **lỗi compile** | bỏ, dùng `get_empty()` của engine |
| `--enable-bdk` bị bỏ | script build BDK cũ **âm thầm rơi về SDK `uci`** | trả lại làm alias của `--with-sdk=bdk` |

### Tám finding của review — trạng thái

| # | Nội dung | Trạng thái |
|---|---|---|
| R3 | compat-off chưa kín: helper vẫn gọi `dmscript_request()` ngoài guard | **ĐÃ SỬA** trong `cd93685`, kiểm lại bằng cách strip khối `#ifdef` |
| R1 | chưa có lựa chọn build độc lập cho hai model (`--enable-icwmp_tr098` chọn engine, không chọn model) | mở — thiết kế đích design §13 |
| R2 | TR-181-only còn phụ thuộc code dưới `tr098/` và `sdk/bdk/dm098/` | mở — cần tách shared services trước |
| R4 | runtime không kiểm model có thật trong binary | mở |
| R5 | "portable" đang lẫn với "dùng lại được trên schema UCI cũ" | mở — vấn đề đặt tên/contract |
| R6 | registry chưa thực thi một-path-một-chủ (không báo trùng owner) | mở — cần kiểm owner/merge/OOM và dynamic provider, không ước lượng theo số dòng |
| R7 | VALUECHECK/VALUESET chưa chứng minh atomic, action queue chưa gắn transaction | mở — phải có trước rollout |
| R8 | build profile chưa là nguồn lựa chọn chung cho app/lib/package | mở |

**Kế hoạch thực thi thống nhất:** A0 pin baseline → A1 rename source → A2 profile/model separation
→ A3 contracts/registry/transaction → A4 migrate P1 → A5 port P2–P8 → A6 release.
R6/R7 là gate trước rollout, không coi transaction là thay đổi rẻ hay chỉ thêm vài dòng.
Nếu sửa sớm trên layout cũ, ghi nhận là phần A3 đã hoàn thành, tránh làm lại sau rename.
Chi tiết đầu vào, artifact và acceptance từng gói ở [phase plan §6](tr098_c_port_phases.md#6-kế-hoạch-thực-thi-từ-source-hiện-tại--2309).

### Tên source, flow thành phần và TR-181 scaffold

- Đích **`public/libs/libicwmp_dm/src/`**, wrapper ở `public/libs/libicwmp_dm/`. Tách lần đổi
  source khỏi SONAME/package, có thể giữ `libtr098.so` chuyển tiếp. Xem [design §12](../../docs/icwmp_multiplatform_tr098_design.md#12-source-layout-đề-xuất-và-bản-đồ-di-chuyển).
- [Flow kiến trúc chi tiết](../../docs/icwmp_multiplatform_tr098_flow.md): module trong icwmpd,
  L4 backend, process SDK và các boundary đã trace xuống driver cho WAN/LAN/Wi-Fi/Mesh/STA/Stats.
  Mũi tên chưa chứng minh tới hardware được đánh Conditional/Not established.
- TR-098 implement đầy đủ theo inventory **783 param** (bản sửa 23/09). TR-181 reuse provider BDK đã có; phần thiếu
  tạo mapping/schema/callback TODO từ mapping đã duyệt. Production không quảng bá stub,
  development gọi callback để kiểm routing/error. Không đổi root string để giả lập mapping.
- **Baseline mới đã kiểm:** cd93685 sạch, MTK b207c4518, BDK 7f837f5f6. Coverage vẫn
  749 param/181 object (**số này đã bị thay bằng 783/184**, xem §0b của phase doc). Ba lỗi build §11 đã có trong d3c82a4; delta lên cd93685 chỉ sửa guard R3.
  Chưa build-test, chưa rename hoặc implement A1–A6 trong lượt thiết kế này.

## Yêu cầu của người dùng

1. (22/09) Cùng một cây source chạy được cho **Broadcom BDK** và **MTK OpenWrt 2025q3**, TR-098
   trước, phải phục vụ đúng tập tham số TR-098 mà `cwmpclient` (easycwmp) đang chạy.
2. (23/09) **Toàn bộ data model phải là C** để nhanh và chạy tốt trên nhiều SDK. App hỗ trợ nhiều
   SDK nhưng **xoá bớt được, chỉ giữ một SDK** khi giao code cho bên khác. Cập nhật tài liệu
   thiết kế cho việc chạy nhiều SDK, đi sâu Broadcom và MTK. Chia phase và code tới TR-098 với
   các tham số easycwmp đang hỗ trợ.
3. (23/09, review) Kiến trúc app unify theo layer, phần SDK specific rõ ràng, release được một
   datamodel bằng xóa phần model và tắt đúng build/profile options, cho phép giữ common cần thiết.
4. (23/09, flow/plan) Đổi tên thư viện trung lập model, flow chi tiết module/process/driver,
   chia phase từ source mới nhất. TR-098 implement thật, TR-181 reuse hoặc callback TODO rõ ràng.

## Baseline thiết kế `0033` — đọc cùng đính chính review bên trên

### 1. Hai trục tách rời

| Trục | Nằm ở đâu |
|---|---|
| **Data model** — cây tham số trông thế nào | `tr098/` (portable), `sdk/<name>/dm098/` (riêng SDK) |
| **SDK** — giá trị nằm ở đâu, ghi bằng cách nào | `sdk/<name>/` |

Mỗi SDK là **một thư mục tự chứa**: `sdk.m4` (mảnh configure), `sdk.mk` (mảnh automake), code
hook, `dm098/`, `scripts/`, `files/`, `compat/`, `README.md`. **Không file nào bên ngoài nhắc tên
SDK** là mục tiêu ban đầu, chưa đạt cho toàn bộ code/wrapper (xem R5/R8). Entry build dùng
`configure.ac` với `m4_include([sdk/enabled.m4])`, `Makefile.am` chỉ
`include $(top_srcdir)/sdk/enabled.mk`, và hai file `enabled.*` do `tools/sdk-scan.sh` sinh ra từ
các thư mục đang tồn tại.

```sh
./sdk-prune.sh mtk      # giữ mtk, xoá bdk + uci ở cả icwmp lẫn libtr098
./sdk-prune.sh --list
# rồi: autoreconf -fi && ./configure --with-sdk=mtk
```

Phiên trước kiểm trên bản copy: source reference của mảnh build sau prune còn tồn tại.
**Review:** kết quả đó là static audit trong phạm vi component, chưa phải configure/build/install
và không bao gồm xóa model hoặc mọi wrapper/package recipe.

### 2. Data model = module C tự đăng ký

`dm_registry.c` mới: một module = vài dòng DMOBJ + bảng DMLEAF + danh sách path nó sở hữu, tự
đăng ký bằng constructor. Registry gộp các module **theo tên object, đệ quy**, theo thứ tự
`(order, name)`. Không còn `tEntry098Obj` viết tay ở ba nơi.

Hệ quả thực dụng:

- Module của SDK **thêm lá vào object portable** mà không sửa file portable
  (`mtk-managementserver` thêm `EnableCWMP`, `UpgradesManaged`).
- Theo luật merge, module `.order` cao có thể ghi đè leaf của module thấp hơn.
  Đính chính: MTK hiện không link `tr098/times.c`, nên không có bước override Time đó ở runtime.
- `.paths` thay danh sách `mtk_native_objs[]` cũ bằng claim prefix. **Review:** registry chưa
  bảo đảm một path một chủ, leaf trùng bị module sau ghi đè (R6).

### 3. MTK: C là đích, shell chỉ là giàn giáo

Đếm lại từ source (không ước lượng, bản sửa extractor 23/09 tối): thư viện hàm của `cwmpclient`
phục vụ **783 parameter / 184 object**, trong đó **235 tham số `X_AIS_*`** của nhà mạng và
**0 tham số `X_HNI_*`** (toàn bộ
file `x_hni_*` bị comment trong snapshot này). Chỉ **34** tham số là `uci get` thuần, **692** đi
qua **449 hàm shell** khác nhau, tổng ~24 600 dòng → **không sinh code tự động được**.

Vì vậy: chuyển sang C **theo phase**, `sdk/mtk/compat/` phục vụ path chưa có chủ C và mục tiêu là bỏ được
bằng compat-off + xóa thư mục. **Guard R3 đã sửa, vẫn cần gate package/build/install trước khi xóa.**

Lộ trình và bảng tra từng tham số: [tr098_c_port_phases.md](tr098_c_port_phases.md),
[tr098_coverage_matrix.tsv](tr098_coverage_matrix.tsv).

| Phase | Nhánh | Param | Trạng thái |
|---|---|---|---|
| P1 | `DeviceInfo.`, `Time.`, `ManagementServer.` | 65 | **Đã viết, chưa build/board** |
| P2 | `LANDevice.` trừ Wi-Fi | 62 | chưa |
| P3 | `LANDevice.{i}.WLANConfiguration.` | 75 | chưa |
| P4 | `WANDevice.` | 173 | chưa |
| P5 | Diagnostics + `Layer3Forwarding` | 88 | chưa |
| P6 | `Firewall.`, `UserInterface.`, root còn lại | 63 | chưa |
| P7 | cây `X_AIS_*` ở root | 79 | chưa |
| P8 | StorageService, STB, DOCSIS, LTE, `IGD.Device.*` | 144 | chưa |

P1 đã kiểm bằng so khớp tự động: **65/65 tham số của cây cũ có trong module C**, dôi 19 tham số là
của chính icwmp (11 lá `ManagementServer` chuẩn TR-098 mà client cũ thiếu, 8 lá `X_HNI_Icwmp`).

### 4. Giữ nguyên hợp đồng bên ngoài

`/etc/init.d/easycwmpd` là wrapper gọi `icwmpd`, UCI `easycwmp` vẫn là config of record, thư viện
hàm vẫn cài đúng `/usr/share/easycwmp/functions` (vì `stuncd.init` sed thẳng vào đó), tên ubus
object `tr069` không đổi, `CONFLICTS:=cwmpclient`.

## Ba SDK sau refactor

| `--with-sdk=` | Data model | Glue icwmpd | Dùng cho |
|---|---|---|---|
| `uci` (mặc định) | cây portable `tr098/` trên UCI OpenWrt | `sdk/uci/` (rỗng) | bản gốc / mẫu để thêm SDK mới |
| `bdk` | Distributed MDM (TR-181) qua `libbcm_generic_hal`, `sdk/bdk/dm098/` | `sdk/bdk/` | `brcm_ap_wifi7_mvn` (MO77300EB) |
| `mtk` | **C** trong `sdk/mtk/dm098/` + `compat/` cho phần chưa port | `sdk/mtk/` | `mtk_openwrt_wifi7` (HP2236B) |

## Thành phần mới ở lượt 23/09

| File | Vai trò |
|---|---|
| `libtr098/dm_registry.c/.h` | registry module data model: đăng ký, gộp cây, claim path |
| `libtr098/sdk/sdk.h` | hợp đồng SDK (đổi chỗ từ `platform/dmplatform.h`), tài liệu layout |
| `libtr098/sdk/{uci,bdk,mtk}/{sdk.m4,sdk.mk,README.md}` | mảnh build + tài liệu của từng SDK |
| `libtr098/sdk/mtk/dmmtk.c/.h` | helper dùng chung cho module C của MTK: UCI, `/var/state`, `/proc`, exec, hàng đợi apply-service |
| `libtr098/sdk/mtk/dm098/deviceinfo_mtk.c` | DeviceInfo đầy đủ bằng C (28 tham số + DeviceId của Inform) |
| `libtr098/sdk/mtk/dm098/time_mtk.c` + `tz_table_mtk.h` | Time bằng C, bảng 167 thành phố **sinh từ chính source sản phẩm** (`tools/gen-tz-table.sh`) |
| `libtr098/sdk/mtk/dm098/managementserver*_mtk.c` | ManagementServer portable + 2 lá riêng của sản phẩm |
| `libtr098/tools/{sdk-scan.sh,sdk-prune.sh,gen-tz-table.sh}`, `icwmp/tools/*`, `sdk-prune.sh` (gốc overlay) | công cụ thêm/xoá SDK, sinh bảng |
| `issues/.../tr098_c_port_phases.md`, `tr098_coverage_matrix.tsv`, `gen-coverage-matrix.py` | lộ trình 8 phase + bảng tra 968 dòng + công cụ sinh lại bảng từ source sản phẩm |

Chi tiết trace và bằng chứng: [analysis.md](analysis.md).
Thiết kế + flow (mermaid): [docs/icwmp_multiplatform_tr098_design.md](../../docs/icwmp_multiplatform_tr098_design.md).
Lệnh debug: [debug-commands.md](debug-commands.md).

## Sửa kèm (ảnh hưởng cả BDK)

- `dm_platform_commit(ctx, parameter_key)` — mtk cần ParameterKey để ghi `easycwmp.@acs[0].parameter_key`.
- `X_..._Icwmp.` chuyển sang `tr098/common/icwmpcfg.c` (dùng chung; `DataModel` trả 9001 ngoài BDK).
- `ubus call tr069 dm` thành tool chung mọi SDK (`icwmp_dm.c`).
- **CR server nhận mọi path**: trước hardcode `GET / HTTP/1.` và tính Digest theo `/`; sản phẩm MTK
  quảng bá `http://<ip>:7547/ConnectionRequest` → ACS gửi `GET /ConnectionRequest` sẽ bị 503 và
  Digest sai. Nay lấy path từ request line và dùng đúng path đó để check Digest.
- `--with-platform` đổi tên thành `--with-sdk` (tên cũ vẫn nhận). Biến build `TR098_PLATFORM`,
  `ICWMP_PLATFORM` vẫn nhận, tên mới là `TR098_SDK`, `ICWMP_SDK`.

## Cài đặt

**Cách khuyến nghị (bản `08e4499`)** — bundle tự chứa, một lệnh cho cả hai SDK, có backup và
rollback. Đã verify `--dry-run` trên **chính hai cây SDK thật** ngày 23/09 22:10:

```sh
tar -xzf icwmp_multiplatform_port.tar.gz     # giải nén NGOÀI cây SDK
cd icwmp_port
sha256sum -c SHA256SUMS                      # kiểm bundle trước
./apply --sdk mtk --dry-run <đường dẫn 2025q3>     # xem trước, không ghi gì
./apply --sdk mtk          <đường dẫn 2025q3>     # ghi thật
./apply --sdk bdk          <đường dẫn bcm963xx>   # BDK, profile mặc định MO77300EB
```

`apply` tự làm luôn phần config: MTK bật `CONFIG_PACKAGE_libtr098`/`icwmp_tr098`/`libmicroxml`
và tắt `cwmpclient` trong `config_7583`; BDK sửa `make.common` + `comp_tr69_md.c` và bật
`BUILD_ICWMP`/TR69 SSL trong profile. Mọi file bị thay có bản gốc trong
`<SDK>/.icwmp-backups/<timestamp>/original/`, kèm `journal.json` để biết đã ghi tới đâu.

**Lưu ý với cây MTK:** `tclinux_phoenix/apps/hni/libtr098` có sẵn trong vendor tree (commit
`2d6f314f7 [ARHT-339] Porting tr069 (iopsys)`, hiện `is not set` trong profile) sẽ bị **archive**
sang backup để tránh hai engine cùng SONAME. Cây 2025q3 sau khi apply sẽ dirty với git của vendor.

Cách cũ (overlay tại chỗ, chỉ MTK, không tự sửa config):

```sh
# từ workspace (dùng overlay tại chỗ)
projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098/install-mtk.sh \
    ~/workspace/openwrt/src_bk/2025q3 --dry-run            # xem trước
projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098/install-mtk.sh \
    ~/workspace/openwrt/src_bk/2025q3 [--only-mtk]         # --only-mtk: xoá luôn sdk/bdk, sdk/uci

# hoặc mang tarball sang máy build
tar -xzf icwmp_mtk_port.tar.gz && ./icwmp_mtk_port/install.sh <đường dẫn 2025q3> [--only-mtk]
```

Sau đó sửa `airoha_feeds/airoha_build/profile/HP2236B/config_7583`:

```
CONFIG_PACKAGE_libtr098=y
CONFIG_PACKAGE_icwmp_tr098=y
# CONFIG_PACKAGE_cwmpclient is not set
```

Build:

```sh
./airoha_script/airoha-compile.sh -c 7583 -f -m HP2236B -w Griffin_logan && \
cd openwrt-21.02/openwrt-21.02.1_dev && make -j 16 MSDK=1 V=s
```

## Trạng thái kiểm thử

| Bước | Kết quả |
|---|---|
| Build host | **NOT RUN** — workspace không có toolchain |
| Build SDK 2025q3 | **NOT RUN** — chờ người dùng |
| Board test | **NOT RUN** |
| Kiểm tĩnh sau SDK prune (bản copy, phiên trước) | PASS kiểm source reference, **chưa configure/build/install** |
| Resolve include cục bộ theo từng SDK (6 tổ hợp) | PASS |
| Symbol tham chiếu nhưng không được link | PASS (rỗng, trừ `tEntry098ObjUPNP` chỉ dùng khi bật UPnP — có từ trước) |
| Cân bằng `#if/#endif`, `sh -n` mọi script | PASS |
| P1: tên tham số khớp cây cũ | PASS (65/65) |
| Hàm `static` bị file khác trong link set gọi | PASS sau khi sửa 3 lỗi build (bảng ở mục Hiện trạng) |
| Nội dung file BDK so với trước refactor | chỉ đổi `#include` + cách dựng root, **không đổi logic** (so 15 file) |
| Compat-off source guard (chưa compile/link) | PASS kiểm tĩnh — strip mọi khối `DM_MTK_SCRIPT_COMPAT` còn 196 dòng, 0 tham chiếu symbol compat (R3) |
| Bundle `icwmp_multiplatform_port.tar.gz` tự kiểm | PASS — `sha256sum -c SHA256SUMS` 373 file, MANIFEST khớp `8711298` |
| P2: tên tham số khớp cây cũ | PASS **70/70**, không dôi (đối chiếu tự động với `tr098_coverage_matrix.tsv`) |
| P3a: tên tham số khớp cây cũ | PASS **54/54** |
| P3 đầy đủ (a+b): tên tham số khớp cây cũ | PASS **67/67**; P2+P3 = **137/137**, dôi 0, thiếu 0 |
| Claim `.paths` của 7 module MTK chồng nhau | PASS **16 claim, 0 cặp** |
| Matcher `.paths` (wildcard `{i}`, leaf chính xác, prefix) | PASS 23/23 case — thuật toán port sang Python để chạy, **không phải kiểm cú pháp C** |
| 44 claim của P2+P3a có chồng nhau không | PASS 0 cặp |
| Cân bằng brace/paren/`#if` 4 module P2 + `dmmtk` | PASS |
| Symbol P2 dùng nhưng không khai báo | PASS (rỗng) |
| Sinh lại `tr098_coverage_matrix.tsv` từ source | PASS byte-for-byte, 968 dòng |
| Replay `0034`+`0035` lên baseline `cd93685` (GNU patch, fuzz=0) | PASS — không reject, kết quả **byte-for-byte khớp HEAD `08e4499`** |
| `apply --dry-run` trên cây BDK thật (`bcm963xx`) | PASS — tìm đúng context `make.common` + `comp_tr69_md.c` + profile `MO77300EB` |
| `apply --dry-run` trên cây MTK thật (`2025q3`) | PASS — đúng `config_7583` HP2236B, 2 feed Makefile, archive legacy `hni/libtr098` |

Ba lỗi build do refactor đã sửa: xem bảng ở mục [Hiện trạng đồng bộ](#hiện-trạng-đồng-bộ--2026-09-23-2100).

**Mọi file C mới chưa qua compiler.** `0034`–`0039` đã dùng — lỗi build đầu tiên gửi lại sẽ sửa
trong patch **`0040`**.

## Việc còn lại

1. Build baseline trên máy SDK, sửa lỗi compile quan sát được. Sau đó triển khai A1–A3
   trong design (profile/model dependency, transaction/registry) trước khi nhân rộng P2–P8.
2. Gate 1 trên board: `icwmpd` lên, `ubus call tr069 status`,
   `ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo."}'`.
3. Gate 2: so **giá trị** P1 với client cũ (quy trình §3 của `tr098_c_port_phases.md`), rồi
   session với ACS thật.
4. Gate 3: Connection Request (path `/ConnectionRequest`), Download/Upload, reboot, factory reset.
5. P2 → P8 theo `tr098_c_port_phases.md`, mỗi phase một patch.
6. R3 đã sửa source guard, chưa xác nhận compile/link. Khi hết phase: bật `--disable-dm-script-compat` trong feed, xóa
   `sdk/mtk/compat/`, verify clean build/install và integration STUN/easycwmp còn cần.
7. TR-181 cho MTK: tạo mapping/schema/callback cùng từng phase TR-098, reuse phần có sẵn,
   backend thiếu cho phép TODO theo design §18. Chưa hỗ trợ production TR-181 đầy đủ.
