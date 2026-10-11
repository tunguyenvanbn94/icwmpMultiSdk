#!/usr/bin/env python3
"""Read-only multi-SDK architecture/release preflight for icwmpMultiSdk.

The guard is intentionally advisory by default: a detection is not proof of
an incorrect runtime behavior. --strict fails on severity ERROR after triage.
Only standard-library dependencies (Python >=3.8).
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import re
import sys
from pathlib import Path
from typing import Iterable, List, Optional


@dataclasses.dataclass(frozen=True)
class Finding:
    code: str
    severity: str
    file: str
    line: int
    detail: str
    action: str

    def as_dict(self):
        return dataclasses.asdict(self)


LIB_REL = Path("userspace/public/libs/libicwmp_dm/src")
APP_REL = Path("userspace/public/apps/icwmp/icwmp")


def _read_lines(path: Path) -> List[str]:
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []


def _without_c_comments(lines: Iterable[str]) -> Iterable[str]:
    """Strip C line/block comments while preserving line numbers.

    This is a lightweight static checker, not a full C preprocessor. Treat
    string/character literals as opaque so URL fragments are not comments.
    """
    in_block = False
    for line in lines:
        parts = []
        i = 0
        quote = None
        while i < len(line):
            ch = line[i]
            nxt = line[i + 1] if i + 1 < len(line) else ""
            if in_block:
                if ch == "*" and nxt == "/":
                    in_block = False
                    i += 2
                else:
                    i += 1
                continue
            if quote is not None:
                parts.append(ch)
                if ch == "\\" and i + 1 < len(line):
                    parts.append(line[i + 1])
                    i += 2
                    continue
                if ch == quote:
                    quote = None
                i += 1
                continue
            if ch in ('"', "'"):
                quote = ch
                parts.append(ch)
                i += 1
                continue
            if ch == "/" and nxt == "/":
                break
            if ch == "/" and nxt == "*":
                in_block = True
                i += 2
                continue
            parts.append(ch)
            i += 1
        yield "".join(parts)


def _relative(path: Path, root: Path) -> str:
    return str(path.relative_to(root)).replace("\\", "/")


def inspect(root: Path, required_sdks: Optional[List[str]] = None) -> List[Finding]:
    root = root.resolve()
    findings: List[Finding] = []

    def add(code: str, severity: str, path: Path, line: int, detail: str, action: str):
        findings.append(Finding(code, severity, _relative(path, root), line, detail, action))

    lib_sdk = root / LIB_REL / "sdk"
    app_sdk = root / APP_REL / "sdk"
    if not lib_sdk.is_dir():
        add("LIB_SDK_NOT_FOUND", "ERROR", root, 0, "Library SDK directory is missing", "Inspect repository root and source layout")
    if not app_sdk.is_dir():
        add("APP_SDK_NOT_FOUND", "ERROR", root, 0, "Application SDK directory is missing", "Inspect repository root and source layout")

    lib_sdks = {p.name for p in lib_sdk.iterdir() if p.is_dir()} if lib_sdk.is_dir() else set()
    app_sdks = {p.name for p in app_sdk.iterdir() if p.is_dir()} if app_sdk.is_dir() else set()
    for name in sorted(lib_sdks | app_sdks):
        if name not in lib_sdks or name not in app_sdks:
            add("SDK_PAIR_MISMATCH", "ERROR", root, 0, f"SDK '{name}' exists only in {'lib' if name in lib_sdks else 'app'}", "Provide SDK hooks on both library and app sides or remove stale SDK")

    for name in sorted(set(required_sdks or [])):
        if name not in lib_sdks or name not in app_sdks:
            add("REQUIRED_SDK_MISSING", "ERROR", root, 0, f"Required SDK '{name}' is not present in both SDK directories", "Verify multi-SDK source vs intentionally pruned release")

    # Check production build flags. The macro is *not* removed automatically,
    # because its build-time use and product policy must be inspected first.
    bdk_sdk_mk = lib_sdk / "bdk" / "sdk.mk"
    for idx, line in enumerate(_read_lines(bdk_sdk_mk), start=1):
        active = line.split("#", 1)[0]
        if re.search(r"(?:^|\s)-DBDK_SAMPLE_OBJECT(?:\s|$|=)", active):
            add("BDK_SAMPLE_IN_BUILD", "ERROR", bdk_sdk_mk, idx,
                "BDK_SAMPLE_OBJECT is enabled in SDK compile flags",
                "Move behind an explicit non-production debug/developer build profile")

    shared_tr098 = root / LIB_REL / "tr098"
    if shared_tr098.is_dir():
        for path in sorted(shared_tr098.rglob("*.c")):
            for idx, line in enumerate(_without_c_comments(_read_lines(path)), start=1):
                if re.search(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b.*\bDM_PLATFORM_(?:BDK|MTK|UCI)\b", line):
                    add("SDK_MACRO_IN_SHARED_MODEL", "WARN", path, idx,
                        "Shared TR-098 binding branches on SDK name",
                        "Move platform behavior to service/SDK hook after verifying semantics")

    entry = root / LIB_REL / "dmentry.c"
    for idx, line in enumerate(_without_c_comments(_read_lines(entry)), start=1):
        if "dm_platform_select_root(" in line:
            add("ROOT_SELECTED_BY_SDK_HOOK", "WARN", entry, idx,
                "DM runtime still references platform root selection",
                "Review central model resolver and fail-fast unsupported-model behavior")

    # Explicitly note the existing product coupling; no automatic rewrite.
    mtk_dm = lib_sdk / "mtk" / "dm098"
    if mtk_dm.is_dir():
        extensions = [p for p in mtk_dm.glob("*.c") if re.search(r"(?:ais|hni|operator)", p.name, re.I)]
        if extensions:
            p = sorted(extensions)[0]
            add("PRODUCT_MODULE_IN_PLATFORM", "INFO", p, 0,
                f"{len(extensions)} platform C source(s) look operator/product-specific",
                "Classify by product capability and migrate only with ACS parity tests")

    host_runner = root / "tests" / "host" / "run.sh"
    if not host_runner.is_file():
        add("HOST_RUNNER_NOT_FOUND", "WARN", root, 0,
            "Expected host test entrypoint is absent",
            "Confirm host test contract and document alternate test command")

    # Stable ordering for diff-friendly saved reports.
    return sorted(findings, key=lambda x: (x.severity != "ERROR", x.file, x.line, x.code))


def markdown_report(root: Path, issues: List[Finding]) -> str:
    counts = {s: sum(x.severity == s for x in issues) for s in ("ERROR", "WARN", "INFO")}
    lines = [
        "# ICWMP Multi-SDK preflight",
        "",
        "Repository: `.` (repository-relative report)",
        "",
        f"Findings: **{counts['ERROR']} errors / {counts['WARN']} warnings / {counts['INFO']} information**",
        "",
        "Automated static observations only. Review each finding before code changes.",
        "",
        "| Severity | Rule | Location | Observation | Recommended next action |",
        "|---|---|---|---|---|",
    ]
    for x in issues:
        def esc(s: str) -> str:
            return s.replace("|", "\\|").replace("\n", " ")
        loc = f"`{x.file}`" + (f":{x.line}" if x.line else "")
        lines.append(f"| {x.severity} | `{x.code}` | {loc} | {esc(x.detail)} | {esc(x.action)} |")
    if not issues:
        lines.append("| — | — | — | No static findings | Continue host/board verification |")
    lines.append("")
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo", type=Path, default=Path.cwd(), help="Path to icwmpMultiSdk checkout")
    ap.add_argument("--require-sdk", action="append", default=[], help="SDK expected in the full source tree; repeat as needed")
    ap.add_argument("--json-out", type=Path, help="Write machine-readable findings")
    ap.add_argument("--md-out", type=Path, help="Write Markdown report")
    ap.add_argument("--strict", action="store_true", help="Exit 2 on ERROR findings (release/CI gate)")
    args = ap.parse_args(argv)
    issues = inspect(args.repo, args.require_sdk)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps({"root": ".", "findings": [x.as_dict() for x in issues]}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    text = markdown_report(args.repo.resolve(), issues)
    if args.md_out:
        args.md_out.parent.mkdir(parents=True, exist_ok=True)
        args.md_out.write_text(text, encoding="utf-8")
    print(text)
    return 2 if args.strict and any(x.severity == "ERROR" for x in issues) else 0


if __name__ == "__main__":
    sys.exit(main())
