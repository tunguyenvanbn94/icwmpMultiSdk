#!/usr/bin/env python3
"""Thư viện mà binary link tới có nằm trong DEPENDS của gói OpenWrt không.

Lỗi thật trên máy build (24/09):

    Package icwmp_tr098 is missing dependencies for the following libraries:
    libz.so.1

`bin/Makefile.am` link `icwmp_tr098d` với `$(LIBZ_LIBS)` cho `zlib.c`, nhưng
`feeds/icwmp_tr098/Makefile` không có `+zlib`.  Compile và link đều xong, chỉ
bước đóng gói mới chặn -- tức là mất trọn một vòng build mới biết.

Script KHÔNG thay `ipkg-build`.  Linker bỏ thư viện không dùng
(`--as-needed`), nên một `-l` vắng trong DEPENDS chưa chắc đã hỏng.  Vì vậy
đây là **cảnh báo để người đọc quyết định**, không phải lỗi -- trừ khi
`--strict`.

Tên gói được tra TỪ CHÍNH CÂY SDK (`projects/mtk_openwrt_wifi7/src/2025q3`),
không phải bảng đoán sẵn: tìm Makefile nào install `lib<x>.so`.

    ./check-pkg-deps.py
    ./check-pkg-deps.py --strict        # thoát 1 khi có mục chưa phủ
    ./check-pkg-deps.py <Makefile>      # đọc DEPENDS từ file khác (chạy ngược)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Cây userspace/ của repo này.  ICWMP_USERSPACE trỏ sang cây khác (overlay của
# workspace cũ: .../sdk-overlay/userspace) khi cần kiểm ở đó.
USERSPACE = os.environ.get("ICWMP_USERSPACE") or os.path.normpath(
    os.path.join(HERE, "..", "..", "userspace"))
APP = os.path.join(USERSPACE, "public", "apps", "icwmp", "icwmp")
# feeds/ ở gốc repo; ICWMP_FEED_MAKEFILE cho cây khác
FEED = os.environ.get("ICWMP_FEED_MAKEFILE") or os.path.normpath(
    os.path.join(HERE, "..", "..", "feeds", "icwmp_tr098", "Makefile"))
# cây SDK MTK (.../src/2025q3) để tra tên gói; không có thì chỉ in LDADD/DEPENDS
SDK = os.environ.get("ICWMP_SDK_SRC") or os.path.normpath(
    os.path.join(HERE, "..", "..", "src", "2025q3"))
SDK_ROOTS = ("openwrt-21.02/openwrt-21.02.1_dev/package",
             "openwrt-21.02/openwrt-21.02.1_dev/feeds",
             "openwrt-21.02/openwrt-21.02.1_dev/include",
             "airoha_feeds/package",
             "airoha_feeds/target")

TARGET = "icwmp_tr098d"
# libc/toolchain: không có gói riêng, luôn có mặt
LIBC = {"m", "dl", "rt", "c"}


def subst_libs(path):
    """configure.ac: XXX_LIBS='-lfoo -lbar' -> {'XXX_LIBS': ['foo','bar']}"""
    out = {}
    text = open(path, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"^(\w+_LIBS)\s*=\s*'([^']*)'", text, re.M):
        out[m.group(1)] = re.findall(r"-l(\S+)", m.group(2))
    return out


def ldadd(path, target, subst):
    """Mọi -l của <target>_LDADD, đã khai triển $(XXX_LIBS)."""
    libs, cur, cont = [], None, False
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^\s*(\w+)_LDADD\s*\+?=", ln)
        if m:
            cur, cont = m.group(1), ln.rstrip().endswith("\\")
        elif cont:
            cont = ln.rstrip().endswith("\\")
        else:
            cur = None
        if cur != target:
            continue
        for v in re.findall(r"\$\((\w+_LIBS)\)", ln):
            libs.extend(subst.get(v, []))
        libs.extend(re.findall(r"(?<![\w$(])-l(\S+)", ln))
    seen, out = set(), []
    for l in libs:
        if l not in seen:
            seen.add(l)
            out.append(l)
    return out


def depends(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"^\s*DEPENDS\s*:?=\s*(.*)$", text, re.M)
    return set(re.findall(r"\+(\S+)", m.group(1))) if m else set()


def sdk_package(lib):
    """Gói nào install lib<lib>.so -- tra trong cây SDK thật."""
    needle = "lib%s.so" % lib
    for root in SDK_ROOTS:
        base = os.path.join(SDK, root)
        if not os.path.isdir(base):
            continue
        for dirpath, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d not in (".git", "build_dir", "staging_dir")]
            if "Makefile" not in files:
                continue
            path = os.path.join(dirpath, "Makefile")
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if needle not in text:
                continue
            # `define ...` có thể thụt lề (package/libs/toolchain/Makefile:559)
            for m in re.finditer(r"^[ \t]*define Package/([\w.+-]+)/install\b.*$",
                                 text, re.M):
                blk = text[m.end():]
                blk = blk[:blk.find("endef")]
                if needle in blk:
                    return m.group(1), os.path.relpath(path, SDK)
            # không khớp install block: gói cùng tên `lib<x>` nếu có
            if re.search(r"^[ \t]*define Package/lib%s\s*$" % re.escape(lib),
                         text, re.M):
                return "lib" + lib, os.path.relpath(path, SDK)
            m = re.search(r"^PKG_NAME\s*:?=\s*(\S+)", text, re.M)
            if m:
                return m.group(1), os.path.relpath(path, SDK)
    return None, None


# Biến mà CHỈ target Makefile định nghĩa: build gói lẻ không thấy.
OPENWRT_VARS = set("""
TOPDIR INCLUDE_DIR PKG_NAME PKG_VERSION PKG_RELEASE PKG_SOURCE PKG_BUILD_DIR
PKG_FIXUP PKG_INSTALL PKG_INSTALL_DIR PKG_CONFIG_DEPENDS BUILD_DIR
KERNEL_BUILD_DIR STAGING_DIR TARGET_CFLAGS TARGET_LDFLAGS TARGET_CPPFLAGS
CP INSTALL_DIR INSTALL_BIN INSTALL_DATA INSTALL_CONF GNU_TARGET_NAME
CONFIGURE_ARGS MAKE_FLAGS HOST_BUILD_DIR TRUNK_DIR 1 2 3
""".split())
TARGET_ONLY = os.sep + os.path.join("target", "linux") + os.sep


def feed_vars(path):
    """$(VAR) dùng trong Makefile, trừ biến tự nó gán."""
    text = open(path, encoding="utf-8", errors="replace").read()
    # bỏ comment: chính comment giải thích lỗi cũng nhắc tên biến
    text = re.sub(r"^\s*#.*$", "", text, flags=re.M)
    local = set(re.findall(r"^\s*([A-Za-z_]\w*)\s*[:+?]?=", text, re.M))
    # Biến chỉ xuất hiện trong ifeq/ifdef là CỜ tính năng: không định nghĩa
    # nghĩa là nhánh đó tắt, đúng như mong đợi.  Chỉ quan tâm biến được thay
    # vào một câu lệnh -- rỗng ở đó mới thành `cp -fpR /.`.
    used = set()
    for ln in text.splitlines():
        if re.match(r"\s*(ifeq|ifneq|ifdef|ifndef)\b", ln):
            continue
        used.update(re.findall(r"\$\(([A-Za-z_]\w*)\)", ln))
    return used - local - OPENWRT_VARS


def var_scope(name):
    """(có định nghĩa ở đâu đó, chỉ nằm trong target/linux)."""
    found, target_only = False, True
    for root in SDK_ROOTS:
        base = os.path.join(SDK, root)
        if not os.path.isdir(base):
            continue
        for dirpath, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d not in (".git", "build_dir", "staging_dir")]
            for fn in files:
                if not (fn.endswith(".mk") or fn.endswith(".mak") or fn == "Makefile"):
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    text = open(path, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                if re.search(r"^\s*(?:export\s+)?%s\s*[:+?]?=" % re.escape(name),
                             text, re.M):
                    found = True
                    if TARGET_ONLY not in path:
                        target_only = False
    return found, target_only


def check_feed_vars(feeds):
    """Biến chỉ có trong target/linux/**/dir.mak -> build gói lẻ thấy rỗng."""
    bad = []
    for feed in feeds:
        for name in sorted(feed_vars(feed)):
            found, target_only = var_scope(name)
            if not found:
                bad.append((feed, name, "không định nghĩa ở đâu trong SDK"))
            elif target_only:
                bad.append((feed, name, "chỉ định nghĩa trong target/linux/** -- "
                                        "build gói lẻ không include, sẽ RỖNG"))
    return bad


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    strict = "--strict" in sys.argv[1:]
    feed = args[0] if args else FEED          # để chạy ngược trên bản chưa sửa
    subst = subst_libs(os.path.join(APP, "configure.ac"))
    libs = ldadd(os.path.join(APP, "bin", "Makefile.am"), TARGET, subst)
    deps = depends(feed)
    have_sdk = os.path.isdir(SDK)

    print("%s_LDADD: %s" % (TARGET, " ".join("-l" + l for l in libs)))
    print("DEPENDS : %s" % " ".join("+" + d for d in sorted(deps)))
    if not have_sdk:
        print("\nKHÔNG tra được tên gói: %s không tồn tại." % SDK)
        return 0
    print()

    gap = []
    for lib in libs:
        if lib in LIBC:
            continue
        pkg, where = sdk_package(lib)
        if pkg is None:
            print("  -l%-14s  không thấy gói nào install lib%s.so trong SDK" % (lib, lib))
            continue
        if pkg in deps:
            continue
        gap.append((lib, pkg, where))

    feeds = [feed, os.path.join(os.path.dirname(os.path.dirname(feed)),
                                "libtr098", "Makefile")]
    varbad = check_feed_vars([f for f in feeds if os.path.isfile(f)])
    for path, name, why in varbad:
        print("BIẾN $(%s) trong %s: %s" % (name, os.path.basename(os.path.dirname(path)), why))
    if varbad:
        print()

    if not gap:
        print("Mọi -l đều có gói tương ứng trong DEPENDS.")
        return 1 if (varbad and strict) else 0
    print("CHƯA PHỦ trong DEPENDS:")
    for lib, pkg, where in gap:
        print("  -l%-14s -> cần `+%s`  (%s)" % (lib, pkg, where))
    print("\nLinker bỏ thư viện không dùng (--as-needed), nên chưa chắc tất cả đều hỏng.")
    print("`ipkg-build` trên máy build mới là lời cuối. Thiếu thật thì nó báo:")
    print('  "Package ... is missing dependencies for the following libraries: lib<x>.so.1"')
    return 1 if strict else 0


if __name__ == "__main__":
    sys.exit(main())
