# Đồng bộ `main`, `dev` và thiết kế v2: kế hoạch hợp nhất

**Mốc:** 2026-10-04, `dev` = `0362184` (patch 0077 + `tests/host`).
**Đầu vào:** bốn tài liệu rà soát v2 ngày 2026-09-30 ([docs/plan/v2](v2/)), viết dựa trên
`main` (tới 0066); code của `dev`; kiểm tĩnh và test host chạy lại trên chính repo này.
**Cập nhật cùng ngày (đợt 2, trực tiếp trên `dev`):** K1, K2 sửa bằng 0078; K10 (PeriodicInformTime)
sửa bằng 0079; checksum bundle hỏng do commit docs (K11) đã sửa, thêm `update-sums.py`. K1, K2, K10 có
test host (`msrv`, `stun`, `ptime`): FAIL trước bản sửa, PASS sau, và nằm trong `run.sh all`. K3 giữ
nguyên, K6 giữ trace. Chi tiết ở §3, §4, §7.
**Vai trò:** file này là kế hoạch hiện hành. Trạng thái dạng máy đọc nằm ở
[implementation-status.json](../issue/implementation-status.json). Kiến trúc đích là v2,
áp dụng cùng các điều chỉnh ở §2.

---

## 0. Kết luận

1. **Git không có gì phải merge.** `dev` = `main` + 13 commit, không phân kỳ (merge-base =
   `main` = `4965f5d`). Việc cần đồng bộ là trạng thái và kế hoạch: tài liệu v2 viết trên
   0066 nên chưa biết 0067–0077 và tầng test host.
2. **Kiến trúc đích của v2 đúng và được giữ.** SDK chọn lúc build. Model là một chiều độc lập,
   product là chiều thứ ba. Giữ router native/compat. Service được rút ra theo từng lát dọc,
   ManagementServer làm trước. Code `dev` xác nhận đủ các khoảng hở v2 đã nêu (§2).
3. **Baseline của PH0 là HEAD `dev`, không phải 0066.** Các bản 0067, 0069, 0073 và 0077 sửa
   những lỗi mà board 0065 đã gặp hoặc sẽ gặp ngay:
   - reply của script lệch một request;
   - treo sau phiên đầu;
   - `dmcmd` treo khi output lớn hơn 64 KB;
   - ACS làm crash agent;
   - IP của CPE không được lấy từ netlink.

   Test board nào chạy từ `main` cũng sẽ lặp lại các triệu chứng cũ.
4. **Phát hiện mới chặn PH0: K1.** Khi ACS ghi `ManagementServer.URL`, `PeriodicInformInterval`
   hoặc 8 leaf khác, giá trị bị **đảo về giá trị cũ ở cuối phiên**. Lỗi đã tái hiện trên host
   (`tests/host/run.sh msrv` → FAIL). K1 cùng gốc với khoảng hở STUN (K2) mà v2 đã nêu. Module
   ManagementServer dùng chung ghi vào `cwmp`/`cwmp_stun`, còn config of record của sản phẩm là
   `easycwmp`/`stun`. Một module backend ManagementServer cho MTK sửa được cả hai.
5. **Cổng kiểm tĩnh trong repo trước đây không kiểm gì.** Đường dẫn của các script trỏ về
   workspace cũ: `check-c-sanity` báo "0 file kiểm, 0 vấn đề", còn `verify-dm-paths` thấy cây C
   có 0 param. Branch này đã sửa. Kết quả chạy lại ở §4.
6. **Cách làm việc để duy trì lâu dài** (§6):
   - `main` chỉ nhận baseline đã qua cổng board;
   - `dev` là nhánh tích hợp, mọi thay đổi qua cổng static + host;
   - chỉ có một file trạng thái, và mức trạng thái không cao hơn bằng chứng thấp nhất trên HEAD.

---

## 1. Hiện trạng branch

| Branch | Nội dung | Quan hệ | Đề xuất |
|---|---|---|---|
| `main` | 0034–0066, docs, apply bundle | tổ tiên của `dev` | fast-forward lên `dev` sau PH0.1 (build SDK MTK đạt) |
| `dev` | `main` + 0067–0077 + `tests/host` + docs | nhánh tích hợp | đích của mọi PR |
| `claude/sync-main-dev-multi-sdk-*` | kế hoạch này + sửa tooling + test `msrv` | đã fast-forward vào `dev` | xóa |
| `claude/ecstatic-carson-*` | 0067–0068 | là tập con của `dev` | xóa |
| `claude/design-system-extraction-*` | token màu cho tooling | không liên quan app | để riêng, không merge vào `dev` |

Không nên giữ `main` ở 0066 làm "bản ổn định", vì `main` vẫn còn lỗi treo sau phiên đầu (0069)
và các crash do RPC (0077). `dev` trội hơn hẳn `main` về độ đúng runtime.

---

## 2. Đối chiếu v2 (`main`/0066) với `dev` (0077)

| Chủ đề | v2 nói | Thực tế trên `dev` | Hệ quả |
|---|---|---|---|
| Coverage | P1–P5 = 458/783, P6–P8 = 320 | Không đổi. Chạy lại `verify-dm-paths` phase 1–5: thiếu 0 | giữ |
| Registry | conflict được phát hiện, chưa phải lỗi chặn | `--claims`: 126 claim, 21 module, 0 cặp chồng | đưa vào cổng PR (§6.3) |
| Chọn model | engine lấy TR098 rồi SDK đổi root | đúng: `dmentry.c:113-120`, hook `dm_platform_select_root` (`sdk.h:107`) | PH1 |
| Hai nguồn sự thật về model | app và proxy BDK cùng đọc `datamodel` | đúng: app đọc một lần lúc init (`icwmp_bdk.c:90`), proxy đọc ở mỗi ctx init (`dmproxy_bdk.c:130`). Giá trị khác `tr181` âm thầm thành TR098 | PH1, kèm luật "không fallback âm thầm" |
| Build | `bin/Makefile.am` link cứng nguồn TR098 | đúng: dòng 25–27 (`managementserver.c`, `softwaremodules.c`, `icwmpcfg.c`) | PH2 |
| Router MTK | hard-code `DM_MODEL_TR098` | đúng: `dmplatform_mtk.c:104,115`. `dev` **thêm phụ thuộc**: pruned walk và Inform cache (0068, 0076) dựa trên `owns`/`covers` | khi đổi sang active model ở PH2 phải giữ hành vi của 0068/0076 (`run.sh unit`) |
| STUN | MTK dùng `cwmp_stun`/`icwmp_stund`, sản phẩm dùng `stun.@stun[0]`/`stuncd` | vẫn mở. Bổ sung: tên option cũng khác (`serveraddress`, `serverport`, `stun_enable`, `natdetect`, `udpcontnreqaddr`) và kiểu khác (`STUNServerPort` là `xsd:int` ở shell, `unsignedInt` ở C) | K2 (§3) |
| Config of record | cần bảng theo từng field | bảng mirror có trong `icwmp_mtk.c:51`, nhưng **lệch với P1** (xem K1) | K1, PH0 |
| Transaction | PARTIAL | đúng. 0075 chỉ gọi `apply_service` của script khi setter của script đã chạy | phần này thuộc compat provider |
| Notification | 0066 sửa deadlock, chưa test board | 0076 sửa thêm lỗi mất thay đổi khi giá trị đổi độ dài; host `notify` 100/100 | cổng board G3 |
| Bằng chứng board | 0065 PARTIAL_PASS, 0066 chưa test | status JSON trên `dev` lại ghi `board: NOT_RUN`, **sai** so với `analysis.md` §40. Đúng ra: gate 1 (khởi động + Inform đầu) PASS trên bundle `c776a013dcfa` (0065), nhưng xảy ra **trước** 0067/0069, nên giá trị GPV đi qua script và phiên thứ hai trở đi chưa được chứng minh chạy đúng trên board | K5, đã sửa trong JSON |
| Tầng kiểm | static → SDK build → board | `dev` thêm tầng **host** (`tests/host`): agent và lib thật, ACS test, valgrind, soak | thang bằng chứng §4 |
| Hợp đồng SDK | liệt kê các hook | `dev` thêm `dm_platform_prefetch_values/drop` (0076, `sdk.h:128`). Hai hook này chỉ phục vụ compat provider | K7, PH2 |
| BDK | prototype | 0067–0077 sửa file dùng chung mà BDK cũng build (`external.c`, `http.c`, `config.c`, `log.c`, `event.c`, `xml.c`, `dmuci.c`, `dmcommon.c`, `dm_registry.c`, `dmentry.c`) và thêm stub prefetch. **BDK chưa được build lại** | PH0.1 build cả BDK |
| Trace debug 0063 | cần quyết policy | vẫn bật cứng (`cwmp.c:49-200`): handler tín hiệu, `/tmp/icwmpd_boot.log` | K6, PH0.3 |
| Tài liệu cũ | 3 file stale | thêm: comment đầu `dmplatform_mtk.c:49` ("ManagementServer.* is served by the script") sai từ P1, và đó cũng là gốc của K1 | sửa cùng K1 |

---

## 3. Phát hiện mới (không có trong v2)

| ID | Mức | Vấn đề | Bằng chứng | Hướng xử lý | Phase |
|---|---|---|---|---|---|
| **K1** | chặn → **sửa (0078)** | ACS SPV 10 leaf ManagementServer bị đảo lại cuối phiên | host trước: `run.sh msrv` 13 chỗ lệch (`cwmp=86400 easycwmp=86400` sau khi set 3600); sau 0078: PASS | backend ManagementServer MTK ghi `easycwmp` (bảng §3.1) | board G6 |
| **K2** | chặn → **sửa (0078)** | STUN sai config/dịch vụ/tên option/kiểu | host trước: `run.sh stun` 14 chỗ lệch; sau 0078: PASS | `stun.@stun[0]` + cờ `/tmp/stunclient_reload_needed` + một `stuncd reload` cuối phiên | board G7 (xác nhận ai đọc cờ, reload có đủ không) |
| K3 | cao | Cây C có thêm 11 leaf ManagementServer mà cây sản phẩm không có (`AliasBasedAddressing`, `ConnReqAllowedJabberIDs`, `ConnReqJabberID`, `HTTPCompression`, `HTTPCompressionSupported`, `InstanceMode`, `LightweightNotificationProtocolsSupported/Used`, `SupportedConnReqMethods`, `UDPLightweightNotificationHost/Port`) | `verify-dm-paths --phase 1..5`: dôi 11 | **giữ**: đây là setting của chính icwmp, đã ghi nhận từ P1 ("dôi 19 tham số là của chính icwmp", `issue/README.md`). Nếu ACS của nhà mạng không chấp nhận leaf lạ thì ẩn bằng override MTK | xong (giữ) |
| K4 | quy trình | Script kiểm tĩnh trỏ workspace cũ, chạy trong repo không kiểm gì mà vẫn exit 0 | chạy trước khi sửa: `[lib/mtk] 0 file kiểm` | **đã sửa**: đường dẫn theo repo (`ICWMP_USERSPACE` để đổi), `check-c-sanity` exit 1 khi không có file nào | xong |
| K5 | trạng thái | JSON ghi `board: NOT_RUN` | `analysis.md` §40 | **đã sửa** trong JSON, kèm giới hạn của bằng chứng đó | xong |
| K6 | PH0 | Trace và crash handler 0063 luôn bật | `cwmp.c` | **giữ**: file `/tmp/icwmpd_boot.log` giới hạn 64 KB trên tmpfs, chỉ ghi lúc khởi động và khi crash; đó là bằng chứng duy nhất sau khi procd respawn. Muốn bỏ thì tách thành `--enable-boot-trace` | xong (giữ) |
| K7 | kiến trúc | Compat provider (~900 dòng trong `#ifdef DM_MTK_SCRIPT_COMPAT`) nằm trong `dmplatform_mtk.c`, prefetch nằm trong `sdk.h`; `tests/host/harness/harness.c` include thẳng file này | source | PH2: tách thành `sdk/mtk/compat/` với interface provider; prefetch thành năng lực của provider | PH2 |
| K8 | lifecycle | AddObject/DeleteObject của WAN connection vẫn chạy qua compat | v2 + status P4 | đóng ở PH4.4 hoặc ghi rõ là ngoại lệ | PH4 |
| **K10** | cao → **sửa (0079)** | `cwmp.acs.periodic_inform_time` là chuỗi dateTime (MTK mirror từ `easycwmp.@acs[0].periodic_time`, BDK từ MDM) nhưng `config.c` đọc bằng `atol()`: `0001-01-01T00:00:00Z` → 1 giây, `2026-01-01T00:17:00Z` → 2026 giây. Inform định kỳ căn sai mốc (board §40: phiên kế tiếp 19:00:01 +07 = epoch + 1 s) | host trước: `run.sh ptime` Inform kế tiếp hh:33:46; sau 0079: hh:17:00 | `config.c` đọc cả epoch lẫn dateTime; getter portable trả nguyên dateTime | board G6 |
| **K11** | quy trình → **sửa** | Commit docs `f410de4` sửa `README.md` (thuộc bundle) mà không cập nhật `SHA256SUMS`/`MANIFEST.json`: `sha256sum -c` FAIL, `apply` có thể từ chối | `sha256sum -c` báo `README.md: FAILED` | `docs/issue/update-sums.py` (`--check` là cổng PR, `--staged` dùng trước commit) | xong |
| K12 | thấp | Thread Download/Upload chờ `mutex_session_send`; khi các phiên nối tiếp nhau (ACS gửi CR liên tục) transfer chỉ chạy khi có khoảng trống giữa hai phiên | host valgrind: 4 Download chạy cùng lúc sau phiên cuối, giống nhau trên build trước 0078 và sau 0079 | hành vi upstream; xem lại khi làm regression RPC ở PH5 | PH5 |
| K9 | đa SDK | `check-c-sanity` cho lib BDK/UCI có nhiễu (thiếu header vendor, danh sách libc chưa đủ); BDK chưa build với 0067–0077 | chạy 2026-10-04: lib/bdk 4, lib/uci 3 "vấn đề" đều là cảnh báo giả | PH0.1 build BDK thật; cổng tĩnh chỉ tính lib/mtk + app×3 | PH0.1 |
| **K13** | cao | 0078 bỏ một phần validation/side effect của shell sản phẩm: URL không kiểm scheme và không ghi `/usr/share/easycwmp/defaults` (shell `management_server:69,75`), STUNServerAddress không qua `is_valid_domain`/`is_valid_ip` (`:114`) | đọc shell SDK so với `managementserver_mtk.c`; chưa chạy board | test SPV URL không scheme, STUN address sai, GPV sau phiên/reboot; quyết định sản phẩm giữ hay bỏ từng side effect | PH0 |
| **K14** | trung bình | PeriodicInformTime chỉ kiểm hình dạng: `ms_valid_datetime` không kiểm lịch/timezone, `config.c` `periodic_time_value` không kiểm ngày của tháng, giờ, suffix | source | thống nhất validation với parser; test leap day, ngày không tồn tại, giờ ≥24, timezone sai; fault 9007 không đổi mốc Inform | PH0 |
| **K15** | trung bình | Agent/AP sau controller: `icwmpd.init:125` không chạy khi `opermode=auto`, `:139-140` network mặc định `if0`, `:167` không có IP thì chỉ chờ. `stuncd.init:26` (vendor) có cổng opmode riêng, biến không nháy: opmode rỗng → busybox `[ != auto ]` exit 2 → icwmpd chạy còn stuncd không | source icwmpd.init + stuncd.init + busybox 1.33.1 `test.c` | đổi cổng ở cả hai script cùng lúc, bọc nháy; chọn network theo opmode hay cấu hình; CR qua STUN (G7) hoặc port-forward + `cr_host` | PH0 quyết định sản phẩm, board |
| **K16** | trung bình | `icwmp_stund` đọc `cwmp.cpe.password` (`stun/stun.c:439`) nhưng icwmp lưu `cwmp.cpe.passwd` (`inc/cwmp.h:69`): mật khẩu rỗng → bỏ qua kiểm chữ ký, nhận mọi UDP Connection Request. Không build hiện tại (MTK dùng `stunclient` vendor) | source | sửa tên option trước khi bật `--enable-icwmp_stun` cho SDK nào | PH3+ |

### 3.1 K1: cơ chế và bảng config of record

Lỗi xảy ra theo các bước sau:

1. Từ P1, `ManagementServer.` do C trả lời, qua `tr098/managementserver.c` (module dùng chung).
   Setter ghi vào `cwmp.acs.*` và `cwmp.cpe.*`.
2. App MTK vẫn giữ giả định cũ là script phục vụ ManagementServer và ghi vào `easycwmp`
   (comment ở `dmplatform_mtk.c:49`). Trong bảng mirror (`icwmp_mtk.c:51`), mọi field trừ
   `parameter_key` đều có `to_easy = 0`, tức là đi một chiều easycwmp → cwmp.
3. Cuối phiên, `END_SESSION_RELOAD` gọi `icwmp_platform_config_reload()`, hàm này chạy
   `sync_easycwmp_to_cwmp()` và chép giá trị cũ của `easycwmp` đè lên `cwmp`. Lần boot sau
   cũng làm y như vậy. WebUI cũng không bao giờ thấy giá trị ACS đã ghi.

Đích của bản sửa: sản phẩm ghi vào đâu thì C ghi vào đó. Theo
[ma trận coverage](../issue/tr098_coverage_matrix.tsv), getter và setter của shell là:

| Leaf | Config of record (MTK) | C hiện ghi | Ghi chú |
|---|---|---|---|
| URL | `easycwmp.@acs[0].url` | `cwmp.acs.url` (+ `dhcp_discovery=disable`) | setter shell `management_server_set_url` có thể có side effect, cần đối chiếu thư viện hàm |
| Username / Password | `easycwmp.@acs[0].username` / `.password` | `cwmp.acs.userid` / `.passwd` | |
| PeriodicInformEnable / Interval / Time | `easycwmp.@acs[0].periodic_enable` / `periodic_interval` / `periodic_time` | `cwmp.acs.periodic_inform_*` | |
| ConnectionRequestUsername / Password | `easycwmp.@local[0].username` / `.password` | `cwmp.cpe.userid` / `.passwd` | |
| CWMPRetryMinimumWaitInterval / IntervalMultiplier | `easycwmp.@acs[0].cwmpretryinterval` / `cwmpretryintervalmultiplier` | `cwmp.acs.retry_*` | |
| STUNEnable, ServerAddress, ServerPort, Username, Password, Min/MaxKeepAlivePeriod | `stun.@stun[0].stun_enable`, `serveraddress`, `serverport`, `username`, `password`, `min_keepalive`, `max_keepalive` + `/tmp/stunclient_reload_needed` | `cwmp_stun.stun.*` + `icwmp_stund` | K2 |
| NATDetected, UDPConnectionRequestAddress (RO) | `stun.@stun[0].natdetect`, `udpcontnreqaddr` | varstate `cwmp_stun` | K2 |
| ParameterKey | `cwmp.acs.ParameterKey` → đẩy về `easycwmp` (`to_easy = 1`) | đúng | giữ |
| EnableCWMP, UpgradesManaged | `easycwmp.@acs[0]` | đúng (`managementserver_mtk.c`) | giữ |

Cách làm, đúng theo hướng của v2:

- Module MTK override các leaf trên. Quy tắc merge của registry là module sau ghi đè leaf của
  module trước, nên không phải sửa `tr098/managementserver.c`.
- Validation giữ theo input contract 0061.
- Ghi vào `easycwmp`/`stun` rồi để reload mirror sang `cwmp`. Bảng mirror không phải đổi.
- PH3 sau này nâng module này thành backend MTK của management service, không viết lại.
- Cổng của bản sửa: `run.sh msrv` PASS và được đưa vào `all`; thêm test host cho STUN; trên board
  chạy G6 và G7 (§5).
- **Đã làm (0078):** `sdk/mtk/dm098/managementserver_mtk.c` theo đúng bảng trên. Các leaf easycwmp đặt
  `END_SESSION_RELOAD`; các leaf STUN tạo cờ và xếp hàng đúng một lệnh
  `[ -x /etc/init.d/stuncd ] && /etc/init.d/stuncd reload` (`mtk_apply_service_once()`). Khoảng giá trị
  theo TR-098, lỗi trả 9007. Kiểu của STUN theo TR-098 (`STUNServerPort` là `unsignedInt`), không theo
  `xsd:int` của shell.

Side effect chính xác của các setter shell (`management_server_set_url`,
`management_server_set_stun_enable`, ...) cần đối chiếu với thư viện hàm easycwmp của sản phẩm.
Repo này không chứa thư viện đó.

---

## 4. Thang bằng chứng

Mức trạng thái của một hạng mục **không được cao hơn bằng chứng thấp nhất trên commit HEAD**, và
mỗi mức phải ghi rõ commit hoặc bundle tương ứng.

| Mức | Nghĩa | Công cụ | Kết quả gần nhất |
|---|---|---|---|
| `STATIC_VERIFIED` | nguồn, path và claim đúng; không có compiler của SDK | `check-c-sanity.py --tree lib/app --sdk ...`, `verify-dm-paths.py --phase 1..5` và `--claims`, `check-automake-conds.py`, `update-sums.py --check` | 06/10 tại 0087: lib/mtk 41 file/0, app mtk/bdk/uci 17/0; phase 1–5 thiếu 0, dôi 11 (K3); claim 126/0 chồng; automake 0; sums OK |
| `HOST_VERIFIED` | agent và lib thật trên Linux host, ACS test | `tests/host/run.sh all` (+ `soak`) | 06/10 tại 0086, container `ubuntu:24.04`: `all` PASS: unit 3/3, smoke 5 phiên, notify 100/100, rpc 5/5, msrv, stun, ptime 2/2, valgrind 0 lost 0 error; cũng PASS từ bundle MTK `e273359` (analysis §55) |
| `SDK_BUILD_PASS` | build bằng SDK thật (MTK, BDK) | `apply` + build `libtr098`/`icwmp_tr098` | MTK đạt tới 0086 (06/10, gói + image, cả từ bundle export); BDK chưa build với 0067+ (K9) |
| `BOARD_GATE_n` | gate board theo §5 PH0.4 | board HP2236B | Image 0083 (06/10): G1–G7 phía router PASS, G4/G6 qua GenieACS NBI; G8 bỏ; G9 24 h tới 07/10 08:15 (analysis §48–§54) |
| `SOAK` | 24 h, RSS/fd/thread phẳng | `run.sh soak` trên board/host | host: `dev` báo 300 phiên RSS phẳng; board chưa |
| `RELEASE` | baseline đóng băng, có tag | §6.1 | baseline chưa; tag bản giao `release/mtk-20261006` (`8dea75b`) |

`check-cc-syntax.py` (cross-gcc của SDK) và `check-pkg-deps.py` cần cây SDK. Đặt đường dẫn bằng
`--sdk-root` hoặc `ICWMP_SDK_SRC`; nếu không có thì script chỉ in phần làm được.

---

## 5. Lộ trình hợp nhất

Dùng ID phase của v2 (PH0–PH8). ID cũ (A0–A6, P1–P8, R8, R9) được giữ trong JSON để truy vết,
và mỗi PH ghi rõ nó thay thế ID nào.

| Phase | Mục tiêu | Thay cho | Trạng thái 2026-10-04 |
|---|---|---|---|
| PH0 | Đóng băng baseline MTK TR-098 từ HEAD `dev` | R8, R9, phần board của A0 | đang làm: host PASS gồm K1/K2/K10 đã sửa; còn SDK build và board |
| PH1 | Profile + model capability + resolver trung tâm | A2a | chưa bắt đầu |
| PH2 | Routing và build ownership thống nhất, tách compat provider | phần còn lại của A3 (registry) | có nền: registry, claim, router |
| PH3 | Lát dọc service: ManagementServer, rồi DeviceInfo/Time | A2b, A3 (service), A4 | chưa; module MTK làm ở PH0.2 là điểm xuất phát |
| PH4 | P6 → P7 (lớp product) → P8 (phân loại trước) + lifecycle | P6, P7, P8 | chưa |
| PH5 | Full-C TR-098, compat off | một phần A6 | chưa |
| PH6 | TR-181 trên MTK theo mapping manifest | A2c | chưa |
| PH7 | Đưa BDK lên cùng contract | — | prototype |
| PH8 | Ma trận build, CI, release, đổi tên ABI nếu đáng | một phần A6 | mới có tooling |

### PH0: các bước theo thứ tự

1. **PH0.1 Build SDK tại HEAD `dev`** (sau khi merge branch này). Build MTK: `libtr098` và
   `icwmp_tr098` sạch, ghi lại bundle hash và image. Build BDK ít nhất phải compile và link được,
   vì file dùng chung đã đổi (K9). Xong bước này thì fast-forward `main` lên `dev`.
2. **PH0.2 Sửa K1 + K2** bằng module ManagementServer MTK (§3.1). Cổng: `run.sh msrv` PASS rồi
   đưa vào `all`, test host STUN PASS, `verify-dm-paths` không đổi số thiếu. **Xong trên host (0078),
   kèm K10 (0079).**
3. **PH0.3 Quyết K3 và K6**, ghi quyết định vào JSON. **Xong: giữ cả hai** (§3); đổi lại được.
4. **PH0.4 Gate board** (HP2236B, build từ PH0.1 + PH0.2):
   - G1 boot, `tr069` lên, Inform đầu;
   - G2 ít nhất 3 phiên liên tiếp (định kỳ + Connection Request) (0069);
   - G3 notify qua nhiều chu kỳ 30 s, `ubus call tr069 dm get` vẫn trả lời (0066, 0076);
   - G4 GPV/GPN/SPV theo DeviceInfo, ManagementServer, LAN, Wi-Fi, WAN, Diagnostics; SPV nhiều
     param có một param lỗi phải rollback hết;
   - G5 input contract (bộ case §0.5 trong [plan v2](v2/icwmp_next_phases_plan_v2.md));
   - G6 ACS ghi URL/PeriodicInformInterval/CR credential: còn nguyên sau phiên, WebUI thấy
     đúng, còn nguyên sau reboot (K1);
   - G7 STUN bật/tắt, server/port, `UDPConnectionRequestAddress`, UDP CR nếu mạng cho phép (K2);
   - G8 Download/ScheduleDownload thiếu FileType hoặc 1 window: fault đúng, agent sống (0077);
   - G9 soak 24 h: VmRSS, fd, thread phẳng.
5. **PH0.5 Đóng băng.**
   - Tag `baseline/ph0-mtk-tr098-<ngày>` trên `dev`, rồi fast-forward `main`.
   - Viết báo cáo board vào `analysis.md`.
   - Cập nhật JSON: PH0 = DONE, kèm commit và bundle.
   - Từ đó baseline này là chuẩn so sánh cho mọi thay đổi kiến trúc.

### PH1–PH8: điều chỉnh so với v2

- **PH1.**
  - Resolver phải thay cả hai chỗ đang đọc `datamodel`: `icwmp_bdk_tr181_mode()` và
    `bdk_proxy_load_mode()`.
  - Bỏ hook `dm_platform_select_root`.
  - Giá trị model sai là lỗi khi khởi động, không âm thầm thành TR098.
  - Cổng: test host thêm ca profile hợp lệ và không hợp lệ.
- **PH2.**
  - Tách compat provider khỏi `dmplatform_mtk.c` vào `sdk/mtk/compat/` sau một interface provider
    (owns/covers, get/set/add/del, inform, prefetch).
  - Chuyển prefetch từ `sdk.h` sang interface đó.
  - Sửa `harness.c` theo.
  - Router dùng active model nhưng phải giữ các con số của `run.sh unit`
    (GPV root 417 getter / 9 request).
  - `verify-dm-paths --claims` trả 0 cặp chồng là cổng PR.
- **PH3.** ManagementServer: tổng quát hóa module MTK của PH0.2 thành
  `management service` + backend MTK + adapter BDK. Sau đó làm DeviceInfo/Time.
- **PH4.**
  - P6–P8 cần **thư viện hàm easycwmp của sản phẩm** làm tham chiếu (giá trị, side effect).
  - `fake_dm.py` chỉ phục vụ được tên và kiểu theo ma trận.
  - Đưa thư viện vào một vị trí tham chiếu (không ship) trước khi port.
  - P7 (`X_AIS_*`) vào lớp product ngay từ đầu.
- **PH5–PH8** giữ như v2. Từ PH5, `run.sh` cần thêm biến thể `--disable-dm-script-compat`.

---

## 6. Quy ước phát triển và bảo trì

### 6.1 Branch và release

- `dev`: nhánh tích hợp. Thay đổi đi qua PR từ `fix/<id>-<chủ đề>` hoặc
  `feat/<phase>-<chủ đề>`. Không commit thẳng vào `main`.
- `main`: chỉ fast-forward từ `dev` tại một mốc đã qua cổng (PH0.1, sau đó mỗi gate board).
  Mỗi mốc có tag `baseline/...` hoặc `release/...`.
- `release/<sdk>-<YYYYMMDD>` (annotated): commit đã xuất một bản giao. `./export.py --sdk <sdk>` chạy tại tag
  cho lại đúng sha256 ghi trong message của tag. Tag này không có nghĩa đã qua mọi gate; mốc đóng băng dùng
  `baseline/...`. Đã có: `release/mtk-20261006` → `8dea75b`.
- Branch đã merge thì xóa. Branch tạm của công cụ AI cũng không giữ lâu.

### 6.2 Commit

- Thay đổi code giữ tiêu đề `[icwmp NNNN] <phạm vi>: <việc>`, đánh số tiếp từ 0078.
  Docs/tests không đánh số.
- Thân commit ghi ba điều: lỗi hoặc yêu cầu là gì, vì sao sửa như vậy, bằng chứng (lệnh test
  host, kết quả trước/sau, hoặc log board).
- Khi một file của bundle đổi (`userspace/`, `feeds/`, `README.md`, `apply`, `apply.py`,
  `bdk-integration.json`) thì cập nhật `SHA256SUMS` và `MANIFEST.json` trong cùng commit:
  `git add <file>`, `python3 docs/issue/update-sums.py --staged`, `git add MANIFEST.json SHA256SUMS`.

### 6.3 Cổng của mỗi PR vào `dev`

```sh
python3 docs/issue/check-c-sanity.py --tree lib --sdk mtk          # 0 vấn đề
for s in mtk bdk uci; do python3 docs/issue/check-c-sanity.py --tree app --sdk $s; done
python3 docs/issue/verify-dm-paths.py --phase 1 --phase 2 --phase 3 --phase 4 --phase 5   # thiếu 0
python3 docs/issue/verify-dm-paths.py --claims                      # 0 cặp chồng
python3 docs/issue/check-automake-conds.py                          # 0 vấn đề
python3 docs/issue/update-sums.py --check                            # bundle sums khớp
tests/host/run.sh all                                               # container dùng một lần
```

Ngoài ra, PR phải cập nhật `implementation-status.json` (mức bằng chứng thật, commit) và tài liệu
nào có hành vi bị đổi. Một lỗi đã sửa thì phải có test host bắt được nó.

### 6.4 Một nguồn sự thật cho mỗi loại thông tin

| Thông tin | File | Luật |
|---|---|---|
| Trạng thái, phase, known issue | `docs/issue/implementation-status.json` | duy nhất; đọc bằng `progress.py` |
| Kế hoạch, quyết định | `docs/plan/sync-main-dev.md` (file này) | sửa khi kế hoạch đổi |
| Kiến trúc đích | `docs/plan/v2/*` + §2 của file này | v2 giữ nguyên làm nguồn gốc |
| Nhật ký bằng chứng | `docs/issue/analysis.md` | chỉ thêm, không sửa lịch sử |
| Inventory TR-098 | `docs/issue/tr098_coverage_matrix.tsv` | sinh lại bằng `gen-coverage-matrix.py` |
| Thiết kế cũ (`docs/mtk/*`, `issue/tr098_c_port_phases.md`) | lịch sử | không cập nhật trạng thái ở đây nữa |

### 6.5 Quy tắc code rút ra từ 0062–0077

- Tiến trình nền mà init script sinh ra phải đóng fd 1000 (0062). Tiến trình sống lâu thì giao
  cho procd quản.
- Không dùng `wait()` cho mọi con. Chờ đúng pid, xử lý `EINTR`/`ECHILD`, vì thread uloop có thể
  đã reap con đó (0069).
- Lệnh ngoài chạy qua `dmcmd()` (`posix_spawn`, đọc output trong lúc lệnh chạy). Không tự
  `fork()` + `waitpid()` rồi mới đọc pipe (0072, 0073).
- Mọi đường lỗi sau `pthread_mutex_lock` phải unlock (0066). Thư mục trạng thái do platform
  init tạo. Không `rename()` qua filesystem khác.
- Với JSON từ ubus, kiểm kiểu trước khi `foreach`; mảng thì duyệt theo chỉ số (0065,
  `check_json_foreach`).
- Không `vsprintf`/`strcpy` vào buffer cố định. Luôn kiểm `fopen` (0071).
- Mọi field của RPC từ ACS có thể vắng hoặc rỗng. Kiểm trước khi dùng, và giới hạn số phần tử
  theo mảng (0077).
- Module C mới phải ghi đúng config of record của sản phẩm (§3.1), không ghi vào config riêng
  của icwmp trừ khi field đó do icwmp sở hữu.
- Code compat chỉ nằm trong `DM_MTK_SCRIPT_COMPAT`. Tên SDK không xuất hiện ngoài `sdk/<tên>/`.
  `X_AIS_*` thuộc lớp product.

---

## 7. Việc tiếp theo

1. ~~Merge kế hoạch vào `dev`~~ (xong). Xóa `claude/ecstatic-carson-*` và `claude/sync-main-dev-*`.
2. **PH0.1:** build SDK MTK và BDK tại HEAD `dev` (0034–0079). Đạt thì fast-forward `main`.
3. ~~PH0.2: K1 + K2~~ (0078), ~~K10~~ (0079), ~~PH0.3: K3, K6~~: xong trên host.
4. **PH0.4–0.5:** chốt K13/K14 (có test host) và quyết định sản phẩm cho K15, rồi gate board G1–G9, tag baseline, cập nhật JSON. G6 kiểm thêm PeriodicInformTime
   (đặt một mốc, xem giờ phiên định kỳ); G7 xác nhận ai đọc `/tmp/stunclient_reload_needed`.
   **2026-10-06:** K13/K14 sửa ở 0082/0081 (MTK SDK build đạt, board chưa); K10, K17, G1–G3, G5 đạt trên
   board. Danh sách còn lại để đóng PH0 ở
   [handover/icwmp_progress_matrix.md §3.1](../handover/icwmp_progress_matrix.md#31-để-đóng-ph0).
5. Sau đó mới bắt đầu PH1 (resolver), PH2 (provider/routing), PH3 (service ManagementServer).
   PH2 thêm việc: tách `dmcommon.c` thành helper trung lập và helper schema OpenWrt, xoá hàm không ai gọi;
   **giữ tên `dmuci_*`** (lý do ở
   [handover/icwmp_architecture_guide.md §7](../handover/icwmp_architecture_guide.md#7-quy-ước-tên-dmuci_-uci_foreach_element-có-nên-đổi-không)).
