/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_AutoWifiScan. -- ported from
 *	functions/tr098/X_AIS_AutoWifiScan.  Options of the first autowifiscan
 *	section, read as stored, with the shell's default when unset:
 *	  Enable           enabled           "1"  (stored "1"/"0")
 *	  TrafficLimit     traffic_limit     "300"
 *	  TrafficKeepTime  traffic_keeptime  "120"
 *	No section, no write: "uci set autowifiscan.@autowifiscan[0]..." failed
 *	in the shell too, and the setter still answered success.
 *
 *	Enable=1 starts the service when it is not running ("running || start"),
 *	queued for the end of the session.  The two counters restart nothing.
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define AWS_PACKAGE	"autowifiscan"
#define AWS_SECTION	"@autowifiscan[0]"

static char *aws_get(const char *option, char *dflt)
{
	char *v = mtk_uci(AWS_PACKAGE, AWS_SECTION, option);

	return *v ? v : dflt;
}

static int get_aws_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = aws_get("enabled", "1");
	return 0;
}

static int set_aws_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(AWS_PACKAGE, AWS_SECTION, "enabled", b ? "1" : "0");
	if (b)
		mtk_apply_service_once("/etc/init.d/autowifiscan running || /etc/init.d/autowifiscan start");
	return 0;
}

/* [ "$val" -ge 0 ]: an integer, not negative, stored as written */
static int aws_set_count(const char *option, const char *value, int action)
{
	long long n;

	if (mtk_shell_getn(value, &n) != 0 || n < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(AWS_PACKAGE, AWS_SECTION, (char *)option, (char *)value);
	return 0;
}

static int get_aws_traffic_limit(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = aws_get("traffic_limit", "300");
	return 0;
}

static int set_aws_traffic_limit(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return aws_set_count("traffic_limit", value, action);
}

static int get_aws_traffic_keeptime(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = aws_get("traffic_keeptime", "120");
	return 0;
}

static int set_aws_traffic_keeptime(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return aws_set_count("traffic_keeptime", value, action);
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tAwsParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_aws_enable, set_aws_enable, NULL, NULL},
{"TrafficLimit", &DMWRITE, DMT_UNINT, get_aws_traffic_limit, set_aws_traffic_limit, NULL, NULL},
{"TrafficKeepTime", &DMWRITE, DMT_UNINT, get_aws_traffic_keeptime, set_aws_traffic_keeptime, NULL, NULL},
{0}
};

static DMOBJ tAwsRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_AutoWifiScan", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tAwsParams, NULL},
{0}
};

static const char *const autowifiscan_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_AutoWifiScan.",
	NULL
};

static const struct dm_module autowifiscan_mtk_module = {
	.name  = "mtk-x-ais-autowifiscan",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tAwsRoot,
	.paths = autowifiscan_mtk_paths,
};
DM_MODULE_REGISTER(autowifiscan_mtk_module);
