/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	libtr098 SDK contract.
 *
 *	Two independent dimensions, do not mix them up:
 *
 *	  data model   WHAT the tree looks like.  Portable modules live in
 *	               tr098/ (TR-098) and later tr181/ (TR-181).  They only use
 *	               dmuci / dmubus / dmcommon and must compile on every SDK.
 *	  SDK          WHERE a value really is and HOW it is written.  One
 *	               directory per SDK under sdk/<name>/, self contained:
 *
 *	    sdk/<name>/sdk.m4    configure fragment (AC_DEFINE, extra flags)
 *	    sdk/<name>/sdk.mk    automake fragment (sources, CFLAGS, install)
 *	    sdk/<name>/*.c       the dm_platform_* hooks below
 *	    sdk/<name>/dm098/    data model modules only this SDK has
 *	    sdk/<name>/compat/   optional scaffolding, deletable on its own
 *	    sdk/<name>/README.md what the SDK needs, how it is wired
 *
 *	Nothing outside sdk/<name>/ names that SDK: tools/sdk-scan.sh rebuilds
 *	sdk/enabled.m4 and sdk/enabled.mk from whatever directories exist, so
 *
 *	    ./tools/sdk-prune.sh mtk       (keep only sdk/mtk, drop the rest)
 *
 *	leaves a tree that still configures and builds.  Same rule in icwmp.
 *
 *	SDKs in this tree:
 *
 *	  uci   sdk/uci/   OpenWrt, stock iopsys layout.  The portable tr098/
 *	                   modules on plain UCI, no vendor glue.  Reference SDK.
 *	  bdk   sdk/bdk/   Broadcom BDK Distributed MDM through
 *	                   libbcm_generic_hal.  TR-098 tree projected on the
 *	                   TR-181 MDM (sdk/bdk/dm098/), TR-181 proxy object.
 *	  mtk   sdk/mtk/   MediaTek/Airoha OpenWrt, HNI product tree.  Native C
 *	                   objects in sdk/mtk/dm098/.  sdk/mtk/compat/ still
 *	                   drives the product's easycwmp shell function library
 *	                   for the objects not ported to C yet, and disappears
 *	                   with --disable-dm-script-compat (see sdk/mtk/README.md).
 *
 *	Data model modules register themselves with dm_registry.h, they are not
 *	listed anywhere central: the root tree of a build is whatever modules got
 *	linked into it, merged by name.
 *
 *	Transaction model (mirrors what tr69c does in dmCms.c):
 *	  SetParameterValues = VALUECHECK for every param (leaf setter validates
 *	  and only records), then dm_entry_apply() = VALUESET for every param
 *	  (leaf setter queues the write), then dm_platform_commit() = one batch
 *	  write to the SDK.  A failure in commit is reported per parameter
 *	  through ctx->list_fault_param and the whole RPC is faulted.
 */
#ifndef __LIBTR098_SDK_H__
#define __LIBTR098_SDK_H__

#include "../dmtr098.h"
#include "../dm_registry.h"

/* Called from dm_ctx_init()/dm_ctx_clean() with CTX_INIT_ALL (once per RPC). */
int dm_platform_ctx_init(struct dmctx *ctx);
int dm_platform_ctx_clean(struct dmctx *ctx);

/* End of dm_entry_apply(CMD_SET_VALUE): flush queued writes.  parameter_key
 * is the ParameterKey of the SPV (may be NULL/"").  Returns 0 or a CWMP
 * fault code (FAULT_9xxx).  On fault the platform must leave nothing
 * half-applied where it can avoid it.
 *   uci: nothing (dmuci_commit() in the engine does it)
 *   bdk: one bcm_generic_setParameterValues for the whole RPC
 *   mtk: "set_apply" of the script child = run the queued shell setters,
 *        uci commit, easycwmp parameter_key */
int dm_platform_commit(struct dmctx *ctx, const char *parameter_key);

/* Drop queued writes (set failed during VALUESET, nothing was committed). */
void dm_platform_revert(struct dmctx *ctx);

/* Called by icwmp at the end of every session (dm_entry_restart_services).
 * uci: "ubus call uci commit" per changed package.  bdk: nothing to do, the
 * MDM already applied the values, persistence is handled by icwmpd
 * (icwmp_bdk_save_config) after the session.  mtk: uci commit of the
 * packages the static tree changed + "apply_service" of the script child
 * (ucitrack service restarts, delayed commands of the setters). */
int dm_platform_restart_services(void);

/* Human readable name, for logs / ManagementServer.X_..._Icwmp.DataModelBackend. */
const char *dm_platform_name(void);

/* Optional platform subtree handled outside the static DMOBJ/DMLEAF tables.
 * Called first thing in dm_entry_param_method() (and again for every VALUESET
 * of dm_entry_apply): returns 1 when inparam belongs to the platform subtree
 * and the command was handled (*fault = 0 or a FAULT_9xxx), 0 to let the
 * normal tree walk run.
 *   uci: always 0.
 *   bdk: the TR-181 proxy InternetGatewayDevice.X_MARUSYS_COM_Device. <->
 *        Device. (sdk/bdk/dmproxy_bdk.c), the whole tree in TR-181 mode.
 *   mtk: every path not claimed by a native object of sdk/mtk/dm098/root_mtk.c
 *        goes to the script child, the static tree is walked afterwards for
 *        paths that cover both and the two lists are merged. */
int dm_platform_param_method(struct dmctx *ctx, int cmd, char *inparam, char *arg1, int *fault);

/* Called at the end of every dm_ctx_init()/dm_ctx_init_sub(), after the engine
 * set its TR-098 defaults (dmroot = "InternetGatewayDevice", ctx->dm_entryobj =
 * tEntry098Obj).  Returns 1 when the platform replaced them.  uci/mtk: 0.
 * bdk: with cwmp.cpe.datamodel=tr181 the root becomes "Device" / tEntry181Obj
 * and dm_platform_param_method() serves the whole tree (sdk/bdk/dmproxy_bdk.c). */
int dm_platform_select_root(struct dmctx *ctx);

/* dm_entry_enabled_notify(): (re)build DM_ENABLED_NOTIFY from the notification
 * lists of the cwmp config.  Returns 1 when the platform did it without the
 * static tree (BDK TR-181 mode), 0 to let the tree walk add its own entries
 * afterwards (mtk: the script parameters were already written). */
int dm_platform_enabled_notify(struct dmctx *ctx);

/* dm_entry_enabled_notify_check_value_change(): same for the start-up diff of
 * DM_ENABLED_NOTIFY against the current values (ctx->add_list_value_change /
 * ctx->send_active_value_change callbacks). */
int dm_platform_enabled_notify_check_value_change(struct dmctx *ctx);

/* dm_entry_prefetch_values(): the agent is about to ask GET_VALUE of each
 * of these leaves in turn (the value-change check of DM_ENABLED_NOTIFY).
 * A platform that pays per request may fetch them in one go now and answer
 * the GET_VALUE of exactly one of them from that until
 * dm_platform_prefetch_drop().  Callers are serialised by the agent
 * (mutex_session_send).  Returns how many it fetched.
 *   uci, bdk: 0, nothing kept.
 *   mtk: one get_value_list of the script for the paths it serves. */
int dm_platform_prefetch_values(char **params, int n);
void dm_platform_prefetch_drop(void);

#endif
