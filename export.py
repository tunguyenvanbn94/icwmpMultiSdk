#!/usr/bin/env python3
"""Export committed overlay source, apply tooling and per-commit review patches."""

import argparse
import gzip
import hashlib
import io
import json
import subprocess
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE = "cd93685595d4a52302d5b14adc9cbf38ac82553c"


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Tar.gz path outside the source repository")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("write delivery outside the source repository")
    if git("status", "--porcelain").strip():
        parser.error("source worktree must be clean and committed before release")
    commit = git("rev-parse", "HEAD").decode().strip()
    payload = {}
    executable = set()
    # Export tracked committed files only, never local build output or Git metadata.
    for record in git("ls-tree", "-r", "HEAD").decode().splitlines():
        mode_type_oid, name = record.split("\t", 1)
        mode = mode_type_oid.split()[0]
        if mode not in ("100644", "100755"):
            raise ValueError("unsupported source entry: " + name)
        if name.startswith("release/tests/") or name == "release/export.py":
            continue
        target = name[len("release/"):] if name.startswith("release/") else "userspace/" + name
        payload[target] = git("show", commit + ":" + name)
        if mode == "100755":
            executable.add(target)
    commits = git("rev-list", "--reverse", BASELINE + "..HEAD").decode().splitlines()
    history = []
    for number, revision in enumerate(commits, start=34):
        title = git("show", "-s", "--format=%s", revision).decode().strip()
        name = "patches/{:04d}-{}.patch".format(number, revision[:12])
        payload[name] = git("format-patch", "-1", "--stdout", "--no-renames", revision)
        history.append({"sequence": number, "commit": revision, "subject": title, "patch": name})
    manifest = {"format": 1, "baseline": BASELINE, "commit": commit, "history": history,
                "validation": "source/apply-fixture verified, SDK build and board NOT RUN",
                "files": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(payload.items())}}
    payload["MANIFEST.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    sums = "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n" for name, data in sorted(payload.items()))
    payload["SHA256SUMS"] = sums.encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as archive:
            for name, data in sorted(payload.items()):
                info = tarfile.TarInfo("icwmp_port/" + name)
                info.size = len(data)
                info.mode = 0o755 if name in executable else 0o644
                info.mtime = 0
                archive.addfile(info, io.BytesIO(data))
    print(output)
    print("commit:", commit)
    print("sha256:", hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
