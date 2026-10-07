/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_DHCPClient. -- ported from
 *	functions/tr098/X_AIS_DHCPClient.
 *	  SessionResponse  lanhost.common.total_hosts as stored, "" when unset
 *	  Session          "3" while total_hosts is unset, else "0"; a set
 *	                   accepts 1 only and does nothing else
 *	  Clean            reads "0"; a set accepts 1 only and empties the DHCP
 *	                   leases: dnsmasq stop, rm /tmp/dhcp.leases, start
 *	"1 only" is the shell's [ $v -ne 1 ]: any integer equal to 1.
 *
 *	The lease flush ran inside the setter; it is queued for the end of the
 *	session here, after the reply, like every service restart of this
 *	directory.
 */
#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

static int get_dhcpc_session_response(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("lanhost", "common", "total_hosts");
	return 0;
}

static int get_dhcpc_session(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = *mtk_uci("lanhost", "common", "total_hosts") ? "0" : "3";
	return 0;
}

/* [ $input_value -ne 1 ] && return $E_INVALID_PARAMETER_VALUE.  A value
 * that is not an integer made "test" fail, not succeed, so the shell let it
 * through; the int check in front refuses it before that. */
static int dhcpc_one(const char *value)
{
	long long n;

	return mtk_shell_getn(value, &n) != 0 || n == 1;
}

static int set_dhcpc_session(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return dhcpc_one(value) ? 0 : FAULT_9007;
}

static int get_dhcpc_clean(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0";
	return 0;
}

static int set_dhcpc_clean(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!dhcpc_one(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	mtk_apply_service_once("/etc/init.d/dnsmasq stop; rm -f /tmp/dhcp.leases; /etc/init.d/dnsmasq start");
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDhcpcParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"SessionResponse", &DMREAD, DMT_INT, get_dhcpc_session_response, NULL, NULL, NULL},
{"Session", &DMWRITE, DMT_INT, get_dhcpc_session, set_dhcpc_session, NULL, NULL},
{"Clean", &DMWRITE, DMT_INT, get_dhcpc_clean, set_dhcpc_clean, NULL, NULL},
{0}
};

static DMOBJ tDhcpcRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_DHCPClient", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDhcpcParams, NULL},
{0}
};

static const char *const dhcpclient_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_DHCPClient.",
	NULL
};

static const struct dm_module dhcpclient_mtk_module = {
	.name  = "mtk-x-ais-dhcpclient",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tDhcpcRoot,
	.paths = dhcpclient_mtk_paths,
};
DM_MODULE_REGISTER(dhcpclient_mtk_module);
