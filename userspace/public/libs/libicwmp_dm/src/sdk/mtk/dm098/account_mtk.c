/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Account.Web. -- the WebUI session timeout, ported
 *	from functions/tr098/account.
 *
 *	SessionMaxTime is the WebUI backend's (wsl) hmxwslbackend
 *	SessionTimeOut, in seconds.  The shell accepted 300..3600 only (5 to 60
 *	minutes), stored the value as written, committed and restarted the
 *	backend at once.  Here the value goes through the engine's commit and
 *	the restart is queued for the end of the session, like every other
 *	service restart of this directory.
 */
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define WSL_PACKAGE	"hmxwslbackend"
#define WSL_SECTION	"@hmxwslbackend[0]"

/* restart_Backend() of the shell.  A subshell: in the compat build the queue
 * runs as one script, and a bare "cd" would move every later line. */
#define WSL_RESTART	"killall -9 wsl 2>/dev/null; (cd /tmp/wsl/ 2>/dev/null && exec ./start_wsl.sh) &"

static int get_web_session_max_time(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_uci(WSL_PACKAGE, WSL_SECTION, "SessionTimeOut");

	*value = *v ? v : "0";
	return 0;
}

static int set_web_session_max_time(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	unsigned long n;
	const char *p;

	/* is_integer; the unsignedInt check in front already refused a sign */
	if (!value || !*value)
		return FAULT_9007;
	for (p = value; *p; p++) {
		if (*p < '0' || *p > '9')
			return FAULT_9007;
	}
	if (strlen(value) > 9)
		return FAULT_9007;	/* far above 3600, and no overflow below */
	n = strtoul(value, NULL, 10);
	if (n < 300 || n > 3600)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(WSL_PACKAGE, WSL_SECTION, "SessionTimeOut", value);
	mtk_apply_service_once(WSL_RESTART);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tAccountWebParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"SessionMaxTime", &DMWRITE, DMT_UNINT, get_web_session_max_time, set_web_session_max_time, NULL, NULL},
{0}
};

static DMOBJ tAccountObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Web", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tAccountWebParams, NULL},
{0}
};

static DMOBJ tAccountRoot[] = {
{"Account", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tAccountObj, NULL, NULL},
{0}
};

static const char *const account_mtk_paths[] = {
	"InternetGatewayDevice.Account.",
	NULL
};

static const struct dm_module account_mtk_module = {
	.name  = "mtk-account",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tAccountRoot,
	.paths = account_mtk_paths,
};
DM_MODULE_REGISTER(account_mtk_module);
