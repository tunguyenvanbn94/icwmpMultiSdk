# icwmp multi-SDK source delivery

Source: A1 model-neutral layout `libicwmp_dm/src`, ABI/package vẫn `libtr098`.
TR-098 P1–P5 có 458 param C; các nhánh chưa port vẫn dùng compat. A2, A4, A6
và P6–P8 còn trong kế hoạch; A3 mới làm một phần. Xem [trạng thái và kế hoạch](docs/README.md).
**Đã kiểm source và apply; các bản sửa 0056–0077 chưa được build bằng SDK/board-test như một bộ hoàn chỉnh.**
0067–0077 (treo sau phiên đầu, leak, crash do RPC của ACS, tải shell data model) đã chạy thật trên host:
`tests/host/run.sh all` PASS, valgrind 0 leak — xem [tests/host](tests/host/README.md).
Kế hoạch hiện hành, known issue (K1: ACS ghi `ManagementServer.*` bị đảo cuối phiên trên MTK) và
quy ước phát triển: [docs/plan/sync-main-dev.md](docs/plan/sync-main-dev.md).

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
