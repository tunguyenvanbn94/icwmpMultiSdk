#!/usr/bin/env python3
"""Exercise apply on small SDK fixtures. --scratch is an empty task-owned directory."""

import argparse
import ast
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = ROOT.parents[5]


def run(bundle, *args, success=True):
    result = subprocess.run([str(bundle / "apply"), *map(str, args)], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if success and result.returncode:
        raise RuntimeError(result.stdout)
    if not success and not result.returncode:
        raise AssertionError("expected failure: " + result.stdout)
    return result.stdout


def snapshot(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file() and ".icwmp-backups" not in p.relative_to(root).parts}


def fixture(root, sdk):
    root.mkdir()
    if sdk == "bdk":
        source = WORKSPACE / "projects/brcm_ap_wifi7_mvn/src/bcm963xx"
        for name in ("make.common", "packages/common/mgmt/bcm_comp_md/comp_tr69_md.c",
                     "targets/MO77300EB/MO77300EB"):
            dest = root / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / name, dest)
        (root / "userspace/public/apps").mkdir(parents=True)
        legacy = root / "userspace/public/libs/libtr098"
    else:
        source = WORKSPACE / "projects/mtk_openwrt_wifi7/src/2025q3"
        name = "airoha_feeds/airoha_build/profile/HP2236B/config_7583"
        dest = root / name
        dest.parent.mkdir(parents=True)
        shutil.copy2(source / name, dest)
        (root / "tclinux_phoenix/apps/hni/cwmpclient/ext/openwrt").mkdir(parents=True)
        legacy = root / "tclinux_phoenix/apps/hni/libtr098"
    legacy.mkdir(parents=True)
    (legacy / "legacy-marker").write_text("old source must remain recoverable")
    return legacy


def make_test_bundle(dest):
    shutil.copytree(ROOT, dest / "userspace", ignore=shutil.ignore_patterns(".git", "release"))
    for source in (ROOT / "release").iterdir():
        if source.name in ("tests", "export.py"):
            continue
        if source.is_dir():
            shutil.copytree(source, dest / source.name)
        else:
            shutil.copy2(source, dest / source.name)
    files = {str(p.relative_to(dest)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in dest.rglob("*") if p.is_file()}
    (dest / "MANIFEST.json").write_text(json.dumps({"format": 1, "commit": "fixture-working-tree", "files": files}))


# apply.py runs on the SDK build hosts, which are older than this workspace.
# Two gates so a newer API cannot slip in again: an AST scan, and a run with
# the 3.9 pathlib methods removed.  (2026-09-24: Path.is_relative_to() reached
# a build host as an AttributeError traceback.)
POST36 = {
    "is_relative_to": "3.9", "with_stem": "3.9", "readlink": "3.9",
    "removeprefix": "3.9", "removesuffix": "3.9", "root_dir": "3.10",
    "link_to": "3.8", "missing_ok": "3.8", "dirs_exist_ok": "3.8",
    "capture_output": "3.7",
}
PATHLIB_39 = ("is_relative_to", "with_stem", "readlink")


def check_py36_source(path):
    found = set()
    tree = ast.parse(path.read_text(), str(path))
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute) and node.attr in POST36:
            found.add((node.lineno, node.attr, POST36[node.attr]))
        elif isinstance(node, ast.keyword) and node.arg in POST36:
            found.add((node.lineno, node.arg + "=", POST36[node.arg]))
        elif isinstance(node, ast.NamedExpr):
            found.add((node.lineno, ":=", "3.8"))
        elif node.__class__.__name__ == "Match":
            found.add((node.lineno, "match", "3.10"))
    if found:
        raise AssertionError("apply.py uses APIs newer than Python 3.6: " + repr(sorted(found)))


def run_as_py36(bundle, scratch, *args):
    """apply.py with the 3.9-only pathlib methods taken away."""
    shim = scratch / "py36shim.py"
    shim.write_text(
        "import pathlib, runpy, sys\n"
        "for cls in (pathlib.PurePath, pathlib.Path, pathlib.PurePosixPath, pathlib.PosixPath):\n"
        "    for name in %r:\n"
        "        if name in cls.__dict__:\n"
        "            delattr(cls, name)\n"
        "assert not hasattr(pathlib.Path('/'), 'is_relative_to')\n"
        "sys.argv = sys.argv[1:]\n"
        "runpy.run_path(sys.argv[0], run_name='__main__')\n" % (PATHLIB_39,))
    result = subprocess.run([sys.executable, str(shim), str(bundle / "apply.py"), *map(str, args)],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode:
        raise RuntimeError("apply.py needs a Python newer than 3.6:\n" + result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, help="Use an extracted committed release instead of source fixture")
    args = parser.parse_args()
    temp = args.scratch.resolve()
    temp.mkdir(parents=True, exist_ok=True)
    if any(temp.iterdir()):
        parser.error("scratch must be empty")
    bundle = temp / "bundle"
    if args.bundle:
        shutil.copytree(args.bundle, bundle)
    else:
        make_test_bundle(bundle)
    check_py36_source(bundle / "apply.py")
    results = []
    for sdk in ("bdk", "mtk"):
        target = temp / (sdk + " SDK's source")
        legacy = fixture(target, sdk)
        before = snapshot(target)
        run(bundle, "--sdk", sdk, "--dry-run", target)
        assert snapshot(target) == before and not (target / ".icwmp-backups").exists()
        run_as_py36(bundle, temp, "--sdk", sdk, "--dry-run", target)
        assert snapshot(target) == before and not (target / ".icwmp-backups").exists()
        run(bundle, target)  # autodetect
        assert not legacy.exists()
        backup = next((target / ".icwmp-backups").iterdir())
        saved = backup / "original" / legacy.relative_to(target) / "legacy-marker"
        assert saved.read_text() == "old source must remain recoverable"
        after = snapshot(target)
        assert "Already applied" in run(bundle, target)
        assert after == snapshot(target) and len(list((target / ".icwmp-backups").iterdir())) == 1
        assert json.loads((target / ".icwmp-release.json").read_text())["sdk"] == sdk
        if sdk == "bdk":
            config = (target / "targets/MO77300EB/MO77300EB").read_text()
            assert "BUILD_ICWMP=y\n" in config and "BUILD_TR69C_SSL=dynamic\n" in config
            assert "#ifdef SUPPORT_ICWMP" in (target / "packages/common/mgmt/bcm_comp_md/comp_tr69_md.c").read_text()
        else:
            config = (target / "airoha_feeds/airoha_build/profile/HP2236B/config_7583").read_text()
            for line in ("CONFIG_PACKAGE_libtr098=y", "CONFIG_PACKAGE_icwmp_tr098=y",
                         "CONFIG_PACKAGE_libmicroxml=y", "# CONFIG_PACKAGE_cwmpclient is not set"):
                assert line + "\n" in config
            assert (target / "tclinux_phoenix/apps/hni/libicwmp_dm/configure.ac").is_file()
        results.append("PASS " + sdk + ": current SDK profile, dry-run, install, backup, auto-detect, repeat/no-op")

    target = temp / "bdk-clean-profile"
    fixture(target, "bdk")
    profile = target / "targets/MO77300EB/MO77300EB"
    text = profile.read_text()
    start = text.index("BRCM_WEBUI_LANG=en_US\n") + len("BRCM_WEBUI_LANG=en_US\n")
    end = text.index("# BUILD_TR69_TR143 is not set\n", start)
    profile.write_text(text[:start] + "# MGMT_TR69C is not set\nMGMT_none=y\nBUILD_TR69C=\nBUILD_TR69C_SSL=\n" + text[end:])
    run(bundle, target)
    results.append("PASS BDK originally-disabled TR69 profile")

    target = temp / "bdk-bad-context"
    fixture(target, "bdk")
    file = target / "packages/common/mgmt/bcm_comp_md/comp_tr69_md.c"
    file.write_text(file.read_text().replace("if (shmId == UNINITIALIZED_SHM_ID)", "if (shmId == -123)"))
    before = snapshot(target)
    assert "unsupported" in run(bundle, target, success=False)
    assert snapshot(target) == before and not (target / ".icwmp-backups").exists()
    results.append("PASS unknown BDK context rejected before target writes")

    target = temp / "mtk-negative"
    fixture(target, "mtk")
    before = snapshot(target)
    payload = bundle / "userspace/public/libs/libicwmp_dm/src/README.md"
    original = payload.read_bytes()
    payload.write_bytes(original + b"tampered")
    assert "checksum mismatch" in run(bundle, target, success=False)
    payload.write_bytes(original)
    extra = bundle / "userspace/unlisted.c"
    extra.write_text("unlisted")
    assert "unlisted bundle payload" in run(bundle, target, success=False)
    extra.unlink()
    assert snapshot(target) == before
    outside = temp / "outside"
    outside.mkdir()
    (target / ".icwmp-backups").symlink_to(outside)
    assert "symlink" in run(bundle, target, success=False)
    assert not list(outside.iterdir())
    results.append("PASS modified/unlisted payload and target symlink rejected")

    spec = importlib.util.spec_from_file_location("apply_tool", bundle / "apply.py")
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    target = temp / "rollback"
    target.mkdir()
    (target / "a").write_bytes(b"old-a")
    (target / "b").write_bytes(b"old-b")
    count = 0

    def fail_fourth(src, dst):
        nonlocal count
        count += 1
        if count == 4:
            raise OSError("injected I/O failure")
        return os.replace(src, dst)

    try:
        tool.execute(target, {}, {"a": b"new-a", "b": b"new-b"}, [], replace=fail_fourth)
        raise AssertionError("failure not injected")
    except OSError as error:
        assert "injected" in str(error)
    assert (target / "a").read_bytes() == b"old-a" and (target / "b").read_bytes() == b"old-b"
    journal = next((target / ".icwmp-backups").glob("*/journal.json"))
    assert json.loads(journal.read_text())["state"] == "rolled_back"
    results.append("PASS injected write failure restores both managed files")
    print("\n".join(results))
    print("NOT RUN: SDK compiler/link, board behavior")
    (temp / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
