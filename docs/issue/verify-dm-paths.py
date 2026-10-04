#!/usr/bin/env python3
"""Dựng lại toàn bộ đường dẫn TR-098 mà cây C của SDK MTK trả lời, rồi đối
chiếu với tr098_coverage_matrix.tsv (cây shell của sản phẩm).

Không biên dịch gì: đọc thẳng bảng DMOBJ/DMLEAF trong sdk/mtk/dm098/*.c, nối
theo đúng cách dm_registry.c gộp (theo tên object, cùng cấp thì gộp vào nhau),
rồi in ra tham số nào thiếu, tham số nào dôi so với cây shell.

    ./verify-dm-paths.py                 # toàn bộ
    ./verify-dm-paths.py --phase 4       # chỉ một phase của ma trận
    ./verify-dm-paths.py --prefix InternetGatewayDevice.WANDevice.

Object nhiều instance nhận diện bằng cột browseinstobj khác NULL -> '{i}'.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Cây userspace/ của repo này.  ICWMP_USERSPACE trỏ sang cây khác (overlay của
# workspace cũ: .../sdk-overlay/userspace) khi cần kiểm ở đó.
USERSPACE = os.environ.get("ICWMP_USERSPACE") or os.path.normpath(
    os.path.join(HERE, "..", "..", "userspace"))
DEFAULT_SRC = os.path.join(USERSPACE, "public", "libs", "libicwmp_dm", "src")
DEFAULT_MATRIX = os.path.join(HERE, "tr098_coverage_matrix.tsv")

TABLE_RE = re.compile(
    r"(static\s+)?(DMOBJ|DMLEAF)\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\n\};", re.S)
MODULE_RE = re.compile(r"\.objs\s*=\s*(\w+)")
INSTANCE_RE = re.compile(r"\$\d+")
# Bộ trích xuất giữ lại số instance viết cứng trong script shell
# (IPInterface.1., WANDevice.1.).  Quy mọi segment toàn chữ số về {i}.
NUM_SEG_RE = re.compile(r"(?<=\.)\d+(?=\.)")


def norm(path):
    return NUM_SEG_RE.sub("{i}", INSTANCE_RE.sub("{i}", path))
ROW_RE = re.compile(r'^\{\s*("(?:[^"\\]|\\.)*"|\w+(?:\s*"[^"]*")?)\s*,\s*(.*)\}\s*,?\s*$')



SRC_RE = re.compile(r"^\s*\.\./(\S+\.c)\s*\\?\s*$")


def collect_sources(src, sdk):
    """Danh sách .c (relative tới src/) mà build của SDK này biên dịch."""
    out = []
    for mk in (os.path.join(src, "bin", "Makefile.am"),
               os.path.join(src, "sdk", sdk, "sdk.mk")):
        if not os.path.isfile(mk):
            continue
        for line in open(mk, encoding="utf-8", errors="replace"):
            m = SRC_RE.match(line)
            if m:
                out.append(m.group(1))
    return out


def split_fields(body):
    """Tách các trường của một hàng bảng, bỏ qua dấu phẩy trong ngoặc/nháy."""
    out, cur, depth, instr = [], "", 0, False
    i = 0
    while i < len(body):
        c = body[i]
        if instr:
            cur += c
            if c == "\\":
                if i + 1 < len(body):
                    cur += body[i + 1]
                    i += 1
            elif c == '"':
                instr = False
        elif c == '"':
            instr = True
            cur += c
        elif c in "([{":
            depth += 1
            cur += c
        elif c in ")]}":
            depth -= 1
            cur += c
        elif c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


def literal(tok, defines):
    """'"A" B "C"' -> 'AxC' với B là macro trong defines."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"|([A-Za-z_]\w*)', tok)
    val = ""
    for quoted, ident in parts:
        if quoted:
            val += quoted.encode().decode("unicode_escape")
        elif ident in defines:
            val += defines[ident]
        else:
            return None
    return val


def parse_file(path, defines):
    src = open(path, encoding="utf-8", errors="replace").read()
    tables, roots, shared = {}, [], set()
    for is_static, kind, name, body in TABLE_RE.findall(src):
        rows = []
        # một hàng có thể trải nhiều dòng vật lý: gom tới khi ngoặc cân bằng
        pending, depth = "", 0
        for line in body.split("\n"):
            line = line.strip()
            if not pending and (not line or line.startswith("/*") or line.startswith("//")):
                continue
            pending = (pending + " " + line).strip() if pending else line
            depth += line.count("{") - line.count("}")
            if depth > 0:
                continue
            row, pending, depth = pending, "", 0
            if row in ("{0}", "{0},"):
                continue
            m = ROW_RE.match(row)
            if not m:
                continue
            first = literal(m.group(1), defines)
            if first is None:
                continue
            rows.append((first, split_fields(m.group(2))))
        tables[name] = (kind, rows)
        if not is_static:
            shared.add(name)
    for m in MODULE_RE.finditer(src):
        roots.append(m.group(1))
    return tables, roots, shared


class Scope(dict):
    """Bảng static của một file, phủ lên tập bảng dùng chung của cả build."""

    def __init__(self, local, shared):
        super().__init__()
        self.local, self.shared = local, shared

    def get(self, key, default=None):
        if key in self.local:
            return self.local[key]
        return self.shared.get(key, default)

    def __getitem__(self, key):
        if key in self.local:
            return self.local[key]
        return self.shared[key]

    def __contains__(self, key):
        return key in self.local or key in self.shared


def merge(tables, table_name, node, stack):
    """Gộp một bảng DMOBJ vào cây đã có, theo đúng cách dm_registry.c gộp:
    cùng tên object thì gộp làm một, module nào đặt browseinstobj thì object
    đó là multi-instance.  lanhosts_mtk.c để browse NULL và mượn {i} của
    lan_mtk.c -- duyệt từng bảng riêng sẽ dựng sai đường dẫn."""
    kind, rows = tables.get(table_name, (None, []))
    if kind != "DMOBJ" or table_name in stack:
        return
    stack = stack | {table_name}
    for name, fields in rows:
        # perm, addobj, delobj, checkobj, browse, forced, notif, nextobj, leaf,
        # linker, container_leaf
        browse = fields[4] if len(fields) > 4 else "NULL"
        nextobj = fields[7] if len(fields) > 7 else "NULL"
        leaf = fields[8] if len(fields) > 8 else "NULL"
        cleaf = fields[10] if len(fields) > 10 else "NULL"
        child = node.setdefault(name, {"browse": False, "leaves": set(),
                                       "cleaves": set(), "kids": {}})
        if browse != "NULL":
            child["browse"] = True
        if leaf != "NULL" and leaf in tables:
            for lname, _ in tables[leaf][1]:
                child["leaves"].add(lname)
        # DMOBJ.container_leaf: leaves of the container node itself
        if cleaf != "NULL" and cleaf in tables:
            for lname, _ in tables[cleaf][1]:
                child["cleaves"].add(lname)
        if nextobj != "NULL":
            merge(tables, nextobj, child["kids"], stack)


def emit(node, prefix, out):
    for name in sorted(node):
        n = node[name]
        obj = prefix + name + "."
        out.add(("obj", obj))
        for lname in n.get("cleaves", ()):
            out.add(("param", obj + lname))
        if n["browse"]:
            obj += "{i}."
            out.add(("obj", obj))
        for lname in n["leaves"]:
            out.add(("param", obj + lname))
        emit(n["kids"], obj, out)


PATHS_RE = re.compile(r"static\s+const\s+char\s*\*\s*const\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\}\s*;", re.S)
NAME_RE = re.compile(r'\.name\s*=\s*"([^"]*)"')


def path_match(claim, path, sym):
    """Bản Python của path_match() trong dm_registry.c: so từng segment, {i}
    là ký tự đại diện, claim kết thúc bằng '.' phủ cả nhánh con."""
    object_claim = claim.endswith(".")
    o = [x for x in claim.split(".")]
    q = [x for x in path.split(".")]
    i = 0
    while True:
        if i >= len(o) or o[i] == "":
            if i < len(o) and o[i] == "":
                return True                       # dấu chấm cuối
            return True if object_claim else (i >= len(q))
        if i >= len(q):
            return False
        if o[i] != "{i}" and not (sym and q[i] == "{i}"):
            if o[i] != q[i]:
                return False
        i += 1


def check_claims(src, sdk):
    """Mọi .paths của các module đang build: có cặp nào phủ nhau không."""
    entries = []
    for rel in collect_sources(src, sdk):
        full = os.path.join(src, rel)
        if not os.path.isfile(full):
            continue
        txt = open(full, encoding="utf-8", errors="replace").read()
        mod = NAME_RE.search(txt)
        mod = mod.group(1) if mod else rel
        for _, body in PATHS_RE.findall(txt):
            for line in body.split("\n"):
                line = line.strip()
                if not line or line.startswith("/*") or line.startswith("//"):
                    continue
                if line.startswith("NULL"):
                    continue
                val = literal(line.rstrip(","), {"CUSTOM_PREFIX": "X_HNI_"})
                if val:
                    entries.append((mod, rel, val))
    print("claim: %d, module: %d" % (len(entries), len({e[0] for e in entries})))
    bad = 0
    for i in range(len(entries)):
        for j in range(i + 1, len(entries)):
            a, b = entries[i], entries[j]
            if a[0] == b[0]:
                continue
            if path_match(a[2], b[2], 1) or path_match(b[2], a[2], 1):
                print("  CHỒNG: %s (%s) <-> %s (%s)" % (a[2], a[0], b[2], b[0]))
                bad += 1
    print("cặp chồng: %d" % bad)
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--sdk", default="mtk")
    ap.add_argument("--matrix", default=DEFAULT_MATRIX)
    ap.add_argument("--phase", type=int, action="append")
    ap.add_argument("--prefix")
    ap.add_argument("--dump", action="store_true", help="in mọi path cây C dựng được")
    ap.add_argument("--claims", action="store_true", help="chỉ kiểm .paths có chồng nhau không")
    args = ap.parse_args()

    if args.claims:
        return 1 if check_claims(args.src, args.sdk) else 0

    # Chỉ đọc đúng những file build MTK thật sự biên dịch: danh sách nguồn
    # của bin/Makefile.am cộng sdk/<sdk>/sdk.mk.  Quét cả thư mục sẽ kéo vào
    # model portable của iopsys (tr098/landevice.c ...) -- không nằm trong
    # build này và trùng tên bảng với module MTK.
    sources = collect_sources(args.src, args.sdk)
    defines = {"CUSTOM_PREFIX": "X_HNI_"}
    all_tables, all_roots, per_file = {}, [], {}
    for rel in sources:
        full = os.path.join(args.src, rel)
        if not os.path.isfile(full):
            continue
        tables, roots, shared = parse_file(full, defines)
        per_file[rel] = tables
        for k in shared:
            if k in all_tables and all_tables[k] != tables[k]:
                print("CẢNH BÁO: bảng dùng chung %s khai ở hai file đang build (%s)"
                      % (k, rel), file=sys.stderr)
            all_tables[k] = tables[k]
        for r in roots:
            all_roots.append((rel, r))

    tree = {}
    for fn, root in all_roots:
        if root not in per_file[fn] and root not in all_tables:
            print("CẢNH BÁO: %s khai .objs = %s nhưng không tìm thấy bảng"
                  % (fn, root), file=sys.stderr)
            continue
        merge(Scope(per_file[fn], all_tables), root, tree, frozenset())
    got = set()
    emit(tree, "InternetGatewayDevice.", got)
    got = {(k, norm(p)) for k, p in got}

    want = set()
    matrix_objs = set()
    rows = []
    with open(args.matrix, encoding="utf-8") as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) < 4:
                continue
            if c[2] == "obj":
                matrix_objs.add(norm(c[3]))
            if args.phase and int(c[0]) not in args.phase:
                continue
            # bộ trích xuất ghi biến shell ($1, $2, $3) ở chỗ số instance;
            # cây C dùng {i} -- quy về một chính tả trước khi so
            rows.append((c[2], norm(c[3])))
    # Cây con X.{i}. chỉ tồn tại trên sản phẩm khi object chứa X. được đăng ký
    # (dòng common_execute_method_obj mang hàm browse).  Bộ trích xuất đọc
    # văn bản, nên vẫn thu lá trong hàm sub_entry_* mà lời gọi browse đã bị
    # comment -- DNSDiagnostics.Result.{i}.* (dns_diagnostics:21) là trường
    # hợp duy nhất trên 783 dòng (25/09).  Các lá đó không được port, và
    # không tính là thiếu.
    unreachable = set()
    for k, pth in rows:
        parts = pth.split(".")
        dead = any(seg == "{i}" and ".".join(parts[:n]) + "." not in matrix_objs
                   for n, seg in enumerate(parts))
        if dead:
            if k == "param":
                unreachable.add(pth)
            continue
        want.add((k, pth))

    if args.prefix:
        got = {(k, p) for k, p in got if p.startswith(args.prefix)}
        want = {(k, p) for k, p in want if p.startswith(args.prefix)}
    elif args.phase:
        # chỉ so phần cây C nằm trong các nhánh mà phase đó khai
        pref = {p.split("{i}")[0] for k, p in want}
        got = {(k, p) for k, p in got if any(p.startswith(x) for x in pref)}

    gp = {p for k, p in got if k == "param"}
    wp = {p for k, p in want if k == "param"}
    if args.dump:
        for p in sorted(gp):
            print(p)
        return 0

    missing, extra = sorted(wp - gp), sorted(gp - wp)
    print("cây C  : %d param, %d object" % (len(gp), len([1 for k, _ in got if k == "obj"])))
    print("cây shell: %d param" % len(wp))
    if unreachable and not args.prefix:
        print("không tới được trên sản phẩm (không port, không tính thiếu): %d" % len(unreachable))
        for p in sorted(unreachable):
            print("  ~", p)
    print("thiếu  : %d" % len(missing))
    for p in missing:
        print("  -", p)
    print("dôi    : %d" % len(extra))
    for p in extra:
        print("  +", p)
    return 1 if (missing or extra) else 0


if __name__ == "__main__":
    sys.exit(main())
