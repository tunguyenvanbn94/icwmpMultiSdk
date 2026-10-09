#!/usr/bin/env python3
"""TR-098 -> TR-181 mapping of the MTK data model (docs/plan/tr181_mtk_design.md),
rules in tr181_mapping.tsv:

    tr181-map.py expected                         TR-181 name of each of the 783 TR-098 parameters
    tr181-map.py check [--src <libicwmp_dm/src>]  expected names against the C tree of the build
    tr181-map.py equiv <tr098.json> <tr181.json>  value of every mapped pair, two "tr069 dm get" dumps
                                                  of the same device, one per model

A rule maps a TR-098 path (instances spelled {i}) to a TR-181 one.  On the
TR-181 side {iN} is the Nth instance of the TR-098 path, {i} the next one, and
{lan} the Device.IP.Interface instance whose Name is "lan", {radio:iN} the
Device.WiFi.Radio of the SSID numbered like the Nth instance, {wanif:iN} the
Device.IP.Interface of WAN connection N (NAT.InterfaceSetting.N.Interface),
{ppp:iN} its Device.PPP.Interface (by Name: the connection's, else if<N-1>)
{pm:iC:iJ} the Jth Device.NAT.PortMapping whose Interface is connection C's
-- read from the dumps, "{i}" for check -- and {dns:iN} / {gw:iN} the fixed
numbers 3N-2 (first DNS.Client.Server) / 64+N (default route):
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
under Device., "equal ref"); a pair whose TR-098 value is empty or 0.0.0.0
may have no TR-181 instance (a bridge has no address, no DNS list); a TR-181
name no pair reaches is fine when it reads empty and a rule targets its
pattern (a leaf of a shared table on an instance without that TR-098 leaf); exit 1 on a missing name or a different
value.  Pending (T2..) and D rows are counted, not checked."""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RULES = os.path.join(HERE, "tr181_mapping.tsv")
MATRIX = os.path.join(HERE, "tr098_coverage_matrix.tsv")
GRAFT = "InternetGatewayDevice.Device."  # the TR-181 branch inside the product's TR-098 tree
# TR-181 "secured" parameters (BBF syntax secured="true"): read empty in
# TR-181 whatever their value, while the product's TR-098 leaf reads it back;
# an A pair of them is "secured" when the TR-181 side is empty
SECURED = [r"\.Security\.KeyPassphrase$", r"^Device\.DynamicDNS\.Client\.\d+\.Password$",
           r"\.TC\.Authentication\.Password$"]
DYNAMIC = [r"\.Stats\.", r"\.(Bytes|Packets)(Sent|Received)$", r"\.UpTime$", r"\.Uptime$", r"\.LastChange$",
           r"\.CurrentLocalTime$", r"MemoryStatus\.Free$", r"ProcessStatus\.CPUUsage$", r"TemperatureSensor\.\d+\.Value$",
           r"\.LeaseTimeRemaining$", r"ManagementServer\.UDPConnectionRequestAddress$", r"\.UsedSpace$",
           r"X_AIS_GPON\.(Rx|Tx)Power$"]


def pat(p):
    p = p.replace("{lan}", "{i}")
    p = re.sub(r"\{(radio|wanif|ppp):(\d+|\{i\})\}", "{i}", p)
    p = re.sub(r"\{pm:(\d+|\{i\}):(\d+|\{i\})\}", "{i}", p)
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
    out = re.sub(r"\{(radio|wanif|ppp):i(\d)\}", lambda x: "{%s:%s}" % (x.group(1), caps[int(x.group(2)) - 1]), rule[2])
    out = re.sub(r"\{(dns|gw):i(\d)\}", lambda x: fixed(x.group(1), caps[int(x.group(2)) - 1]), out)
    out = re.sub(r"\{pm:i(\d):i(\d)\}", lambda x: "{pm:%s:%s}" % (caps[int(x.group(1)) - 1], caps[int(x.group(2)) - 1]), out)
    out = re.sub(r"\{i(\d)\}", lambda x: caps[int(x.group(1)) - 1], out)
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


def fixed(kind, cap):
    """the fixed instance numbers of a WAN connection's DNS server / default route"""
    if not cap.isdigit():
        return "{i}"
    return str(3 * int(cap) - 2) if kind == "dns" else str(64 + int(cap))


def wanif_of(d181, conn):
    v = d181.get("Device.NAT.InterfaceSetting.%s.Interface" % conn, {}).get("value", "")
    m = re.search(r"Interface\.(\d+)$", v)
    return m.group(1) if m else "{wanif:%s}" % conn


def ppp_of(d98, d181, conn):
    name = ""
    for k, p in d98.items():
        if re.search(r"\.WANPPPConnection\.%s\.Name$" % conn, k):
            name = p.get("value", "")
    for cand in (name, "if%d" % (int(conn) - 1)):
        for k, p in d181.items():
            m = re.match(r"^Device\.PPP\.Interface\.(\d+)\.Name$", k)
            if m and cand and p.get("value") == cand:
                return m.group(1)
    return "{ppp:%s}" % conn


def pm_of(d98, d181, conn, nth, name):
    """the NAT.PortMapping row of the nth (1-based) TR-098 rule of connection
    conn: rows of that connection's IP.Interface in order, a tcp/udp rule
    being two of them (TCP then UDP, the TCP one is its row here)"""
    w = wanif_of(d181, conn)
    ref = "Device.IP.Interface.%s" % w
    ks = sorted(int(m.group(1)) for k, p in d181.items()
                for m in [re.match(r"^Device\.NAT\.PortMapping\.(\d+)\.Interface$", k)]
                if m and p.get("value") == ref)
    m = re.match(r"^(.*\.PortMapping\.)\d+\.", name)
    at = 0
    for j in range(1, int(nth)):
        v = d98.get("%s%d.PortMappingProtocol" % (m.group(1), j), {}).get("value", "") if m else ""
        at += 2 if v.lower() in ("tcp/udp", "udp/tcp", "both") else 1
    return str(ks[at]) if len(ks) > at else "{pm:%s:%s}" % (conn, nth)


def radio_of(d181, ssid):
    """Device.WiFi.Radio instance of Device.WiFi.SSID.<ssid> (its LowerLayers)"""
    v = d181.get("Device.WiFi.SSID.%s.LowerLayers" % ssid, {}).get("value", "")
    m = re.search(r"Radio\.(\d+)$", v)
    return m.group(1) if m else "{radio:%s}" % ssid


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
            if t and "{radio:" in t:
                t = re.sub(r"\{radio:(\d+)\}", lambda x: radio_of(d181, x.group(1)), t)
            if t and "{wanif:" in t:
                t = re.sub(r"\{wanif:(\d+)\}", lambda x: wanif_of(d181, x.group(1)), t)
            if t and "{ppp:" in t:
                t = re.sub(r"\{ppp:(\d+)\}", lambda x: ppp_of(d98, d181, x.group(1)), t)
            if t and "{pm:" in t:
                t = re.sub(r"\{pm:(\d+):(\d+)\}", lambda x: pm_of(d98, d181, x.group(1), x.group(2), name), t)
            kind = r[3] if r else "none"
            if not t:
                cls[kind] = cls.get(kind, 0) + 1
                if kind == "none":
                    print("  no rule:", name)
                    bad += 1
                continue
            reached.add(t)
            if t not in d181 and d98[name]["value"] in ("", "0.0.0.0"):
                cls["absent, TR-098 empty"] = cls.get("absent, TR-098 empty", 0) + 1
            elif t not in d181:
                print("  missing in TR-181: %s (from %s)" % (t, name))
                bad += 1
            elif kind == "B":
                cls["B present"] = cls.get("B present", 0) + 1
            elif d181[t]["value"] == d98[name]["value"]:
                cls["equal"] = cls.get("equal", 0) + 1
            elif d181[t]["value"] == "" and any(re.search(x, t) for x in SECURED):
                cls["secured"] = cls.get("secured", 0) + 1
            elif d98[name]["value"].startswith(GRAFT) and d181[t]["value"] == "Device." + d98[name]["value"][len(GRAFT):]:
                cls["equal ref"] = cls.get("equal ref", 0) + 1
            elif any(re.search(x, name) or re.search(x, t) for x in DYNAMIC):
                # TR-098 TotalBytesSent is TR-181 Stats.BytesSent: either spelling marks a counter
                cls["dynamic"] = cls.get("dynamic", 0) + 1
            else:
                print("  differs: %s=%r  %s=%r" % (name, d98[name]["value"][:60], t, d181[t]["value"][:60]))
                bad += 1
        news = {pat(r[2]) for r in rules if r[0] == "new"}
        targets = set(expected(rules)[0])
        empty = [n for n in d181 if n not in reached and pat(n) not in news and pat(n) in targets
                 and d181[n].get("value", "") == ""]
        if empty:
            cls["empty, other instance"] = len(empty)
        extra = sorted(n for n in d181 if n not in reached and pat(n) not in news and n not in set(empty))
        for n in extra:
            print("  TR-181 name no TR-098 pair reaches:", n)
        print("TR-098 %d, TR-181 %d: %s; TR-181 not reached %d"
              % (len(d98), len(d181), ", ".join("%s %d" % kv for kv in sorted(cls.items())), len(extra)))
        print("RESULT: %s" % ("PASS" if not bad and not extra else "FAIL"))
        return 1 if bad or extra else 0
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
