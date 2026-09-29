#!/usr/bin/env python3
"""Cổng compile THẬT cho cây MTK: chạy cross-gcc của SDK với -fsyntax-only
trên đúng những file mà libtr098 và icwmp_tr098d build, lấy nguồn từ overlay.

Vì sao có script này (25/09): 8 lớp của check-c-sanity.py đều đạt, vậy mà
gcc bắt ngay hai lỗi trong vài giây --
  - một định danh chưa khai báo (`D` sót lại sau khi đổi tên macro);
  - `__dmjson_get_value_in_array_idx` không có prototype trong dmjson.h, nên
    gcc 10 coi nó trả `int`: con trỏ `char *` mất 32 bit cao trên aarch64.
    gcc 10 chỉ CẢNH BÁO, nên build của SDK vẫn ra .ipk -- lỗi chỉ lộ lúc
    chạy, bằng segfault.
Kiểm tĩnh bằng regex không thay được compiler.  Cổng này chạy trong vài
chục giây, không ghi gì vào cây SDK (-fsyntax-only), và biến đúng những
cảnh báo nguy hiểm thành lỗi.

Dùng:
    ./check-cc-syntax.py                       # SDK mặc định ~/workspace/openwrt/1_src/2025q3
    ./check-cc-syntax.py --sdk-root <2025q3>   # cây SDK khác
    ./check-cc-syntax.py --tree lib            # chỉ libtr098
    ./check-cc-syntax.py -v                    # in cả cảnh báo thường

Trả về 0 khi không có lỗi.
"""
import argparse
import glob
import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# tái dùng danh sách nguồn + include của check-c-sanity.py: một nguồn sự thật
_spec = importlib.util.spec_from_file_location("ccs", os.path.join(HERE, "check-c-sanity.py"))
ccs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ccs)

# Cảnh báo mà gcc 10 để lọt nhưng là lỗi thật lúc chạy.
FATAL = [
    "-Werror=implicit-function-declaration",	# hàm trả int ngầm -> con trỏ bị cắt
    "-Werror=int-conversion",			# int <-> pointer
    "-Werror=incompatible-pointer-types",
    "-Werror=return-type",			# hàm non-void rơi khỏi cuối
    "-Werror=implicit-int",
    "-Werror=format-security",
]
WARN = ["-Wall", "-Wno-unused-parameter", "-Wno-sign-compare",
        "-Wno-unused-variable", "-Wno-unused-but-set-variable",
        "-Wno-unused-function"]

# DEFS lấy từ bin/Makefile của build_dir thật (25/09), phần ảnh hưởng code
DEFS = {
    "lib": ["-DDM_SDK_MTK=1", "-DDM_PLATFORM_MTK=1", "-DDM_MTK_SCRIPT_COMPAT=1",
            "-DSTDC_HEADERS=1", "-DHAVE_STDLIB_H=1", "-DHAVE_STRING_H=1",
            '-DCUSTOM_PREFIX="X_HNI_"',
            '-DDMSCRIPT_PATH="/usr/share/icwmp/icwmp_dm.sh"',
            '-DTR098_VERSION="3"'],
    "app": ["-DACS_MULTI=1", "-DWITH_CWMP_DEBUG=1", "-DICWMP_TR098=1",
            "-DTR098=1", "-DICWMP_MTK=1", "-DHTTP_CURL=1", "-DSTDC_HEADERS=1",
            "-DHAVE_STDLIB_H=1", "-DHAVE_STRING_H=1",
            "-DICWMP_SDK_HEADER=\"icwmp_mtk.h\"",
            '-DCWMP_VERSION="3.0.0"',
            '-DCWMP_BKP_FILE="/etc/icwmpd/.icwmpd_backup_session.xml"'],
}


def find_toolchain(sdk_root):
    ow = os.path.join(sdk_root, "openwrt-21.02", "openwrt-21.02.1_dev")
    cc = sorted(glob.glob(os.path.join(ow, "staging_dir", "toolchain-aarch64*",
                                       "bin", "aarch64-openwrt-linux-gcc")))
    inc = sorted(glob.glob(os.path.join(ow, "staging_dir", "target-aarch64*",
                                        "usr", "include")))
    if not cc or not inc:
        sys.exit("không thấy toolchain/staging dưới %s -- SDK đã build lần nào chưa?" % ow)
    return ow, cc[0], inc[0]


def lib_header_dirs():
    root = ccs.TREES["lib"]["root"]
    return [os.path.join(root, d % {"sdk": "mtk"}) for d in ccs.TREES["lib"]["inc"]]


def build_icwmp_dm(tmp):
    """<tmp>/icwmp_dm/*.h -> header hiện tại của libtr098, cài phẳng như
    InstallDev làm, để app KHÔNG kiểm với bản cũ trong staging_dir."""
    d = os.path.join(tmp, "icwmp_dm")
    os.makedirs(d)
    for hd in lib_header_dirs():
        for h in sorted(glob.glob(os.path.join(hd, "*.h"))):
            dst = os.path.join(d, os.path.basename(h))
            if not os.path.exists(dst):
                os.symlink(h, dst)
    return tmp


def compile_one(cc, env, path, incs, defs, verbose):
    cmd = [cc, "-fsyntax-only", "-std=gnu11", "-Os"] + WARN + FATAL + defs
    for i in incs:
        cmd += ["-I", i]
    cmd.append(path)
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, universal_newlines=True)
    lines = p.stdout.splitlines()
    errs = [l for l in lines if " error: " in l]
    warns = [l for l in lines if " warning: " in l]
    return p.returncode, errs, warns, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk-root", default=os.path.expanduser("~/workspace/openwrt/1_src/2025q3"))
    ap.add_argument("--tree", choices=("lib", "app", "all"), default="all")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    ow, cc, staging_inc = find_toolchain(a.sdk_root)
    env = dict(os.environ, STAGING_DIR=os.path.join(ow, "staging_dir"))
    total_err = 0
    with tempfile.TemporaryDirectory(prefix="icwmp_ccsyn_") as tmp:
        fake = build_icwmp_dm(tmp)
        for tree in (("lib", "app") if a.tree == "all" else (a.tree,)):
            t = ccs.TREES[tree]
            root = t["root"]
            srcs = ccs.collect_sources(root, "mtk", t["target"])
            incs = [os.path.join(root, d % {"sdk": "mtk"}) for d in t["inc"]]
            if tree == "app":
                incs.append(fake)
            incs.append(staging_inc)
            nerr = nwarn = 0
            for s in srcs:
                rc, errs, warns, lines = compile_one(cc, env, s, incs, DEFS[tree], a.verbose)
                nwarn += len(warns)
                if rc != 0:
                    nerr += 1
                    print("LỖI  %s" % ccs._short(s))
                    for l in (errs or lines)[:8]:
                        print("     " + l.replace(root + "/", ""))
                elif a.verbose and warns:
                    print("cảnh báo %s" % ccs._short(s))
                    for l in warns[:8]:
                        print("     " + l.replace(root + "/", ""))
            print("[%s/mtk] %d file, %d file lỗi, %d cảnh báo thường" % (tree, len(srcs), nerr, nwarn))
            total_err += nerr
    print("gcc: %s" % os.path.relpath(cc, ow))
    return 1 if total_err else 0


if __name__ == "__main__":
    sys.exit(main())
