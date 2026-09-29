# SDK `mtk` — MediaTek / Airoha OpenWrt (HNI product)

`./configure --enable-icwmp_tr098 --with-sdk=mtk`, built by the OpenWrt feed package
`icwmp_tr098`.

| | |
|---|---|
| Config of record | UCI `easycwmp` — the product's WebUI, `hal_gateway`, DHCP option 43 and `stuncd` all write it |
| icwmpd's config | UCI `cwmp`, a two-way mirror of the 16 options that overlap plus what only icwmpd has |
| Actions | `scripts/icwmp.sh` → the product's `hni_validate_image.sh`, `HumaxUpgradeStatus`, `next_reboot_reason`, `sysupgrade` |
| Files | `files/` — init script, the `easycwmpd` compatibility wrapper, the WAN watcher and the value-change poller lifted from the old package |

## What must not change

Six places in the product call the old client by name. They keep working because:

| Caller | Kept by |
|---|---|
| `/etc/init.d/easycwmpd reload` from HAL / WebUI / ubusmon | `files/easycwmpd`, a one line wrapper around `/etc/init.d/icwmpd` |
| WebUI writing UCI `easycwmp` | it stays the config of record, `icwmp_mtk.c` mirrors it |
| `stuncd.init` sed-ing into `/usr/share/easycwmp/functions/management_server` | the function library is installed at its original path |
| `ubus call tr069 …` | the ubus object name is unchanged |
| firewall rule `easycwmp-wan-access` | unchanged, the connection request port comes from the same UCI option |
| DHCP option 43 | writes `easycwmp.@acs[0].url`, mirrored into `cwmp` on reload |

The feed package declares `CONFLICTS:=cwmpclient` so the two clients can never be installed
together.
