#!/usr/bin/env python3
"""Index of the standard TR-181 names (objects, parameters, type, access) taken
from the Broadcom BDK data model XML, for checking the MTK TR-181 mapping:

    tr181-schema.py <bcm963xx>/data-model                   # TSV on stdout
    tr181-schema.py <bcm963xx>/data-model --check <file>   # every Device.* path of <file>

Only entries with specSource="TR181" are kept: the BBF names.  Vendor entries
(X_BROADCOM_COM_*, X_MARUSYS_COM_*) and the SDK's defaults/profiles are left
out, so nothing of the proprietary SDK ends up in this (public) repository;
the XML is read at run time.  The SDK keeps its TR-181 tree under
"InternetGatewayDevice.Device." (hybrid layout); the prefix is dropped here.

Columns: kind (obj|param), path ("{i}" for instances), type, access
(ReadWrite|ReadOnly|NotSupported, the SDK's supportLevel; for an object
"Present"/"NotSupported"), profile.

--check prints each Device.* path of <file> (TSV or text, instance numbers
normalised to {i}) that the index does not know, then a count; exit 1 if any.
A path the SDK marks NotSupported is still a real TR-181 name and passes."""
import os, re, sys

ATTR = re.compile(r'(\w+)="([^"]*)"')


def norm(p):
    p = re.sub(r"\.\d+\.", ".{i}.", p)
    p = re.sub(r"\.\d+\.", ".{i}.", p)
    return re.sub(r"\.\d+$", ".{i}", p)


def load(d):
    out = {}
    for f in sorted(os.listdir(d)):
        if not f.endswith(".xml"):
            continue
        obj = None
        for line in open(os.path.join(d, f), encoding="utf-8", errors="replace"):
            s = line.strip()
            if s.startswith("<object "):
                a = dict(ATTR.findall(s))
                name = a.get("name", "")
                obj = None
                if a.get("specSource") != "TR181":
                    continue
                for pre in ("InternetGatewayDevice.Device.", "Device."):
                    if name.startswith(pre):
                        obj = "Device." + name[len(pre):]
                        break
                if obj:
                    out[obj] = ("obj", "", a.get("supportLevel", ""), a.get("profile", ""))
            elif s.startswith("<parameter ") and obj:
                a = dict(ATTR.findall(s))
                if a.get("specSource") != "TR181":
                    continue
                out[obj + a.get("name", "")] = ("param", a.get("type", ""), a.get("supportLevel", ""),
                                                a.get("profile", ""))
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    idx = load(sys.argv[1])
    if len(sys.argv) == 4 and sys.argv[2] == "--check":
        seen, bad = set(), 0
        for line in open(sys.argv[3], encoding="utf-8", errors="replace"):
            for p in re.findall(r"\bDevice\.[A-Za-z0-9_.{}]+", line):
                if "InternetGatewayDevice" in line[:line.find(p)][-25:]:
                    continue
                n = norm(p)
                if n in seen or n.split(".")[-1].startswith("X_") or ".X_" in n:
                    continue
                seen.add(n)
                if n not in idx and n + "." not in idx:
                    print("unknown:", n)
                    bad += 1
        print("%d Device.* names checked, %d unknown to the TR-181 index" % (len(seen), bad))
        return 1 if bad else 0
    for p in sorted(idx):
        k, t, acc, prof = idx[p]
        print("%s\t%s\t%s\t%s\t%s" % (k, p, t, acc, prof))
    return 0


if __name__ == "__main__":
    sys.exit(main())
