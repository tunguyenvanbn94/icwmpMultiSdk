/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_SSH. and InternetGatewayDevice.X_AIS_Telnet.
 *	-- the operator's console access, ported from functions/tr098/X_AIS_SSH
 *	and functions/tr098/X_AIS_Telnet.  Both live in the account package,
 *	named sections "ssh" and "telnet" (type account), added when missing.
 *
 *	  SSH.Enable, Telnet.Enable  read account.<ssh|telnet>.enabled == "1" as
 *	      true/false.  hni owns the switch: a set does not write the option,
 *	      it calls "ubus call hni setSshAccess / setTelnetAccess
 *	      {"enabled":<bool>}", and only when the value changes or the section
 *	      is new.
 *	  Telnet.Username, Telnet.Password  non-empty; written to the telnet
 *	      section AND the ssh one (one console account for both; when ssh
 *	      has no section that write fails, as in the shell), then account
 *	      reloaded, telnet restarted, dropbear clients killed and dropbear
 *	      reloaded.  Password reads "".
 *
 *	The shell made those calls inside the setter; they are queued for the
 *	end of the session here, after the engine's commit -- the hni handler
 *	finds the section a new SSH/Telnet entry needs.  A section that cannot be
 *	added is E_INTERNAL_ERROR.
 */
#include <stdio.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define ACCOUNT_RELOAD	"/etc/init.d/account reload; /etc/init.d/telnet restart; " \
			"/etc/init.d/dropbear killclients; /etc/init.d/dropbear reload"

static int get_access_enabled(const char *section, char **value)
{
	*value = mtk_bool_str(strcmp(mtk_uci("account", section, "enabled"), "1") == 0);
	return 0;
}

static int set_access_enabled(const char *section, const char *method, const char *value, int action)
{
	char cmd[96];
	int b = mtk_parse_bool(value), rc;

	if (b < 0)
		return FAULT_9007;	/* normalize_bool_01 */
	if (action == VALUECHECK)
		return 0;
	rc = mtk_uci_ensure_section("account", section, "account");
	if (rc < 0)
		return FAULT_9002;
	if (rc == 0 && strcmp(mtk_uci("account", section, "enabled"), b ? "1" : "0") == 0)
		return 0;
	snprintf(cmd, sizeof(cmd), "ubus call hni %s '{\"enabled\":%s}' >/dev/null 2>&1",
		 method, b ? "true" : "false");
	mtk_apply_service_once(cmd);
	return 0;
}

static int get_ssh_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_access_enabled("ssh", value);
}

static int set_ssh_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_access_enabled("ssh", "setSshAccess", value, action);
}

static int get_telnet_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_access_enabled("telnet", value);
}

static int set_telnet_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_access_enabled("telnet", "setTelnetAccess", value, action);
}

/* "Synchronize SSH <option> with Telnet <option>" */
static int set_console_account(const char *option, const char *value, int action)
{
	int rc;

	if (!value || !*value)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	rc = mtk_uci_ensure_section("account", "telnet", "account");
	if (rc < 0)
		return FAULT_9002;
	if (rc == 0 && strcmp(mtk_uci("account", "telnet", option), value) == 0)
		return 0;
	dmuci_set_value("account", "telnet", (char *)option, (char *)value);
	dmuci_set_value("account", "ssh", (char *)option, (char *)value);
	mtk_apply_service_once(ACCOUNT_RELOAD);
	return 0;
}

static int get_telnet_username(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("account", "telnet", "username");
	return 0;
}

static int set_telnet_username(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_console_account("username", value, action);
}

/* write only */
static int get_telnet_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int set_telnet_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_console_account("password", value, action);
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tSshParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_ssh_enable, set_ssh_enable, NULL, NULL},
{0}
};

static DMLEAF tTelnetParams[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_telnet_enable, set_telnet_enable, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_telnet_username, set_telnet_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_telnet_password, set_telnet_password, NULL, NULL},
{0}
};

static DMOBJ tConsoleRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_SSH", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tSshParams, NULL},
{"X_AIS_Telnet", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tTelnetParams, NULL},
{0}
};

static const char *const sshtelnet_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_SSH.",
	"InternetGatewayDevice.X_AIS_Telnet.",
	NULL
};

static const struct dm_module sshtelnet_mtk_module = {
	.name  = "mtk-x-ais-sshtelnet",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tConsoleRoot,
	.paths = sshtelnet_mtk_paths,
};
DM_MODULE_REGISTER(sshtelnet_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const sshtelnet_mtk_paths181[] = {
	"Device.X_AIS_SSH.",
	"Device.X_AIS_Telnet.",
	NULL
};

static const struct dm_module sshtelnet_mtk_module181 = {
	.name  = "mtk-x-ais-sshtelnet-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tConsoleRoot,
	.paths = sshtelnet_mtk_paths181,
};
DM_MODULE_REGISTER(sshtelnet_mtk_module181);
