#!/usr/bin/env python3
"""Sinh lại tr098_coverage_matrix.tsv từ thư viện hàm của cwmpclient (easycwmp).

    ./gen-coverage-matrix.py \
        <2025q3>/tclinux_phoenix/apps/hni/cwmpclient/ext/openwrt/scripts/functions \
        > tr098_coverage_matrix.tsv

Cột: phase, phase_name, kind, path, perm, getter, setter, type, forced_inform, easycwmp_file.

Cách làm: đọc từng file MỘT LƯỢT theo thứ tự dòng, vừa thu phép gán biến đơn giản vừa bung
biến cho lời gọi common_execute_method_obj/param gặp sau đó (cộng với
DMROOT=InternetGatewayDevice). Dòng bắt đầu bằng '#' bị bỏ (rất nhiều object X_HNI_* đang
bị comment). Chỉ số instance ($i, $j, $inst, $wan_index, ...) chuẩn hoá thành {i}.

Lần quét phải là TUẦN TỰ và "gán sau đè gán trước". Bản đầu gom hết phép gán của file rồi mới
bung, lại dùng setdefault (gán ĐẦU tiên thắng), nên mọi path dựng từ biến cục bộ đặt lại trong
từng hàm - `local base="$DMROOT.LANDevice.1.LANEthernetInterfaceConfig.$inst."` chẳng hạn - đều
hỏng: path không bung được sẽ không bắt đầu bằng InternetGatewayDevice. và bị loại im lặng.
Đó là lý do bản ma trận đầu tiên thiếu 61 param (8 leaf mức port của LANEthernetInterfaceConfig,
17 leaf WLANConfiguration.AssociatedDevice, 36 leaf Firewall.X_AIS_*).

Lưu ý khi sửa: tham số rỗng "" là token thật, không được bỏ - nếu bỏ thì các cột
getter/setter/type/forced lệch nhau một ô.
"""
import re, os, sys, csv, collections

ROOTDIR = sys.argv[1]
files = []
for d in ('common','tr098','tr143'):
    p = os.path.join(ROOTDIR, d)
    for f in sorted(os.listdir(p)):
        files.append((d + '/' + f, os.path.join(p, f)))

GLOBAL = {'DMROOT': 'InternetGatewayDevice'}

assign_re = re.compile(r'^\s*(?:local\s+|export\s+)?([A-Za-z_][A-Za-z0-9_]*)=("([^"]*)"|\'([^\']*)\'|([^\s;#|&()]+))\s*$')
call_re = re.compile(r'common_execute_method_(obj|param)\s+(.*)$')

def toks(s):
    """split on whitespace; a quoted empty argument "" is a real token"""
    out, cur, q, started = [], '', None, False
    i = 0
    while i < len(s):
        c = s[i]
        if q:
            if c == q:
                q = None
            else:
                cur += c
        elif c == '"' or c == "'":
            q = c
            started = True
        elif c.isspace():
            if cur or started:
                out.append(cur)
                cur = ''
                started = False
        else:
            cur += c
            started = True
        i += 1
    if cur or started:
        out.append(cur)
    return out

INST = re.compile(r'\$\{?(i|j|k|inst|instance|object_idx|wan_index|rule_index|offset|wlan_index|index|idx|num|n|id|entry|port_idx|host_idx)\}?\b')

rows = []
for rel, path in files:
    txt = open(path, encoding='utf-8', errors='replace').read()
    env = dict(GLOBAL)
    def expand(s, depth=0):
        if depth > 8: return s
        def rep(m):
            n = m.group(1) or m.group(2)
            return env.get(n, m.group(0))
        prev = None
        while prev != s and depth < 8:
            prev = s
            s = re.sub(r'\$\{([A-Za-z_][A-Za-z0-9_]*)\}|\$([A-Za-z_][A-Za-z0-9_]*)', rep, s)
            depth += 1
        return s
    for line in txt.splitlines():
        ls = line.strip()
        if ls.startswith('#'): continue
        m = assign_re.match(line)
        if m:
            name = m.group(1)
            val = m.group(3) if m.group(3) is not None else (m.group(4) if m.group(4) is not None else m.group(5))
            # giá trị mới nhất thắng: đúng ngữ nghĩa shell cho code tuần tự
            if val is not None and name not in ('i','j','k','inst'):
                env[name] = expand(val)
            continue
        m = call_re.search(ls)
        if not m: continue
        kind = m.group(1)
        t = toks(m.group(2))
        if not t: continue
        p = expand(t[0])
        p = INST.sub('{i}', p)
        p = re.sub(r'\$\{?[A-Za-z_][A-Za-z0-9_]*\}?', '{v}', p)
        perm = t[1] if len(t) > 1 else '0'
        getter = t[2] if len(t) > 2 else ''
        setter = t[3] if len(t) > 3 else ''
        typ = t[4] if len(t) > 4 else ''
        forced = t[5] if len(t) > 5 else ''
        rows.append((kind, p, perm, getter, setter, typ, forced, rel))


# ---- chia phase theo nhánh của cây (xem tr098_c_port_phases.md) -------------

def phase(path):
    p = path[len('InternetGatewayDevice.'):]
    if p.startswith(('DeviceInfo.','Time.','ManagementServer.')): return (1,'P1 identity, time, management server')
    if p.startswith('LANDevice.'):
        rest = p.split('.',2)[2] if p.count('.')>=2 else ''
        if rest.startswith('WLANConfiguration.'): return (3,'P3 Wi-Fi')
        return (2,'P2 LAN')
    if p.startswith('WANDevice.'): return (4,'P4 WAN')
    if p.startswith(('IPPingDiagnostics.','TraceRouteDiagnostics.','DNSDiagnostics.','NSLookupDiagnostics.','DownloadDiagnostics.','UploadDiagnostics.','Layer3Forwarding.','SelfTestDiagnostics.','WiFi.')): return (5,'P5 diagnostics')
    if p.startswith(('Firewall.','UserInterface.','CaptivePortal.','Account.','User.','XMPP.','SoftwareModules.','BulkData.','FaultMgmt.','USBHosts.','Layer2Bridging.','FAP.','DeviceSummary')): return (6,'P6 firewall, UI, misc root')
    if p.startswith('X_AIS'): return (7,'P7 operator X_AIS tree')
    return (8,'P8 storage, STB, DOCSIS, LTE, IGD.Device')

NORM = [
    ('InternetGatewayDevice.LANDevice.1.', 'InternetGatewayDevice.LANDevice.{i}.'),
    ('InternetGatewayDevice.WANDevice.1.', 'InternetGatewayDevice.WANDevice.{i}.'),
    ('WANConnectionDevice.1.', 'WANConnectionDevice.{i}.'),
]

out, seen = [], set()
for r in rows:
    kind, path = r[0], r[1]
    if not path.startswith('InternetGatewayDevice.'):
        continue
    for a, b in NORM:
        path = path.replace(a, b)
    if (kind, path) in seen:
        continue
    seen.add((kind, path))
    ph, name = phase(path)
    out.append((ph, name, kind, path, r[2], r[3], r[4], r[5], r[6], r[7]))

w = csv.writer(sys.stdout, delimiter='\t', lineterminator='\n')
w.writerow(['phase','phase_name','kind','path','perm','getter','setter','type','forced_inform','easycwmp_file'])
for r in sorted(out, key=lambda r: (r[0], r[3])):
    w.writerow(r)

c = collections.Counter()
for r in out:
    if r[2] == 'param':
        c[r[1]] += 1
sys.stderr.write("param theo phase:\n")
for k in sorted(c):
    sys.stderr.write("  %-42s %4d\n" % (k, c[k]))
sys.stderr.write("  %-42s %4d\n" % ("TONG", sum(c.values())))
