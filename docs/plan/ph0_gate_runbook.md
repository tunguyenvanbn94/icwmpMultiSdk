# PH0 — runbook kiểm build và board cho HEAD `dev`

> Chuyển từ issue workspace vào repo ngày 2026-10-06. Các tham chiếu `analysis §NN` trỏ tới
> [../issue/analysis.md](../issue/analysis.md), cùng số mục. Kết quả gate cập nhật ở
> [../handover/icwmp_progress_matrix.md](../handover/icwmp_progress_matrix.md) và `implementation-status.json`.

Gate lấy từ [sync-main-dev.md §5 PH0](sync-main-dev.md) và
[plan v2 §0.5](v2/icwmp_next_phases_plan_v2.md). Source: `icwmpMultiSdk/dev`,
code = `6352623` (các commit sau đó chỉ sửa docs). Mỗi gate ghi **PASS/FAIL/NOT RUN** kèm output vào
`analysis.md`; không nâng mức bằng chứng cao hơn gate thấp nhất (§4 của plan).

## 0. PH0.1 — build

### 0.1 MTK (`1_src/2025q3`) — Claude Code đã kiểm 2026-10-05

| Kiểm | Kết quả |
|---|---|
| Apply | `.icwmp-backups/20261005-194746-*`, journal `complete`, thay `libicwmp_dm` + `icwmp_tr098` |
| Source đã cài = repo `dev` | `diff -rq` 0 file ở cả `libicwmp_dm` và `icwmp_tr098`; 2 feed Makefile cùng md5 ở repo, `airoha_feeds`, `feeds/airoha` |
| Gói | `libtr098_3` 19:49:19, `icwmp_tr098_3-2` 19:49:42 trong `bin/packages/aarch64_cortex-a53/airoha/` |
| Có code 0063–0079 | symbol `icwmp_boot_trace`, `periodic_time_value` (0079), `dm_*_prefetch_*` (0076), `mtk_apply_service_once`/`ms_valid_datetime`/`get_udp_cr_addr` (0078) |
| Image | `tclinux.bin` 19:54; `root-airoha` `libtr098.so.3.0.0` và `icwmp_tr098d` cùng md5 với gói; `/etc/init.d/icwmpd` có `1000>&-` |

Kết luận: **SDK_BUILD_PASS (MTK)** cho source `6352623`.

**05/10 22:02 — `0080`** (repo `dev` `0ff05dc`, analysis §50): Claude Code apply + build qua docker. Chỉ `libicwmp_dm` đổi;
`libtr098.so` md5 `3f6265bf…`, `icwmp_tr098d` giữ `0a7065f9…`. Image để nạp: `tclinux.bin` 22:02:37 md5 `13e99856…`,
có thêm init dev-access (workspace `projects/mtk_openwrt_wifi7/patches/20261005_board_dev_access_feature/`) cho lab. Không còn log build để quét cảnh báo;
các lớp cảnh báo nguy hiểm đã là lỗi trong `check-cc-syntax.py` (0 lỗi, analysis §44).

### 0.2 BDK — chạy trên máy build BDK (cây này không có trên máy workspace)

Máy build đã dùng 06/10: `192.168.100.38`, cây `/home/vtanh/workspaceBRCM/tunv/2_src/bcm963xx`, build trong
container `vtanh-brcm` (tmux `bdk1`, user `vtanh`; user SSH không có quyền ghi cây). Apply, rồi chạy các lệnh
build mà apply in ra.

Lần apply đầu tiên thêm `BUILD_ICWMP=y` vào profile. Lần `make PROFILE=MO77300EB` kế tiếp sẽ dừng ở
`profile_saved_check` ("profile … has been modified since the last build"), vì profile mới hơn `.last_profile`.
Thay đổi đó chỉ thêm `-DSUPPORT_ICWMP`, và chỉ `comp_tr69_md.c` dùng define này, nên không cần `make clean`:

1. Chạy `make PROFILE=MO77300EB FORCE=1` một lần. Rule của vendor touch `.last_profile` nhưng vẫn
   `exit 1`, nên lần này vẫn báo lỗi.
2. Chạy lại `make PROFILE=MO77300EB`.

Kiểm sau build:

```sh
cd <bcm963xx>
ls -lt .icwmp-backups | head -2                      # lần apply mới nhất
diff -rq <icwmpMultiSdk>/userspace/public/libs/libicwmp_dm userspace/public/libs/libicwmp_dm | head
diff -rq <icwmpMultiSdk>/userspace/public/apps/icwmp userspace/public/apps/icwmp | head
grep -n 'BUILD_ICWMP' targets/MO77300EB/MO77300EB
find targets/MO77300EB/fs.install -name 'libtr098.so*' -o -name icwmpd | xargs ls -l
NM=$(ls /opt/toolchains/*/usr/bin/*-nm | head -1)    # nm của toolchain BDK
$NM -D $(find targets/MO77300EB/fs.install -name 'libtr098.so*' | head -1) | grep -E 'dm_entry_prefetch_values|dm_platform_prefetch_values'
$NM -D $(find targets/MO77300EB/fs.install -name 'libtr098.so*' | head -1) | grep -cE 'posix_spawnp|waitpid'   # dmcmd của lib; icwmpd BDK dùng external_bdk.c, không có hai symbol này
```

Đạt khi: `diff` chỉ còn file sinh ra lúc build (`Only in` phía cây), `BUILD_ICWMP=y`, có `libtr098.so` +
`icwmpd`, lib có symbol prefetch (0076 thêm stub cho BDK) và `posix_spawnp`/`waitpid` (≥ 2). So `diff` với
bundle đã apply, không so với repo đủ SDK nếu apply bằng bundle một SDK. Image nằm ở
`targets/MO77300EB/*.pkgtb`, không nằm ở `images/`. Kết quả 06/10: analysis §56.

## 1. Chuẩn bị board MTK (HP2236B, mode router)

```sh
uci set easycwmp.@local[0].logging_level='4'; uci commit easycwmp
/etc/init.d/icwmpd restart; sleep 30
```

## 2. Gate board

Mỗi khối dưới đây **chỉ chứa lệnh**, copy cả khối dán vào shell board. Việc làm trên ACS ghi riêng
ở dòng "Trên ACS". Tiêu chí đạt ở bảng cuối mục.

**Trước mọi gate** — bằng chứng khởi động lại (K17, sửa ở 0080):
```sh
grep -c CRASH /tmp/icwmpd_boot.log; grep '==== start' /tmp/icwmpd_boot.log | tail -5
```

**G1**
```sh
tail -15 /tmp/icwmpd_boot.log; ubus call tr069 status; ls -l /etc/tr098/.dm_enabled_notify
```

**G2** — đặt chu kỳ 120 s, chờ 7 phút:
```sh
ubus call tr069 dm '{"cmd":"set","path":"InternetGatewayDevice.ManagementServer.PeriodicInformInterval","value":"120","key":"g2"}'
```
Trên ACS: gửi 1 Connection Request. Sau đó:
```sh
ubus call tr069 status; pgrep -f icwmp_dm.sh | wc -l; ps | awk '$4 ~ /^Z/' | wc -l
```

**G3** — notify, `dm get` toàn cây, value change từ ngoài ACS:
```sh
grep -c 'triggered ubus notification' /var/log/icwmpd.log
time ubus -t 300 call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.","file":"/tmp/g3.txt"}'; wc -l /tmp/g3.txt
uci set easycwmp.@local[0].provisioning_code=g3test; uci commit easycwmp; sleep 70
grep -i 'value change' /var/log/icwmpd.log | tail -3
```

**G4** — GPV từng nhánh:
```sh
for b in DeviceInfo ManagementServer LANDevice WANDevice Layer3Forwarding IPPingDiagnostics; do s=$(date +%s); ubus -t 120 call tr069 dm "{\"cmd\":\"get\",\"path\":\"InternetGatewayDevice.$b.\",\"file\":\"/tmp/g4_$b.txt\"}" >/dev/null; echo "$b rc=$? $(( $(date +%s)-s ))s $(wc -l < /tmp/g4_$b.txt)"; done
```
Trên ACS: GPN `InternetGatewayDevice.` (next level); SPV 1 tham số mỗi nhánh; **SPV 2 tham số trong
một lệnh**: `DeviceInfo.ProvisioningCode=g4ok` và `ManagementServer.PeriodicInformInterval=abc`. Sau đó:
```sh
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo.ProvisioningCode"}'
```

**G5** — mục 3.

**G6** — Trên ACS: SPV `ManagementServer.PeriodicInformInterval=3600` và
`ManagementServer.ConnectionRequestUsername=<u>`. Sau phiên:
```sh
uci get easycwmp.@acs[0].periodic_interval; uci get cwmp.acs.periodic_inform_interval; uci get easycwmp.@local[0].username
grep '==== start' /tmp/icwmpd_boot.log | tail -2
```
Xem WebUI, rồi `reboot` và chạy lại khối trên. K10 — Trên ACS: SPV
`ManagementServer.PeriodicInformTime=<hôm nay, giờ hiện tại +5 phút>Z` và
`ManagementServer.PeriodicInformInterval=300`, rồi `ubus call tr069 status`.

**G7** — tạm hoãn, làm trên agent (analysis §47).

**G8** — Trên ACS: Download thiếu `FileType`, rồi ScheduleDownload 1 TimeWindow. Sau mỗi lệnh:
```sh
pidof icwmp_tr098d; grep -c CRASH /tmp/icwmpd_boot.log
```

**G9** — mục 4.

| Gate | Đạt khi |
|---|---|
| Trước mọi gate | `CRASH` = 0; dòng `==== start` chỉ tăng khi bạn chủ động restart/reboot |
| G1 | boot log tới `ubus object tr069 registered`; `status=up`, `last_session=success`; file notify có |
| G2 | `success_sessions` tăng ≥3 (có 1 phiên do Connection Request), `failure_sessions` 0; đúng 1 `icwmp_dm.sh`; zombie 0 |
| G3 | `get` toàn cây xong (ghi `real` và số dòng); có `4 VALUE CHANGE` |
| G4 | mọi nhánh `rc=0`; SPV 2 tham số trả fault 9003 có 9007 ở Interval, `ProvisioningCode` **không** thành `g4ok` |
| G5 | đúng bảng mục 3; **sau 0080**: không có `Command failed: Not found` sau lệnh set ManagementServer |
| G6 | 3 giá trị đúng sau phiên, không có `==== start` mới, WebUI đúng, còn nguyên sau reboot; `next_session` đúng mốc K10 |
| G8 | fault đúng, pid không đổi, `CRASH` không tăng |
| G9 | VmRSS tăng < 5 %, fd/thread không đổi, `failure_sessions` ≈ 0 |

## 3. G5 — input contract

```sh
t() { ubus call tr069 dm "{\"cmd\":\"set\",\"path\":\"InternetGatewayDevice.$1\",\"value\":\"$2\",\"key\":\"g5\"}" | tr -d '\n\t'; echo "  <= $1='$2'"; }
P=DeviceInfo.ProvisioningCode; B=ManagementServer.PeriodicInformEnable; U=ManagementServer.PeriodicInformInterval
t $P ''; t $P '   '; t $P 'a;b'; t $P 'a&b'; t $P 'a|b'; t $P 'a`b'; t $P 'a$b'; t $P 'g5ok'
t $B yes; t $B on; t $B true; t $B 0; t $B 1
t $U abc; t $U -1; t $U 4294967296; t $U 43200
```

| Case | Mong đợi |
|---|---|
| `''`, `'   '`, `a;b`, `a&b`, `a\|b`, `` a`b ``, `a$b` | fault 9007 |
| `g5ok` | OK |
| boolean `yes`, `on` | fault 9007 |
| boolean `true`, `0`, `1` | OK |
| unsignedInt `abc`, `-1`, `4294967296` | fault 9007 |
| `43200` | OK (trả lại giá trị gốc) |

IPv4 (cần một route tạm). **HP2236B 05/10:** `add` trả 9002 vì board không có
`network.routev4Common.max_rules` (shell cũ cũng vậy, analysis §50.2). Khi đó chỉ thử giá trị sai trên
một tham số IPv4 có sẵn. Lệnh này bị từ chối trước khi ghi nên không có tác dụng phụ:
```sh
t LANDevice.1.LANHostConfigManagement.MinAddress 1.2.3   # 9007
t LANDevice.1.LANHostConfigManagement.MinAddress abc     # 9007
```
Trên board có `max_rules`:
```sh
ubus call tr069 dm '{"cmd":"add","path":"InternetGatewayDevice.Layer3Forwarding.Forwarding."}'   # ghi lại instance N
t Layer3Forwarding.Forwarding.N.DestIPAddress 1.2.3     # 9007
t Layer3Forwarding.Forwarding.N.DestIPAddress abc       # 9007
t Layer3Forwarding.Forwarding.N.DestIPAddress 10.9.9.0  # OK
ubus call tr069 dm '{"cmd":"del","path":"InternetGatewayDevice.Layer3Forwarding.Forwarding.N."}'
```
Shell cũ kiểm IPv4 bằng `grep -o` không neo, nên `256.1.1.1` **được nhận** (khớp `56.1.1.1`) — đó là
hành vi giữ nguyên, không tính là lỗi.

Trả lại giá trị gốc sau G5: `t $P <giá trị cũ>`, `t $B 1`, `t $U <giá trị cũ>`.

## 4. G9 — sampler 24 h

```sh
( while :; do P=$(pidof icwmp_tr098d); D=$(pgrep -f icwmp_dm.sh | head -1)
  echo "$(date +%s),$P,$(awk '/VmRSS/{print $2}' /proc/$P/status),$(ls /proc/$P/fd | wc -l),$(ls /proc/$P/task | wc -l),$(awk '/VmRSS/{print $2}' /proc/$D/status 2>/dev/null),$(ubus call tr069 status | grep -A2 statistics | tr -d ' \n\t')" >> /tmp/g9.csv
  sleep 300; done ) </dev/null >/dev/null 2>&1 &
```
288 dòng/ngày (~40 KB trên tmpfs). Gửi `head -2 /tmp/g9.csv; tail -2 /tmp/g9.csv` sau 24 h.

## 5. BDK — smoke sau khi flash (PH0 chỉ yêu cầu build; board BDK thuộc PH7)

```sh
pidof icwmpd; tail -15 /tmp/icwmpd_boot.log; ls -l /data/icwmp/crash.log 2>/dev/null
ubus call tr069 status
ubus -t 300 call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.DeviceInfo."}' | head -20
```
Đạt khi: không crash, `status` up, có Inform thành công, GPV `DeviceInfo.` trả giá trị.
