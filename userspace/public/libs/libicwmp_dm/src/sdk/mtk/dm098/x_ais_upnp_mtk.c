/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_UPnP. -- ported from functions/tr098/X_AIS_UPnP.
 *
 *	Enable is upnpd.config.enabled.  The shell wrote it on every set, with
 *	no "unchanged" shortcut, and reloaded miniupnpd; the reload is queued for
 *	the end of the session here.
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

static int get_upnp_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool_str(strcmp(mtk_uci("upnpd", "config", "enabled"), "1") == 0);
	return 0;
}

static int set_upnp_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("upnpd", "config", "enabled", b ? "1" : "0");
	mtk_apply_service_once("/etc/init.d/miniupnpd reload");
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tUpnpParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_upnp_enable, set_upnp_enable, NULL, NULL},
{0}
};

static DMOBJ tUpnpRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_UPnP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tUpnpParams, NULL},
{0}
};

static const char *const upnp_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_UPnP.",
	NULL
};

static const struct dm_module upnp_mtk_module = {
	.name  = "mtk-x-ais-upnp",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tUpnpRoot,
	.paths = upnp_mtk_paths,
};
DM_MODULE_REGISTER(upnp_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const upnp_mtk_paths181[] = {
	"Device.X_AIS_UPnP.",
	NULL
};

static const struct dm_module upnp_mtk_module181 = {
	.name  = "mtk-x-ais-upnp-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tUpnpRoot,
	.paths = upnp_mtk_paths181,
};
DM_MODULE_REGISTER(upnp_mtk_module181);
