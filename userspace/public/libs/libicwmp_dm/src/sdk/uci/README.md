# SDK `uci` — OpenWrt, stock iopsys tree

Reference SDK, upstream behaviour. Selected with `./configure --with-sdk=uci` (the default).

| | |
|---|---|
| Storage | UCI (`network`, `wireless`, `dhcp`, `firewall`, `router.*`) |
| Data model | the portable modules in `tr098/`, registered by `tr098/root.c` |
| Extra deps | libuci, libubox, libubus, json-c |

Nothing here is product specific. Use it as the starting point for a new SDK:

```sh
cp -r sdk/uci sdk/<new>
# edit sdk/<new>/sdk.m4, sdk.mk, dmplatform_<new>.c
./tools/sdk-scan.sh          # picks the directory up, rewrites sdk/enabled.*
./configure --with-sdk=<new>
```

Deleting this directory is allowed: `./tools/sdk-prune.sh <other sdk>` does it and
rewrites `sdk/enabled.*`, nothing else refers to the name `uci`.
