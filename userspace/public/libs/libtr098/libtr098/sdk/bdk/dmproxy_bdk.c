/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-181 passthrough for Broadcom BDK — what tr69c (SOAPParser/dmCms.c)
 *	does with libbcm_generic_hal, done from libtr098.  Two uses, chosen at
 *	runtime by cwmp.cpe.datamodel:
 *
 *	1. TR-098 mode (tr098, default): vendor subtree
 *	     InternetGatewayDevice.X_MARUSYS_COM_Device.<rest>  <->  Device.<rest>
 *	   Everything the Distributed MDM has (Device.WiFi.DataElements.* =
 *	   EasyMesh / Wi-Fi 7 MLD / per-STA data, X_BROADCOM_COM_WbdCfg, the
 *	   X_BROADCOM_COM_* leaves of Radio/SSID/AccessPoint, ...) is reachable
 *	   from a TR-098 ACS through one vendor object, without a mapping table
 *	   per leaf.  The curated IGD tree (tr098/bdk/*) stays the standard view.
 *	   The subtree is NOT included in a GetParameterNames/Values of the IGD
 *	   root (~20k parameters); the ACS addresses it explicitly.  Attributes
 *	   are not supported there (9001).
 *
 *	2. TR-181 mode (tr181): the whole data model.  dm_platform_select_root()
 *	   switches the engine root to "Device." (tr098/bdk/root181_bdk.c, a
 *	   static tree holding only what icwmpd adds itself: X_MARUSYS_COM_MloCfg)
 *	   and every other Device.* path is served here:
 *	     GPV/GPN/SPV/AddObject/DeleteObject  generic HAL, OGF_OMIT_HIDDEN_OBJ_PARAM,
 *	                                        isPassword values reported as ""
 *	     forced Inform parameters            tr69c informParameters_TR181
 *	     Get/SetParameterAttributes          bcm_generic_get/setParameterAttributes:
 *	                                        notification lives in the MDM
 *	                                        (persisted with the config, WebUI
 *	                                        visible) like tr69c, not in UCI
 *	     DM_ENABLED_NOTIFY                   built from the MDM attributes so
 *	                                        icwmp's value-change thread works
 *	                                        unchanged (it GPVs through this hook)
 *	   For a path that covers both the HAL and the static tree (Device.,
 *	   Device.WiFi.) the static walk is run after the HAL call and merged.
 *	   Device.ManagementServer.ParameterKey / ConnectionRequestURL are owned
 *	   by icwmpd (UCI cwmp.acs.ParameterKey, netlink IP + cwmp.cpe.port): they
 *	   are overridden on read and pushed into the MDM by icwmpd at the end of
 *	   the session.
 *
 *	Implementation: dm_platform_param_method() is called by the engine
 *	before the static table walk (dm_entry_param_method, and again for every
 *	VALUESET of dm_entry_apply).  It returns 0 for paths the static tree owns.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "cms.h"
#include "cms_log.h"
#include "cms_mem.h"
#include "bcm_generic_hal.h"

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmjson.h"
#include "dmentry.h"
#include "dmcommon.h"
#include "sdk/sdk.h"
#include "dmbdk.h"
#include "root_bdk.h"
#include "icwmpcfg.h"
#include "managementserver.h"

extern char *DMT_TYPE[];                       /* dmtr098.c, "xsd:string"... */

#define PROXY_PREFIX  "InternetGatewayDevice." CUSTOM_PREFIX "Device."
#define PROXY_TARGET  "Device."
#ifndef OGF_OMIT_HIDDEN_OBJ_PARAM
#define OGF_OMIT_HIDDEN_OBJ_PARAM 0x0008   /* cms_obj.h */
#endif
#define PROXY_GET_FLAGS OGF_OMIT_HIDDEN_OBJ_PARAM
#define PROXY_GPV_CHUNK 64                 /* paths per bcm_generic_getParameterValues */

#define MS_PARAMETER_KEY  "Device.ManagementServer.ParameterKey"
#define MS_CONN_REQ_URL   "Device.ManagementServer.ConnectionRequestURL"

/* tr69c SOAPParser/dmCms.c informParameters_TR181 (STUN entries left out) */
static const char *const proxy_inform_params[] = {
	"Device.RootDataModelVersion",
	"Device.DeviceInfo.HardwareVersion",
	"Device.DeviceInfo.SoftwareVersion",
	"Device.DeviceInfo.ProvisioningCode",
	MS_PARAMETER_KEY,
	MS_CONN_REQ_URL,
	NULL
};

/* objects of the TR-181 static tree (tr098/bdk/root181_bdk.c): the engine,
 * not the HAL, serves them and everything below */
static const char *const proxy_local_objs[] = {
	"Device.WiFi." CUSTOM_PREFIX "MloCfg",
	"Device.ManagementServer." CUSTOM_PREFIX "Icwmp",
#ifdef BDK_SAMPLE_OBJECT
	"Device." CUSTOM_PREFIX "Sample",           /* developer template, sample_bdk.c */
#endif
	NULL
};

/* HAL objects that also carry static leaves of their own (same object under
 * both trees): the leaf names come from the static table */
static const struct {
	const char *obj;        /* with trailing dot */
	DMLEAF *leaves;
} proxy_static_leaves[] = {
	{"Device.ManagementServer.", tManagementServer181Params},
	{"Device.DeviceInfo.", tDeviceInfo181Params},
	{NULL, NULL}
};

#define DEVICEINFO_OBJ "Device.DeviceInfo."

static int proxy_tr181;   /* cwmp.cpe.datamodel = tr181 */

static const size_t proxy_prefix_len = sizeof(PROXY_PREFIX) - 1;
static const size_t proxy_target_len = sizeof(PROXY_TARGET) - 1;

/* ------------------------------------------------------------------------ */
/* mode                                                                      */
/* ------------------------------------------------------------------------ */

/* dm_platform_ctx_init(): the UCI context of libtr098 is up at that point */
void bdk_proxy_load_mode(void)
{
	char *v = NULL;
	int was = proxy_tr181;

	dmuci_get_option_value_string("cwmp", "cpe", "datamodel", &v);
	proxy_tr181 = (v && strcasecmp(v, "tr181") == 0) ? 1 : 0;
	if (proxy_tr181 != was)
		cmsLog_notice("data model root: %s", proxy_tr181 ? "Device. (TR-181, generic HAL passthrough)"
		                                                  : "InternetGatewayDevice. (TR-098)");
}

int bdk_dm_tr181_mode(void)
{
	return proxy_tr181;
}

int dm_platform_select_root(struct dmctx *ctx)
{
	if (!proxy_tr181 || ctx->dm_type != DM_CWMP)
		return 0;
	strcpy(dmroot, "Device");
	ctx->dm_entryobj = dm_registry_entry(DM_MODEL_TR181);
	return 1;
}

/* ------------------------------------------------------------------------ */
/* path classification / translation                                         */
/* ------------------------------------------------------------------------ */

static int proxy_is_object(const char *path)
{
	size_t l = strlen(path);

	return l > 0 && path[l - 1] == '.';
}

/* path is one of the static objects of the TR-181 tree or below it, or one
 * of the static leaves living inside a HAL object */
static int proxy_is_local(const char *path)
{
	int i;

	for (i = 0; proxy_local_objs[i]; i++) {
		size_t l = strlen(proxy_local_objs[i]);

		if (strncmp(path, proxy_local_objs[i], l) == 0 && (path[l] == '.' || path[l] == '\0'))
			return 1;
	}
	for (i = 0; proxy_static_leaves[i].obj; i++) {
		size_t l = strlen(proxy_static_leaves[i].obj);
		DMLEAF *leaf;

		if (strncmp(path, proxy_static_leaves[i].obj, l) != 0)
			continue;
		for (leaf = proxy_static_leaves[i].leaves; leaf->parameter; leaf++)
			if (strcmp(path + l, leaf->parameter) == 0)
				return 1;
	}
	return 0;
}

/* an object path that has static children as well as HAL content, so the
 * static tree must be walked after the HAL call ("Device.", "Device.WiFi.",
 * and the objects carrying static leaves such as "Device.DeviceInfo.") */
static int proxy_covers_local(const char *path)
{
	int i;

	if (path[0] == '\0')
		return 1;
	if (!proxy_is_object(path))
		return 0;
	for (i = 0; proxy_local_objs[i]; i++)
		if (strncmp(proxy_local_objs[i], path, strlen(path)) == 0)
			return 1;
	for (i = 0; proxy_static_leaves[i].obj; i++)
		if (proxy_static_leaves[i].leaves[0].parameter &&
		    strncmp(proxy_static_leaves[i].obj, path, strlen(path)) == 0)
			return 1;
	return 0;
}

/* TR-098: "InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.Radio.1." -> "Device.WiFi.Radio.1."
 * TR-181: "Device.WiFi.Radio.1." -> same, "" -> "Device." (whole tree)
 * (dm-allocated).  NULL when the path is not ours (static tree, other root). */
static char *proxy_to_tr181(const char *inparam)
{
	char *out;

	if (!inparam)
		return NULL;
	if (proxy_tr181) {
		if (inparam[0] == '\0')
			return dmstrdup(PROXY_TARGET);
		if (strncmp(inparam, PROXY_TARGET, proxy_target_len) != 0)
			return NULL;
		if (proxy_is_local(inparam))
			return NULL;
		return dmstrdup(inparam);
	}
	if (strncmp(inparam, PROXY_PREFIX, proxy_prefix_len) != 0)
		return NULL;
	dmasprintf(&out, "%s%s", PROXY_TARGET, inparam + proxy_prefix_len);
	return out;
}

/* "Device.WiFi.Radio.1.Channel" -> the name the ACS sees */
static char *proxy_from_tr181(const char *fullpath)
{
	char *out;

	if (!fullpath || strncmp(fullpath, PROXY_TARGET, proxy_target_len) != 0)
		return NULL;
	if (proxy_tr181)
		return dmstrdup(fullpath);
	dmasprintf(&out, "%s%s", PROXY_PREFIX, fullpath + proxy_target_len);
	return out;
}

/* MDM (BBF) type names -> the xsd: strings icwmp puts in xsi:type */
static char *proxy_xsd_type(const char *t)
{
	if (!t) return DMT_TYPE[DMT_STRING];
	if (strcasecmp(t, "boolean") == 0) return DMT_TYPE[DMT_BOOL];
	if (strcasecmp(t, "unsignedInt") == 0) return DMT_TYPE[DMT_UNINT];
	if (strcasecmp(t, "int") == 0) return DMT_TYPE[DMT_INT];
	if (strcasecmp(t, "long") == 0 || strcasecmp(t, "unsignedLong") == 0) return DMT_TYPE[DMT_LONG];
	if (strcasecmp(t, "dateTime") == 0) return DMT_TYPE[DMT_TIME];
	if (strcasecmp(t, "base64") == 0) return DMT_TYPE[DMT_BASE64];
	if (strcasecmp(t, "hexBinary") == 0) return DMT_TYPE[DMT_HEXBIN];
	return DMT_TYPE[DMT_STRING];
}

/* ------------------------------------------------------------------------ */
/* ManagementServer values owned by icwmpd (TR-181 mode)                     */
/* ------------------------------------------------------------------------ */

/* value of a leaf icwmpd keeps in UCI rather than in the MDM, dm-allocated;
 * NULL = not one of them */
static char *proxy_ms_override(struct dmctx *ctx, const char *fullpath)
{
	char *v = NULL;

	if (!proxy_tr181)
		return NULL;
	if (strcmp(fullpath, MS_PARAMETER_KEY) == 0) {
		dmuci_get_option_value_string("cwmp", "acs", "ParameterKey", &v);
		return dmstrdup(v ? v : "");
	}
	if (strcmp(fullpath, MS_CONN_REQ_URL) == 0) {
		get_management_server_connection_request_url(NULL, ctx, NULL, NULL, &v);
		return dmstrdup(v ? v : "");
	}
	return NULL;
}

/* ------------------------------------------------------------------------ */
/* GPV / GPN / SPV / Add / Del                                               */
/* ------------------------------------------------------------------------ */

/* value the ACS may see: password parameters read back as "" (tr69c
 * writeGetPValueToFile), icwmpd-owned ManagementServer leaves overridden,
 * DeviceInfo identity override of the UCI config (the Inform DeviceId uses
 * the same override, deviceinfo_bdk.c) */
static char *proxy_value_of(struct dmctx *ctx, const BcmGenericParamInfo *pi)
{
	char *v = proxy_ms_override(ctx, pi->fullpath);

	if (v)
		return v;
	if (proxy_tr181 && strncmp(pi->fullpath, DEVICEINFO_OBJ, sizeof(DEVICEINFO_OBJ) - 1) == 0 &&
	    !strchr(pi->fullpath + sizeof(DEVICEINFO_OBJ) - 1, '.') &&
	    devinfo_uci_override(pi->fullpath + sizeof(DEVICEINFO_OBJ) - 1, &v))
		return v;
	if (pi->isPassword)
		return "";
	return dmstrdup(pi->value ? pi->value : "");
}

static int proxy_get_values(struct dmctx *ctx, const char **paths, unsigned int npaths)
{
	BcmGenericParamInfo *arr = NULL;
	UINT32 n = 0, i;
	BcmRet ret;

	bdk_lock();
	ret = bcm_generic_getParameterValues(paths, npaths, FALSE, PROXY_GET_FLAGS, &arr, &n);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_notice("proxy GPV %s%s failed ret=%d", paths[0], npaths > 1 ? " ..." : "", ret);
		return bdk_fault_from_ret(ret);
	}
	for (i = 0; i < n; i++) {
		char *name = proxy_from_tr181(arr[i].fullpath);

		if (!name)
			continue;
		add_list_paramameter(ctx, name, proxy_value_of(ctx, &arr[i]), proxy_xsd_type(arr[i].type), NULL, 0);
	}
	bcm_generic_freeParamInfoArray(&arr, n);
	return 0;
}

static int proxy_get_value(struct dmctx *ctx, const char *target)
{
	const char *paths[1] = { target };

	return proxy_get_values(ctx, paths, 1);
}

static int proxy_get_name(struct dmctx *ctx, const char *target, int nextlevel)
{
	BcmGenericParamInfo *arr = NULL;
	UINT32 n = 0, i;
	BcmRet ret;

	/* a parameter (no trailing dot) with NextLevel=true is 9003 per TR-069 */
	if (nextlevel && !proxy_is_object(target))
		return FAULT_9003;

	bdk_lock();
	ret = bcm_generic_getParameterNames(target, nextlevel ? TRUE : FALSE, PROXY_GET_FLAGS, &arr, &n);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_notice("proxy GPN %s failed ret=%d", target, ret);
		return bdk_fault_from_ret(ret);
	}
	for (i = 0; i < n; i++) {
		char *name = proxy_from_tr181(arr[i].fullpath);

		if (!name)
			continue;
		add_list_paramameter(ctx, name, arr[i].writable ? "1" : "0", NULL, NULL, 0);
	}
	bcm_generic_freeParamInfoArray(&arr, n);
	return 0;
}

/* does the path (parameter or object) exist in the MDM? 0 or a fault */
static int proxy_exists(const char *target)
{
	BcmGenericParamInfo *arr = NULL;
	UINT32 n = 0;
	BcmRet ret;

	bdk_lock();
	ret = bcm_generic_getParameterNames(target, FALSE, PROXY_GET_FLAGS, &arr, &n);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS)
		return bdk_fault_from_ret(ret);
	bcm_generic_freeParamInfoArray(&arr, n);
	return n ? 0 : FAULT_9005;
}

static int proxy_set_value(struct dmctx *ctx, const char *inparam, const char *target)
{
	if (!strlen(target) || proxy_is_object(target))
		return FAULT_9005;
	if (ctx->setaction == VALUESET)
		return bdk_queue_set(ctx, (char *)inparam, target, ctx->in_value);
	/* VALUECHECK: exists and writable, then remember for dm_entry_apply */
	{
		int fault = bdk_check_writable(target);

		if (fault)
			return fault;
		add_set_list_tmp(ctx, (char *)inparam, ctx->in_value, 0);
	}
	return 0;
}

/* HAL answered for the path; the static tree may hold more of it (TR-181
 * mode, "Device." / "Device.WiFi."): walk it and merge.  9005 from the walk
 * means "nothing static here", not an error. */
static int proxy_merge_static(struct dmctx *ctx, int hal_fault, int (*walk)(struct dmctx *))
{
	int sf;

	if (!proxy_tr181 || !proxy_covers_local(ctx->in_param))
		return hal_fault;
	sf = walk(ctx);
	if (hal_fault == 0)
		return 0;
	return (sf == 0) ? 0 : hal_fault;
}

/* ------------------------------------------------------------------------ */
/* TR-181 mode: Inform, attributes, enabled-notify file                      */
/* ------------------------------------------------------------------------ */

static int proxy_inform(struct dmctx *ctx)
{
	unsigned int n = 0;

	while (proxy_inform_params[n])
		n++;
	return proxy_get_values(ctx, (const char **)proxy_inform_params, n);
}

static char *proxy_notif_str(unsigned int notif)
{
	switch (notif) {
	case GENATTR_PASSIVE_NOTIFICATION: return "1";
	case GENATTR_ACTIVE_NOTIFICATION:  return "2";
	default:                           return "0";
	}
}

/* GetParameterAttributes from the MDM (tr69c doGetParameterAttributes) */
static int proxy_get_notification(struct dmctx *ctx, const char *target)
{
	const char *paths[1] = { target };
	BcmGenericParamAttr *arr = NULL;
	UINT32 n = 0, i;
	BcmRet ret;

	bdk_lock();
	ret = bcm_generic_getParameterAttributes(paths, 1, FALSE, PROXY_GET_FLAGS, &arr, &n);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_notice("proxy GPA %s failed ret=%d", target, ret);
		return bdk_fault_from_ret(ret);
	}
	for (i = 0; i < n; i++) {
		char *name;

		if (proxy_is_object(arr[i].fullpath))
			continue;
		name = proxy_from_tr181(arr[i].fullpath);
		if (!name)
			continue;
		add_list_paramameter(ctx, name, proxy_notif_str(arr[i].notif), NULL, NULL, 0);
	}
	bcm_generic_freeParamAttrArray(&arr, n);
	return 0;
}

/* SetParameterAttributes: VALUECHECK validates and records, VALUESET (from
 * dm_entry_apply) writes the notification into the MDM for the parameter
 * or the whole object (tr69c doSetParameterAttributes passes the ACS path
 * as is).  The notification lists of the cwmp UCI config are not used for
 * HAL parameters: the MDM keeps them, saved with the config. */
static int proxy_set_notification(struct dmctx *ctx, const char *inparam, const char *target)
{
	BcmGenericParamAttr attr;
	BcmRet ret;

	if (ctx->setaction == VALUECHECK) {
		int fault;

		if (dmcommon_check_notification_value(ctx->in_notification) < 0)
			return FAULT_9003;
		if (ctx->in_notification[0] > '2')
			return FAULT_9003;      /* lightweight notification: not in the MDM */
		fault = proxy_exists(target);
		if (fault)
			return fault;
		if (ctx->notification_change)
			add_set_list_tmp(ctx, (char *)inparam, ctx->in_notification, 0);
		/* an object path may also cover static leaves (Device.WiFi.): let
		 * the engine record those too, "nothing static" is not an error */
		if (proxy_covers_local(inparam))
			dm_entry_set_notification(ctx);
		return 0;
	}
	memset(&attr, 0, sizeof(attr));
	attr.fullpath = (char *)target;
	attr.setNotif = TRUE;
	attr.notif = (UINT16)atoi(ctx->in_notification);
	bdk_lock();
	ret = bcm_generic_setParameterAttributes(&attr, 1, 0);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_notice("proxy SPA %s=%s failed ret=%d", target, ctx->in_notification, ret);
		return bdk_fault_from_ret(ret);
	}
	/* rebuild DM_ENABLED_NOTIFY at the end of the session (dm_entry_reload_enabled_notify) */
	cwmp_set_end_session(END_SESSION_RELOAD);
	return 0;
}

static void proxy_notify_write(FILE *fp, const char *param, const char *notif, const char *value, const char *type)
{
	dmjson_fprintf(fp, 4, DMJSON_ARGS{{"parameter", (char *)param}, {"notification", (char *)notif},
	                                  {"value", (char *)value}, {"type", (char *)type}});
}

/* DM_ENABLED_NOTIFY from the MDM: every parameter whose notification
 * attribute is set, with its current value, so cwmp_add_notification()
 * (event.c) diffs them like the static tree's entries.  Returns 0 so the
 * engine appends the static tree's own entries afterwards. */
int dm_platform_enabled_notify(struct dmctx *ctx)
{
	const char *paths[1] = { PROXY_TARGET };
	BcmGenericParamAttr *attrs = NULL;
	UINT32 n = 0, i, k, m;
	const char **want = NULL;
	char **notif = NULL;
	FILE *fp;
	BcmRet ret;

	(void)ctx;
	if (!proxy_tr181)
		return 0;
	bdk_lock();
	ret = bcm_generic_getParameterAttributes(paths, 1, FALSE, PROXY_GET_FLAGS, &attrs, &n);
	bdk_unlock();
	if (ret != BCMRET_SUCCESS) {
		cmsLog_error("enabled-notify: GPA Device. failed ret=%d", ret);
		return 0;
	}
	want = dmcalloc(n + 1, sizeof(*want));
	notif = dmcalloc(n + 1, sizeof(*notif));
	for (i = 0, m = 0; i < n; i++) {
		if (attrs[i].notif != GENATTR_PASSIVE_NOTIFICATION && attrs[i].notif != GENATTR_ACTIVE_NOTIFICATION)
			continue;
		if (proxy_is_object(attrs[i].fullpath) || proxy_is_local(attrs[i].fullpath))
			continue;
		want[m] = dmstrdup(attrs[i].fullpath);
		notif[m] = proxy_notif_str(attrs[i].notif);
		m++;
	}
	bcm_generic_freeParamAttrArray(&attrs, n);
	if (!m)
		return 0;

	fp = fopen(DM_ENABLED_NOTIFY, "a");
	if (!fp) {
		cmsLog_error("cannot append %s", DM_ENABLED_NOTIFY);
		return 0;
	}
	/* values in chunks, one HAL call each */
	for (k = 0; k < m; k += PROXY_GPV_CHUNK) {
		BcmGenericParamInfo *arr = NULL;
		UINT32 cnt = (m - k > PROXY_GPV_CHUNK) ? PROXY_GPV_CHUNK : (m - k), got = 0, j;

		bdk_lock();
		ret = bcm_generic_getParameterValues(want + k, cnt, FALSE, PROXY_GET_FLAGS, &arr, &got);
		bdk_unlock();
		if (ret != BCMRET_SUCCESS) {
			cmsLog_notice("enabled-notify: GPV chunk %u failed ret=%d", k, ret);
			continue;
		}
		for (j = 0; j < got; j++) {
			UINT32 w;

			for (w = k; w < k + cnt; w++)
				if (strcmp(want[w], arr[j].fullpath) == 0)
					break;
			if (w == k + cnt)
				continue;
			proxy_notify_write(fp, arr[j].fullpath, notif[w], proxy_value_of(ctx, &arr[j]),
			                   proxy_xsd_type(arr[j].type));
		}
		bcm_generic_freeParamInfoArray(&arr, got);
	}
	fclose(fp);
	cmsLog_notice("enabled-notify: %u MDM parameters with notification", m);
	return 0;
}

/* start-up diff of DM_ENABLED_NOTIFY for the HAL parameters; the engine
 * then does the same for the static tree's entries (returns 0) */
int dm_platform_enabled_notify_check_value_change(struct dmctx *ctx)
{
	FILE *fp;
	char buf[512], cur[512];
	char *jval, *parameter, *value, *notification, *type;

	if (!proxy_tr181)
		return 0;
	fp = fopen(DM_ENABLED_NOTIFY, "r");
	if (fp == NULL)
		return 0;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		int len = strlen(buf);

		if (len)
			buf[len - 1] = '\0';
		dmjson_parse_init(buf);
		dmjson_get_var("parameter", &jval);
		parameter = dmstrdup(jval);
		dmjson_get_var("value", &jval);
		value = dmstrdup(jval);
		dmjson_get_var("notification", &jval);
		notification = dmstrdup(jval);
		dmjson_get_var("type", &jval);
		type = dmstrdup(jval);
		dmjson_parse_fini();

		if (proxy_is_local(parameter))
			continue;
		if (bdk_get_value_buf(parameter, cur, sizeof(cur)) != 0)
			continue;
		if (strcmp(cur, value) != 0) {
			if (ctx->add_list_value_change)
				ctx->add_list_value_change(parameter, cur, type);
			if (notification[0] == '2' && ctx->send_active_value_change)
				ctx->send_active_value_change();
		}
	}
	fclose(fp);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* engine hook                                                               */
/* ------------------------------------------------------------------------ */

int dm_platform_param_method(struct dmctx *ctx, int cmd, char *inparam, char *arg1, int *fault)
{
	char *target = proxy_to_tr181(inparam);
	unsigned int inst = 0;
	bool nextlevel = false;

	if (!target)
		return 0;
	*fault = 0;
	switch (cmd) {
	case CMD_GET_VALUE:
		*fault = proxy_merge_static(ctx, proxy_get_value(ctx, target), dm_entry_get_value);
		break;
	case CMD_GET_NAME:
		if (arg1 && string_to_bool(arg1, &nextlevel)) {
			*fault = FAULT_9003;
		} else {
			ctx->nextlevel = nextlevel;
			*fault = proxy_merge_static(ctx, proxy_get_name(ctx, target, nextlevel), dm_entry_get_name);
		}
		break;
	case CMD_SET_VALUE:
		*fault = proxy_set_value(ctx, inparam, target);
		break;
	case CMD_ADD_OBJECT:
		*fault = bdk_add_object(target, &inst);
		if (!*fault) {
			dmasprintf(&ctx->addobj_instance, "%u", inst);
			/* what dm_entry_param_method() does for the static tree: arg1 = ParameterKey */
			dmuci_set_value("cwmp", "acs", "ParameterKey", arg1 ? arg1 : "");
			dmuci_commit();
		}
		break;
	case CMD_DEL_OBJECT:
		*fault = bdk_del_object(target);
		if (!*fault) {
			dmuci_set_value("cwmp", "acs", "ParameterKey", arg1 ? arg1 : "");
			dmuci_commit();
		}
		break;
	case CMD_GET_NOTIFICATION:
		/* TR-098 vendor subtree: attributes not tracked (9001 request denied) */
		*fault = proxy_tr181 ? proxy_merge_static(ctx, proxy_get_notification(ctx, target), dm_entry_get_notification)
		                     : FAULT_9001;
		break;
	case CMD_SET_NOTIFICATION:
		*fault = proxy_tr181 ? proxy_set_notification(ctx, inparam, target) : FAULT_9001;
		break;
	case CMD_INFORM:
		if (proxy_tr181)
			*fault = proxy_inform(ctx);
		break;
	default:
		*fault = FAULT_9000;
		break;
	}
	dmfree(target);
	return 1;
}
