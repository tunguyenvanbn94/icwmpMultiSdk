#!/usr/bin/env python3
"""Print the .paths claims of every sdk/mtk/dm098 module of this build
(the preprocessed sources, so macros like CUSTOM_PREFIX are resolved)."""
import re, subprocess, sys

src, inc = sys.argv[1], sys.argv[2]
cflags = ["-std=gnu11", "-DDM_SDK_MTK=1", "-DDM_PLATFORM_MTK=1", "-DDM_MTK_SCRIPT_COMPAT=1",
          '-DCUSTOM_PREFIX="X_HNI_"', '-DTR098_VERSION="3"', "-I" + src, "-I" + src + "/tr098",
          "-I" + src + "/tr098/common", "-I" + src + "/upnp", "-I" + src + "/sdk/mtk",
          "-I" + src + "/sdk/mtk/dm098", "-I" + inc]
mk = open(src + "/sdk/mtk/sdk.mk").read()
out = set()
for f in re.findall(r"\.\./(sdk/mtk/dm098/\S+\.c)", mk):
    e = subprocess.run(["gcc", "-E", "-P"] + cflags + [src + "/" + f], capture_output=True, text=True).stdout
    for name in set(re.findall(r"\.paths\s*=\s*(\w+)", e)):
        m = re.search(r"const char \*const " + name + r"\[\]\s*=\s*\{(.*?)\};", e, re.S)
        for item in m.group(1).split(","):
            parts = re.findall(r'"([^"]*)"', item)
            if parts:
                out.add("".join(parts))
if len(out) < 50:
    sys.exit("claims.py: only %d claims found" % len(out))
print("\n".join(sorted(out)))
