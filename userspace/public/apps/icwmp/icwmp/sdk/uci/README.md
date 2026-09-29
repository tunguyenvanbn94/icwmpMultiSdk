# SDK `uci` — stock OpenWrt

`./configure --with-sdk=uci` (default). Reference implementation of the contract in
`sdk/sdk.h`: all six hooks are no-ops because the UCI `cwmp` config is already the config of
record and the actions go to the `/usr/sbin/icwmp` shell script through `external.c`.

Read `icwmp_uci.c` first when adding an SDK — it is the whole contract in 60 lines.
