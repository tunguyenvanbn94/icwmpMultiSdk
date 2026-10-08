/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_3rdAgent. -- the operator's MQTT agent,
 *	ported from functions/tr098/X_AIS_3rdAgent.  Every leaf is an option of
 *	3rdpartyagent.3rdpartyagent:
 *	  enable       enabled       "1"/"0", read as true/false
 *	  server_Cert  cert_enable   "1"/"0", read as true/false
 *	  server_URL   broker_url    any string
 *
 *	A set always writes (no "unchanged" shortcut in the shell) and restarts
 *	the agent, queued once for the end of the session -- the same line as
 *	X_AIS_CPEagent's, so an SPV touching both restarts it once.
 *
 *	server_URL in AP mode (clay.opermode.mode "ap"): a new URL also clears
 *	client_id, so the agent asks the broker for a new one.
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define AGENT_PACKAGE	"3rdpartyagent"
#define AGENT_SECTION	"3rdpartyagent"
#define AGENT_RESTART	"/etc/init.d/3rdpartyagent restart"

static int agent_set_bool(const char *option, const char *value, int action)
{
	int b = mtk_parse_bool(value);

	/* common_set_bool refused the rest; the type check in front already did */
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(AGENT_PACKAGE, AGENT_SECTION, (char *)option, b ? "1" : "0");
	mtk_apply_service_once(AGENT_RESTART);
	return 0;
}

static int get_agent_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool_str(strcmp(mtk_uci(AGENT_PACKAGE, AGENT_SECTION, "enabled"), "1") == 0);
	return 0;
}

static int set_agent_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return agent_set_bool("enabled", value, action);
}

static int get_agent_cert(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool_str(strcmp(mtk_uci(AGENT_PACKAGE, AGENT_SECTION, "cert_enable"), "1") == 0);
	return 0;
}

static int set_agent_cert(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return agent_set_bool("cert_enable", value, action);
}

static int get_agent_url(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(AGENT_PACKAGE, AGENT_SECTION, "broker_url");
	return 0;
}

static int set_agent_url(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	if (!value)
		value = "";
	if (strcmp(mtk_uci("clay", "opermode", "mode"), "ap") == 0 &&
	    strcmp(mtk_uci(AGENT_PACKAGE, AGENT_SECTION, "broker_url"), value) != 0)
		dmuci_set_value(AGENT_PACKAGE, AGENT_SECTION, "client_id", "");
	dmuci_set_value(AGENT_PACKAGE, AGENT_SECTION, "broker_url", value);
	mtk_apply_service_once(AGENT_RESTART);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tAgentParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"enable", &DMWRITE, DMT_BOOL, get_agent_enable, set_agent_enable, NULL, NULL},
{"server_Cert", &DMWRITE, DMT_BOOL, get_agent_cert, set_agent_cert, NULL, NULL},
{"server_URL", &DMWRITE, DMT_STRING, get_agent_url, set_agent_url, NULL, NULL},
{0}
};

static DMOBJ tAgentRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_3rdAgent", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tAgentParams, NULL},
{0}
};

static const char *const agent_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_3rdAgent.",
	NULL
};

static const struct dm_module agent_mtk_module = {
	.name  = "mtk-x-ais-3rdagent",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tAgentRoot,
	.paths = agent_mtk_paths,
};
DM_MODULE_REGISTER(agent_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const agent_mtk_paths181[] = {
	"Device.X_AIS_3rdAgent.",
	NULL
};

static const struct dm_module agent_mtk_module181 = {
	.name  = "mtk-x-ais-3rdagent-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tAgentRoot,
	.paths = agent_mtk_paths181,
};
DM_MODULE_REGISTER(agent_mtk_module181);
