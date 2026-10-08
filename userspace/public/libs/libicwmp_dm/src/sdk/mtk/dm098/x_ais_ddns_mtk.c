/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_DDNS. -- ported from functions/tr098/X_AIS_DDNS.
 *	Options of ddns.service:
 *	  Enable      enabled       1|0|true|false exactly, stored "1"/"0"
 *	  Provider    service_name  "DynDNS" <-> dyndns.org, "No-IP" <->
 *	                            no-ip.com; any other stored name reads ""
 *	  Username    username      1..256 characters
 *	  Password    password      1..256 characters, read back as stored
 *	                            (the shell did; the product's choice)
 *	  DomainName  domain        1..256 characters
 *	Every set writes (no "unchanged" shortcut in the shell) and restarts
 *	ddns, queued once for the end of the session.
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define DDNS_PACKAGE	"ddns"
#define DDNS_SECTION	"service"
#define DDNS_RESTART	"/etc/init.d/ddns restart"

static void ddns_write(const char *option, const char *value)
{
	dmuci_set_value(DDNS_PACKAGE, DDNS_SECTION, (char *)option, (char *)value);
	mtk_apply_service_once(DDNS_RESTART);
}

static int get_ddns_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool_str(strcmp(mtk_uci(DDNS_PACKAGE, DDNS_SECTION, "enabled"), "1") == 0);
	return 0;
}

static int set_ddns_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int on;

	if (!value)
		return FAULT_9007;
	if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
		on = 1;
	else if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0)
		on = 0;
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	ddns_write("enabled", on ? "1" : "0");
	return 0;
}

static int get_ddns_provider(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *name = mtk_uci(DDNS_PACKAGE, DDNS_SECTION, "service_name");

	if (strcmp(name, "dyndns.org") == 0)
		*value = "DynDNS";
	else if (strcmp(name, "no-ip.com") == 0)
		*value = "No-IP";
	else
		*value = "";
	return 0;
}

static int set_ddns_provider(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *name;

	if (!value)
		return FAULT_9007;
	if (strcmp(value, "No-IP") == 0)
		name = "no-ip.com";
	else if (strcmp(value, "DynDNS") == 0)
		name = "dyndns.org";
	else
		return FAULT_9007;	/* the 1..32 length check is implied */
	if (action == VALUECHECK)
		return 0;
	ddns_write("service_name", name);
	return 0;
}

#define DDNS_TEXT(name, option)							\
static int get_ddns_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char **value)			\
{										\
	*value = mtk_uci(DDNS_PACKAGE, DDNS_SECTION, option);			\
	return 0;								\
}										\
static int set_ddns_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char *value, int action)		\
{										\
	size_t len = value ? strlen(value) : 0;					\
										\
	if (len < 1 || len > 256)						\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	ddns_write(option, value);						\
	return 0;								\
}

DDNS_TEXT(username, "username")
DDNS_TEXT(password, "password")
DDNS_TEXT(domain, "domain")

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDdnsParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_ddns_enable, set_ddns_enable, NULL, NULL},
{"Provider", &DMWRITE, DMT_STRING, get_ddns_provider, set_ddns_provider, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_ddns_username, set_ddns_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_ddns_password, set_ddns_password, NULL, NULL},
{"DomainName", &DMWRITE, DMT_STRING, get_ddns_domain, set_ddns_domain, NULL, NULL},
{0}
};

static DMOBJ tDdnsRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_DDNS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDdnsParams, NULL},
{0}
};

static const char *const ddns_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_DDNS.",
	NULL
};

static const struct dm_module ddns_mtk_module = {
	.name  = "mtk-x-ais-ddns",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tDdnsRoot,
	.paths = ddns_mtk_paths,
};
DM_MODULE_REGISTER(ddns_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const ddns_mtk_paths181[] = {
	"Device.X_AIS_DDNS.",
	NULL
};

static const struct dm_module ddns_mtk_module181 = {
	.name  = "mtk-x-ais-ddns-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tDdnsRoot,
	.paths = ddns_mtk_paths181,
};
DM_MODULE_REGISTER(ddns_mtk_module181);
