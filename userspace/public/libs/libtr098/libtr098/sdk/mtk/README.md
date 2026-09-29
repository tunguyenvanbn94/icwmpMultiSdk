# SDK `mtk` — MediaTek / Airoha OpenWrt (HNI product)

`./configure --with-sdk=mtk`. Built by the OpenWrt feed package `libtr098`.

| | |
|---|---|
| Storage | UCI, the product's own schema (`easycwmp`, `network`, `wireless`, `system`, `pon`, `firewall`) + ubus |
| Data model | native C modules in `dm098/`, plus `compat/` while the port is in progress |
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

So `compat/` answers exactly the paths no C module claims yet, and every phase moves
parameters from it into `dm098/`. When the last one moves:

```sh
./configure --with-sdk=mtk --disable-dm-script-compat
rm -rf sdk/mtk/compat
```

Nothing else changes: `dm_platform_param_method()` then returns 0 for every path and an
unknown parameter is a plain 9005 from the engine.

## Adding an object

1. Write `dm098/<object>_mtk.c`: DMOBJ/DMLEAF tables plus getters and setters that read the
   **same** UCI option / ubus call / file the shell function read. Helpers in `dmmtk.h`.
2. End the file with a `struct dm_module` and `DM_MODULE_REGISTER()`, listing the full path
   in `.paths` — that single line takes the subtree away from `compat/`.
3. Add the file to `sdk.mk`.
4. Check the result against the old client on a board, per parameter:
   `ubus call tr069 dm '{"cmd":"get","path":"<object>."}'` vs
   `/usr/sbin/easycwmp get value "<object>."`.

The phase plan and the per-parameter status are in the issue:
`projects/mtk_openwrt_wifi7/issues/20260922_icwmp_multiplatform_tr098/`.
