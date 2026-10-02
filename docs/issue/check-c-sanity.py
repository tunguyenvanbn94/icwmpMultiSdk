#!/usr/bin/env python3
"""Kiểm tra tĩnh nguồn C của bản port, cho máy KHÔNG có compiler.

Không thay được compiler. Mục tiêu hẹp: bắt đúng những lỗi mà không có
compiler thì không thấy, nhưng đọc bằng mắt lại rất dễ bỏ sót.

    ./check-c-sanity.py                      # thư viện libtr098, mọi file MTK
    ./check-c-sanity.py --tree app           # gói app icwmp_tr098d
    ./check-c-sanity.py --tree app --sdk bdk
    ./check-c-sanity.py <file.c> [...]

Bắt:
  1. `*/` đóng block comment SỚM (ví dụ `mlo_sync_*/backhaul_sync_*`) — lỗi
     đã thật sự lọt ra máy build ngày 24/09.
  2. Block comment hoặc chuỗi không đóng tới hết file.
  3. Ngoặc {} () [] lệch sau khi bỏ comment và chuỗi.
  4. Hàm gọi trong file nhưng không định nghĩa ở file, không khai báo ở header
     nào của cây, cũng không nằm trong danh sách libc/libubox/json-c.
  5. Hàm `static` trùng tên với một khai báo KHÔNG static trong header mà
     chính file đó include (kể cả include gián tiếp) — gcc báo
     "static declaration of 'x' follows non-static declaration". Lỗi này đã
     thật sự lọt ra máy build ba lần: deviceinfo_mtk.c, wlansec_mtk.c, rồi
     icwmp_dm.c của gói app (`dm_add_end_session`).  Cây app include header
     của libtr098 bằng <icwmp_dm/...>, nên phải map tiền tố đó về cây thư
     viện thì mới thấy được.
  6. Hai file đang build cùng định nghĩa một hàm KHÔNG static — trùng symbol
     lúc link.
  7. Macro mang sẵn dấu `;` (inc/log.h: `#define CWMP_LOG(...) puts_log(...);`)
     dùng làm thân KHÔNG ngoặc của một `if` có `else` đi sau — `if (x) f();;
     else` là `else` mồ côi, gcc báo "expected '}' before 'else'".  Lớp 3 mù
     với lỗi này vì nó đếm ngoặc TRƯỚC khi macro được khai triển.
  8. Truyền `NULL` vào một tham số `T **` mà hàm nhận GHI qua nó (`*p = ...`)
     không điều kiện.  Compiler im lặng, thiết bị segfault.  Đã suýt lọt:
     `dmuci_add_section(pkg, type, &s, NULL)` -- dmuci.c ghi `*value` trên
     MỌI đường ra, kể cả đường lỗi.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "brcm_ap_wifi7_mvn", "issues",
    "20260916_tr069_app_use_icwmp", "sdk-overlay", "userspace", "public",
    "libs", "libicwmp_dm", "src"))

APP_SRC = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "brcm_ap_wifi7_mvn", "issues",
    "20260916_tr069_app_use_icwmp", "sdk-overlay", "userspace", "public",
    "apps", "icwmp", "icwmp"))

# Gói app include header của thư viện bằng <icwmp_dm/dmtr098.h>; trên máy build
# đó là bản đã install vào staging_dir, ở đây là chính cây nguồn libtr098.
ANGLE_PREFIX = {"icwmp_dm/": DEFAULT_SRC}

# Cây nào lấy nguồn ở đâu, include ở đâu, và build ra binary nào.
#   target=None  -> lấy mọi *_SOURCES trong Makefile.am (libtr098 chỉ có một)
#   target=<tên> -> CHỈ lấy <tên>_SOURCES.  Cây app còn dựng icwmp_xmppd,
#   icwmp_twampd, icwmp_udpechoserverd -- mỗi cái có bản dmuci_* riêng, gộp
#   chung vào là báo trùng symbol nhầm.
TREES = {
    "lib": {
        "root": DEFAULT_SRC,
        "target": None,
        "inc": ("", "tr098", "tr098/common", "upnp", "sdk/%(sdk)s",
                "sdk/%(sdk)s/dm098"),
    },
    "app": {
        "root": APP_SRC,
        "target": "icwmp_tr098d",
        "inc": ("", "inc", "sdk", "sdk/%(sdk)s", "bulkdata", "stun"),
    },
}

SRC_RE = re.compile(r"^\s*\.\./(\S+\.c)\s*\\?\s*$")
VAR_RE = re.compile(r"^\s*(\w+)_SOURCES\s*\+?=")
COND_RE = re.compile(r"^(if|else|endif)\b\s*(\S*)")
# Điều kiện automake luôn SAI ở mọi build của ba SDK (25/09): khối của chúng
# không vào binary, kiểm chúng là báo lỗi ma.  UPNP_TR064 chỉ bật bằng
# --enable-tr064 mà không feed nào truyền.  Điều kiện lạ -> coi là đúng.
COND_FALSE = {"UPNP_TR064", "!ICWMP_TR098"}

# tên của libc / libubox / json-c / uci mà cây này dùng
EXTERNAL = set("""
strcmp strncmp strcasecmp strncasecmp strcpy strncpy strcat strncat strlen strnlen
strchr strrchr strstr strtok_r strdup strerror memcmp memcpy memmove memset
snprintf sprintf vsnprintf printf fprintf sscanf
malloc calloc realloc free abort exit
atoi atol atoll strtol strtoul strtoll strtod
isdigit isxdigit isalpha isalnum isspace isupper islower toupper tolower
open close read write lseek unlink access stat lstat mkdir rmdir rename
fopen fclose fgets fputs fread fwrite fflush feof ferror fileno remove
popen pclose fork execv execvp waitpid kill _exit dup2 pipe
posix_spawn posix_spawnp posix_spawn_file_actions_init posix_spawn_file_actions_destroy
posix_spawn_file_actions_addclose posix_spawn_file_actions_adddup2
time localtime gmtime mktime strftime strptime difftime gettimeofday
socket bind connect send recv inet_ntop inet_pton htons htonl ntohs ntohl
opendir readdir closedir glob globfree
json_object_object_get_ex json_object_get_string json_object_array_length
json_object_array_get_idx json_object_get_type json_object_put json_object_get
json_object_new_object json_object_new_string json_object_object_add
json_object_object_foreach json_object_to_json_string json_tokener_parse
blobmsg_data blobmsg_data_len blobmsg_parse blobmsg_get_string
list_add list_add_tail list_del list_empty INIT_LIST_HEAD list_entry
uci_lookup_ptr uci_set uci_commit uci_free_context uci_alloc_context
va_start va_end va_arg
strcspn strspn strtoull strtoumax abs labs llabs log10 pow floor ceil
usleep sleep nanosleep poll select signal execl execlp getenv setenv unsetenv
sysconf memchr ftell fseek rewind vfprintf fputc fgetc getline
pthread_mutex_lock pthread_mutex_unlock pthread_mutex_init pthread_mutex_destroy
pthread_create pthread_join pthread_detach pthread_self
json_object_object_get json_object_is_type json_object_new_array
json_object_array_add json_object_new_int json_object_get_int
uci_foreach_element uci_foreach_element_safe uci_save uci_load uci_unload
list_for_each list_for_each_entry list_for_each_entry_safe list_to_element
container_of uci_to_section uci_to_option uci_list_configs uci_list_empty
uci_revert uci_add_list uci_del_list uci_add_section uci_delete uci_rename
ubus_connect ubus_free ubus_lookup_id ubus_invoke blob_buf_init blob_buf_free
blobmsg_add_json_element blobmsg_format_json_indent blobmsg_open_table
blobmsg_open_array blobmsg_close_table blobmsg_close_array blobmsg_add_string
clock_gettime inet_ntoa inet_aton asprintf vasprintf strtok uname wait
vsprintf vsprintf_s MD5Transform _list_add
getpid strpbrk if_nametoindex regcomp regexec regfree fnmatch
""".split())

# Hàm sinh bằng nối token trong header: `dmuci_delete_by_section_##UCI_PATH`
# cho ra `dmuci_delete_by_section_tr098`.  Không khai triển được bằng regex,
# nên chấp nhận mọi tên bắt đầu bằng phần cố định.
PASTE_RE = re.compile(r"\b(\w+_)##\w+\s*\(")
PASTE_PREFIXES = set()

BLOCK_OPEN = re.compile(r"/\*")


def scan_comments(src, path, problems):
    """Đi qua từng ký tự, theo đúng luật của trình biên dịch."""
    i, n = 0, len(src)
    line = 1
    state = "code"          # code | block | line | str | chr
    block_start = 0
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if c == "\n":
            line += 1
            if state in ("line",):
                state = "code"
            i += 1
            continue
        if state == "code":
            if c == "/" and nxt == "*":
                state, block_start = "block", line
                i += 2
                continue
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == '"':
                state = "str"
            elif c == "'":
                state = "chr"
            i += 1
            continue
        if state == "block":
            if c == "*" and nxt == "/":
                # `*/` của một wildcard kiểu `foo_*/bar_*`: chữ dính liền ở
                # CẢ HAI phía.  Chỉ nhìn phía trước là không đủ -- upstream
                # viết `/* Only One instance should run*/` rất nhiều, và đó
                # là comment đóng đúng chỗ (sau `*/` là hết dòng).
                prev = src[i - 1] if i else ""
                after = src[i + 2] if i + 2 < n else ""
                if (prev and (prev.isalnum() or prev in "_)]")
                        and after and (after.isalnum() or after == "_")):
                    problems.append((line, "comment đóng SỚM ở `%s*/%s` — block mở dòng %d"
                                     % (prev, after, block_start)))
                state = "code"
                i += 2
                continue
            i += 1
            continue
        if state == "line":
            i += 1          # comment // chỉ kết thúc ở '\n', đã xử lý ở trên
            continue
        if state in ("str", "chr"):
            if c == "\\":
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = "code"
            i += 1
            continue
        i += 1              # không rơi ra khỏi vòng mà quên tăng con trỏ
    if state == "block":
        problems.append((block_start, "block comment không đóng tới hết file"))
    elif state in ("str", "chr"):
        problems.append((line, "chuỗi/ký tự không đóng"))


def strip(src):
    """Bỏ comment và chuỗi, giữ số dòng."""
    out = []
    i, n = 0, len(src)
    state = "code"
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == '"':
                state = "str"
                out.append('""')
                i += 1
                continue
            if c == "'":
                state = "chr"
                out.append("''")
                i += 1
                continue
            out.append(c)
            i += 1
            continue
        if state == "block":
            if c == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
            if c == "\n":
                out.append("\n")
            i += 1
            continue
        if state == "line":
            if c == "\n":
                state = "code"
                out.append("\n")
            i += 1
            continue
        if c == "\\":
            i += 2
            continue
        if (state == "str" and c == '"') or (state == "chr" and c == "'"):
            state = "code"
        i += 1
    return "".join(out)


def one_branch(code):
    """Giữ nhánh đầu của mỗi #if, bỏ #elif/#else -- gần đúng một cấu hình
    build.  Không đánh giá điều kiện, chỉ cần ngoặc khớp như compiler thấy."""
    out, skip_depth, depth = [], 0, 0
    for ln in code.splitlines():
        t = ln.lstrip()
        if t.startswith("#if"):
            depth += 1
            if skip_depth:
                out.append("")
                continue
        elif t.startswith("#el") and depth and not skip_depth:
            skip_depth = depth
        elif t.startswith("#endif"):
            if skip_depth == depth:
                skip_depth = 0
            depth = max(0, depth - 1)
        out.append("" if skip_depth else ln)
    return "\n".join(out)


def check_balance(code, problems):
    code = one_branch(code)
    for o, c, name in (("{", "}", "{}"), ("(", ")", "()"), ("[", "]", "[]")):
        a, b = code.count(o), code.count(c)
        if a != b:
            problems.append((0, "ngoặc %s lệch: %d mở / %d đóng" % (name, a, b)))


DEF_RE = re.compile(r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;]*?\)\s*\{", re.M)
DECL_RE = re.compile(r"\b(\w+)\s*\(")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "defined",
            "do", "else", "case", "break", "continue", "goto", "typedef",
            "struct", "union", "enum", "static", "const", "void", "int",
            "char", "unsigned", "long", "short", "float", "double", "extern",
            "inline", "register", "volatile", "signed", "_Bool"}


def header_symbols(src_roots):
    names = set()
    for base, _dirs, files in _walk_headers(src_roots):
        for fn in files:
            if not fn.endswith(".h"):
                continue
            text = open(os.path.join(base, fn), encoding="utf-8", errors="replace").read()
            text = strip(text)
            for m in DECL_RE.finditer(text):
                names.add(m.group(1))
            # macro có tham số cũng dùng như hàm
            for m in re.finditer(r"^\s*#\s*define\s+(\w+)\s*\(", text, re.M):
                names.add(m.group(1))
            for m in PASTE_RE.finditer(text):
                PASTE_PREFIXES.add(m.group(1))
    return names


def check_calls(path, src, code, known, problems):
    defined = set(DEF_RE.findall(code))
    # macro định nghĩa ngay trong file
    defined |= set(re.findall(r"^\s*#\s*define\s+(\w+)", code, re.M))
    # hàm sinh bằng macro kiểu WAN_STAT_GETTER(tên, ...)
    defined |= set(re.findall(r"^[A-Z][A-Z0-9_]*\(\s*(\w+)\s*,", code, re.M))
    # tham số của macro trong chính file: `#define ASSOC_GET(fn, member)` thì
    # `fn(...)` trong thân macro là tên do người gọi đặt, không phải hàm thiếu
    # con trỏ hàm: tham số kiểu `int (*measure)(...)` hay biến cùng dạng
    defined |= set(re.findall(r"\(\s*\*\s*(\w+)\s*\)\s*\(", code))
    for m in re.finditer(r"^\s*#\s*define\s+\w+\(([^)]*)\)", code, re.M):
        for arg in m.group(1).split(","):
            arg = arg.strip()
            if re.fullmatch(r"\w+", arg):
                defined.add(arg)
    for m in DECL_RE.finditer(code):
        name = m.group(1)
        if name in KEYWORDS or name in defined or name in known or name in EXTERNAL:
            continue
        if name.isupper():          # macro của engine (UBUS_ARGS, DMJSON_ARGS...)
            continue
        # `ctx->get_permission(...)`, `m->init()`: thành viên struct, không
        # phải hàm toàn cục thiếu khai báo.
        before = code[:m.start()].rstrip()
        if before.endswith("->") or before.endswith("."):
            continue
        # Tên còn xuất hiện ở dạng KHÔNG gọi (`if (cb)`, `f(cb, priv)`) thì nó
        # là con trỏ hàm -- tham số hoặc biến.  Một hàm thiếu khai báo thật thì
        # chỉ bao giờ cũng xuất hiện kèm `(` ngay sau.
        if re.search(r"\b%s\b\s*(?!\()" % re.escape(name), code):
            continue
        if any(name.startswith(pfx) for pfx in PASTE_PREFIXES):
            continue
        line = code[:m.start()].count("\n") + 1
        problems.append((line, "gọi `%s()` mà không thấy định nghĩa hay khai báo" % name))


HDR_DECL_RE = re.compile(
    r"^\s*(?!static\b)[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*;", re.M)
STATIC_DEF_RE = re.compile(r"^\s*static\s+[\w \t\*]*?\b(\w+)\s*\(", re.M)
EXTERN_DEF_RE = re.compile(r"^(?!static\b)[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;]*\)\s*$", re.M)


def find_header(roots, name, angle=False):
    """roots: [(gốc cây, các thư mục -I)].  Include kiểu <icwmp_dm/x.h> chỉ
    tra trong cây mà ANGLE_PREFIX trỏ tới."""
    if angle:
        for prefix, root in ANGLE_PREFIX.items():
            if name.startswith(prefix):
                path = os.path.normpath(os.path.join(root, name[len(prefix):]))
                return path if os.path.isfile(path) else None
        return None
    for root, incs in roots:
        for d in incs:
            path = os.path.normpath(os.path.join(root, d, name))
            if os.path.isfile(path):
                return path
    return None


def include_closure(roots, path, seen=None):
    """Header của cây mà file này kéo vào, kể cả gián tiếp."""
    if seen is None:
        seen = set()
    # KHÔNG dùng strip() ở đây: nó thay nội dung mọi chuỗi bằng rỗng, kể cả
    # tên file trong #include "...".  Chỉ bỏ comment.
    raw = open(path, encoding="utf-8", errors="replace").read()
    raw = re.sub(r"/\*.*?\*/", "", raw, flags=re.S)
    raw = re.sub(r"//[^\n]*", "", raw)
    for m in re.finditer(r'^\s*#\s*include\s+(?:"([^"]+)"|<([^>]+)>)', raw, re.M):
        h = find_header(roots, m.group(1) or m.group(2), angle=m.group(2) is not None)
        if h and h not in seen:
            seen.add(h)
            include_closure(roots, h, seen)
    return seen


def _walk_headers(src_roots):
    for root in src_roots:
        for base, dirs, files in os.walk(root):
            if os.sep + ".git" in base:
                continue
            yield base, dirs, files


def header_decls(src_roots):
    """{đường dẫn header: {tên hàm non-static}}"""
    out = {}
    for base, _dirs, files in _walk_headers(src_roots):
        for fn in files:
            if not fn.endswith(".h"):
                continue
            path = os.path.normpath(os.path.join(base, fn))
            code = strip(open(path, encoding="utf-8", errors="replace").read())
            out[path] = set(HDR_DECL_RE.findall(code))
    return out


def check_static_clash(roots, path, code, decls, problems):
    visible = {}
    for h in include_closure(roots, path):
        for name in decls.get(h, ()):
            visible.setdefault(name, h)
    for m in STATIC_DEF_RE.finditer(code):
        name = m.group(1)
        if name in visible:
            line = code[:m.start()].count("\n") + 1
            problems.append((line, "static `%s` trùng khai báo non-static ở %s -- "
                                   "gcc: static declaration follows non-static"
                             % (name, _short(visible[name]))))


# Macro mà thân kết thúc bằng `;`: dùng không ngoặc trong `if` có `else` đi
# sau là lỗi biên dịch.  Chỉ nhận macro có tham số -- macro hằng số không phải
# câu lệnh.
SEMI_MACRO_RE = re.compile(r"^\s*#\s*define\s+(\w+)\s*\([^)]*\)\s+.*;\s*$", re.M)
CTRL_RE = re.compile(r"^([ \t]*)(?:\}[ \t]*)?(if|else[ \t]+if|else|for|while)\b(.*)$")


def semi_macros(src_roots):
    out = set()
    for base, _dirs, files in _walk_headers(src_roots):
        for fn in files:
            if not fn.endswith(".h"):
                continue
            path = os.path.join(base, fn)
            text = open(path, encoding="utf-8", errors="replace").read()
            for m in SEMI_MACRO_RE.finditer(text):
                out.add(m.group(1))
    return out


def check_semi_macro_else(src, macros, problems):
    """`if (...) MACRO(...);` không ngoặc, rồi `else` -> else mồ côi."""
    lines = src.splitlines()
    for i, ln in enumerate(lines):
        m = CTRL_RE.match(ln)
        if not m or m.group(2) not in ("if", "else if", "else"):
            continue
        if ln.rstrip().endswith("{"):
            continue
        tail = m.group(3).strip()
        body_no = i
        body = tail
        if not body or body.endswith(")"):      # thân nằm ở dòng kế
            if i + 1 >= len(lines):
                continue
            body_no = i + 1
            body = lines[body_no].strip()
        name = body.split("(")[0].strip()
        if name not in macros:
            continue
        # câu lệnh ngay sau thân có phải `else` không?
        nxt = body_no + 1
        while nxt < len(lines) and not lines[nxt].strip():
            nxt += 1
        if nxt < len(lines) and re.match(r"^\s*else\b", lines[nxt]):
            problems.append((body_no + 1,
                             "`%s(...)` không ngoặc làm thân `%s`, ngay sau là `else` -- "
                             "macro mang sẵn `;` nên else mồ côi; bọc cả hai nhánh bằng {}"
                             % (name, m.group(2))))


# Tham số `T **p` mà thân hàm có `*p =`: gọi với NULL ở vị trí đó là segfault.
OUTPARAM_DEF_RE = re.compile(
    r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\(([^;{]*)\)\s*$", re.M)


def out_params(src_roots):
    """{tên hàm: {vị trí tham số (0-based) là con trỏ-ra bắt buộc}}"""
    out = {}
    for root in src_roots:
        for base, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d != ".git"]
            for fn in files:
                if not fn.endswith(".c"):
                    continue
                path = os.path.join(base, fn)
                code = strip(open(path, encoding="utf-8", errors="replace").read())
                lines = code.splitlines()
                for i, ln in enumerate(lines):
                    m = OUTPARAM_DEF_RE.match(ln)
                    if not m or i + 1 >= len(lines) or lines[i + 1].strip() != "{":
                        continue
                    body = "\n".join(lines[i + 2:i + 60])
                    end = body.find("\n}")
                    if end >= 0:
                        body = body[:end]
                    marks = set()
                    for pos, arg in enumerate(m.group(2).split(",")):
                        am = re.search(r"\*\s*\*\s*(\w+)\s*$", arg.strip())
                        if not am:
                            continue
                        if re.search(r"(?<![\w>.])\*\s*%s\s*=[^=]" % re.escape(am.group(1)), body):
                            marks.add(pos)
                    if marks:
                        out.setdefault(m.group(1), set()).update(marks)
    return out


def split_args(text):
    args, depth, cur = [], 0, ""
    for ch in text:
        if ch == "," and depth == 0:
            args.append(cur)
            cur = ""
            continue
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        cur += ch
    args.append(cur)
    return args


def check_null_outparam(code, outp, problems):
    for m in re.finditer(r"\b(\w+)\s*\(", code):
        name = m.group(1)
        if name not in outp:
            continue
        depth, j, n = 0, m.end() - 1, len(code)
        while j < n:
            if code[j] == "(":
                depth += 1
            elif code[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        if j >= n:
            continue
        args = split_args(code[m.end():j])
        for pos in sorted(outp[name]):
            if pos < len(args) and args[pos].strip() == "NULL":
                line = code[:m.start()].count("\n") + 1
                problems.append((line, "`%s()` nhận NULL ở tham số %d, nhưng hàm ghi "
                                       "`*p = ...` qua nó -- segfault lúc chạy"
                                 % (name, pos + 1)))


JSON_FOREACH_RE = re.compile(r"json_object_object_foreach\s*\(\s*(\w+)")


def check_json_foreach(path, code, problems):
    """json_object_object_foreach(X, ...) expands to
    json_object_get_object(X)->head: NULL->head when X is an array, a string,
    a number.  Board crash 2026-09-26 (wlanassoc_mtk.c, "infor" is an array).
    In the ported code (sdk/) X must be checked for json_type_object in the
    40 lines before the loop."""
    if os.sep + "sdk" + os.sep not in path:
        return
    lines = code.split("\n")
    for m in JSON_FOREACH_RE.finditer(code):
        var = m.group(1)
        line = code.count("\n", 0, m.start()) + 1
        window = "\n".join(lines[max(0, line - 41):line - 1])
        ok = re.search(r"json_object_is_type\s*\(\s*%s\s*,\s*json_type_object" % var, window) or \
            re.search(r"json_object_get_type\s*\(\s*%s\s*\)\s*[!=]=\s*json_type_object" % var, window)
        if not ok:
            problems.append((line, "json_object_object_foreach(`%s`) mà không kiểm `%s` là "
                             "json_type_object trước -- mảng/chuỗi/số sẽ làm NULL->head (SIGSEGV)"
                             % (var, var)))


def check_duplicate_symbols(files, codes):
    """Hàm không static định nghĩa ở hai file cùng build = trùng symbol khi link."""
    owner = {}
    dup = []
    for path, code in zip(files, codes):
        for m in EXTERN_DEF_RE.finditer(code):
            head = code[m.start():m.start() + 400].split(";")[0]
            if "{" not in head:
                continue
            name = m.group(1)
            if name in owner and owner[name] != path:
                dup.append((name, owner[name], path))
            else:
                owner[name] = path
    return dup


def collect_sources(src, sdk="mtk", target=None):
    """Chỉ những .c thật sự được build, và khi có `target` thì chỉ của đúng
    binary đó -- theo dõi từng khối `<var>_SOURCES = ... \\` trong Makefile."""
    out = []
    for mk in (os.path.join(src, "bin", "Makefile.am"),
               os.path.join(src, "sdk", sdk, "sdk.mk")):
        if not os.path.isfile(mk):
            continue
        cur = None
        cont = False
        conds = []		# ngăn xếp khối if/else/endif: True = đang build
        for line in open(mk, encoding="utf-8", errors="replace"):
            mc = COND_RE.match(line)
            if mc:
                kw, name = mc.group(1), mc.group(2)
                if kw == "if":
                    conds.append(name not in COND_FALSE)
                elif kw == "else" and conds:
                    conds[-1] = not conds[-1]
                elif kw == "endif" and conds:
                    conds.pop()
                cur, cont = None, False
                continue
            if not all(conds):
                continue
            m = VAR_RE.match(line)
            if m:
                cur = m.group(1)
                cont = line.rstrip().endswith("\\")
            elif not cont:
                cur = None
            else:
                cont = line.rstrip().endswith("\\")
            if target and cur != target:
                continue
            m = SRC_RE.match(line)
            if m:
                path = os.path.normpath(os.path.join(src, m.group(1)))
                if path not in out:
                    out.append(path)
    return out


def _short(path):
    """Đường dẫn gọn, tính từ gốc cây gần nhất."""
    best = path
    for root in (DEFAULT_SRC, APP_SRC):
        rel = os.path.relpath(path, root)
        if not rel.startswith("..") and len(rel) < len(best):
            best = ("libtr098/" if root == DEFAULT_SRC else "app/") + rel
    return best


def main():
    args = sys.argv[1:]
    tree, sdk = "lib", "mtk"
    files = []
    i = 0
    while i < len(args):
        if args[i] == "--tree":
            tree = args[i + 1]
            i += 2
        elif args[i] == "--sdk":
            sdk = args[i + 1]
            i += 2
        else:
            files.append(args[i])
            i += 1
    if tree not in TREES:
        print("--tree phải là %s" % " hoặc ".join(sorted(TREES)))
        return 2
    cfg = TREES[tree]
    src_root = cfg["root"]
    # Cây app đọc cả header của mình lẫn header libtr098 (qua <icwmp_dm/...>).
    roots = [(src_root, tuple(d % {"sdk": sdk} for d in cfg["inc"]))]
    hdr_roots = [src_root]
    if tree == "app":
        roots.append((DEFAULT_SRC, TREES["lib"]["inc"]))
        hdr_roots.append(DEFAULT_SRC)

    if not files:
        files = collect_sources(src_root, sdk, cfg["target"])
    known = header_symbols(hdr_roots)
    decls = header_decls(hdr_roots)
    macros = semi_macros(hdr_roots)
    outp = out_params(hdr_roots)
    bad = 0
    checked, codes = [], []
    for path in files:
        if not os.path.isfile(path):
            print("THIẾU:", path)
            bad += 1
            continue
        src = open(path, encoding="utf-8", errors="replace").read()
        problems = []
        scan_comments(src, path, problems)
        code = strip(src)
        check_balance(code, problems)
        if tree == "lib":
            # Cây app gọi rất nhiều hàm của BDK/CMS mà header không nằm trong
            # workspace -- lớp này chỉ có nghĩa ở cây thư viện.
            check_calls(path, src, code, known, problems)
        check_static_clash(roots, path, code, decls, problems)
        check_semi_macro_else(src, macros, problems)
        check_null_outparam(code, outp, problems)
        check_json_foreach(path, code, problems)
        checked.append(path)
        codes.append(code)
        rel = os.path.relpath(path, src_root)
        if problems:
            bad += 1
            for line, msg in sorted(problems):
                print("%s:%d  %s" % (rel, line, msg))
        else:
            print("%s: OK" % rel)
    for name, a, b in check_duplicate_symbols(checked, codes):
        print("TRÙNG SYMBOL khi link: `%s` định nghĩa ở %s và %s"
              % (name, os.path.relpath(a, src_root), os.path.relpath(b, src_root)))
        bad += 1
    print("\n[%s/%s] %d file kiểm, %d vấn đề" % (tree, sdk, len(files), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
