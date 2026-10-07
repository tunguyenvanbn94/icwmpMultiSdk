/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.TraceRouteDiagnostics., 10 parameters and the
 *	RouteHops. object.
 *
 *	Ported from functions/tr098/traceroute_diagnostic.  The trace itself
 *	stays in functions/common/traceroute_launch, queued by
 *	DiagnosticsState=Requested; see diag_mtk.h.
 *
 *	State lives in easycwmp.@local[0] of "uci -P /var/state/traceroute",
 *	its own savedir: DiagnosticsState here is not the one of
 *	IPPingDiagnostics even though the option has the same name.
 *
 *	VENDOR QUIRKS KEPT:
 *
 *	  - Interface takes a network DEVICE name, not a TR-098 path (unlike
 *	    IPPingDiagnostics.Interface), and reads "default" when unset.  The
 *	    shell accepted it when "$(ifconfig $2)" printed something, unquoted:
 *	      ""     ifconfig with no argument lists the up interfaces -> accepted
 *	      "-a"   lists all of them                                 -> accepted
 *	      "a b"  two words, ifconfig tries to CONFIGURE a         -> rejected
 *	    The third case also ran that configuration ("eth0 down" took eth0
 *	    down) -- the rejection is kept, the side effect is not.
 *
 *	  - RouteHops. has no instance.  Its browse function
 *	    (traceroute_routehops_browse_instances) lives in functions/tr181/,
 *	    which the cwmpclient package never installed (cwmpclient/Makefile:
 *	    the tr181 copy line is commented out), so on the product the object
 *	    was always empty and only RouteHopsNumberOfEntries carried a count.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "diag_mtk.h"

#define TRC	(&diag_traceroute)

/* ------------------------------------------------------------------ */
/* getters                                                             */
/* ------------------------------------------------------------------ */

#define TRACEROUTE_GET(fn, option, def)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = diag_get(TRC, option, def);					\
	return 0;								\
}

TRACEROUTE_GET(get_trc_state,     "DiagnosticsState",         "None")
TRACEROUTE_GET(get_trc_host,      "Host",                     NULL)
TRACEROUTE_GET(get_trc_blocksize, "DataBlockSize",            "38")
TRACEROUTE_GET(get_trc_maxhops,   "MaxHopCount",              "30")
TRACEROUTE_GET(get_trc_interface, "Interface",                "default")
TRACEROUTE_GET(get_trc_tries,     "NumberOfTries",            "3")
TRACEROUTE_GET(get_trc_timeout,   "Timeout",                  "5000")
TRACEROUTE_GET(get_trc_dscp,      "DSCP",                     "0")
TRACEROUTE_GET(get_trc_resptime,  "ResponseTime",             "0")
TRACEROUTE_GET(get_trc_hops,      "RouteHopsNumberOfEntries", "0")

/* ------------------------------------------------------------------ */
/* setters                                                             */
/* ------------------------------------------------------------------ */

static int set_trc_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (strcmp(value, "Requested") != 0)
		return FAULT_9007;
	if (action == VALUESET)
		diag_request(TRC);
	return 0;
}

static int set_trc_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!diag_host_valid(TRC, value))
		return FAULT_9007;
	if (action == VALUESET)
		diag_store_value(TRC, "Host", value);
	return 0;
}

/* traceroute_set_tries / _timout / _mhops / _dsize / _dscp: digits, then a
 * range; bounds as the shell has them */
#define TRACEROUTE_SET_RANGE(fn, option, lo, hi)				\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char *value, int action)				\
{										\
	if (diag_check_uint(value, lo, hi) != 0)				\
		return FAULT_9007;						\
	if (action == VALUESET)							\
		diag_store_value(TRC, option, value);				\
	return 0;								\
}

TRACEROUTE_SET_RANGE(set_trc_tries,     "NumberOfTries", 1,  3)
TRACEROUTE_SET_RANGE(set_trc_timeout,   "Timeout",       1,  86400000)
TRACEROUTE_SET_RANGE(set_trc_maxhops,   "MaxHopCount",   1,  64)
TRACEROUTE_SET_RANGE(set_trc_blocksize, "DataBlockSize", 38, 32768)
TRACEROUTE_SET_RANGE(set_trc_dscp,      "DSCP",          0,  63)

static int set_trc_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!diag_ifconfig_prints(value))
		return FAULT_9007;
	if (action == VALUESET)
		diag_store_value(TRC, "Interface", value);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tTraceRouteMtkParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_trc_state, set_trc_state, NULL, NULL},
{"Host", &DMWRITE, DMT_STRING, get_trc_host, set_trc_host, NULL, NULL},
{"DataBlockSize", &DMWRITE, DMT_UNINT, get_trc_blocksize, set_trc_blocksize, NULL, NULL},
{"MaxHopCount", &DMWRITE, DMT_UNINT, get_trc_maxhops, set_trc_maxhops, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_trc_interface, set_trc_interface, NULL, NULL},
{"NumberOfTries", &DMWRITE, DMT_UNINT, get_trc_tries, set_trc_tries, NULL, NULL},
{"Timeout", &DMWRITE, DMT_UNINT, get_trc_timeout, set_trc_timeout, NULL, NULL},
{"DSCP", &DMWRITE, DMT_UNINT, get_trc_dscp, set_trc_dscp, NULL, NULL},
{"ResponseTime", &DMREAD, DMT_UNINT, get_trc_resptime, NULL, NULL, NULL},
{"RouteHopsNumberOfEntries", &DMREAD, DMT_UNINT, get_trc_hops, NULL, NULL, NULL},
{0}
};

/* always empty on the product -- see the header */
static DMOBJ tTraceRouteMtkChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"RouteHops", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tTraceRouteMtkObj[] = {
{"TraceRouteDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tTraceRouteMtkChildObj, tTraceRouteMtkParams, NULL},
{0}
};

static const char *const traceroute_mtk_paths[] = {
	"InternetGatewayDevice.TraceRouteDiagnostics.",
	NULL
};

static const struct dm_module traceroute_mtk_module = {
	.name  = "mtk-traceroute",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tTraceRouteMtkObj,
	.paths = traceroute_mtk_paths,
};
DM_MODULE_REGISTER(traceroute_mtk_module);
