#!/usr/bin/env python3
"""Stand-in for icwmp_dm.sh --json-input (tests/host): serves the easycwmp
tree of docs/issue/tr098_coverage_matrix.tsv, {i} expanded to instances 1
and 2 (LANDevice 1 only), with the protocol of icwmp_dm.sh including
get_value_list / get_name_list.  Values are "v<epoch>:<path>", the epoch
read from $FAKE_DM_EPOCH (default 1) on every get.

Test hooks: {"cmd":"stats"} / {"cmd":"reset"} (getters run, requests),
{"cmd":"forget","param":P}; FAKE_DM_LOG=<file> logs every command;
FAKE_DM_NO_LIST=1 behaves like a driver without the *_list commands;
FAKE_DM_INSTANCE_INFORM=1 adds a forced-inform parameter with an instance."""
import json, os, sys, itertools

MATRIX = os.environ["FAKE_DM_MATRIX"]
PROMPT = "icwmp_dm>"

def expand(path):
    parts = path.split(".")
    choices = []
    for i, seg in enumerate(parts):
        if seg == "{i}":
            # claims name LANDevice.1 literally; two instances elsewhere
            choices.append(["1"] if parts[i - 1] == "LANDevice" else ["1", "2"])
        else:
            choices.append([seg])
    return [".".join(c) for c in itertools.product(*choices)]

params, forced = {}, set()
for n, line in enumerate(open(MATRIX)):
    if n == 0:
        continue
    f = line.rstrip("\n").split("\t")
    if f[2] != "param":
        continue
    for p in expand(f[3]):
        params[p] = "v:" + p
        if f[8] == "1":
            forced.add(p)
if os.environ.get("FAKE_DM_INSTANCE_INFORM") == "1":
    # under a root no C module claims: since P8c (0099) the whole product
    # tree is C and the bridge drops every claimed path
    for p in expand("InternetGatewayDevice.X_HNI_FakeShell.{i}.Forced"):
        params[p] = "v:" + p
        forced.add(p)
objects = set()
for p in params:
    segs = p.split(".")
    for k in range(1, len(segs)):
        objects.add(".".join(segs[:k]) + ".")

getters = requests = 0

def out(o):
    sys.stdout.write(json.dumps(o) + "\n")

def value(p):
    global getters
    getters += 1
    epoch = "1"
    if os.environ.get("FAKE_DM_EPOCH"):
        try:
            epoch = open(os.environ["FAKE_DM_EPOCH"]).read().strip() or "1"
        except OSError:
            pass
    v = "v" + epoch + ":" + p
    out({"parameter": p, "value": v, "type": "xsd:string"})

def get_value(p):
    if p in params:
        value(p)
    elif p in objects:
        for q in sorted(params):
            if q.startswith(p):
                value(q)
    else:
        out({"parameter": p, "fault_code": "9005"})

def get_name(p, nl):
    if p in params:
        out({"parameter": p, "writable": "0"})
        return
    if p not in objects:
        out({"parameter": p, "fault_code": "9005"})
        return
    names = set(q for q in params if q.startswith(p)) | set(o for o in objects if o.startswith(p) and o != p)
    for q in sorted(names):
        rest = q[len(p):].rstrip(".")
        if nl == "1" and "." in rest:
            continue
        out({"parameter": q, "writable": "0"})

out_prompt = lambda: (sys.stdout.write(PROMPT + "\n"), sys.stdout.flush())
out_prompt()
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    req = json.loads(line)
    cmd = req.get("cmd")
    if os.environ.get("FAKE_DM_LOG"):
        with open(os.environ["FAKE_DM_LOG"], "a") as lf:
            lf.write("%s\n" % cmd)
    if cmd not in ("stats", "reset"):
        requests += 1
    if cmd == "get_value":
        get_value(req.get("param", ""))
    elif cmd == "get_name":
        get_name(req.get("param", ""), req.get("next_level", "0"))
    elif cmd in ("get_value_list", "get_name_list") and not os.environ.get("FAKE_DM_NO_LIST"):
        for x in req.get("params", []):
            if cmd == "get_value_list":
                get_value(x)
            else:
                get_name(x, req.get("next_level", "0"))
    elif cmd in ("get_value_list", "get_name_list"):
        out({"parameter": "", "fault_code": "9000"})
    elif cmd == "inform":
        for p in sorted(forced):
            value(p)
    elif cmd == "stats":
        out({"getters": str(getters), "requests": str(requests)})
    elif cmd == "reset":
        getters = requests = 0
    elif cmd == "forget":
        params.pop(req["param"], None)
        forced.discard(req["param"])
    elif cmd == "exit":
        break
    else:
        out({"status": "1"})
    out_prompt()
