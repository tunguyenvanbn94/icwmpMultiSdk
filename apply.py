#!/usr/bin/env python3
"""Install the icwmp overlay and SDK build integration, with preflight and backup."""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent

# --sdk-only: files outside sdk/<name>/ that only one SDK uses.
SDK_ONLY_FILES = {"bdk": ("BDK-CHANGES.md",)}

# The SDK build hosts are older than this workspace: keep to what Python 3.6
# has.  Path.is_relative_to() is 3.9 and was the one thing that slipped
# through -- see within() below.
if sys.version_info < (3, 6):
    sys.exit("apply.py needs Python 3.6 or newer, found %d.%d"
             % (sys.version_info[0], sys.version_info[1]))


def within(path, other):
    """path is other or lies under it.  Path.is_relative_to() without 3.9."""
    try:
        path.relative_to(other)
        return True
    except ValueError:
        return False


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_target(root, relative):
    path = root / relative
    current = root
    for part in Path(relative).parts:
        if part in (".", ".."):
            raise ValueError("invalid target path: " + relative)
        current /= part
        if current.is_symlink():
            raise ValueError("refusing symlink in managed target: " + str(current))
    return path


def read_text(path):
    if not path.is_file():
        raise ValueError("required SDK file missing: " + str(path))
    return path.read_text()


def config_values(text, values):
    """Change only explicitly managed build options, preserving unrelated config."""
    for key, value in values.items():
        pattern = re.compile(r"^(?:" + re.escape(key) + r"=.*|# " + re.escape(key) + r" is not set)$", re.M)
        matches = list(pattern.finditer(text))
        if len(matches) > 1:
            raise ValueError("duplicate build option: " + key)
        replacement = key + "=" + value if value is not None else "# " + key + " is not set"
        if matches:
            text = pattern.sub(lambda _: replacement, text)
        else:
            text = text.rstrip("\n") + "\n" + replacement + "\n"
    return text


def bdk_hunks(text):
    """Exact before/after source contexts, with whitespace preserved as JSON data."""
    result = json.loads(text)
    expected = {"make.common", "packages/common/mgmt/bcm_comp_md/comp_tr69_md.c"}
    if set(result) != expected:
        raise ValueError("invalid BDK integration paths")
    for pair in result.values():
        if not isinstance(pair, list) or len(pair) != 2 or not all(isinstance(s, str) and s for s in pair):
            raise ValueError("invalid BDK integration context")
    return result


def integrate(text, before, after, name):
    if text.count(after) == 1:
        return text
    if text.count(before) != 1:
        raise ValueError("unsupported or partially modified BDK integration: " + name)
    return text.replace(before, after, 1)


def verify_bundle(here):
    manifest = here / "MANIFEST.json"
    if not manifest.is_file():
        raise ValueError("MANIFEST.json missing, use an exported release tarball")
    data = json.loads(manifest.read_text())
    if data.get("format") != 1 or not data.get("files") or not data.get("commit"):
        raise ValueError("invalid release manifest")
    for name, expected in data["files"].items():
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("invalid manifest path: " + name)
        path = here / relative
        if not path.is_file() or path.is_symlink() or digest(path) != expected:
            raise ValueError("bundle checksum mismatch: " + name)
    # Unlisted source files must not be silently copied into an SDK.
    for folder in ("userspace", "feeds"):
        for path in (here / folder).rglob("*"):
            if path.is_symlink() or (path.is_file() and str(path.relative_to(here)) not in data["files"]):
                raise ValueError("unlisted bundle payload: " + str(path))
    return data


def sdk_roots(tree):
    """Component roots inside a copied tree: the directories holding tools/sdk-scan.sh."""
    return [path.parent.parent for path in sorted(tree.rglob("tools/sdk-scan.sh"))]


INCLUDE_RE = re.compile(r'#\s*include\s*[<"](?:icwmp_dm/|libtr098/)?([^>"]+)[>"]')
SOURCE_RE = re.compile(r'([\w./-]+\.[ch])\b')


def unused_portable_sources(lib_root, trees):
    """Files of the library's tr098/ that what is left no longer uses: not
    named by bin/Makefile.am or a remaining sdk/<name>/sdk.mk, and not included,
    directly or through another kept file, by the remaining sources of any tree
    (the app includes <icwmp_dm/...>).  Matching by file name keeps a file when
    in doubt."""
    portable = [p for p in sorted((lib_root / "tr098").rglob("*")) if p.is_file()]
    named = set()
    for mk in [lib_root / "bin" / "Makefile.am"] + sorted((lib_root / "sdk").glob("*/sdk.mk")):
        named |= {Path(m).name for m in SOURCE_RE.findall(mk.read_text(errors="replace"))}
    keep = {p for p in portable if p.name in named}
    todo = list(keep) + [p for tree in trees for p in sorted(tree.rglob("*"))
                         if p.suffix in (".c", ".h") and p.is_file() and p not in portable]
    seen = set()
    while todo:
        path = todo.pop()
        if path in seen:
            continue
        seen.add(path)
        for include in INCLUDE_RE.findall(path.read_text(errors="replace")):
            for candidate in portable:
                if candidate.name == Path(include).name and candidate not in keep:
                    keep.add(candidate)
                    todo.append(candidate)
    return [p for p in portable if p not in keep]


def prune_trees(trees, sdk):
    """Keep only the code of one SDK in trees ({label: directory}, edited in
    place): every component root drops its other sdk/<name>/ and regenerates
    sdk/enabled.* the way tools/sdk-prune.sh does; then the library drops the
    tr098/ sources nothing left uses, and files only another SDK uses go."""
    for label, tree in sorted(trees.items()):
        for root in sdk_roots(tree):
            names = sorted(d.name for d in (root / "sdk").iterdir() if (d / "sdk.mk").is_file())
            if sdk not in names:
                raise ValueError("no sdk/{} in {}".format(sdk, label))
            for name in names:
                if name != sdk:
                    shutil.rmtree(str(root / "sdk" / name))
                    print("Drop:", label + "/" + str((root / "sdk" / name).relative_to(tree)))
            subprocess.run(["sh", "tools/sdk-scan.sh"], cwd=str(root), check=True,
                           stdout=subprocess.DEVNULL)
    for label, tree in sorted(trees.items()):
        for root in sdk_roots(tree):
            if (root / "tr098").is_dir():
                for path in unused_portable_sources(root, list(trees.values())):
                    path.unlink()
                    print("Drop:", label + "/" + str(path.relative_to(tree)))
        for other, files in SDK_ONLY_FILES.items():
            if other == sdk:
                continue
            for name in files:
                for path in sorted(tree.rglob(name)):
                    path.unlink()
                    print("Drop:", label + "/" + str(path.relative_to(tree)))


def prune_copies(copies, sdk, workdir):
    """--sdk-only: copy every source tree that has sdk/<name>/ directories to
    workdir and prune the copies with prune_trees().  The bundle itself stays
    untouched; the SDK gets the pruned copies."""
    pruned = {}
    for index, (relative, source_dir) in enumerate(sorted(copies.items())):
        if not sdk_roots(source_dir):
            pruned[relative] = source_dir
            continue
        dest = workdir / str(index)
        shutil.copytree(str(source_dir), str(dest))
        pruned[relative] = dest
    prune_trees({relative: path for relative, path in pruned.items() if within(path, workdir)}, sdk)
    return pruned


def source_commit(here, metadata):
    """The commit being installed: git HEAD when apply runs from a checkout of
    the repository ("-dirty" with uncommitted changes), else the commit the
    export wrote into MANIFEST.json.  The MANIFEST.json kept in the repository
    names the old overlay baseline, not the checkout (K19)."""
    if (here / ".git").exists():
        try:
            run = lambda *args: subprocess.run(["git", "-C", str(here)] + list(args), check=True,
                                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                               universal_newlines=True).stdout.strip()
            return run("rev-parse", "HEAD") + ("-dirty" if run("status", "--porcelain") else "")
        except (OSError, subprocess.CalledProcessError):
            pass
    return metadata["commit"]


def plan(here, target, sdk, profile, metadata, layout="full"):
    """Return directory copies, exact file replacements and legacy removals."""
    if not re.fullmatch(r"[A-Za-z0-9_-]+", profile):
        raise ValueError("invalid profile name")
    copies, edits, removals = {}, {}, []
    source = here / "userspace"
    if sdk == "bdk":
        if not (target / "userspace/public/apps").is_dir():
            raise ValueError("not a BDK source root: " + str(target))
        for name, (before, after) in bdk_hunks(read_text(here / "bdk-integration.json")).items():
            edits[name] = integrate(read_text(checked_target(target, name)), before, after, name).encode()
        profile_path = "targets/{0}/{0}".format(profile)
        values = {"MGMT_TR69C": "y", "MGMT_none": None, "MGMT_TR69C_NO_SSL": None,
                  "MGMT_TR69C_SSL": "y", "BUILD_TR69C": "dynamic", "BUILD_TR69C_SSL": "dynamic",
                  "BUILD_ICWMP": "y"}
        edits[profile_path] = config_values(read_text(checked_target(target, profile_path)), values).encode()
        for name in ("public/libs/microxml", "public/libs/uci", "public/libs/libicwmp_dm", "public/apps/icwmp"):
            copies["userspace/" + name] = source / name
        removals.append("userspace/public/libs/libtr098")
    else:
        hni = "tclinux_phoenix/apps/hni/"
        # Existing product scripts/config are used by the migration feed.
        if not (target / hni / "cwmpclient/ext/openwrt").is_dir():
            raise ValueError("MTK product cwmpclient/ext/openwrt missing")
        profile_path = "airoha_feeds/airoha_build/profile/{}/config_7583".format(profile)
        values = {"CONFIG_PACKAGE_libtr098": "y", "CONFIG_PACKAGE_icwmp_tr098": "y",
                  "CONFIG_PACKAGE_libmicroxml": "y", "CONFIG_PACKAGE_cwmpclient": None}
        edits[profile_path] = config_values(read_text(checked_target(target, profile_path)), values).encode()
        copies[hni + "libicwmp_dm"] = source / "public/libs/libicwmp_dm/src"
        copies[hni + "icwmp_tr098"] = source / "public/apps/icwmp/icwmp"
        for name in ("libtr098", "icwmp_tr098"):
            relative = "airoha_feeds/package/airoha/apps/{}/Makefile".format(name)
            edits[relative] = (here / "feeds" / name / "Makefile").read_bytes()
        removals.append(hni + "libtr098")
    for relative, source_dir in copies.items():
        if not source_dir.is_dir():
            raise ValueError("source missing: " + str(source_dir))
    for relative in set(copies) | set(edits) | set(removals):
        checked_target(target, relative)
    marker = {"format": 1, "commit": metadata["commit"], "sdk": sdk, "profile": profile, "layout": layout}
    edits[".icwmp-release.json"] = (json.dumps(marker, indent=2) + "\n").encode()
    checked_target(target, ".icwmp-release.json")
    checked_target(target, ".icwmp-backups")
    return copies, edits, removals


def tree_equal(source, target):
    if not target.is_dir():
        return False
    left = {p.relative_to(source) for p in source.rglob("*") if p.is_file()}
    right = {p.relative_to(target) for p in target.rglob("*") if p.is_file()}
    if left != right or any(p.is_symlink() for p in target.rglob("*")):
        return False
    return all((source / p).read_bytes() == (target / p).read_bytes() and
               bool((source / p).stat().st_mode & 0o111) == bool((target / p).stat().st_mode & 0o111)
               for p in left)


def execute(target, copies, edits, removals, replace=os.replace):
    """Stage everything before writes. Roll back completed writes on process error."""
    changes = [(name, "directory", src) for name, src in copies.items() if not tree_equal(src, target / name)]
    changes += [(name, "file", data) for name, data in edits.items()
                if not (target / name).is_file() or (target / name).read_bytes() != data]
    changes += [(name, "remove", None) for name in removals if (target / name).exists()]
    if not changes:
        print("Already applied, source/profile unchanged.")
        return None
    backups = target / ".icwmp-backups"
    backups.mkdir(exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime("%Y%m%d-%H%M%S-"), dir=backups))
    print("Preparing backup:", backup)
    stage = backup / "staging"
    stage.mkdir()
    journal = {"state": "preparing", "entries": []}
    journal_path = backup / "journal.json"

    def save():
        journal_path.write_text(json.dumps(journal, indent=2) + "\n")

    save()
    for index, (name, kind, value) in enumerate(changes):
        staged = stage / str(index)
        if kind == "directory":
            shutil.copytree(value, staged)
        elif kind == "file":
            staged.write_bytes(value)
            staged.chmod((target / name).stat().st_mode & 0o777 if (target / name).exists() else 0o644)
    journal["state"] = "applying"
    save()
    try:
        for index, (name, kind, _) in enumerate(changes):
            dest = checked_target(target, name)
            entry = {"path": name, "had_original": dest.exists(), "saved": False, "installed": False}
            journal["entries"].append(entry)
            save()
            if entry["had_original"]:
                original = backup / "original" / name
                original.parent.mkdir(parents=True, exist_ok=True)
                replace(dest, original)
                entry["saved"] = True
                save()
            if kind != "remove":
                dest.parent.mkdir(parents=True, exist_ok=True)
                replace(stage / str(index), dest)
                entry["installed"] = True
                save()
            print("Applied:", name)
    except (Exception, KeyboardInterrupt):
        for entry in reversed(journal["entries"]):
            dest = target / entry["path"]
            if entry["installed"]:
                shutil.rmtree(dest) if dest.is_dir() else dest.unlink()
            if entry["saved"]:
                os.replace(backup / "original" / entry["path"], dest)
        journal["state"] = "rolled_back"
        save()
        print("Apply failed, restored original managed paths. Backup:", backup, file=sys.stderr)
        raise
    journal["state"] = "complete"
    save()
    shutil.rmtree(stage)
    print("Backup:", backup)
    return backup


def build_commands(sdk, profile):
    if sdk == "bdk":
        return ["make -C userspace/public/libs/microxml -f Bcmbuild.mk",
                "make -C userspace/public/libs/uci",
                "make -C userspace/public/libs/libicwmp_dm -f Bcmbuild.mk clean",
                "make -C userspace/public/libs/libicwmp_dm -f Bcmbuild.mk",
                "make -C userspace/public/apps/icwmp -f Bcmbuild.mk clean",
                "make -C userspace/public/apps/icwmp -f Bcmbuild.mk",
                "make PROFILE=" + profile]
    return ["./airoha_script/airoha-compile.sh -c 7583 -f -m " + profile + " -w Griffin_logan",
            "cd openwrt-21.02/openwrt-21.02.1_dev", "make -j 16 MSDK=1 V=s"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", type=Path, help="SDK build source root")
    parser.add_argument("--sdk", choices=("bdk", "mtk"), help="Auto-detected if omitted")
    parser.add_argument("--profile", help="Defaults: MO77300EB for BDK, HP2236B for MTK")
    parser.add_argument("--dry-run", action="store_true", help="Validate and show changes without writing")
    parser.add_argument("--sdk-only", action="store_true",
                        help="Install only the code of the selected SDK: drop the other sdk/<name>/ "
                             "directories of libicwmp_dm and icwmp, and files only another SDK uses")
    args = parser.parse_args()
    workdir = None
    try:
        metadata = verify_bundle(HERE)
        target = args.target.resolve(strict=True)
        if HERE == target or within(HERE, target) or within(target, HERE):
            raise ValueError("extract the bundle outside the SDK target directory")
        sdk = args.sdk
        if not sdk:
            found = []
            if (target / "make.common").is_file():
                found.append("bdk")
            if (target / "airoha_feeds").is_dir():
                found.append("mtk")
            if len(found) != 1:
                raise ValueError("cannot detect SDK unambiguously, use --sdk")
            sdk = found[0]
        profile = args.profile or ("MO77300EB" if sdk == "bdk" else "HP2236B")
        carried = metadata.get("sdks")
        if carried and sdk not in carried:
            raise ValueError("this bundle carries only SDK: " + ", ".join(carried))
        layout = "sdk-only" if args.sdk_only or carried == [sdk] else "full"
        metadata = dict(metadata, commit=source_commit(HERE, metadata))
        copies, edits, removals = plan(HERE, target, sdk, profile, metadata, layout)
        print("Release:", metadata["commit"], "SDK:", sdk, "profile:", profile, "layout:", layout)
        print("Target:", target)
        if args.sdk_only:
            workdir = Path(tempfile.mkdtemp(prefix="icwmp-apply-"))
            copies = prune_copies(copies, sdk, workdir)
        for relative in sorted(set(copies) | set(edits)):
            print("Manage:", relative)
        for relative in removals:
            if (target / relative).exists():
                print("Archive legacy:", relative)
        if args.dry_run:
            print("DRY RUN: no target files changed.")
        else:
            execute(target, copies, edits, removals)
        print("\nBuild next, from SDK root with its toolchain environment:")
        print("\n".join(build_commands(sdk, profile)))
        if sdk == "mtk":
            # airoha-compile.sh calls airoha-feeds-prepare.sh, which runs
            # "feeds update airoha" -- so the full build above is fine.  A
            # single-package build is not: feeds.conf.default uses src-cpy, so
            # feeds/airoha holds a COPY of the two package Makefiles this just
            # rewrote, and the build keeps the old DEPENDS until refreshed.
            # Refresh only the two Makefiles: "feeds update airoha" re-copies
            # the whole airoha feed and broke a later "make MSDK=1" image
            # build on the user's tree (2026-09-26).  package/feeds/airoha/<p>
            # already links into feeds/airoha, so no "feeds install" either.
            print("\nBuilding one package instead of the whole image?  The airoha feed")
            print("is src-cpy (a copy); refresh ONLY the two package Makefiles:")
            print("cd openwrt-21.02/openwrt-21.02.1_dev")
            print("for p in libtr098 icwmp_tr098; do cmp -s ../../airoha_feeds/package/airoha/apps/$p/Makefile"
                  " feeds/airoha/package/airoha/apps/$p/Makefile || cp -v ../../airoha_feeds/package/airoha/apps/$p/Makefile"
                  " feeds/airoha/package/airoha/apps/$p/Makefile; done")
            print("make package/libtr098/{clean,compile} V=sc -j1")
            print("make package/icwmp_tr098/{clean,compile} V=sc -j1")
        print("\nApply does not run the compiler or flash a board.")
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("ERROR:", error, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Interrupted, see backup journal if apply had started.", file=sys.stderr)
        return 130
    finally:
        if workdir:
            shutil.rmtree(str(workdir), ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
