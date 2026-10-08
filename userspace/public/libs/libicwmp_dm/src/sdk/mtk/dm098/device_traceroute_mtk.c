/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Device.IP.Diagnostics.TraceRoute. -- ported from
 *	functions/tr098/device_diagnostic_traceroute.
 *
 *	Same store and same launcher as InternetGatewayDevice.TraceRouteDiagnostics.
 *	(traceroute_mtk.c): easycwmp.@local[0] of "uci -P /var/state/traceroute",
 *	functions/traceroute_launch -- the two objects are two views of one
 *	test.  Its own rules differ:
 *	  DiagnosticsState  "Requested" or "None".  Requested without an
 *	                    Interface takes the default route's device (none:
 *	                    9007), stops a running test and queues the launcher
 *	                    (diag_request); None stops it and stores None
 *	  Interface         reads "" when unset; a set keeps a value that is an
 *	                    existing network device ("ifconfig <v>"), otherwise
 *	                    the default route's device, 9007 when there is none
 *	  Host              ProtocolVersion IPv4 (the default): a dotted quad of
 *	                    digits or [a-zA-Z0-9.-]+
 *	  NumberOfTries 1..3, Timeout 1..86400000, DataBlockSize 1..65535,
 *	  DSCP 0..63, MaxHopCount 1..64   digits only (diag_check_uint)
 *	Every set but DiagnosticsState stops a running test and drops the state
 *	to None unless a test is pending (diag_store_value).
 *
 *	RouteHops.{i}: listed only for a request below RouteHops. itself (the
 *	shell's entry returned for any other path) and only while the state is
 *	Complete, RouteHopsNumberOfEntries of them, read from line <i>+1 of
 *	/var/state/trace_results.txt (the launcher's traceroute output):
 *	  HopHost         field 2
 *	  HopHostAddress  field 3 without its parentheses
 *	  HopRTTTimes     the fields that look like 1.234, comma separated
 *	  HopErrorCode    1 when field 2 is "*", else 0
 *
 *	One difference, on purpose: Requested queues the launcher for the end
 *	of the session like every diagnostic of this directory; the shell
 *	started it inside the setter ("$LAUNCH_SCRIPT &").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "diag_mtk.h"

#define TRC		(&diag_traceroute)
#define TRC_RESULTS	"/var/state/trace_results.txt"
#define TRC_HOPS_REST	"IP.Diagnostics.TraceRoute.RouteHops."	/* after mtk_dev_prefix() */

/* get_default_interface: the device of the first default route */
static char *trc_default_device(void)
{
	FILE *f = fopen("/proc/net/route", "r");
	char line[256], iface[64], dest[16];

	if (!f)
		return "";
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%63s %15s", iface, dest) == 2 && strcmp(dest, "00000000") == 0) {
			fclose(f);
			return dmstrdup(iface);
		}
	}
	fclose(f);
	return "";
}

/* ------------------------------------------------------------------ */
/* parameters                                                          */
/* ------------------------------------------------------------------ */

#define TRC_GET(fn, option, def)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = diag_get(TRC, option, def);					\
	return 0;								\
}

TRC_GET(get_dtr_state,     "DiagnosticsState",         "None")
TRC_GET(get_dtr_interface, "Interface",                NULL)
TRC_GET(get_dtr_host,      "Host",                     NULL)
TRC_GET(get_dtr_tries,     "NumberOfTries",            "3")
TRC_GET(get_dtr_timeout,   "Timeout",                  "5000")
TRC_GET(get_dtr_blocksize, "DataBlockSize",            "38")
TRC_GET(get_dtr_dscp,      "DSCP",                     "0")
TRC_GET(get_dtr_maxhops,   "MaxHopCount",              "30")
TRC_GET(get_dtr_resptime,  "ResponseTime",             "0")
TRC_GET(get_dtr_hops,      "RouteHopsNumberOfEntries", "0")

static int set_dtr_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface;

	if (strcmp(value, "Requested") != 0 && strcmp(value, "None") != 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(value, "None") == 0) {
		diag_stop(TRC);
		diag_set(TRC, "DiagnosticsState", "None");
		return 0;
	}
	/* an Interface set earlier in the same SPV counts: checked here */
	iface = diag_get(TRC, "Interface", NULL);
	if (!*iface) {
		iface = trc_default_device();
		if (!*iface)
			return FAULT_9007;
		diag_set(TRC, "Interface", iface);
	}
	diag_request(TRC);
	return 0;
}

static int set_dtr_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (strcmp(diag_get(TRC, "ProtocolVersion", "IPv4"), "IPv4") == 0 &&
	    !mtk_ere_match("^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$", value) &&
	    !mtk_ere_match("^[a-zA-Z0-9.-]+$", value))
		return FAULT_9007;
	if (action == VALUESET)
		diag_store_value(TRC, "Host", value);
	return 0;
}

#define TRC_SET_UINT(fn, option, min, max)					\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char *value, int action)				\
{										\
	if (diag_check_uint(value, min, max) != 0)				\
		return FAULT_9007;						\
	if (action == VALUESET)							\
		diag_store_value(TRC, option, value);				\
	return 0;								\
}

TRC_SET_UINT(set_dtr_tries,     "NumberOfTries", 1, 3)
TRC_SET_UINT(set_dtr_timeout,   "Timeout",       1, 86400000)
TRC_SET_UINT(set_dtr_blocksize, "DataBlockSize", 1, 65535)
TRC_SET_UINT(set_dtr_dscp,      "DSCP",          0, 63)
TRC_SET_UINT(set_dtr_maxhops,   "MaxHopCount",   1, 64)

static int set_dtr_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface = value;

	/* [ -n "$val" ] && ifconfig "$val" -- quoted here, one device name */
	if (!*iface || !mtk_netdev_exists(iface))
		iface = trc_default_device();
	if (!*iface)
		return FAULT_9007;
	if (action == VALUESET)
		diag_store_value(TRC, "Interface", iface);
	return 0;
}

/* ------------------------------------------------------------------ */
/* RouteHops.{i}                                                       */
/* ------------------------------------------------------------------ */

static int browse_dtr_hops(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL, hops[96];
	int i, count;

	snprintf(hops, sizeof(hops), "%s" TRC_HOPS_REST, mtk_dev_prefix());
	if (!dmctx->in_param || strncmp(dmctx->in_param, hops, strlen(hops)) != 0)
		return 0;
	if (strcmp(diag_get(TRC, "DiagnosticsState", NULL), "Complete") != 0)
		return 0;
	count = atoi(diag_get(TRC, "RouteHopsNumberOfEntries", "0"));
	for (i = 1; i <= count && i <= 64; i++) {
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, i);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, NULL, idx) == DM_STOP)
			break;
	}
	return 0;
}

/* the fields of line <hop>+1 of the results file, awk-split; 0 fields when
 * the file or the line is not there */
static int trc_hop_fields(const char *instance, char *line, size_t sz, char *f[], int max)
{
	FILE *fp = fopen(TRC_RESULTS, "r");
	int want = atoi(instance) + 1, nr = 0, n = 0;
	char *p, *save = NULL;

	if (!fp)
		return 0;
	while (fgets(line, (int)sz, fp)) {
		if (++nr == want)
			break;
	}
	fclose(fp);
	if (nr != want)
		return 0;
	line[strcspn(line, "\n")] = '\0';
	for (p = strtok_r(line, " \t", &save); p && n < max; p = strtok_r(NULL, " \t", &save))
		f[n++] = p;
	return n;
}

static int get_dtr_hop_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char line[512], *f[32];
	int n = trc_hop_fields(instance, line, sizeof(line), f, 32);

	*value = n >= 2 ? dmstrdup(f[1]) : "";
	return 0;
}

static int get_dtr_hop_addr(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char line[512], *f[32], *out, *o;
	const char *c;
	int n = trc_hop_fields(instance, line, sizeof(line), f, 32);

	if (n < 3) {
		*value = "";
		return 0;
	}
	out = dmstrdup(f[2]);	/* gsub(/[()]/, "", $3) */
	if (!out) {
		*value = "";
		return 0;
	}
	for (c = f[2], o = out; *c; c++) {
		if (*c != '(' && *c != ')')
			*o++ = *c;
	}
	*o = '\0';
	*value = out;
	return 0;
}

static int get_dtr_hop_rtt(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char line[512], *f[32], out[512] = "";
	int n = trc_hop_fields(instance, line, sizeof(line), f, 32), i;

	for (i = 0; i < n; i++) {
		if (!mtk_ere_match("^[0-9]+\\.[0-9]+$", f[i]))
			continue;
		if (*out)
			strncat(out, ",", sizeof(out) - strlen(out) - 1);
		strncat(out, f[i], sizeof(out) - strlen(out) - 1);
	}
	*value = dmstrdup(out);
	return 0;
}

static int get_dtr_hop_error(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char line[512], *f[32];
	int n = trc_hop_fields(instance, line, sizeof(line), f, 32);

	*value = (n >= 2 && strcmp(f[1], "*") == 0) ? "1" : "0";
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDtrHopParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"HopHost", &DMREAD, DMT_STRING, get_dtr_hop_host, NULL, NULL, NULL},
{"HopHostAddress", &DMREAD, DMT_STRING, get_dtr_hop_addr, NULL, NULL, NULL},
{"HopErrorCode", &DMREAD, DMT_UNINT, get_dtr_hop_error, NULL, NULL, NULL},
{"HopRTTTimes", &DMREAD, DMT_STRING, get_dtr_hop_rtt, NULL, NULL, NULL},
{0}
};

static DMLEAF tDtrParams[] = {
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_dtr_state, set_dtr_state, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_dtr_interface, set_dtr_interface, NULL, NULL},
{"Host", &DMWRITE, DMT_STRING, get_dtr_host, set_dtr_host, NULL, NULL},
{"NumberOfTries", &DMWRITE, DMT_UNINT, get_dtr_tries, set_dtr_tries, NULL, NULL},
{"Timeout", &DMWRITE, DMT_UNINT, get_dtr_timeout, set_dtr_timeout, NULL, NULL},
{"DataBlockSize", &DMWRITE, DMT_UNINT, get_dtr_blocksize, set_dtr_blocksize, NULL, NULL},
{"DSCP", &DMWRITE, DMT_UNINT, get_dtr_dscp, set_dtr_dscp, NULL, NULL},
{"MaxHopCount", &DMWRITE, DMT_UNINT, get_dtr_maxhops, set_dtr_maxhops, NULL, NULL},
{"ResponseTime", &DMREAD, DMT_UNINT, get_dtr_resptime, NULL, NULL, NULL},
{"RouteHopsNumberOfEntries", &DMREAD, DMT_UNINT, get_dtr_hops, NULL, NULL, NULL},
{0}
};

static DMOBJ tDtrChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"RouteHops", &DMREAD, NULL, NULL, NULL, browse_dtr_hops, NULL, NULL, NULL, tDtrHopParams, NULL},
{0}
};

static DMOBJ tDtrDiagObj[] = {
{"TraceRoute", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDtrChildObj, tDtrParams, NULL},
{0}
};

static DMOBJ tDtrIpObj[] = {
{"Diagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDtrDiagObj, NULL, NULL},
{0}
};

static DMOBJ tDtrDeviceObj[] = {
{"IP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDtrIpObj, NULL, NULL},
{0}
};

static DMOBJ tDtrRoot[] = {
{"Device", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDtrDeviceObj, NULL, NULL},
{0}
};

static const char *const device_traceroute_mtk_paths[] = {
	"InternetGatewayDevice.Device.IP.Diagnostics.",
	NULL
};

static const struct dm_module device_traceroute_mtk_module = {
	.name  = "mtk-device-ip-traceroute",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tDtrRoot,
	.paths = device_traceroute_mtk_paths,
};
DM_MODULE_REGISTER(device_traceroute_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): this branch is TR-181 already, the
 * product grafted it under InternetGatewayDevice.Device.; the same getters at
 * the root (type A of docs/plan/tr181_mtk_design.md), but RouteHops.{i}
 * with the TR-181 leaf names: the product kept the TR-098 ones
 * (HopHost, HopHostAddress, HopErrorCode, HopRTTTimes). */
static DMLEAF tDtr181HopParams[] = {
{"Host", &DMREAD, DMT_STRING, get_dtr_hop_host, NULL, NULL, NULL},
{"HostAddress", &DMREAD, DMT_STRING, get_dtr_hop_addr, NULL, NULL, NULL},
{"ErrorCode", &DMREAD, DMT_UNINT, get_dtr_hop_error, NULL, NULL, NULL},
{"RTTimes", &DMREAD, DMT_STRING, get_dtr_hop_rtt, NULL, NULL, NULL},
{0}
};

static DMOBJ tDtr181ChildObj[] = {
{"RouteHops", &DMREAD, NULL, NULL, NULL, browse_dtr_hops, NULL, NULL, NULL, tDtr181HopParams, NULL},
{0}
};

static DMOBJ tDtr181DiagObj[] = {
{"TraceRoute", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDtr181ChildObj, tDtrParams, NULL},
{0}
};

static DMOBJ tDtr181IpObj[] = {
{"Diagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDtr181DiagObj, NULL, NULL},
{0}
};

static DMOBJ tDtr181DeviceObj[] = {
{"IP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDtr181IpObj, NULL, NULL},
{0}
};

static const char *const device_traceroute_mtk_paths181[] = {
	"Device.IP.Diagnostics.TraceRoute.",
	NULL
};

static const struct dm_module device_traceroute_mtk_module181 = {
	.name  = "mtk-device-ip-traceroute-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tDtr181DeviceObj,
	.paths = device_traceroute_mtk_paths181,
};
DM_MODULE_REGISTER(device_traceroute_mtk_module181);
