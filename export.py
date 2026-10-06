#!/usr/bin/env python3
"""Export a release bundle of this repository: the committed HEAD, for every
SDK or for one, with the MANIFEST.json and SHA256SUMS that apply.py checks.

    ./export.py --sdk mtk /tmp/icwmp_mtk.tar.gz     # MTK/OpenWrt only
    ./export.py /tmp/icwmp_all.tar.gz               # every SDK

A one-SDK bundle keeps only sdk/<sdk>/ in libicwmp_dm and icwmp (sdk/enabled.*
regenerated), drops the tr098/ sources nothing left uses and the files and
components only another SDK needs (see SDK_EXCLUDES).  It carries the
apply tool, the docs, and for MTK the host test harness.  MANIFEST.json names
the exported commit and the SDKs, so apply.py records the right commit in
<SDK>/.icwmp-release.json and refuses a bundle without the SDK asked for."""

import argparse
import gzip
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True      # no __pycache__ in the repository: export needs it clean
import apply as applytool  # noqa: E402  (prune_trees, the same pruning as apply --sdk-only)

SDKS = ("bdk", "mtk", "uci")

# Repository paths a one-SDK bundle leaves out, besides the other sdk/<name>/
# directories.  A path ending in "/" is a directory.
BDK_GLUE = ("autodetect", "Bcmbuild.mk", "Makefile", "Manifest.brcmoss")
SDK_EXCLUDES = {
    "mtk": ("bdk-integration.json", "docs/bdk/",
            "userspace/public/libs/microxml/", "userspace/public/libs/uci/",
            "userspace/public/apps/icwmp/files/")
           + tuple("userspace/public/libs/libicwmp_dm/" + name for name in BDK_GLUE)
           + tuple("userspace/public/apps/icwmp/" + name for name in BDK_GLUE),
    # tests/host builds --with-sdk=mtk
    "bdk": ("feeds/", "docs/mtk/", "tests/host/"),
}


def git(*args):
    return subprocess.check_output(["git", "-C", str(HERE)] + list(args))


def excluded(name, sdk):
    for path in SDK_EXCLUDES.get(sdk, ()):
        if name == path or (path.endswith("/") and name.startswith(path)):
            return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path, help="tar.gz path, outside the repository")
    parser.add_argument("--sdk", choices=("all",) + tuple(SDK_EXCLUDES), default="all",
                        help="SDK of the bundle (default: all)")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == HERE or HERE in output.parents:
        parser.error("write the bundle outside the repository")
    if git("status", "--porcelain").strip():
        parser.error("commit or stash every change first: the bundle is HEAD")
    commit = git("rev-parse", "HEAD").decode().strip()

    with tempfile.TemporaryDirectory(prefix="icwmp-export-") as tmp:
        root = Path(tmp) / "tree"
        root.mkdir()
        with tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", "HEAD"))) as archive:
            archive.extractall(str(root))
        for name in ("MANIFEST.json", "SHA256SUMS"):
            (root / name).unlink()
        if args.sdk != "all":
            for path in sorted(root.rglob("*"), reverse=True):
                name = path.relative_to(root).as_posix() + ("/" if path.is_dir() else "")
                if path.exists() and excluded(name, args.sdk):
                    shutil.rmtree(str(path)) if path.is_dir() else path.unlink()
            applytool.prune_trees({"userspace": root / "userspace"}, args.sdk)
            sdks = [args.sdk]
        else:
            sdks = sorted(d.name for d in (root / "userspace/public/libs/libicwmp_dm/src/sdk").iterdir()
                          if (d / "sdk.mk").is_file())

        files = {p.relative_to(root).as_posix(): p for p in sorted(root.rglob("*")) if p.is_file()}
        digests = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}
        manifest = {"format": 1, "commit": commit, "sdks": sdks,
                    "layout": "full" if args.sdk == "all" else "sdk-only", "files": digests}
        payload = {name: path.read_bytes() for name, path in files.items()}
        payload["MANIFEST.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
        payload["SHA256SUMS"] = "".join(
            hashlib.sha256(data).hexdigest() + "  " + name + "\n"
            for name, data in sorted(payload.items())).encode()
        executable = {name for name, path in files.items() if path.stat().st_mode & 0o111}

        top = "icwmp_{}_{}".format(args.sdk, commit[:12])
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as gz:
            with tarfile.open(fileobj=gz, mode="w") as archive:
                for name, data in sorted(payload.items()):
                    info = tarfile.TarInfo(top + "/" + name)
                    info.size = len(data)
                    info.mode = 0o755 if name in executable else 0o644
                    info.mtime = 0
                    archive.addfile(info, io.BytesIO(data))

    print(output)
    print("commit:", commit, "sdks:", " ".join(sdks), "files:", len(files))
    print("sha256:", hashlib.sha256(output.read_bytes()).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
