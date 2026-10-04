#!/usr/bin/env python3
"""Cập nhật (hoặc kiểm) MANIFEST.json "files" và SHA256SUMS của bundle.

Bundle = mọi file git đang track dưới userspace/ và feeds/, cộng các file gốc
mà apply cần (README.md, apply, apply.py, bdk-integration.json).  MANIFEST.json
liệt kê sha256 của chúng; SHA256SUMS liệt kê chúng cộng chính MANIFEST.json.
apply kiểm từng file theo SHA256SUMS, nên sửa một file trong bundle mà không
chạy lại script này là bundle hỏng (`sha256sum -c SHA256SUMS` báo FAILED).

    ./update-sums.py           # ghi lại MANIFEST.json + SHA256SUMS
    ./update-sums.py --check   # chỉ kiểm, exit 1 khi lệch (cổng PR)
    ./update-sums.py --staged  # tính trên git index: sau "git add", trước commit

Chỉ đụng "files" của MANIFEST.json; baseline/history/validation giữ nguyên.
Mặc định tính trên worktree; --staged tính trên những gì đã "git add", để một
commit chỉ mang checksum của chính nó khi worktree còn thay đổi khác.
"""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOP_FILES = ("README.md", "apply", "apply.py", "bdk-integration.json")


STAGED = False


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def exists(name):
    if STAGED:
        return bool(git("ls-files", "--", name).strip())
    return (ROOT / name).is_file()


def read(name):
    return git("show", ":" + name) if STAGED else (ROOT / name).read_bytes()


def bundle_files():
    tracked = git("ls-files", "-z", "userspace", "feeds").decode().split("\0")
    names = {n for n in tracked if n} | {n for n in TOP_FILES if exists(n)}
    return sorted(n for n in names if exists(n))


def sha256(name):
    return hashlib.sha256(read(name)).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="chỉ kiểm, không ghi")
    ap.add_argument("--staged", action="store_true", help="tính trên git index thay vì worktree")
    a = ap.parse_args()
    global STAGED
    STAGED = a.staged

    manifest_path = ROOT / "MANIFEST.json"
    sums_path = ROOT / "SHA256SUMS"
    manifest = json.loads(read("MANIFEST.json").decode("utf-8"))
    files = {n: sha256(n) for n in bundle_files()}
    old = manifest.get("files", {})
    changed = sorted(n for n in files if old.get(n) != files[n])
    removed = sorted(set(old) - set(files))
    manifest["files"] = files
    manifest_text = json.dumps(manifest, indent=2) + "\n"
    entries = dict(files)
    entries["MANIFEST.json"] = hashlib.sha256(manifest_text.encode()).hexdigest()
    sums_text = "".join("%s  %s\n" % (entries[n], n) for n in sorted(entries))

    stale = manifest_text != read("MANIFEST.json").decode("utf-8") or \
        sums_text != read("SHA256SUMS").decode("utf-8")
    for n in changed:
        print(("lệch   " if a.check else "cập nhật ") + n)
    for n in removed:
        print(("thừa   " if a.check else "bỏ      ") + n)
    if a.check:
        print("OK" if not stale else "MANIFEST.json/SHA256SUMS cũ: chạy docs/issue/update-sums.py")
        return 1 if stale else 0
    if stale:
        manifest_path.write_text(manifest_text, encoding="utf-8")
        sums_path.write_text(sums_text, encoding="utf-8")
    print("%d file trong bundle, %d đổi, %d bỏ" % (len(files), len(changed), len(removed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
