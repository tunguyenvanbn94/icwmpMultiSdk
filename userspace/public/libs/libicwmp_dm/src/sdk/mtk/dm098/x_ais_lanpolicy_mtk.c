/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Two one-leaf LAN switches of the operator, ported from
 *	functions/tr098/X_AIS_DnsLandingPage and functions/tr098/X_AIS_Isolation:
 *	  X_AIS_DnsLandingPage.Enable  landingpage.@landingpage[0].enabled,
 *	      the section added when there is none; landingpage restarted
 *	  X_AIS_Isolation.LANIsolation  dhcp.lan.isolation, the dhcp.lan section
 *	      added when missing; "ubus call hni doLanIsolation" applies it
 *	Both take "0" or "1" only and read "1" only for a stored "1".  A set
 *	writes, and queues its apply for the end of the session, when the value
 *	changes or the section is new; a section that cannot be added is
 *	E_INTERNAL_ERROR, as in the shell.
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

static int lp_01(const char *value)
{
	return value && (strcmp(value, "0") == 0 || strcmp(value, "1") == 0);
}

/* ------------------------------------------------------------------ */
/* X_AIS_DnsLandingPage                                                */
/* ------------------------------------------------------------------ */

static int get_landing_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("landingpage", "@landingpage[0]", "enabled"), "1") == 0 ? "1" : "0";
	return 0;
}

static int set_landing_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct uci_section *s;
	char *name = NULL;	/* dmuci_add_section() writes it on every path */
	int created = 0;

	if (!lp_01(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	/* ensure_landingpage_section */
	s = dmuci_walk_section("landingpage", "landingpage", NULL, NULL, CMP_SECTION, NULL, NULL, GET_FIRST_SECTION);
	if (!s) {
		dmuci_add_section("landingpage", "landingpage", &s, &name);
		if (!s)
			return FAULT_9002;
		created = 1;
	}
	if (!created && strcmp(mtk_uci("landingpage", "@landingpage[0]", "enabled"), value) == 0)
		return 0;
	dmuci_set_value_by_section(s, "enabled", value);
	mtk_apply_service_once("/etc/init.d/landingpage restart");
	return 0;
}

/* ------------------------------------------------------------------ */
/* X_AIS_Isolation                                                     */
/* ------------------------------------------------------------------ */

static int get_lan_isolation(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("dhcp", "lan", "isolation"), "1") == 0 ? "1" : "0";
	return 0;
}

static int set_lan_isolation(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int rc;

	if (!lp_01(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	rc = mtk_uci_ensure_section("dhcp", "lan", "dhcp");
	if (rc < 0)
		return FAULT_9002;
	if (rc == 0 && strcmp(mtk_uci("dhcp", "lan", "isolation"), value) == 0)
		return 0;
	dmuci_set_value("dhcp", "lan", "isolation", (char *)value);
	mtk_apply_service_once("ubus call hni doLanIsolation");
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tLandingParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_STRING, get_landing_enable, set_landing_enable, NULL, NULL},
{0}
};

static DMLEAF tIsolationParams[] = {
{"LANIsolation", &DMWRITE, DMT_STRING, get_lan_isolation, set_lan_isolation, NULL, NULL},
{0}
};

static DMOBJ tLanPolicyRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_DnsLandingPage", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tLandingParams, NULL},
{"X_AIS_Isolation", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tIsolationParams, NULL},
{0}
};

static const char *const lanpolicy_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_DnsLandingPage.",
	"InternetGatewayDevice.X_AIS_Isolation.",
	NULL
};

static const struct dm_module lanpolicy_mtk_module = {
	.name  = "mtk-x-ais-lanpolicy",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanPolicyRoot,
	.paths = lanpolicy_mtk_paths,
};
DM_MODULE_REGISTER(lanpolicy_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const lanpolicy_mtk_paths181[] = {
	"Device.X_AIS_DnsLandingPage.",
	"Device.X_AIS_Isolation.",
	NULL
};

static const struct dm_module lanpolicy_mtk_module181 = {
	.name  = "mtk-x-ais-lanpolicy-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tLanPolicyRoot,
	.paths = lanpolicy_mtk_paths181,
};
DM_MODULE_REGISTER(lanpolicy_mtk_module181);
