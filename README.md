# icwmp multi-SDK source delivery

Source: A1 model-neutral layout `libicwmp_dm/src`, ABI/package vẫn `libtr098`.
TR-098: 783/783 param bằng C (P1–P8, toàn cây); compat shell không còn tham số nào. A2, A4, A6
còn trong kế hoạch; A3 mới làm một phần. Xem [trạng thái và kế hoạch](docs/README.md).
**MTK:** các bản sửa tới 0083 đã build bằng SDK và chạy trên board HP2236B (gate PH0 G1–G7 phía router,
image 0083). 0084–0086 (ghi chú, `export.py`, gỡ hàm không ai gọi) build gói + image đạt, chưa nạp board;
0087 chỉ đổi nội dung bundle export. `tests/host/run.sh all` PASS ngày 06/10 trong container `ubuntu:24.04`,
valgrind 0 leak — xem [tests/host](tests/host/README.md). **BDK:** build image `MO77300EB` đạt ngày 06/10 tại 0088, chưa nạp board.
Tiến độ và việc còn lại: [docs/handover/icwmp_progress_matrix.md](docs/handover/icwmp_progress_matrix.md).
Kế hoạch hiện hành, known issue và quy ước phát triển: [docs/plan/sync-main-dev.md](docs/plan/sync-main-dev.md).

## Giải nén và apply

Host Linux, Python **3.6+**. Apply không cần compiler và không build/flash tự động.
Clone repo bên ngoài cây SDK đang build:

```sh
git clone git@github.com:tunguyenvanbn94/icwmpMultiSdk.git
cd icwmpMultiSdk
./apply --sdk bdk /path/to/bcm963xx
# hoặc
./apply --sdk mtk /path/to/2025q3
```

Có thể bỏ `--sdk` để tự nhận dạng. `--dry-run` kiểm hết payload và SDK/profile nhưng không ghi.

`--sdk-only` chỉ cài code của SDK đã chọn. Lib và app trong SDK đích chỉ còn `sdk/<sdk>/` (với MTK: bỏ
`sdk/bdk`, `sdk/uci`); `sdk/enabled.*` được sinh lại như `tools/sdk-prune.sh`, và file chỉ SDK khác dùng
(`BDK-CHANGES.md`) cũng bị bỏ. Bundle trong repo không đổi, chỉ bản cài bị lược. Không có cờ này thì apply
cài đủ mọi SDK như trước. Chuyển qua lại giữa hai cách chỉ cần apply lại, có backup như thường.
`.icwmp-release.json` ghi `"layout": "sdk-only"` hoặc `"full"`.

```sh
./apply --sdk mtk --sdk-only /path/to/2025q3
./apply --sdk bdk --sdk-only /path/to/bcm963xx
```

`--sdk-only` cũng bỏ các source `tr098/` không còn ai dùng (ví dụ với MTK: 33 file chỉ SDK `uci` build).
`.icwmp-release.json` ghi commit đang cài: `git HEAD` (thêm `-dirty` nếu có thay đổi chưa commit) khi chạy
từ repo, hoặc commit trong `MANIFEST.json` khi chạy từ bundle export.

## Xuất bản giao (release bundle)

```sh
./export.py --sdk mtk /tmp/icwmp_mtk.tar.gz     # chỉ MTK/OpenWrt
./export.py /tmp/icwmp_all.tar.gz               # mọi SDK
```

Export lấy đúng HEAD đã commit (worktree phải sạch). Bundle một SDK chỉ còn code của SDK đó, bỏ thêm các
thành phần chỉ SDK khác cần (với MTK: libuci, glue build BDK, `bdk-integration.json`, `docs/bdk`). Source
microxml vẫn đi kèm bundle MTK vì `tests/host/build.sh` build nó; apply MTK không cài microxml.
`MANIFEST.json` ghi commit và danh sách SDK. Tarball tái lập được: cùng commit cho cùng sha256. Người nhận
giải nén rồi chạy `./apply --sdk mtk /path/to/2025q3`; yêu cầu một SDK mà bundle không mang thì bị từ chối.
Profile mặc định: BDK **MO77300EB**, MTK **HP2236B/config_7583**, đổi tên bằng `--profile NAME`
nếu cùng layout/chipset. Không coi profile khác đã được board-verify.

Apply tự làm các bước cần để đưa source vào build:

- Kiểm SHA256 từng file của bundle và context SDK trước khi ghi.
- Cài app/library, BDK thêm microxml/UCI wrappers, MTK cài hai feed Makefile.
- BDK tích hợp make.common + comp_tr69_md.c, bật BUILD_ICWMP và TR69 SSL trong profile.
  Hỗ trợ profile chưa bật TR69, đã bật TR69, hoặc đã apply overlay trước.
- MTK bật libtr098/icwmp_tr098/libmicroxml, tắt cwmpclient trong config_7583.
- Source legacy libtr098 được đưa vào backup, tránh discovery hai engine cùng SONAME.
- Đích bị thay có backup đầy đủ tại `<SDK>/.icwmp-backups/<timestamp-id>/original/`.
  Source mới thay thế cả generated Makefile/object cũ của component được quản lý.
- Lỗi trong quá trình thay managed paths sẽ rollback, có journal tại backup. Đây không phải
  transaction chống mất điện, không chạy đồng thời với build hay sửa SDK.
- Apply lại khi source/profile giống nhau sẽ báo Already applied, không tạo backup mới.

Không tự sửa code vendor khi context BDK khác cả bản trước lẫn sau integration được hỗ trợ.
Trường hợp đó báo lỗi cụ thể trước khi chép source. Config khác các option quản lý được giữ nguyên.

## Build và test

Apply in lệnh build phù hợp. Dùng môi trường/toolchain SDK đã setup.

BDK từ SDK root:

```sh
make -C userspace/public/libs/microxml -f Bcmbuild.mk
make -C userspace/public/libs/uci
make -C userspace/public/libs/libicwmp_dm -f Bcmbuild.mk clean
make -C userspace/public/libs/libicwmp_dm -f Bcmbuild.mk
make -C userspace/public/apps/icwmp -f Bcmbuild.mk clean
make -C userspace/public/apps/icwmp -f Bcmbuild.mk
make PROFILE=MO77300EB
```

MTK từ 2025q3 root:

```sh
./airoha_script/airoha-compile.sh -c 7583 -f -m HP2236B -w Griffin_logan
cd openwrt-21.02/openwrt-21.02.1_dev
make -j 16 MSDK=1 V=s
```

Nếu máy MTK đã build trước đó, sau setup profile và trước rebuild full image, clean hai package:

```sh
make package/feeds/airoha/libtr098/clean V=s
make package/feeds/airoha/icwmp_tr098/clean V=s
make package/feeds/airoha/libtr098/compile V=s
make package/feeds/airoha/icwmp_tr098/compile V=s
```

Chỉ clean/build trên SDK đích, không dùng object từ layout cũ. Kiểm staging có icwmp_dm headers,
legacy forwarding headers, DT_NEEDED vẫn libtr098. Test board: daemon, ACS Inform, Connection
Request, GPN/GPV P1 và reload/WebUI/STUN, BDK kiểm cả model 098/181 hiện có.

## Xem từng lần sửa

`MANIFEST.json` ghi baseline và ánh xạ commit gốc sang từng commit trong repo này.
Commit nền dựng lại trạng thái trước 0034; 33 commit tiếp theo tương ứng từng patch
0034–0066. Từ 0067 các bản sửa được commit thẳng trong repo này (branch `dev`), không
có commit overlay gốc tương ứng. Patch files đã xóa sau khi ghi commit, xem phần sửa bằng Git:

```sh
sha256sum -c SHA256SUMS
cat MANIFEST.json
git log --reverse --oneline main
git show --stat HEAD
```

`<SDK>/.icwmp-release.json` cho biết source commit/profile đã cài. SHA256 phát hiện bundle thiếu
hoặc thay đổi file, không phải chữ ký xác thực nhà phát hành.
