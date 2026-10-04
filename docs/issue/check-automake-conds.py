#!/usr/bin/env python3
"""Kiểm điều kiện automake của các fragment sdk/<name>/sdk.mk.

Máy này không có automake, mà automake kiểm **tĩnh trên mọi tổ hợp điều kiện**,
không phải trên cái `configure` thực sự chọn. Lỗi điển hình đã lọt ra máy build
ngày 24/09:

    sdk/mtk/sdk.mk:4: error: cannot apply '+=' because 'icwmp_tr098d_SOURCES'
        is not defined in the following conditions:
        ICWMP_SDK_MTK and !ICWMP_TR098

Luật: một `VAR +=` chỉ hợp lệ nếu tập điều kiện bao quanh nó **bao hàm** tập
điều kiện mà `VAR =` được định nghĩa ở Makefile.am gốc.

    ./check-automake-conds.py                 # cả hai cây (app icwmp + lib)
    ./check-automake-conds.py <Makefile.am> <sdk.mk> [...]
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Cây userspace/ của repo này.  ICWMP_USERSPACE trỏ sang cây khác (overlay của
# workspace cũ: .../sdk-overlay/userspace) khi cần kiểm ở đó.
USERSPACE = os.environ.get("ICWMP_USERSPACE") or os.path.normpath(
    os.path.join(HERE, "..", "..", "userspace"))
OVERLAY = USERSPACE
TREES = (
    (os.path.join(OVERLAY, "public/apps/icwmp/icwmp/bin/Makefile.am"),
     os.path.join(OVERLAY, "public/apps/icwmp/icwmp/sdk")),
    (os.path.join(OVERLAY, "public/libs/libicwmp_dm/src/bin/Makefile.am"),
     os.path.join(OVERLAY, "public/libs/libicwmp_dm/src/sdk")),
)

IF_RE = re.compile(r"^\s*if\s+(!?)(\w+)\s*$")
ELSE_RE = re.compile(r"^\s*else\s*$")
ENDIF_RE = re.compile(r"^\s*endif\b")
ASSIGN_RE = re.compile(r"^\s*(\w+)\s*(\+?=)")


def scan(path):
    """[(dòng, biến, '=' hay '+=', frozenset điều kiện)] của một file."""
    out, stack = [], []
    cont = False
    for no, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        raw = line.rstrip("\n")
        body = raw.split("#", 1)[0] if not raw.lstrip().startswith("\t") else raw
        if cont:                       # dòng nối tiếp của phép gán trước
            cont = raw.rstrip().endswith("\\")
            continue
        m = IF_RE.match(body)
        if m:
            stack.append(("!" if m.group(1) else "") + m.group(2))
            continue
        if ELSE_RE.match(body):
            if stack:
                c = stack.pop()
                stack.append(c[1:] if c.startswith("!") else "!" + c)
            continue
        if ENDIF_RE.match(body):
            if stack:
                stack.pop()
            continue
        m = ASSIGN_RE.match(body)
        if m:
            out.append((no, m.group(1), m.group(2), frozenset(stack)))
            cont = raw.rstrip().endswith("\\")
    return out


def check(makefile, sdkdir):
    base = {}
    for _no, var, op, conds in scan(makefile):
        if op == "=":
            base.setdefault(var, conds)
    problems = []
    if not os.path.isdir(sdkdir):
        return problems
    for name in sorted(os.listdir(sdkdir)):
        frag = os.path.join(sdkdir, name, "sdk.mk")
        if not os.path.isfile(frag):
            continue
        for no, var, op, conds in scan(frag):
            if op != "+=" or var not in base:
                continue
            missing = base[var] - conds
            if missing:
                problems.append("%s:%d  `%s +=` thiếu điều kiện %s -- "
                                "`%s =` ở %s chỉ tồn tại khi %s"
                                % (os.path.relpath(frag, OVERLAY), no, var,
                                   ", ".join(sorted(missing)), var,
                                   os.path.relpath(makefile, OVERLAY),
                                   ", ".join(sorted(base[var])) or "(không điều kiện)"))
    return problems


def main():
    args = sys.argv[1:]
    trees = [(args[0], args[1])] if len(args) == 2 else TREES
    bad = 0
    for makefile, sdkdir in trees:
        if not os.path.isfile(makefile):
            print("THIẾU:", makefile)
            bad += 1
            continue
        problems = check(makefile, sdkdir)
        rel = os.path.relpath(makefile, OVERLAY)
        if problems:
            bad += len(problems)
            for p in problems:
                print(p)
        else:
            print("%s + %s/: OK" % (rel, os.path.relpath(sdkdir, OVERLAY)))
    print("\n%d vấn đề" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
