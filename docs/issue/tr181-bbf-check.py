#!/usr/bin/env python3
"""The TR-181 tree of a "tr069 dm" dump against the Broadband Forum data model:

    tr181-bbf-check.py <bbf-dir> <names.json> [<values.json>] [--list]
    tr181-bbf-check.py <bbf-dir> <names.json> --profile <P:v>[,<P:v>...] [--list]
    tr181-bbf-check.py <bbf-dir> <names.json> --profiles

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

--profile checks the requirements of TR-181 profiles (base / extends
followed) against the dump: a required parameter missing, a readWrite one
read-only here, a table the profile wants created/deleted that cannot be;
a table with no instance in the dump is listed as not checked.  Exit 1 when
something is missing or read-only.  --profiles prints one line per profile
of the Device:2 model (summary, to choose which ones to claim).
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
            # a service model's own parameters (StorageServiceNumberOfEntries
            # of TR-140) sit on the object it is mounted under
            if strip_ns(obj.tag) == "parameter":
                out[prefix + obj.get("name", "")] = (syntax_type(obj, datatypes), obj.get("access", "readOnly"),
                                                     obj.get("status", "current"))
                continue
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


def load_profiles(path):
    """name -> (status, [inherited names], [(object, requirement, [(param, requirement)])])
    of the first <model> of path"""
    root = ET.parse(path).getroot()
    out = {}
    for model in root.iter():
        if strip_ns(model.tag) != "model":
            continue
        for pr in model:
            if strip_ns(pr.tag) != "profile":
                continue
            inh = (pr.get("base", "") + " " + pr.get("extends", "")).split()
            items = []
            for o in pr:
                if strip_ns(o.tag) != "object":
                    continue
                ps = [(x.get("ref"), x.get("requirement", "")) for x in o if strip_ns(x.tag) == "parameter"]
                items.append((o.get("ref"), o.get("requirement", ""), ps))
            out[pr.get("name")] = (pr.get("status", "current"), inh, items)
        break
    return out


def profile_items(profiles, name, seen=None):
    seen = set() if seen is None else seen
    if name in seen or name not in profiles:
        return []
    seen.add(name)
    st, inh, items = profiles[name]
    out = list(items)
    for b in inh:
        out += profile_items(profiles, b, seen)
    return out


def check_profile(profiles, name, writable, objects):
    """missing, read-only, no create/delete, unchecked tables, required count"""
    missing, ro, nocd, unchecked, req = [], [], [], set(), 0
    for oref, oreq, ps in profile_items(profiles, name):
        present = oref in objects
        if not present and "{i}" in oref:
            # inside a table without an instance in the dump: nothing to read
            # (an object missing under an instance that exists is missing)
            tbl = oref[:oref.rfind("{i}.") + 4]
            if tbl not in objects:
                unchecked.add(tbl)
                continue
        # GPN: a table X. is writable when AddObject works, an instance X.{i}.
        # when DeleteObject does
        if present and oref.endswith("{i}."):
            parent = oref[:-4]
            if oreq in ("create", "createDelete") and not objects.get(parent, False):
                nocd.append("%s (create)" % oref)
            if oreq in ("delete", "createDelete") and not objects[oref]:
                nocd.append("%s (delete)" % oref)
        for pname, preq in ps:
            req += 1
            n = oref + pname
            if n not in writable:
                missing.append(n)
            elif preq == "readWrite" and not writable[n]:
                ro.append(n)
    return missing, ro, nocd, sorted(unchecked), req


def profiles_main(a, listing):
    files = sorted(glob.glob(os.path.join(a[0], "tr-181-*-cwmp-full.xml")))
    if not files:
        sys.exit("no tr-181-*-cwmp-full.xml in %s" % a[0])
    profiles = load_profiles(files[-1])
    writable, objects = {}, {}
    for p in json.load(open(a[1]))["parameters"]:
        k = norm(p["parameter"])
        w = p.get("writable") in ("1", True)
        if k.endswith("."):
            # a table object (X.{i}.) is writable when instances can be added
            objects[k] = objects.get(k, False) or w
        else:
            writable[k] = writable.get(k, False) or w
    # the table objects of the dump appear as X.{i}. ; their parents as X.
    for k in list(writable) + list(objects):
        segs = k.split(".")
        for i in range(1, len(segs) - 1):
            objects.setdefault(".".join(segs[:i]) + ".", False)
    if "--profiles" in sys.argv:
        for name in sorted(profiles):
            if name.startswith("_") or profiles[name][0] != "current":
                continue
            m, ro, nocd, un, req = check_profile(profiles, name, writable, objects)
            print("%-28s required %3d  missing %3d  read-only %2d  no create/delete %d  tables not checked %d" %
                  (name, req, len(m), len(ro), len(nocd), len(un)))
        return 0
    want = sys.argv[sys.argv.index("--profile") + 1].split(",")
    bad = 0
    for name in want:
        if name not in profiles:
            sys.exit("no profile %s in %s" % (name, os.path.basename(files[-1])))
        m, ro, nocd, un, req = check_profile(profiles, name, writable, objects)
        print("%s: required %d, missing %d, read-only %d, no create/delete %d, tables not checked %d" %
              (name, req, len(m), len(ro), len(nocd), len(un)))
        for title, lst in (("missing", m), ("read-only", ro), ("no create/delete", nocd),
                           ("not checked (no instance)", un)):
            if lst and (listing or title != "not checked (no instance)"):
                print("  == %s" % title)
                for x in lst:
                    print("    " + x)
        bad += len(m) + len(ro)
    print("RESULT: %s" % ("FAIL" if bad else "PASS"))
    return 1 if bad else 0


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        sys.exit(__doc__)
    listing = "--list" in sys.argv
    if "--profile" in sys.argv or "--profiles" in sys.argv:
        if "--profile" in sys.argv:
            a = [x for x in a if x != sys.argv[sys.argv.index("--profile") + 1]]
        return profiles_main(a, listing)
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
