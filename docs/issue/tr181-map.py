#!/usr/bin/env python3
"""TR-098 -> TR-181 mapping of the MTK data model (docs/plan/tr181_mtk_design.md),
rules in tr181_mapping.tsv:

    tr181-map.py expected                         TR-181 name of each of the 783 TR-098 parameters
    tr181-map.py check [--src <libicwmp_dm/src>]  expected names against the C tree of the build
    tr181-map.py equiv <tr098.json> <tr181.json>  value of every mapped pair, two "tr069 dm get" dumps
                                                  of the same device, one per model

A rule maps a TR-098 path (instances spelled {i}) to a TR-181 one.  On the
TR-181 side {iN} is the Nth instance of the TR-098 path, {i} the next one, and
{lan} the Device.IP.Interface instance whose Name is "lan" (read from the
TR-181 dump; "{i}" for check):
  prefix  the longest matching prefix wins, the rest of the path is kept
  leaf    exactly that TR-098 parameter (wins over every prefix)
  new     a TR-181 name with no TR-098 source
Types: A same meaning, same getter; C operator/product object, same getter;
B same setting, different spelling of the value (presence checked, value
not compared); D no TR-181 counterpart; T2..T7 phase not done yet (tr181 "?").

check: every A/B/C name must be in the tree (verify-dm-paths.py --model tr181
--dump), every name of the tree must come from a rule; exit 1 otherwise.
equiv: an A/C pair must be present in both dumps with the same value
(counters and clocks excepted; a TR-098 value that is a path reference into
the product's InternetGatewayDevice.Device. branch equals the same reference
under Device., "equal ref"); exit 1 on a missing name or a different
value.  Pending (T2..) and D rows are counted, not checked."""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RULES = os.path.join(HERE, "tr181_mapping.tsv")
MATRIX = os.path.join(HERE, "tr098_coverage_matrix.tsv")
GRAFT = "InternetGatewayDevice.Device."  # the TR-181 branch inside the product's TR-098 tree
DYNAMIC = [r"\.Stats\.", r"\.(Bytes|Packets)(Sent|Received)$", r"\.UpTime$", r"\.Uptime$", r"\.LastChange$",
           r"\.CurrentLocalTime$", r"MemoryStatus\.Free$", r"ProcessStatus\.CPUUsage$", r"TemperatureSensor\.\d+\.Value$",
           r"\.LeaseTimeRemaining$", r"ManagementServer\.UDPConnectionRequestAddress$", r"\.UsedSpace$"]


def pat(p):
    p = p.replace("{lan}", "{i}")
    p = re.sub(r"\$\d", "{i}", p)
    p = re.sub(r"\.\d+\.", ".{i}.", p)
    p = re.sub(r"\.\d+\.", ".{i}.", p)
    return re.sub(r"\.\d+$", ".{i}", p)


def load_rules():
    rules = []
    for line in open(RULES, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 4 or f[0] == "kind":
            continue
        rules.append(tuple(f[:5]) if len(f) >= 5 else tuple(f[:4]) + ("",))
    return rules


_RX = {}


def rx(src, prefix):
    """regex of a rule's TR-098 side; {i} matches an instance number or {i}"""
    key = (src, prefix)
    if key not in _RX:
        r = re.escape(src).replace(re.escape("{i}"), r"(\d+|\{i\})")
        _RX[key] = re.compile("^" + r + ("" if prefix else "$"))
    return _RX[key]


def find(rules, path):
    """(rule, match) of a TR-098 path (real instance numbers or {i})"""
    for r in rules:
        if r[0] == "leaf":
            m = rx(r[1], False).match(path)
            if m:
                return r, m
    best = None
    for r in rules:
        if r[0] == "prefix":
            m = rx(r[1], True).match(path)
            if m and (best is None or len(r[1]) > len(best[0][1])):
                best = (r, m)
    return best if best else (None, None)


def target(rule, m, path):
    """TR-181 name of a TR-098 path under its rule, None if not A/B/C.  The
    instances of the TR-098 side fill the TR-181 side: {iN} is the Nth one,
    a plain {i} the next one in order"""
    if not rule or rule[3] not in ("A", "B", "C") or rule[2] in ("?", "-"):
        return None
    caps = list(m.groups())
    out = re.sub(r"\{i(\d)\}", lambda x: caps[int(x.group(1)) - 1], rule[2])
    it = iter(caps)
    out = re.sub(r"\{i\}", lambda x: next(it), out)
    return out + (path[m.end():] if rule[0] == "prefix" else "")


def sources(src_dir):
    """TR-098 parameters: the product's 783 (coverage matrix) and the names
    only the C tree has (K3 ManagementServer leaves, X_HNI_Icwmp)"""
    names = set()
    for line in open(MATRIX, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 4 and f[2] == "param":
            names.add(pat(f[3]))
    return names | {pat(n) for n in declared(src_dir, "tr098")}


def expected(rules, src_dir=None):
    out, counts, unmapped = {}, {}, []
    for src in sorted(sources(src_dir)):
        r, m = find(rules, src)
        if not r:
            unmapped.append(src)
            continue
        counts[r[3]] = counts.get(r[3], 0) + 1
        t = target(r, m, src)
        if t:
            out[pat(t)] = (src, r[3])
    for r in rules:
        if r[0] == "new":
            out[pat(r[2])] = ("-", r[3])
    return out, counts, unmapped


def declared(src, model="tr181"):
    cmd = [sys.executable, os.path.join(HERE, "verify-dm-paths.py"), "--sdk", "mtk", "--model", model, "--dump"]
    if src:
        cmd += ["--src", src]
    return set(l.strip() for l in subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.splitlines()
               if l.strip())


def named(d181):
    """{lan} -> the Device.IP.Interface instance of network.lan in a TR-181 dump"""
    out = {}
    for name, p in d181.items():
        m = re.match(r"^Device\.IP\.Interface\.(\d+)\.Name$", name)
        if m and p.get("value") == "lan":
            out["{lan}"] = m.group(1)
    return out


def dump(path):
    return {p["parameter"]: p for p in json.load(open(path, encoding="utf-8", errors="replace")).get("parameters", [])}


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    rules = load_rules()
    if a[0] == "expected":
        out, counts, unmapped = expected(rules)
        for t in sorted(out):
            print("%s\t%s\t%s" % (t, out[t][1], out[t][0]))
        print("# per type: %s; no rule: %d" % (", ".join("%s %d" % kv for kv in sorted(counts.items())), len(unmapped)),
              file=sys.stderr)
        return 1 if unmapped else 0
    if a[0] == "check":
        src = a[2] if len(a) > 2 and a[1] == "--src" else None
        out, counts, unmapped = expected(rules, src)
        have = declared(src)
        missing = sorted(set(out) - have)
        extra = sorted(have - set(out))
        for p in unmapped:
            print("  no rule:", p)
        for p in missing:
            print("  missing in the C tree:", p, "(from %s)" % out[p][0])
        for p in extra:
            print("  in the C tree, no rule:", p)
        print("TR-098 params (matrix + C-only) per type: %s" % ", ".join("%s %d" % kv for kv in sorted(counts.items())))
        print("TR-181 expected %d, in the C tree %d, missing %d, without rule %d, TR-098 without rule %d"
              % (len(out), len(have), len(missing), len(extra), len(unmapped)))
        return 1 if missing or extra or unmapped else 0
    if a[0] == "equiv" and len(a) == 3:
        d98, d181 = dump(a[1]), dump(a[2])
        names = named(d181)
        cls, bad, reached = {}, 0, set()
        for name in sorted(d98):
            r, m = find(rules, name)
            t = target(r, m, name)
            for k, v in names.items():
                t = t.replace(k, v) if t else t
            kind = r[3] if r else "none"
            if not t:
                cls[kind] = cls.get(kind, 0) + 1
                if kind == "none":
                    print("  no rule:", name)
                    bad += 1
                continue
            reached.add(t)
            if t not in d181:
                print("  missing in TR-181: %s (from %s)" % (t, name))
                bad += 1
            elif kind == "B":
                cls["B present"] = cls.get("B present", 0) + 1
            elif d181[t]["value"] == d98[name]["value"]:
                cls["equal"] = cls.get("equal", 0) + 1
            elif d98[name]["value"].startswith(GRAFT) and d181[t]["value"] == "Device." + d98[name]["value"][len(GRAFT):]:
                cls["equal ref"] = cls.get("equal ref", 0) + 1
            elif any(re.search(x, name) for x in DYNAMIC):
                cls["dynamic"] = cls.get("dynamic", 0) + 1
            else:
                print("  differs: %s=%r  %s=%r" % (name, d98[name]["value"][:60], t, d181[t]["value"][:60]))
                bad += 1
        news = {pat(r[2]) for r in rules if r[0] == "new"}
        extra = sorted(n for n in d181 if n not in reached and pat(n) not in news)
        for n in extra:
            print("  TR-181 name no TR-098 pair reaches:", n)
        print("TR-098 %d, TR-181 %d: %s; TR-181 not reached %d"
              % (len(d98), len(d181), ", ".join("%s %d" % kv for kv in sorted(cls.items())), len(extra)))
        print("RESULT: %s" % ("PASS" if not bad and not extra else "FAIL"))
        return 1 if bad or extra else 0
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
