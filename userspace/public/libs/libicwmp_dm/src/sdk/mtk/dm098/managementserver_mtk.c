/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.ManagementServer. of the MTK/Airoha product.
 *
 *	The portable module tr098/managementserver.c already implements 33 of
 *	the 35 leaves the product's easycwmp library had, against icwmpd's own
 *	UCI "cwmp" config (which sdk/mtk of icwmp mirrors both ways with the
 *	product's "easycwmp" config, the config of record for the WebUI).
 *	This module only adds what the product had on top:
 *
 *	  EnableCWMP        easycwmp.@acs[0].enablecwmp
 *	  UpgradesManaged   easycwmp.@acs[0].upgradesmanaged
 *
 *	and it demonstrates the merge rule of dm_registry.c: two modules declare
 *	the object "ManagementServer", their leaf tables are merged, so nothing
 *	in the portable module has to be edited to extend it.
 *
 *	ConnectionRequestURL stays with the portable module on purpose: icwmpd
 *	knows the real WAN address (netlink) and the port it listens on, the
 *	shell built the URL from a UCI value written once at boot.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

static int get_enablecwmp(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_uci("easycwmp", "@acs[0]", "enablecwmp");

	/* absent means enabled: the daemon is running, after all */
	*value = (v && v[0]) ? (mtk_bool(v) ? "1" : "0") : "1";
	return 0;
}

static int set_enablecwmp(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b;

	switch (action) {
	case VALUECHECK:
		if (mtk_parse_bool(value) < 0)
			return FAULT_9007;
		return 0;
	case VALUESET:
		b = mtk_parse_bool(value);
		dmuci_set_value("easycwmp", "@acs[0]", "enablecwmp", b ? "1" : "0");
		return 0;
	}
	return 0;
}

static int get_upgradesmanaged(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool(mtk_uci("easycwmp", "@acs[0]", "upgradesmanaged")) ? "1" : "0";
	return 0;
}

static int set_upgradesmanaged(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b;

	switch (action) {
	case VALUECHECK:
		if (mtk_parse_bool(value) < 0)
			return FAULT_9007;
		return 0;
	case VALUESET:
		b = mtk_parse_bool(value);
		dmuci_set_value("easycwmp", "@acs[0]", "upgradesmanaged", b ? "1" : "0");
		return 0;
	}
	return 0;
}

static DMLEAF tManagementServerMtkParam[] = {
{"EnableCWMP", &DMWRITE, DMT_BOOL, get_enablecwmp, set_enablecwmp, NULL, NULL},
{"UpgradesManaged", &DMWRITE, DMT_BOOL, get_upgradesmanaged, set_upgradesmanaged, NULL, NULL},
{0}
};

static DMOBJ tManagementServerMtkRoot[] = {
{"ManagementServer", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, &DMNONE, NULL, tManagementServerMtkParam, NULL},
{0}
};

static const char *const ms_mtk_paths[] = {
	"InternetGatewayDevice.ManagementServer.",
	NULL
};

static const struct dm_module ms_mtk_module = {
	.name  = "mtk-managementserver",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_LATE,	/* merged after the portable module */
	.objs  = tManagementServerMtkRoot,
	.paths = ms_mtk_paths,
};
DM_MODULE_REGISTER(ms_mtk_module);
