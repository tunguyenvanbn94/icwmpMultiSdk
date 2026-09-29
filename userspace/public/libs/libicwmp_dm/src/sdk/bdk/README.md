# SDK `bdk` — Broadcom BDK, Distributed MDM

`./configure --with-sdk=bdk`. Built in the BDK tree by `libs/libtr098/Makefile`
(`TR098_PLATFORM=bdk`), which runs autoreconf and configure for the target toolchain.

| | |
|---|---|
| Storage | Broadcom Distributed MDM (TR-181) through `libbcm_generic_hal` |
| Data model | TR-098 view of the MDM, `sdk/bdk/dm098/` — plus the TR-181 proxy |
| Extra deps | `libbcm_generic_hal`, `libcms_msg`, `libmdm_cbk_tr69`, CMS core libs |
| Vendor prefix | `X_MARUSYS_COM_` |
| Persistent paths | `/data/icwmp/tr098` (the rootfs is read only) |

## Layout

| Path | What |
|---|---|
| `dmplatform_bdk.c` | the `dm_platform_*` hooks: GPV/SPV batches on the HAL, commit of one RPC |
| `dmproxy_bdk.c` | `InternetGatewayDevice.X_MARUSYS_COM_Device.` ⇄ `Device.`, and the whole tree in TR-181 mode |
| `dm098/root_bdk.c` | the TR-098 module: which objects this SDK advertises |
| `dm098/root181_bdk.c` | the TR-181 module (what icwmpd adds itself) |
| `dm098/sample_bdk.c` | developer template, 7 parameter cases, gated by `-DBDK_SAMPLE_OBJECT` and `cwmp.sample.enable` |

## Two data models in one build

`cwmp.cpe.datamodel=tr181` makes `dm_platform_select_root()` swap the engine root to
`Device.` and the registry hands over the TR-181 module. Both models are registered at
start-up, only one is walked per session.

## Removing this SDK

```sh
./tools/sdk-prune.sh mtk uci     # keeps mtk and uci, deletes sdk/bdk
```

Nothing outside `sdk/bdk/` names it, except the two lines `tools/sdk-scan.sh` regenerates.
