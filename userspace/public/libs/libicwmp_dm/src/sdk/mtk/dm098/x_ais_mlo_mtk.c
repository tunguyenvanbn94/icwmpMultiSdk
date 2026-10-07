/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_MLO. -- the two Wi-Fi 7 MLO groups, ported
 *	from functions/tr098/lan_device (entry_execute_method_root_X_AIS_MLO):
 *	  Fronthaul.Enable  wireless.apmld1 with its links ra5 + rai5
 *	  Backhaul.Enable   wireless.apmld2 with its links ra4 + rai4
 *	Fronthaul reads "1" only for apmld1.disabled "0"; Backhaul reads "0"
 *	only for apmld2.disabled "1" (unset counts as enabled there).
 *
 *	A set takes "0" or "1" and always writes (no "unchanged" shortcut):
 *	  Fronthaul 0: apmld1, ra5, rai5 disabled      1: all three enabled
 *	  Backhaul  0: apmld2, ra4 disabled, rai4 ENABLED (the 5 GHz backhaul
 *	               stays up on its own)            1: all three enabled
 *	With EasyMesh on (wlan_mesh_enabled) the two links' "disabled" is
 *	mirrored into their mapd nodes.  "wifi reload" is queued for the end of
 *	the session (wlan_reload, like the WLANConfiguration setters).
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wlan_mtk.h"

static int mlo_set(const char *group, const char *link24, const char *link5,
		   const char *value, int disabled5_when_off, int action)
{
	const char *d24, *d5;

	if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (value[0] == '0') {
		d24 = "1";
		d5 = disabled5_when_off ? "1" : "0";
	} else {
		d24 = d5 = "0";
	}
	dmuci_set_value("wireless", (char *)group, "disabled", value[0] == '0' ? "1" : "0");
	dmuci_set_value("wireless", (char *)link24, "disabled", (char *)d24);
	dmuci_set_value("wireless", (char *)link5, "disabled", (char *)d5);
	if (wlan_mesh_enabled()) {
		wlan_mapd_set(link24, "disabled", (char *)d24);
		wlan_mapd_set(link5, "disabled", (char *)d5);
	}
	wlan_reload();
	return 0;
}

static int get_mlo_fronthaul(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("wireless", "apmld1", "disabled"), "0") == 0 ? "1" : "0";
	return 0;
}

static int set_mlo_fronthaul(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return mlo_set("apmld1", "ra5", "rai5", value, 1, action);
}

static int get_mlo_backhaul(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("wireless", "apmld2", "disabled"), "1") == 0 ? "0" : "1";
	return 0;
}

static int set_mlo_backhaul(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return mlo_set("apmld2", "ra4", "rai4", value, 0, action);
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tMloFronthaulParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_STRING, get_mlo_fronthaul, set_mlo_fronthaul, NULL, NULL},
{0}
};

static DMLEAF tMloBackhaulParams[] = {
{"Enable", &DMWRITE, DMT_STRING, get_mlo_backhaul, set_mlo_backhaul, NULL, NULL},
{0}
};

static DMOBJ tMloObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Fronthaul", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tMloFronthaulParams, NULL},
{"Backhaul", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tMloBackhaulParams, NULL},
{0}
};

static DMOBJ tMloRoot[] = {
{"X_AIS_MLO", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tMloObj, NULL, NULL},
{0}
};

static const char *const mlo_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_MLO.",
	NULL
};

static const struct dm_module mlo_mtk_module = {
	.name  = "mtk-x-ais-mlo",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tMloRoot,
	.paths = mlo_mtk_paths,
};
DM_MODULE_REGISTER(mlo_mtk_module);
