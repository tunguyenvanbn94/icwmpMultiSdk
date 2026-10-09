#!/usr/bin/env python3
"""The TR-181 tree of a "tr069 dm" dump against the Broadband Forum data model:

    tr181-bbf-check.py <bbf-dir> <names.json> [<values.json>] [--list]

<bbf-dir> holds the BBF CWMP "full" XML files, read at run time and never
copied into this repository: tr-181-2-<v>-cwmp-full.xml (Device:2) and the
service models mounted under Device.Services. (tr-135-*-cwmp-full.xml
STBService, tr-140-*-cwmp-full.xml StorageService), from
https://cwmp-data-models.broadband-forum.org/.  <names.json> is the output of
"ubus call tr069 dm '{"cmd":"names","path":"Device.","next_level":false}'",
<values.json> of '{"cmd":"get","path":"Device."}' (types).

Every parameter of the dump is sorted into:
  std        a BBF name
  vendor     a vendor extension: an object or parameter whose name starts
             with X_<id>_ (TR-106 section 3.3; the product's X_AIS_ included)
  unknown    neither -- a name the standard does not have
and for the std ones the differences are listed:
  access     writable here, readOnly in the standard, or the other way
  type       the value type differs (xsd type of the dump vs the BBF syntax)
  status     the standard marks it deprecated / obsoleted / deleted
Exit 1 when there is an unknown name, an access difference or a deleted /
obsoleted name; types and deprecated names are reported, not judged.
--list prints every name of each class, not only the counts.
"""
import glob, json, os, re, sys
import xml.etree.ElementTree as ET

VENDOR = re.compile(r"(^|\.)X_[A-Za-z0-9-]+_")
TYPES = ("string", "unsignedInt", "int", "unsignedLong", "long", "boolean",
         "dateTime", "base64", "hexBinary", "decimal")


def norm(p):
    p = re.sub(r"\.\d+\.", ".{i}.", p)
    p = re.sub(r"\.\d+\.", ".{i}.", p)
    return re.sub(r"\.\d+$", ".{i}", p)


def strip_ns(t):
    return t.split("}", 1)[-1]


def syntax_type(param, datatypes):
    for s in param:
        if strip_ns(s.tag) != "syntax":
            continue
        # a list goes on the wire as one xsd:string, whatever its items are
        if any(strip_ns(t.tag) == "list" for t in s):
            return "string"
        for t in s:
            tag = strip_ns(t.tag)
            if tag in TYPES:
                return tag
            if tag == "dataType":
                base = t.get("ref") or t.get("base") or ""
                return datatypes.get(base, base)
    return ""


def load_model(path, mount):
    """name -> (type, access, status) of the first <model> of path; service
    models (isService) mounted under mount"""
    tree = ET.parse(path)
    root = tree.getroot()
    datatypes = {}
    for d in root.iter():
        if strip_ns(d.tag) == "dataType" and d.get("name"):
            base = d.get("base", "")
            for t in d:
                if strip_ns(t.tag) in TYPES:
                    base = strip_ns(t.tag)
            datatypes[d.get("name")] = base
    # resolve chains (IPAddress -> IPv4Address ... -> string)
    for _ in range(5):
        for k, v in list(datatypes.items()):
            if v in datatypes:
                datatypes[k] = datatypes[v]
    out = {}
    for model in root.iter():
        if strip_ns(model.tag) != "model":
            continue
        prefix = mount if model.get("isService") == "true" else ""
        for obj in model:
            if strip_ns(obj.tag) != "object":
                continue
            oname = prefix + obj.get("name", "")
            ostatus = obj.get("status", "current")
            out[oname] = ("object", obj.get("access", "readOnly"), ostatus)
            for p in obj:
                if strip_ns(p.tag) != "parameter":
                    continue
                st = p.get("status", "current")
                if ostatus != "current" and st == "current":
                    st = ostatus
                out[oname + p.get("name", "")] = (syntax_type(p, datatypes), p.get("access", "readOnly"), st)
        break
    return out


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        sys.exit(__doc__)
    listing = "--list" in sys.argv
    std = {}
    files = sorted(glob.glob(os.path.join(a[0], "tr-181-*-cwmp-full.xml")))
    if not files:
        sys.exit("no tr-181-*-cwmp-full.xml in %s" % a[0])
    std.update(load_model(files[-1], ""))
    for pat in ("tr-135-*-cwmp-full.xml", "tr-140-*-cwmp-full.xml"):
        f = sorted(glob.glob(os.path.join(a[0], pat)))
        if f:
            std.update(load_model(f[-1], "Device.Services."))
    names = json.load(open(a[1]))["parameters"]
    types = {}
    if len(a) > 2:
        types = {p["parameter"]: p.get("type", "") for p in json.load(open(a[2]))["parameters"]}
    seen, cls = {}, {"std": [], "vendor": [], "unknown": []}
    diff = {"access": [], "type": [], "status": []}
    for p in names:
        n = p["parameter"]
        if n.endswith("."):
            continue
        k = norm(n)
        if k in seen:
            continue
        w = p.get("writable") in ("1", True)
        seen[k] = 1
        if k not in std:
            # the first segment the standard does not know must be a
            # vendor name X_<id>_...: Device.UserInterface.CarrierLocking.X_AIS_A
            # is not (CarrierLocking), Device.UserInterface.X_AIS_B.C is
            segs, path, first = k.split("."), "", None
            for i, s in enumerate(segs):
                path += s if i == len(segs) - 1 else s + "."
                # a table is named X.{i}. only: X. is known when X.{i}. is
                if path not in std and path + "{i}." not in std:
                    first = s
                    break
            if first is not None and re.match(r"X_[A-Za-z0-9-]+_", first):
                cls["vendor"].append(k)
            else:
                cls["unknown"].append(k)
            continue
        cls["std"].append(k)
        st, sa, ss = std[k]
        if (sa == "readWrite") != w:
            diff["access"].append("%s  here %s, standard %s" % (k, "writable" if w else "read-only", sa))
        t = types.get(n, "")
        if t and st and t != "xsd:" + st:
            diff["type"].append("%s  here %s, standard %s" % (k, t, st))
        if ss != "current":
            diff["status"].append("%s  %s" % (k, ss))
    print("BBF: %s (+ services %d files)" % (os.path.basename(files[-1]),
          len(glob.glob(os.path.join(a[0], "tr-1[34]*-cwmp-full.xml")))))
    print("parameters %d: std %d, vendor %d, unknown %d" % (len(seen), len(cls["std"]), len(cls["vendor"]),
                                                            len(cls["unknown"])))
    print("std differences: access %d, type %d, status %d" % (len(diff["access"]), len(diff["type"]),
                                                              len(diff["status"])))
    for c in ("unknown",) + (("vendor",) if listing else ()):
        if cls[c]:
            print("== %s" % c)
            for k in sorted(cls[c]):
                print("  " + k)
    for d in ("access", "status", "type"):
        if diff[d]:
            print("== %s" % d)
            for x in sorted(diff[d]):
                print("  " + x)
    bad = cls["unknown"] or diff["access"] or [x for x in diff["status"] if not x.endswith("deprecated")]
    print("RESULT: %s" % ("FAIL" if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
