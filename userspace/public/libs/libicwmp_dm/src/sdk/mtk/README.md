# SDK `mtk` — MediaTek / Airoha OpenWrt (HNI product)

`./configure --with-sdk=mtk`. Built by the OpenWrt feed package `libtr098`.

| | |
|---|---|
| Storage | UCI, the product's own schema (`easycwmp`, `network`, `wireless`, `system`, `pon`, `firewall`) + ubus |
| Data model | native C modules in `dm098/`: the whole TR-098 tree. `compat/` is left out of the product build (`--disable-dm-script-compat`, PH5) |
| Vendor prefix | `X_HNI_` (ours), `X_AIS_` (the operator's, already in the product tree) |
| Extra deps | libuci, libubox, libubus, json-c, libm |

## Layout

| Path | What |
|---|---|
| `dmplatform_mtk.c` | the `dm_platform_*` hooks: routing between the C tree and the compat layer, commit, notification |
| `dmmtk.c/.h` | helpers every module here uses: UCI, `/var/state`, `/proc`, exec, the apply-service queue |
| `dm098/*.c` | one file per object, each registering itself with `dm_registry.h` |
| `compat/` | migration scaffolding: the product's own easycwmp shell function library driven through a persistent child shell |

## Why there is a `compat/` at all

The product shipped a CWMP client whose data model is ~24 000 lines of shell:
**749 TR-098 parameters in 181 objects**, including the operator's `X_AIS_*` tree the ACS is
provisioned against. Only 34 of those parameters are a plain `uci get`; the other 692 go
through 449 shell helper functions. Porting them to C is worth doing — it is what makes a
GetParameterValues of the whole tree fast and what makes the same tree buildable on another
SDK — but it cannot be one change.

So `compat/` answered exactly the paths no C module claimed yet, and every phase moved
parameters from it into `dm098/`. The last ones moved in 0099 (parameters) and 0100
(AddObject/DeleteObject of the WAN connections); since PH5 the feed builds

```sh
./configure --with-sdk=mtk --disable-dm-script-compat
```

`dm_platform_param_method()` then returns 0 for every path, an unknown parameter is a
plain 9005 from the engine, `X_HNI_Icwmp.DataModelBackend` reads `mtk-c` and
`/usr/share/icwmp/icwmp_dm.sh` is not installed.  The source of `compat/` stays for now:
it is the rollback (drop the flag, put the two install lines of `feeds/libtr098` back) and
`icwmp_dm.sh` is what `tests/board/parity_dump.sh` drives the product shell with.

## Adding an object

1. Write `dm098/<object>_mtk.c`: DMOBJ/DMLEAF tables plus getters and setters that read the
   **same** UCI option / ubus call / file the shell function read. Helpers in `dmmtk.h`.
2. End the file with a `struct dm_module` and `DM_MODULE_REGISTER()`, listing the full path
   in `.paths` — that single line takes the subtree away from `compat/`.
3. Add the file to `sdk.mk`.
4. Check the result against the product shell on a board: `tests/board/` (whole tree,
   values, names, writable flags).

The phase plan and the per-parameter status are in the issue:
`projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098/`.
