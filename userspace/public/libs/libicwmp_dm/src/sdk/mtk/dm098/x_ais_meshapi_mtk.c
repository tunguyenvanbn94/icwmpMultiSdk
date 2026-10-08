/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_MeshAPI. -- the operator's mesh reporting
 *	client, ported from functions/tr098/X_AIS_MeshAPI.  Options of the named
 *	section meshapi.meshapi (the product's default config has it):
 *	  Delay_time   delay_time   any non-empty string
 *	  Domain_name  domain_name  any non-empty string
 *	  enable       enable       true|1|false|0 exactly, stored "1"/"0", read
 *	                            true for a stored "1"; unset counts as "0"
 *	Nothing is written, and meshapi is not restarted, when the value is the
 *	current one.  Otherwise the restart is queued for the end of the session.
 *
 *	One difference, on purpose: without the meshapi.meshapi section the shell
 *	ran "uci add meshapi meshapi" -- an anonymous section -- and then set
 *	meshapi.meshapi.<option>, which does not exist: the value was lost and
 *	the setter answered success.  Here the named section is added, as the
 *	product's config has it, and the value lands.
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define MA_PACKAGE	"meshapi"
#define MA_SECTION	"meshapi"
#define MA_RESTART	"/etc/init.d/meshapi restart"

static int ma_set(const char *option, const char *value)
{
	if (strcmp(mtk_uci(MA_PACKAGE, MA_SECTION, option), value) == 0)
		return 0;
	if (mtk_uci_ensure_section(MA_PACKAGE, MA_SECTION, "meshapi") < 0)
		return 0;	/* the shell's uci set failed silently as well */
	dmuci_set_value(MA_PACKAGE, MA_SECTION, (char *)option, (char *)value);
	mtk_apply_service_once(MA_RESTART);
	return 0;
}

static int get_ma_delay_time(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(MA_PACKAGE, MA_SECTION, "delay_time");
	return 0;
}

static int set_ma_delay_time(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || !*value)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return ma_set("delay_time", value);
}

static int get_ma_domain_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(MA_PACKAGE, MA_SECTION, "domain_name");
	return 0;
}

static int set_ma_domain_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || !*value)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return ma_set("domain_name", value);
}

static int get_ma_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool_str(strcmp(mtk_uci(MA_PACKAGE, MA_SECTION, "enable"), "1") == 0);
	return 0;
}

static int set_ma_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *v;
	char *cur;

	if (!value)
		return FAULT_9007;
	if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0)
		v = "1";
	else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0)
		v = "0";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	cur = mtk_uci(MA_PACKAGE, MA_SECTION, "enable");
	if (strcmp(*cur ? cur : "0", v) == 0)
		return 0;
	if (mtk_uci_ensure_section(MA_PACKAGE, MA_SECTION, "meshapi") < 0)
		return 0;
	dmuci_set_value(MA_PACKAGE, MA_SECTION, "enable", (char *)v);
	mtk_apply_service_once(MA_RESTART);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tMeshApiParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Delay_time", &DMWRITE, DMT_STRING, get_ma_delay_time, set_ma_delay_time, NULL, NULL},
{"Domain_name", &DMWRITE, DMT_STRING, get_ma_domain_name, set_ma_domain_name, NULL, NULL},
{"enable", &DMWRITE, DMT_BOOL, get_ma_enable, set_ma_enable, NULL, NULL},
{0}
};

static DMOBJ tMeshApiRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_MeshAPI", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tMeshApiParams, NULL},
{0}
};

static const char *const meshapi_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_MeshAPI.",
	NULL
};

static const struct dm_module meshapi_mtk_module = {
	.name  = "mtk-x-ais-meshapi",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tMeshApiRoot,
	.paths = meshapi_mtk_paths,
};
DM_MODULE_REGISTER(meshapi_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const meshapi_mtk_paths181[] = {
	"Device.X_AIS_MeshAPI.",
	NULL
};

static const struct dm_module meshapi_mtk_module181 = {
	.name  = "mtk-x-ais-meshapi-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tMeshApiRoot,
	.paths = meshapi_mtk_paths181,
};
DM_MODULE_REGISTER(meshapi_mtk_module181);
