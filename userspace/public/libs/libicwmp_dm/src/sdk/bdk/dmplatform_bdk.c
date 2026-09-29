/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Broadcom BDK platform for libtr098.
 *
 *	Storage of every TR-098 parameter that is projected on the device data
 *	model is the Broadcom Distributed MDM (TR-181, Device.*).  This file is the
 *	only place in libtr098 that talks to it, through libbcm_generic_hal, which
 *	is the same PHL entry (bcmGeneric_get/setParameterValuesFlags) tr69c uses
 *	in SOAPParser/dmCms.c.
 *
 *	Local icwmp state (ACS credentials cache, notification attributes, dmmap
 *	instance persistence) stays in UCI under /data/icwmp, see dmuci.h.
 *
 *	Requirements on the process:
 *	  - cmsMsg_initOnBus(EID_TR69C, ..., TR69_MSG_BUS) and
 *	    cmsMdm_initWithConfig(shmId of tr69 component) done before the first
 *	    dm_ctx_init().  icwmpd does this in bdk/icwmp_bdk.c.
 *	  - All calls serialized with bdk_lock() (icwmpd is multi-threaded).
 *
 *	NOT BUILD-TESTED YET: written against the headers of
 *	bcm963xx lguplus 9f2a56abd0de9172bfe1283a8718a56cbce87e4e.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <pthread.h>

#include "cms.h"
#include "cms_log.h"
#include "cms_mem.h"
#include "bcm_generic_hal.h"     /* libbcm_generic_hal: bcm_generic_get/setParameterValues ... */

#include "dmtr098.h"
#include "dmmem.h"
#include "sdk/sdk.h"
#include "dmbdk.h"

/* flags passed to every get: same as tr69c doGetParameterValues */
#define BDK_GET_FLAGS   OGF_OMIT_HIDDEN_OBJ_PARAM
#ifndef OGF_OMIT_HIDDEN_OBJ_PARAM
/* cms_obj.h value, duplicated here so this file only needs public headers */
#define OGF_OMIT_HIDDEN_OBJ_PARAM 0x0008
#endif

/* recursive: icwmp_bdk.c holds the lock around a whole message / end of
 * session while calling bdk_get_value()/bdk_set_value_now() which lock again */
static pthread_mutex_t bdk_mutex;
static pthread_once_t bdk_mutex_once = PTHREAD_ONCE_INIT;

static void bdk_mutex_init(void)
{
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&bdk_mutex, &attr);
	pthread_mutexattr_destroy(&attr);
}

void bdk_lock(void)
{
	pthread_once(&bdk_mutex_once, bdk_mutex_init);
	pthread_mutex_lock(&bdk_mutex);
}

void bdk_unlock(void)
{
	pthread_mutex_unlock(&bdk_mutex);
}

int bdk_fault_from_ret(int ret)
{
	if (ret == BCMRET_SUCCESS ||
	    ret == BCMRET_SUCCESS_REBOOT_REQUIRED ||
	    ret == BCMRET_SUCCESS_APPLY_NOT_COMPLETE ||
	    ret == BCMRET_SUCCESS_OBJECT_UNCHANGED ||
	    ret == BCMRET_SUCCESS_UNRECOGNIZED_DATA_IGNORED)
		return 0;
	/* BcmRet 9000..9032 are literally the CWMP fault codes */
	if (ret >= FAULT_9000 && ret < __FAULT_MAX)
		return ret;
	if (ret == BCMRET_OBJECT_NOT_FOUND)
		return FAULT_9005;
	return FAULT_9002;
}

/* ------------------------------------------------------------------------ */
/* raw get / set                                                             */
/* ------------------------------------------------------------------------ */

/* GPV of exactly one parameter.  paramInfo array is returned (caller frees
 * with bcm_generic_freeParamInfoArray) so the caller can also look at type
 * and writable. */
static int bdk_gpv_one(const char *fullpath, BcmGenericParamInfo **arr, UINT32 *num)
{
	const char *paths[1];
	BcmRet ret;

	if (!fullpath || !*fullpath)
		return FAULT_9005;

	paths[0] = fullpath;
	*arr = NULL;
	*num = 0;

	bdk_lock();
	ret = bcm_generic_getParameterValues(paths, 1, FALSE, BDK_GET_FLAGS, arr, num);
	bdk_unlock();

	if (ret != BCMRET_SUCCESS) {
		cmsLog_notice("GPV %s failed ret=%d", fullpath, ret);
		*arr = NULL;
		*num = 0;
		return bdk_fault_from_ret(ret);
	}
	if (*num == 0 || (*arr)[0].fullpath == NULL) {
		*arr = NULL;
		*num = 0;
		return FAULT_9005;
	}
	return 0;
}

int bdk_get_value(const char *fullpath, char **value)
{
	BcmGenericParamInfo *arr = NULL;
	UINT32 num = 0;
	int fault;

	*value = "";
	fault = bdk_gpv_one(fullpath, &arr, &num);
	if (fault)
		return fault;

	*value = dmstrdup(arr[0].value ? arr[0].value : "");
	bcm_generic_freeParamInfoArray(&arr, num);
	return 0;
}

int bdk_get_value_default(const char *fullpath, const char *def, char **value)
{
	if (bdk_get_value(fullpath, value) != 0)
		*value = dmstrdup(def ? def : "");
	return 0;
}

/* thread-safe variant: no dmmem, result copied into the caller's buffer */
int bdk_get_value_buf(const char *fullpath, char *buf, size_t buflen)
{
	BcmGenericParamInfo *arr = NULL;
	UINT32 num = 0;
	int fault;

	if (!buf || buflen == 0)
		return FAULT_9002;
	buf[0] = '\0';
	fault = bdk_gpv_one(fullpath, &arr, &num);
	if (fault)
		return fault;
	snprintf(buf, buflen, "%s", arr[0].value ? arr[0].value : "");
	bcm_generic_freeParamInfoArray(&arr, num);
	return 0;
}

/* Fill a BcmGenericParamInfo for a set.  Strings are cmsMem_strdup'ed because
 * cmsUtl_freeParamInfoArray / bcm_generic_freeParamInfoArray free them with
 * cmsMem_free. */
static int bdk_fill_set_entry(BcmGenericParamInfo *pi, const char *fullpath,
                              const char *type, const char *value)
{
	memset(pi, 0, sizeof(*pi));
	pi->fullpath = cmsMem_strdup(fullpath);
	pi->type     = cmsMem_strdup(type ? type : "string");
	pi->value    = cmsMem_strdup(value ? value : "");
	if (!pi->fullpath || !pi->type || !pi->value)
		return FAULT_9004;
	return 0;
}

/* Look up the MDM type string ("string", "unsignedInt", "boolean", ...) and
 * writable flag of a parameter.  PHL rejects a set whose type string does not
 * match the MDM type (phl.c local_setParamValuesFlags), so never guess it.
 * *type_out is cmsMem_strdup'ed (not dmmem: callable from any thread), the
 * caller frees it with cmsMem_free. */
static int bdk_param_info(const char *fullpath, char **type_out, int *writable_out)
{
	BcmGenericParamInfo *arr = NULL;
	UINT32 num = 0;
	int fault;

	fault = bdk_gpv_one(fullpath, &arr, &num);
	if (fault)
		return fault;
	if (type_out)
		*type_out = cmsMem_strdup(arr[0].type ? arr[0].type : "string");
	if (writable_out)
		*writable_out = arr[0].writable ? 1 : 0;
	bcm_generic_freeParamInfoArray(&arr, num);
	return 0;
}

int bdk_check_writable(const char *fullpath)
{
	int writable = 0;
	int fault = bdk_param_info(fullpath, NULL, &writable);

	if (fault)
		return fault;
	return writable ? 0 : FAULT_9008;
}

int bdk_set_value_now(const char *fullpath, const char *type, const char *value)
{
	BcmGenericParamInfo pi;
	char *t = (char *)type, *tlookup = NULL;
	BcmRet ret;
	int fault;

	if (!t) {
		fault = bdk_param_info(fullpath, &tlookup, NULL);
		if (fault)
			return fault;
		t = tlookup;
	}
	fault = bdk_fill_set_entry(&pi, fullpath, t, value);
	CMSMEM_FREE_BUF_AND_NULL_PTR(tlookup);
	if (fault) {
		CMSMEM_FREE_BUF_AND_NULL_PTR(pi.fullpath);
		CMSMEM_FREE_BUF_AND_NULL_PTR(pi.type);
		CMSMEM_FREE_BUF_AND_NULL_PTR(pi.value);
		return fault;
	}

	bdk_lock();
	ret = bcm_generic_setParameterValues(&pi, 1, 0);
	bdk_unlock();

	CMSMEM_FREE_BUF_AND_NULL_PTR(pi.fullpath);
	CMSMEM_FREE_BUF_AND_NULL_PTR(pi.type);
	CMSMEM_FREE_BUF_AND_NULL_PTR(pi.value);

	if (ret != BCMRET_SUCCESS && bdk_fault_from_ret(ret) != 0) {
		/* never put ACS / connection request passwords in the log */
		int secret = (strstr(fullpath, "Password") != NULL) || (strstr(fullpath, "PreSharedKey") != NULL) ||
		             (strstr(fullpath, "KeyPassphrase") != NULL);
		cmsLog_error("SPV %s=%s failed ret=%d (param errorCode=%u)", fullpath,
		             secret ? "<masked>" : value, ret, pi.errorCode);
		return pi.errorCode ? bdk_fault_from_ret(pi.errorCode) : bdk_fault_from_ret(ret);
	}
	return 0;
}

int bdk_add_object(const char *objpath, unsigned int *newinst)
{
	BcmRet ret;
	UINT32 inst = 0;

	bdk_lock();
	ret = bcm_generic_addObject(objpath, 0, &inst);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_error("addObject %s failed ret=%d", objpath, ret);
		return bdk_fault_from_ret(ret);
	}
	if (newinst)
		*newinst = inst;
	return 0;
}

int bdk_del_object(const char *objinstpath)
{
	BcmRet ret;

	bdk_lock();
	ret = bcm_generic_deleteObject(objinstpath, 0);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_error("deleteObject %s failed ret=%d", objinstpath, ret);
		return bdk_fault_from_ret(ret);
	}
	return 0;
}

int bdk_get_instances(const char *objpath, unsigned int **inst, unsigned int *num)
{
	BcmGenericParamInfo *arr = NULL;
	UINT32 n = 0, i, count = 0;
	size_t plen;
	BcmRet ret;

	*inst = NULL;
	*num = 0;
	if (!objpath || !*objpath)
		return FAULT_9005;
	plen = strlen(objpath);

	bdk_lock();
	/* nextLevel=TRUE: direct children only, i.e. "Device.WiFi.SSID.1." ... */
	ret = bcm_generic_getParameterNames(objpath, TRUE, 0, &arr, &n);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_notice("GPN %s failed ret=%d", objpath, ret);
		return bdk_fault_from_ret(ret);
	}

	if (n)
		*inst = dmcalloc(n, sizeof(unsigned int));
	for (i = 0; i < n; i++) {
		const char *p = arr[i].fullpath;
		if (!p || strncmp(p, objpath, plen) != 0)
			continue;
		p += plen;
		if (!isdigit((unsigned char)*p))
			continue;
		if (*inst)
			(*inst)[count++] = (unsigned int)strtoul(p, NULL, 10);
	}
	*num = count;
	bcm_generic_freeParamInfoArray(&arr, n);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* SPV transaction: queued writes, flushed by dm_platform_commit()          */
/* ------------------------------------------------------------------------ */

struct bdk_pending {
	struct list_head list;
	char *tr098_param;   /* for fault reporting */
	char *fullpath;      /* TR-181 */
	char *type;
	char *value;
};

static LIST_HEAD(bdk_pending_list);
static unsigned int bdk_pending_count;

static void bdk_pending_free_all(void)
{
	struct bdk_pending *p, *n;

	list_for_each_entry_safe(p, n, &bdk_pending_list, list) {
		list_del(&p->list);
		free(p->tr098_param);
		free(p->fullpath);
		free(p->type);
		free(p->value);
		free(p);
	}
	bdk_pending_count = 0;
}

int bdk_queue_set(struct dmctx *ctx, const char *tr098_param, const char *fullpath, const char *value)
{
	struct bdk_pending *p;
	char *type = NULL;
	int fault;

	(void)ctx;
	/* the same TR-181 parameter queued twice in one RPC (two TR-098 leaves
	 * mapping onto it, e.g. MloCfg Enable + LinkRadios both touching
	 * SSID.{i}.Enable): the last value wins, one entry in the batch */
	list_for_each_entry(p, &bdk_pending_list, list) {
		if (strcmp(p->fullpath, fullpath) == 0) {
			free(p->value);
			p->value = strdup(value ? value : "");
			return 0;
		}
	}
	fault = bdk_param_info(fullpath, &type, NULL);
	if (fault)
		return fault;

	p = calloc(1, sizeof(*p));
	if (!p) {
		CMSMEM_FREE_BUF_AND_NULL_PTR(type);
		return FAULT_9004;
	}
	p->tr098_param = strdup(tr098_param ? tr098_param : fullpath);
	p->fullpath = strdup(fullpath);
	p->type = strdup(type);
	p->value = strdup(value ? value : "");
	CMSMEM_FREE_BUF_AND_NULL_PTR(type);
	list_add_tail(&p->list, &bdk_pending_list);
	bdk_pending_count++;
	return 0;
}

void tr098_bdk_register_all(void);   /* sdk/bdk/dm098/root_bdk.c */

static unsigned int bdk_ctx_gen;

unsigned int bdk_ctx_generation(void)
{
	return bdk_ctx_gen;
}

int bdk_get_subtree(const char *path, BcmGenericParamInfo **arr, UINT32 *num)
{
	const char *paths[1] = { path };
	BcmRet ret;

	*arr = NULL;
	*num = 0;
	if (!path || !*path)
		return FAULT_9005;
	bdk_lock();
	ret = bcm_generic_getParameterValues(paths, 1, FALSE, BDK_GET_FLAGS, arr, num);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_notice("GPV %s failed ret=%d", path, ret);
		return bdk_fault_from_ret(ret);
	}
	return 0;
}

int dm_platform_ctx_init(struct dmctx *ctx)
{
	(void)ctx;
	bdk_ctx_gen++;
	tr098_bdk_register_all();
	/* cwmp.cpe.datamodel: TR-098 (default) or TR-181 root, dmproxy_bdk.c */
	bdk_proxy_load_mode();
	/* a new RPC context: nothing may be left over from a failed one */
	bdk_pending_free_all();
	return 0;
}

int dm_platform_ctx_clean(struct dmctx *ctx)
{
	(void)ctx;
	bdk_pending_free_all();
	return 0;
}

void dm_platform_revert(struct dmctx *ctx)
{
	(void)ctx;
	bdk_pending_free_all();
}

/*
 * One bcm_generic_setParameterValues() for the whole SetParameterValues RPC,
 * mirroring tr69c doSetParameterValues(): PHL validates every entry first
 * (name, type, writable, value) and applies only if all are valid; on
 * BCMRET_INVALID_ARGUMENTS each paramInfo.errorCode carries its own fault.
 */
int dm_platform_commit(struct dmctx *ctx, const char *parameter_key)
{
	BcmGenericParamInfo *arr;
	struct bdk_pending *p;
	unsigned int i = 0;
	BcmRet ret;
	int fault = 0;

	(void)parameter_key;      /* the engine stores it in cwmp.acs.ParameterKey */
	if (bdk_pending_count == 0)
		return 0;

	arr = cmsMem_alloc(bdk_pending_count * sizeof(BcmGenericParamInfo), ALLOC_ZEROIZE);
	if (!arr) {
		bdk_pending_free_all();
		return FAULT_9004;
	}

	list_for_each_entry(p, &bdk_pending_list, list) {
		if (bdk_fill_set_entry(&arr[i], p->fullpath, p->type, p->value)) {
			fault = FAULT_9004;
			break;
		}
		i++;
	}

	if (!fault) {
		bdk_lock();
		ret = bcm_generic_setParameterValues(arr, i, 0);
		bdk_unlock();

		if (ret == BCMRET_SUCCESS_REBOOT_REQUIRED) {
			/* value accepted, device needs a reboot to apply: tr69c only sets
			 * rebootFlag in this case, we just log it */
			cmsLog_notice("SPV applied, reboot required to take effect");
			ret = BCMRET_SUCCESS;
		}
		if (ret == BCMRET_SUCCESS_APPLY_NOT_COMPLETE)
			ret = BCMRET_SUCCESS;

		if (ret != BCMRET_SUCCESS) {
			unsigned int k = 0;
			fault = bdk_fault_from_ret(ret);
			if (fault == 0)
				fault = FAULT_9002;
			list_for_each_entry(p, &bdk_pending_list, list) {
				if (k < i && arr[k].errorCode) {
					add_list_fault_param(ctx, p->tr098_param, bdk_fault_from_ret(arr[k].errorCode));
					cmsLog_error("SPV fault %u on %s (%s)", arr[k].errorCode, p->tr098_param, p->fullpath);
				}
				k++;
			}
			cmsLog_error("batch SPV of %u params failed ret=%d", i, ret);
		} else {
			cmsLog_notice("batch SPV of %u params applied to MDM", i);
		}
	}

	bcm_generic_freeParamInfoArray(&arr, bdk_pending_count);
	bdk_pending_free_all();
	return fault;
}

int dm_platform_restart_services(void)
{
	/* MDM STL/RCL handlers already applied the values when the batch SPV
	 * returned.  Flash persistence is done once per session by icwmpd
	 * (icwmp_bdk_save_config -> cmsMgm_saveConfigToFlash), the same point where
	 * tr69c calls saveConfigurations() in acsDisconnect(). */
	return 0;
}

const char *dm_platform_name(void)
{
	return "bdk";
}

/* ------------------------------------------------------------------------ */
/* table driven mapping                                                      */
/* ------------------------------------------------------------------------ */

#define BDK_MAX_OBJMAPS 64

struct bdk_objmap {
	const char *tr098_obj;
	const struct bdk_leafmap *map;
	const char *base_default;
};

static struct bdk_objmap bdk_objmaps[BDK_MAX_OBJMAPS];
static unsigned int bdk_objmap_count;

int bdk_register_objmap(const char *tr098_obj, const struct bdk_leafmap *map, const char *base_default)
{
	unsigned int i;

	for (i = 0; i < bdk_objmap_count; i++) {
		if (strcmp(bdk_objmaps[i].tr098_obj, tr098_obj) == 0) {
			bdk_objmaps[i].map = map;
			bdk_objmaps[i].base_default = base_default;
			return 0;
		}
	}
	if (bdk_objmap_count >= BDK_MAX_OBJMAPS)
		return -1;
	bdk_objmaps[bdk_objmap_count].tr098_obj = tr098_obj;
	bdk_objmaps[bdk_objmap_count].map = map;
	bdk_objmaps[bdk_objmap_count].base_default = base_default;
	bdk_objmap_count++;
	return 0;
}

/* "InternetGatewayDevice.LANDevice.1.WLANConfiguration.2.SSID" ->
 * obj = "InternetGatewayDevice.LANDevice.{i}.WLANConfiguration.{i}.", leaf = "SSID" */
static int bdk_split_refparam(const char *refparam, char *obj, size_t objlen, const char **leaf)
{
	const char *last, *p;
	size_t o = 0;

	if (!refparam)
		return -1;
	last = strrchr(refparam, '.');
	if (!last)
		return -1;
	*leaf = last + 1;

	for (p = refparam; p <= last; ) {
		const char *dot = strchr(p, '.');
		size_t seg;
		if (!dot)
			break;
		seg = (size_t)(dot - p);
		if (seg > 0 && strspn(p, "0123456789") == seg) {
			if (o + 4 >= objlen)
				return -1;
			memcpy(obj + o, "{i}.", 4);
			o += 4;
		} else {
			if (o + seg + 1 >= objlen)
				return -1;
			memcpy(obj + o, p, seg);
			o += seg;
			obj[o++] = '.';
		}
		p = dot + 1;
	}
	obj[o] = '\0';
	return 0;
}

static const struct bdk_objmap *bdk_find_objmap(const char *obj)
{
	unsigned int i;

	for (i = 0; i < bdk_objmap_count; i++)
		if (strcmp(bdk_objmaps[i].tr098_obj, obj) == 0)
			return &bdk_objmaps[i];
	return NULL;
}

static const struct bdk_leafmap *bdk_find_leaf(const struct bdk_objmap *om, const char *leaf)
{
	const struct bdk_leafmap *m;

	for (m = om->map; m && m->leaf; m++)
		if (strcmp(m->leaf, leaf) == 0)
			return m;
	return NULL;
}

/* Expand "{aux0}".."{aux3}" placeholders of a mapping with the numbers of the
 * object context (related TR-181 instances found by the browse function). */
static char *bdk_expand_aux(const char *fmt, const struct bdk_objctx *oc)
{
	char buf[256];
	size_t o = 0;
	const char *p = fmt;

	while (*p && o + 16 < sizeof(buf)) {
		if (p[0] == '{' && p[1] == 'a' && p[2] == 'u' && p[3] == 'x' &&
		    p[4] >= '0' && p[4] <= '3' && p[5] == '}') {
			unsigned int v = oc ? oc->aux[p[4] - '0'] : 0;
			o += (size_t)snprintf(buf + o, sizeof(buf) - o, "%u", v);
			p += 6;
			continue;
		}
		buf[o++] = *p++;
	}
	buf[o] = '\0';
	return dmstrdup(buf);
}

/* Build the TR-181 path of one mapped leaf.
 *   "Device...."      absolute (placeholders allowed)
 *   "IPv4Address.1.IPAddress"  relative to the object context base, or to
 *                     base_default of the registration for static objects */
static char *bdk_build_path(const struct bdk_objmap *om, const struct bdk_leafmap *m, void *data)
{
	const struct bdk_objctx *oc = (const struct bdk_objctx *)data;
	const char *base;
	char *path, *tmp;

	if (m->flags & BDK_MAP_CONST)
		return NULL;
	if (strncmp(m->tr181, "Device.", 7) == 0)
		return bdk_expand_aux(m->tr181, oc);
	base = (oc && oc->tr181_base[0]) ? oc->tr181_base : om->base_default;
	if (!base)
		return NULL;
	tmp = bdk_expand_aux(m->tr181, oc);
	dmasprintf(&path, "%s%s", base, tmp);
	return path;
}

char *bdk_map_resolve(const char *refparam, void *data)
{
	char obj[256];
	const char *leaf;
	const struct bdk_objmap *om;
	const struct bdk_leafmap *m;

	if (bdk_split_refparam(refparam, obj, sizeof(obj), &leaf))
		return NULL;
	om = bdk_find_objmap(obj);
	if (!om)
		return NULL;
	m = bdk_find_leaf(om, leaf);
	if (!m)
		return NULL;
	return bdk_build_path(om, m, data);
}

static void bdk_normalise_bool(char **value)
{
	if (!*value)
		return;
	if (strcasecmp(*value, "true") == 0 || strcmp(*value, "1") == 0)
		*value = "1";
	else if (strcasecmp(*value, "false") == 0 || strcmp(*value, "0") == 0)
		*value = "0";
}

int bdk_map_get(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char obj[256];
	const char *leaf;
	const struct bdk_objmap *om;
	const struct bdk_leafmap *m;
	char *path;

	(void)ctx;
	(void)instance;
	*value = "";

	if (bdk_split_refparam(refparam, obj, sizeof(obj), &leaf))
		return 0;
	om = bdk_find_objmap(obj);
	if (!om) {
		cmsLog_error("no BDK map registered for %s (from %s)", obj, refparam);
		return 0;
	}
	m = bdk_find_leaf(om, leaf);
	if (!m) {
		cmsLog_error("leaf %s not in BDK map of %s", leaf, obj);
		return 0;
	}
	if (m->flags & BDK_MAP_EMPTY)
		return 0;
	if (m->flags & BDK_MAP_CONST) {
		*value = dmstrdup(m->tr181);
		return 0;
	}
	path = bdk_build_path(om, m, data);
	if (!path)
		return 0;
	bdk_get_value(path, value);
	if (m->flags & BDK_MAP_BOOL)
		bdk_normalise_bool(value);
	return 0;
}

int bdk_map_set(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char obj[256];
	const char *leaf;
	const struct bdk_objmap *om;
	const struct bdk_leafmap *m;
	char *path;
	bool b;

	(void)instance;
	if (bdk_split_refparam(refparam, obj, sizeof(obj), &leaf))
		return FAULT_9005;
	om = bdk_find_objmap(obj);
	if (!om)
		return FAULT_9005;
	m = bdk_find_leaf(om, leaf);
	if (!m)
		return FAULT_9005;
	if (!(m->flags & BDK_MAP_RW) || (m->flags & BDK_MAP_CONST))
		return FAULT_9008;
	path = bdk_build_path(om, m, data);
	if (!path)
		return FAULT_9002;

	switch (action) {
	case VALUECHECK:
		if ((m->flags & BDK_MAP_BOOL) && string_to_bool(value, &b))
			return FAULT_9007;
		return bdk_check_writable(path);
	case VALUESET:
		if (m->flags & BDK_MAP_BOOL)
			bdk_normalise_bool(&value);
		return bdk_queue_set(ctx, refparam, path, value);
	}
	return 0;
}
