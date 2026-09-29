# Build riêng gói icwmp — MTK OpenWrt và Broadcom BDK

Mục đích: **bắt lỗi compile trong vài phút** thay vì chạy lại cả image. Chỉ build hai thứ do
bản port này sinh ra:

| | MTK (Airoha 2025q3) | Broadcom (bcm963xx) |
|---|---|---|
| Thư viện data model | gói `libtr098` | `userspace/public/libs/libicwmp_dm` → `libtr098.so` |
| Ứng dụng CWMP | gói `icwmp_tr098` | `userspace/public/apps/icwmp` → `icwmpd` |

Nguồn nằm ở đâu sau khi `./apply` chạy:

| SDK | Thư viện | App |
|---|---|---|
| MTK | `tclinux_phoenix/apps/hni/libicwmp_dm` | `tclinux_phoenix/apps/hni/icwmp_tr098` |
| BDK | `userspace/public/libs/libicwmp_dm` | `userspace/public/apps/icwmp` |

> Số dòng và tên biến dưới đây là anchor của snapshot `src/2025q3` và `src/bcm963xx` hiện tại.

## 1. MTK — Airoha SDK 2025q3 (OpenWrt 21.02)

### 1.1 Chạy một lần trước đã

Build từng gói **chỉ chạy được sau khi cây OpenWrt đã được dựng một lần** (feeds đã install,
toolchain đã có, `.config` đã sinh):

```sh
cd <SDK>
./airoha_script/airoha-compile.sh -c 7583 -f -m HP2236B -w Griffin_logan
```

Lệnh này làm ba việc mà build gói lẻ cần: ghi `include/ecnt-trunkdir.mk` (biến `TRUNK_DIR`
trỏ vào `tclinux_phoenix` thật, mặc định trong repo là đường dẫn CI `/proj/srv_cicd01/...`),
`./scripts/feeds update airoha` + `install`, và sinh `.config` từ
`airoha_feeds/airoha_build/profile/HP2236B/config_7583`.

### 1.2 Vòng lặp nhanh — chỉ sửa source C

Đây là trường hợp thường gặp khi tôi giao patch mới.

> **Patch có sửa Makefile của feed thì làm mục [1.3](#13-khi-sửa-makefile-của-feed) TRƯỚC.**
> Feed airoha là `src-cpy`, `feeds/airoha` là bản **copy** — không refresh thì build vẫn đọc
> Makefile cũ và patch có vẻ như không có tác dụng gì. Đã mất một vòng build vì đúng chuyện này
> (`0048` `+zlib`, xem `analysis.md` mục 26). `apply` nay cũng in bước này ra.

```sh
cd <SDK>/openwrt-21.02/openwrt-21.02.1_dev

# thư viện data model (toàn bộ sdk/mtk/dm098/*.c nằm trong đây)
make package/libtr098/{clean,compile} V=sc -j1 2>&1 | tee /tmp/libtr098.log

# app, chỉ cần khi sửa phần app
make package/icwmp_tr098/{clean,compile} V=sc -j1 2>&1 | tee /tmp/icwmp.log
```

- `V=sc` in đầy đủ lệnh compile — **bắt buộc** nếu muốn đọc được lỗi.
- `-j1` để lỗi đầu tiên không bị trộn với output của job khác.
- `clean` cần thiết vì `Build/Prepare` **copy** nguồn từ `$(TRUNK_DIR)/apps/hni/...` sang
  `build_dir/.../libtr098`. Không `clean` thì sửa source không vào.

Xem lỗi đầu tiên:

```sh
grep -nE 'error:|Error [0-9]|undefined reference' /tmp/libtr098.log | head
```

Kết quả nằm ở:

```sh
ls -l build_dir/target-aarch64_cortex-a53_musl/libtr098/bin/.libs/libtr098.so*
ls -l build_dir/target-aarch64_cortex-a53_musl/icwmp_tr098/bin/icwmp_tr098d
```

### 1.3 Khi sửa Makefile của feed

`feeds.conf.default` dùng **`src-cpy`**: `feeds/airoha/` là bản **copy** của `airoha_feeds/`, và
`package/feeds/airoha/<gói>` là symlink vào bản copy đó. Build đọc **Makefile** từ bản copy, còn
**source và file cài đặt** (`icwmpd.init`, `cwmp`, script) lấy từ `$(TRUNK_DIR)/apps/hni/<gói>`
qua `Build/Prepare`. Vì vậy:

- Patch chỉ sửa source/init (ví dụ `0062`, `0063`): **không cần bước feed nào**, chỉ `clean,compile`.
- Patch sửa Makefile của gói: chép **đúng Makefile đó**, đừng `./scripts/feeds update airoha`
  (lệnh đó copy lại cả feed airoha và đã làm hỏng build image của người dùng, 2026-09-26):

```sh
cd <SDK>/openwrt-21.02/openwrt-21.02.1_dev
for p in libtr098 icwmp_tr098; do
  cmp -s ../../airoha_feeds/package/airoha/apps/$p/Makefile feeds/airoha/package/airoha/apps/$p/Makefile \
    || cp -v ../../airoha_feeds/package/airoha/apps/$p/Makefile feeds/airoha/package/airoha/apps/$p/Makefile
done
ls -l package/feeds/airoha/libtr098 package/feeds/airoha/icwmp_tr098   # symlink đã có thì không cần feeds install
make package/libtr098/{clean,compile} V=sc -j1
```

Chỉ khi symlink `package/feeds/airoha/<gói>` chưa có (gói mới lần đầu) mới cần
`./scripts/feeds install -p airoha <gói>` — **không** `-f`, không `update`.

### 1.4 Chỉ muốn kiểm compile, không cần đóng gói

```sh
make package/libtr098/compile V=sc -j1        # bỏ clean nếu vừa clean rồi
```

Dừng ngay khi `.so` link xong, không chạy `Package/install`, không dựng image.

### 1.5 Dựng lại image sau khi compile sạch

```sh
cd <SDK>/openwrt-21.02/openwrt-21.02.1_dev
make -j 16 MSDK=1 V=s
```

## 2. Broadcom — BDK bcm963xx

### 2.1 Chạy một lần trước đã

```sh
cd <bcm963xx>
make PROFILE=MO77300EB
```

Cần để có toolchain, `make.common` và `$(BCM_FSBUILD_DIR)`. Component makefile
(`Bcmbuild.mk`) `include $(BUILD_DIR)/make.common`, không tự dựng được những thứ đó.

### 2.2 Vòng lặp nhanh

```sh
cd <bcm963xx>

# thư viện data model
make -C userspace/public/libs/libicwmp_dm -f Bcmbuild.mk clean
make -C userspace/public/libs/libicwmp_dm -f Bcmbuild.mk 2>&1 | tee /tmp/libtr098.log

# app
make -C userspace/public/apps/icwmp -f Bcmbuild.mk clean
make -C userspace/public/apps/icwmp -f Bcmbuild.mk 2>&1 | tee /tmp/icwmpd.log
```

Phụ thuộc, chỉ build lại khi chúng thay đổi:

```sh
make -C userspace/public/libs/microxml -f Bcmbuild.mk
make -C userspace/public/libs/uci
```

### 2.3 Nếu thấy `skipping libtr098.so (not configured)`

Đó **không phải lỗi**: `Bcmbuild.mk` gác bằng `ifneq ($(strip $(BUILD_LIBTR098)),)`, và
`BUILD_LIBTR098=y` chỉ được `make.common` đặt khi `BUILD_ICWMP` khác rỗng. Kiểm:

```sh
grep -n 'BUILD_ICWMP' targets/MO77300EB/MO77300EB      # phải có BUILD_ICWMP=y
grep -n 'BUILD_LIBTR098' make.common                   # phải nằm trong khối ifneq BUILD_ICWMP
```

Thiếu `BUILD_ICWMP=y` nghĩa là `./apply` chưa chạy hoặc profile bị ghi đè — chạy lại
`./apply --sdk bdk <bcm963xx>`.

### 2.4 Shell debug của component

Cả hai `Bcmbuild.mk` có target `shell`, vào đúng môi trường biến của component:

```sh
make -C userspace/public/libs/libicwmp_dm -f Bcmbuild.mk shell
# trong đó: echo $CFLAGS, echo $ALLOWED_INCLUDE_PATHS, chạy tay lệnh gcc bị lỗi
```

### 2.5 Kết quả nằm ở

```sh
ls -l $(grep -m1 BCM_FSBUILD_DIR make.common | cut -d= -f2)/public/lib/libtr098.so*
ls -l userspace/public/apps/icwmp/icwmpd
```

## 2b. Kiểm trước khi build

**Chạy đầu tiên — compiler thật.** Máy workspace không có `gcc` hệ thống, nhưng cây SDK đã build
thì luôn có cross-gcc trong `staging_dir/`. [check-cc-syntax.py](check-cc-syntax.py) chạy nó với
`-fsyntax-only` (không ghi gì vào cây) trên đúng danh sách file mà `libtr098` và `icwmp_tr098d`
build, header lấy từ overlay chứ không từ bản cũ trong staging, và nâng sáu cảnh báo gây lỗi lúc
chạy thành lỗi (`implicit-function-declaration`, `int-conversion`, `incompatible-pointer-types`,
`return-type`, `implicit-int`, `format-security`). Khoảng 1 giây:

```sh
./check-cc-syntax.py                              # SDK mặc định ~/workspace/openwrt/1_src/2025q3
./check-cc-syntax.py --sdk-root <cây 2025q3 khác> -v
```

Vì sao nó đứng trước: lần chạy đầu (25/09) bắt một hàm **không có prototype** trả `char *` —
gcc 10 chỉ cảnh báo, SDK vẫn ra `.ipk`, còn trên aarch64 con trỏ mất 32 bit cao. Tám lớp kiểm
tĩnh bên dưới đều cho qua. Xem [analysis.md §33.2](analysis.md).

Sau đó, các script tĩnh — bắt những gì compiler không bắt (comment đóng sớm, NULL vào
out-parameter, macro mang sẵn `;`, DEPENDS thiếu). Chạy hết trước mỗi lần giao bản mới:

```sh
cd projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098
# cây thư viện libtr098
./check-c-sanity.py
# cây app icwmp_tr098d -- BẮT BUỘC chạy đủ ba SDK, mỗi SDK kéo vào glue khác nhau
./check-c-sanity.py --tree app --sdk mtk
./check-c-sanity.py --tree app --sdk bdk
./check-c-sanity.py --tree app --sdk uci
./check-automake-conds.py    # `VAR +=` phải cùng điều kiện với `VAR =`
./check-pkg-deps.py          # mọi -l trên dòng link có gói trong DEPENDS không
./verify-dm-paths.py --phase 5 && ./verify-dm-paths.py --claims
```

Bảng kiểu shell của hợp đồng input (`sdk/mtk/shelltypes_mtk.h`) được **sinh** từ ma trận. Sinh lại
khi ma trận đổi, rồi commit cùng code:

```sh
./gen-shell-types.py > <overlay>/public/libs/libicwmp_dm/src/sdk/mtk/shelltypes_mtk.h
```

`check-c-sanity.py` bắt: comment đóng sớm, ngoặc lệch, gọi hàm không khai báo, `static` trùng
khai báo non-static trong header đã include, trùng symbol khi link, macro mang sẵn `;`
(`CWMP_LOG`) dùng không ngoặc trong `if` có `else`, và `NULL` truyền vào out-parameter mà hàm
nhận ghi qua nó (`dmuci_add_section` — segfault, compiler im lặng). Cây app include header
thư viện bằng `<icwmp_dm/...>` và dựng bốn binary, nên phải chạy với `--tree app` — chạy `--tree
lib` không nhìn thấy file nào của app.

`check-pkg-deps.py` làm hai việc: so `icwmp_tr098d_LDADD` với `DEPENDS` của feed (tra tên gói
**từ chính cây SDK** `src/2025q3` chứ không dùng bảng đoán sẵn), và kiểm mọi biến mà feed Makefile
**thay vào một câu lệnh** — biến chỉ do `target/linux/**/*.mak` định nghĩa thì build gói lẻ thấy
rỗng (`PKG_SOURCE` rỗng → `cp -fpR /.` copy cả filesystem gốc, đã thật sự xảy ra, 17 GB). Nó **không thay `ipkg-build`**: `--as-needed`
bỏ thư viện không dùng nên một `-l` vắng trong DEPENDS chưa chắc đã hỏng — mặc định là cảnh báo,
thoát 0; `--strict` mới thoát 1. Hiện còn `-lcrypto`/`-lssl` là **cố ý** (curl gọi OpenSSL, binary
này không).

Cả bốn script đều đã bắt được lỗi thật trên máy build (`0043`–`0051`).

## 3. Gửi lỗi về thế nào

Lỗi **đầu tiên**, kèm khoảng 20 dòng trước đó và dòng lệnh compile ngay trên nó:

```sh
grep -nE 'error:|Error [0-9]|undefined reference' /tmp/libtr098.log | head -5
sed -n '<dòng đầu tiên - 20>,<dòng đầu tiên + 5>p' /tmp/libtr098.log
```

Lỗi thứ hai trở đi thường là hệ quả của lỗi đầu.

## 4. Liên quan

- [README.md](README.md) — trạng thái, bộ giao, thứ tự patch
- [debug-commands.md](debug-commands.md) — debug lúc **chạy** trên board, không phải lúc build
- [analysis.md](analysis.md) — mục 17–19: P4a, P4b và sự cố apply/Python
