#!/usr/bin/env python3
"""Compare the C data model with the product's easycwmp shell, from the dumps
of parity_dump.sh (run on the board, files copied back):

    parity.py <dir>          exit 1 on any difference not explained below

Every difference falls in one class:
  dynamic    a counter or a clock read some seconds apart
  known      a deliberate difference, with the place it is written down
  quote      the shell printed the quotes of an "echo \\"...\\"" getter
             ($(...) does not eval), C answers the value meant
  UNEXPECTED anything else: a porting error until shown otherwise
Types are reported, not judged: the engine has no xsd:IPv4Address,
xsd:unsignedLong or misspelled types, and the shell left many untyped
(cwmpclient sends those as xsd:string)."""
import collections, json, os, re, sys

DYNAMIC = [r"\.Stats\.", r"\.(Bytes|Packets)(Sent|Received)$", r"\.Total(Bytes|Packets)(Sent|Received)$",
           r"\.UpTime$", r"\.Uptime$", r"\.LastChange$", r"\.CurrentLocalTime$", r"MemoryStatus\.Free$",
           r"ProcessStatus\.CPUUsage$", r"\.LeaseTimeRemaining$", r"TemperatureSensor\.\d+\.Value$",
           r"ManagementServer\.UDPConnectionRequestAddress$"]   # the vendor STUN client re-maps the port

# (path regex, test on (c, s), where it is written down)
KNOWN = [
    (r"ManagementServer\.ConnectionRequestURL$", lambda c, s: True,
     "icwmpd's own CR server (cwmp.cpe.*), not easycwmp.@local[0]"),
    (r"ManagementServer\.ParameterKey$", lambda c, s: True,
     "icwmpd keeps the key in cwmp.acs.ParameterKey"),
    (r"WANPPPConnection\.\d+\.Password$", lambda c, s: c == "",
     "TR-098 reads it back empty (wanip_mtk.c get_ppp_password)"),
    (r"WLANConfiguration\.\d+\.(Basic|Operational)DataTransmitRates$",
     lambda c, s: c == "6,12,24" and s == "1,2,5.5,11",
     "shell 'case ra*)' also matches rai*: 2.4 GHz rates on 5 GHz (analysis 63)"),
    (r"DHCPv6\.Server\.Pool\.\d+\.DUID$", lambda c, s: c.lower() == s.lower(),
     "shell meant upper case, busybox tr has no [:lower:] (analysis 63)"),
]

ONLY_C = [r"ManagementServer\.(AliasBasedAddressing|HTTPCompression|HTTPCompressionSupported|InstanceMode|"
          r"LightweightNotificationProtocolsSupported|LightweightNotificationProtocolsUsed|SupportedConnReqMethods|"
          r"UDPLightweightNotificationHost|UDPLightweightNotificationPort)$",       # K3
          r"^InternetGatewayDevice\.X_HNI_Icwmp\.",                               # icwmpd's own object
          r"Account\.Web\.SessionMaxTime$"]   # shell lists it only under Account.Web. (analysis 63)

WRITABLE = [r"LANDevice\.\d+\.WLANConfiguration\.(\d+\.)?$"]                     # K24


def shell_lines(path, key):
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            o = json.loads(line)
        except ValueError:
            continue
        if key in o and "parameter" in o:
            out[o["parameter"]] = o
    return out


def pattern(p):
    return re.sub(r"\.\d+\.", ".{i}.", re.sub(r"\.\d+\.", ".{i}.", p))


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    # errors="replace": a C value with bytes that are not UTF-8 (K28: memory
    # read after free) becomes an UNEXPECTED difference, not a traceback
    c = {p["parameter"]: p for p in json.load(open(os.path.join(d, "c_get.json"), encoding="utf-8",
                                                  errors="replace"))["parameters"]}
    s = shell_lines(os.path.join(d, "sh_get.txt"), "value")
    cn = {p["parameter"]: p["writable"] for p in json.load(open(os.path.join(d, "c_names.json"), encoding="utf-8",
                                                               errors="replace"))["parameters"]}
    sn = {k: v["writable"] for k, v in shell_lines(os.path.join(d, "sh_names.txt"), "writable").items()}
    bad = 0
    cls = collections.Counter()
    unexpected = collections.defaultdict(list)
    for p in sorted(set(c) & set(s)):
        cv, sv = c[p]["value"], s[p]["value"]
        if cv == sv:
            cls["equal"] += 1
        elif any(re.search(r, p) for r in DYNAMIC):
            cls["dynamic"] += 1
        elif any(re.search(r, p) and t(cv, sv) for r, t, _ in KNOWN):
            cls["known"] += 1
        elif sv == '"%s"' % cv:
            cls["quote"] += 1
        else:
            unexpected[pattern(p)].append((cv, sv))
    print("values: %d in both trees, %s" % (len(set(c) & set(s)), ", ".join("%s %d" % kv for kv in sorted(cls.items()))))
    for k in sorted(unexpected):
        cv, sv = unexpected[k][0]
        print("  UNEXPECTED x%d %s  C=%r shell=%r" % (len(unexpected[k]), k, cv[:80], sv[:80]))
        bad += 1
    for p in sorted(set(c) - set(s)):
        if not any(re.search(r, p) for r in ONLY_C):
            print("  UNEXPECTED only in C: %s" % p)
            bad += 1
    for p in sorted(set(s) - set(c)):
        print("  UNEXPECTED only in the shell: %s" % p)
        bad += 1
    wd = [p for p in sorted(set(cn) & set(sn)) if cn[p] != sn[p]]
    print("names: C %d, shell %d, writable differs on %d" % (len(cn), len(sn), len(wd)))
    for p in wd:
        if not any(re.search(r, p) for r in WRITABLE):
            print("  UNEXPECTED writable %s C=%s shell=%s" % (p, cn[p], sn[p]))
            bad += 1
    for p in sorted(set(sn) - set(cn)):
        print("  UNEXPECTED name only in the shell: %s" % p)
        bad += 1
    types = collections.Counter()
    for p in set(c) & set(s):
        ct, st = c[p].get("type", ""), s[p].get("type") or "(none)"
        if ct != st:
            types[(ct, st)] += 1
    print("types differ on %d: %s" % (sum(types.values()),
          ", ".join("%s<-%s %d" % (a, b, n) for (a, b), n in types.most_common())))
    print("RESULT: %s" % ("PASS" if not bad else "FAIL, %d unexplained" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
