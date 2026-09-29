# SDK `bdk` — Broadcom BDK

`./configure --enable-icwmp_tr098 --with-sdk=bdk`, built by `apps/icwmp/Makefile`
(`ICWMP_SDK=bdk`) inside the BDK tree.

| | |
|---|---|
| Config of record | Broadcom MDM `Device.ManagementServer.*`, mirrored both ways with UCI `cwmp` |
| Actions | C, through the MDM and CMS (`external_bdk.c`): download, upload, reboot, factory reset |
| Persistence | `/data/icwmp` — the rootfs is a read-only squashfs |
| Extra libs | `$(BDK_LIBS)`: cms_core stack, `libmdm_cbk_tr69`, `libbcm_generic_hal`, `libcms_msg` |

`icwmp_bdk.h` is pulled in through `-DICWMP_SDK_HEADER` and carries the paths the shared code
needs (`CWMP_BKP_FILE`, the boot flag file), so no file outside this directory names the BDK.
