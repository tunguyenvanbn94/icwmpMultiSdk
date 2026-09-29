/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Broadcom BDK backend helpers for the TR-098 object files under tr098/bdk/.
 *
 *	Every TR-098 leaf on this platform ends up as one of:
 *	  1. a table-driven mapping to a TR-181 parameter of the Distributed MDM
 *	     (bdk_map_get / bdk_map_set, driven by struct bdk_leafmap tables), or
 *	  2. a hand written getter/setter that uses bdk_get_value() and friends
 *	     when the TR-098 semantic needs several TR-181 objects.
 *
 *	All MDM access goes through libbcm_generic_hal, exactly like tr69c's
 *	doGetParameterValues/doSetParameterValues (see docs/icwmp_bdk_port_design.md).
 *	The process must already be attached to the tr69 component MDM
 *	(icwmp_bdk_init() in icwmpd does that with the shmId passed by tr69_md).
 */
#ifndef __DMBDK_H__
#define __DMBDK_H__

#include <stddef.h>
#include "dmtr098.h"
#include "bcm_generic_hal.h"   /* BcmGenericParamInfo */

/* ---- raw access --------------------------------------------------------- */
/*
 * Two flavours, pick by thread:
 *  - bdk_get_value / bdk_get_value_default / bdk_get_instances return
 *    dm-allocated memory (dmstrdup/dmcalloc, freed at dm_ctx_clean) and may
 *    ONLY be used from data model code running inside a dm context on the
 *    session thread: the dmmem arena of libtr098 is a global list without a
 *    lock, another thread touching it while a walk runs corrupts it (crash
 *    2026-09-19: uloop thread reload vs Inform on the session thread).
 *  - bdk_get_value_buf / bdk_set_value_now use only cmsMem and the bdk lock:
 *    safe from any thread (icwmpd bdk/icwmp_bdk.c uses these).
 */

/* GetParameterValues of one parameter.  On success *value is a dmstrdup'ed
 * copy of the MDM value.  On any error *value = "" and a CWMP fault code is
 * returned (FAULT_9005 if the path does not exist).  dm context only. */
int bdk_get_value(const char *fullpath, char **value);

/* Same, but returns 0 and *value = def when the parameter cannot be read. */
int bdk_get_value_default(const char *fullpath, const char *def, char **value);

/* Thread-safe GetParameterValues into a caller buffer (no dmmem).  buf is ""
 * on failure, returns 0 or a CWMP fault code. */
int bdk_get_value_buf(const char *fullpath, char *buf, size_t buflen);

/* Immediate single SetParameterValues, outside of the SPV transaction.  Used
 * by the ManagementServer UCI<->MDM sync and by end-of-session actions.
 * type may be NULL: the MDM type is looked up first.  Thread-safe (cmsMem). */
int bdk_set_value_now(const char *fullpath, const char *type, const char *value);

/* ---- data model root (dmproxy_bdk.c) ------------------------------------ */
/* Reads cwmp.cpe.datamodel (tr098 default | tr181); called by
 * dm_platform_ctx_init() once per RPC context. */
void bdk_proxy_load_mode(void);
/* 1 when the engine root is Device. (TR-181 passthrough), 0 for the IGD tree */
int bdk_dm_tr181_mode(void);

/* GetParameterValues of a whole subtree ("Device.WiFi.SSID.") in one HAL call.
 * *arr is freed by the caller with bcm_generic_freeParamInfoArray().  Thread
 * safe (cmsMem only).  Returns 0 or a CWMP fault code. */
int bdk_get_subtree(const char *path, BcmGenericParamInfo **arr, UINT32 *num);

/* Incremented at every dm_platform_ctx_init(): lets a backend cache MDM
 * lookups for the duration of one RPC context (tr098/bdk/mlo_bdk.c). */
unsigned int bdk_ctx_generation(void);

/* AddObject / DeleteObject on a TR-181 multi-instance object.
 * objpath = "Device.WiFi.SSID." ; objinstpath = "Device.WiFi.SSID.5." */
int bdk_add_object(const char *objpath, unsigned int *newinst);
int bdk_del_object(const char *objinstpath);

/* Instance numbers directly under a multi-instance object, in MDM order.
 * *inst is dm-allocated.  Returns 0 or a fault code. */
int bdk_get_instances(const char *objpath, unsigned int **inst, unsigned int *num);

/* Convert a BcmRet/CmsRet into a CWMP fault code (9000..9032 pass through,
 * everything else becomes FAULT_9002). */
int bdk_fault_from_ret(int ret);

/* Serialize MDM access: icwmpd is multi-threaded (session, notify, periodic,
 * uloop) but the CMS msg handle / PHL are not thread safe. */
void bdk_lock(void);
void bdk_unlock(void);

/* ---- table driven leaf mapping ------------------------------------------- */

#define BDK_MAP_RO      0x0000   /* read only (default) */
#define BDK_MAP_RW      0x0001   /* SetParameterValues allowed */
#define BDK_MAP_BOOL    0x0002   /* normalise true/false <-> 1/0 */
#define BDK_MAP_EMPTY   0x0004   /* always report "" (write-only, e.g. passwords) */
#define BDK_MAP_CONST   0x0008   /* tr181 is a literal value, not a path */

struct bdk_leafmap {
	const char *leaf;      /* TR-098 leaf name, e.g. "SerialNumber" */
	const char *tr181;     /* absolute "Device...." path, or relative to the
	                        * object context base (see struct bdk_objctx), or a
	                        * literal when BDK_MAP_CONST.  "{aux0}".."{aux3}" are
	                        * replaced by bdk_objctx.aux[] so one TR-098 object
	                        * can join several TR-181 objects, e.g.
	                        * "Device.WiFi.Radio.{aux0}.Channel" */
	unsigned int flags;
};

/* Context handed to instance objects by their browse function via
 * DM_LINK_INST_OBJ(dmctx, node, ctx, instance).  tr181_base is the TR-181
 * object instance the TR-098 instance is projected on, e.g. "Device.WiFi.SSID.3.".
 * aux[] holds related instances (Radio, AccessPoint, ...) for hand written
 * getters. */
struct bdk_objctx {
	char tr181_base[128];
	unsigned int aux[4];
	void *priv;
};

/* Register the mapping table of one TR-098 object.  tr098_obj is the object
 * path with instance numbers replaced by "{i}", e.g.
 * "InternetGatewayDevice.LANDevice.{i}.WLANConfiguration.{i}.".  base_default
 * is used when the leaf is reached without a bdk_objctx (static objects). */
int bdk_register_objmap(const char *tr098_obj, const struct bdk_leafmap *map, const char *base_default);

/* Generic DMLEAF getvalue/setvalue for mapped leaves. */
int bdk_map_get(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
int bdk_map_set(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action);

/* For hand written setters: queue a write into the current SPV transaction
 * (VALUESET phase).  tr098_param is only used to report per-parameter faults. */
int bdk_queue_set(struct dmctx *ctx, const char *tr098_param, const char *fullpath, const char *value);

/* For hand written setters, VALUECHECK phase: make sure fullpath exists and is
 * writable in the MDM.  Returns 0 or FAULT_9005/FAULT_9008. */
int bdk_check_writable(const char *fullpath);

/* Resolve the TR-181 path of a mapped leaf (for code that needs the path
 * itself, e.g. notifications).  Returns dm-allocated string or NULL. */
char *bdk_map_resolve(const char *refparam, void *data);

#endif
