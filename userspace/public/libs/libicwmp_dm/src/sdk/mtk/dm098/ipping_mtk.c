/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.IPPingDiagnostics., 12 parameters.
 *
 *	Ported from functions/tr098/ipping_diagnostic.  The ping itself stays
 *	in functions/common/ipping_launch (installed as
 *	/usr/share/easycwmp/functions/ipping_launch), queued by
 *	DiagnosticsState=Requested; see diag_mtk.h for the contract.
 *
 *	State lives in easycwmp.@local[0] of "uci -P /var/state" -- the plain
 *	/var/state savedir, unlike every other diagnostic which has its own.
 *
 *	VENDOR QUIRKS KEPT, so an ACS sees exactly what it saw before:
 *
 *	  - Interface takes a TR-098 path and maps it with a fixed table
 *	    (resolve_trpath_to_ifname): LAN IPInterface -> network.lan.ifname,
 *	    any WANPPPConnection -> network.if0.ifname, any WANIPConnection ->
 *	    "eth0.1" hard coded.  The getter returns the path as written.
 *
 *	  - The Interface setter stops the NSLOOKUP diagnostic, not the ping:
 *	    ipping_set_interface() calls nslookup_stop_diagnostic (a copy and
 *	    paste slip of the vendor).  Writing Interface therefore kills a
 *	    running NSLookupDiagnostics and resets ITS state to None, while a
 *	    running ping is left alone.
 *
 *	  - DSCP is not checked at all and is typed string (the shell gave it no
 *	    type); the launcher reads it with a default of 0.
 *
 *	  - The Host check finds a dotted quad ANYWHERE in the value
 *	    (unanchored grep -o), so "x1.2.3.4y" is accepted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "diag_mtk.h"

#define IPP	(&diag_ipping)

/* ------------------------------------------------------------------ */
/* getters                                                             */
/* ------------------------------------------------------------------ */

#define IPPING_GET(fn, option, def)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = diag_get(IPP, option, def);					\
	return 0;								\
}

IPPING_GET(get_ipp_state,        "DiagnosticsState",    "None")
IPPING_GET(get_ipp_host,         "Host",                NULL)
IPPING_GET(get_ipp_dscp,         "DSCP",                NULL)
IPPING_GET(get_ipp_interface,    "InterfacePath",       NULL)
IPPING_GET(get_ipp_repetitions,  "NumberOfRepetitions", "3")
IPPING_GET(get_ipp_timeout,      "Timeout",             "1000")
IPPING_GET(get_ipp_blocksize,    "DataBlockSize",       "64")
IPPING_GET(get_ipp_success,      "SuccessCount",        "0")
IPPING_GET(get_ipp_failure,      "FailureCount",        "0")
IPPING_GET(get_ipp_avg,          "AverageResponseTime", "0")
IPPING_GET(get_ipp_min,          "MinimumResponseTime", "0")
IPPING_GET(get_ipp_max,          "MaximumResponseTime", "0")

/* ------------------------------------------------------------------ */
/* setters                                                             */
/* ------------------------------------------------------------------ */

static int set_ipp_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (strcmp(value, "Requested") != 0)
		return FAULT_9007;
	if (action == VALUESET)
		diag_request(IPP);
	return 0;
}

static int set_ipp_host(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!diag_host_valid(IPP, value))
		return FAULT_9007;
	if (action == VALUESET)
		diag_store_value(IPP, "Host", value);
	return 0;
}

/* ipping_set: no check */
static int set_ipp_dscp(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUESET)
		diag_store_value(IPP, "DSCP", value);
	return 0;
}

/* ipping_set_number: digits, >= 1 */
#define IPPING_SET_NUMBER(fn, option)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char *value, int action)				\
{										\
	if (diag_check_uint(value, 1, -1) != 0)					\
		return FAULT_9007;						\
	if (action == VALUESET)							\
		diag_store_value(IPP, option, value);				\
	return 0;								\
}

IPPING_SET_NUMBER(set_ipp_repetitions, "NumberOfRepetitions")
IPPING_SET_NUMBER(set_ipp_timeout,     "Timeout")
IPPING_SET_NUMBER(set_ipp_blocksize,   "DataBlockSize")

/* resolve_trpath_to_ifname(): shell "case" patterns, "*" matching dots too */
static const char *ipping_ifname_of(const char *path)
{
	if (fnmatch("InternetGatewayDevice.LANDevice.*.LANHostConfigManagement.IPInterface.*", path, 0) == 0)
		return mtk_state("/var/state", "network", "lan", "ifname");
	if (fnmatch("InternetGatewayDevice.WANDevice.*.WANConnectionDevice.*.WANPPPConnection.*", path, 0) == 0)
		return mtk_state("/var/state", "network", "if0", "ifname");
	if (fnmatch("InternetGatewayDevice.WANDevice.*.WANConnectionDevice.*.WANIPConnection.*", path, 0) == 0)
		return "eth0.1";
	return NULL;
}

static int set_ipp_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *ifname;

	if (strncmp(value, "InternetGatewayDevice.", strlen("InternetGatewayDevice.")) != 0)
		return FAULT_9007;
	ifname = ipping_ifname_of(value);
	if (!ifname || !*ifname || !mtk_netdev_exists(ifname))
		return FAULT_9007;
	if (action != VALUESET)
		return 0;

	/* nslookup_stop_diagnostic, as the shell calls it -- see the header */
	diag_stop(&diag_nslookup);
	if (strcmp(diag_get(IPP, "DiagnosticsState", NULL), "Requested") != 0)
		diag_set(IPP, "DiagnosticsState", "None");
	diag_set(IPP, "Interface", ifname);
	diag_set(IPP, "InterfacePath", value);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tIPPingMtkParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_ipp_state, set_ipp_state, NULL, NULL},
{"Host", &DMWRITE, DMT_STRING, get_ipp_host, set_ipp_host, NULL, NULL},
{"DSCP", &DMWRITE, DMT_STRING, get_ipp_dscp, set_ipp_dscp, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_ipp_interface, set_ipp_interface, NULL, NULL},
{"NumberOfRepetitions", &DMWRITE, DMT_UNINT, get_ipp_repetitions, set_ipp_repetitions, NULL, NULL},
{"Timeout", &DMWRITE, DMT_UNINT, get_ipp_timeout, set_ipp_timeout, NULL, NULL},
{"DataBlockSize", &DMWRITE, DMT_UNINT, get_ipp_blocksize, set_ipp_blocksize, NULL, NULL},
{"SuccessCount", &DMREAD, DMT_UNINT, get_ipp_success, NULL, NULL, NULL},
{"FailureCount", &DMREAD, DMT_UNINT, get_ipp_failure, NULL, NULL, NULL},
{"AverageResponseTime", &DMREAD, DMT_UNINT, get_ipp_avg, NULL, NULL, NULL},
{"MinimumResponseTime", &DMREAD, DMT_UNINT, get_ipp_min, NULL, NULL, NULL},
{"MaximumResponseTime", &DMREAD, DMT_UNINT, get_ipp_max, NULL, NULL, NULL},
{0}
};

static DMOBJ tIPPingMtkObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"IPPingDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tIPPingMtkParams, NULL},
{0}
};

static const char *const ipping_mtk_paths[] = {
	"InternetGatewayDevice.IPPingDiagnostics.",
	NULL
};

static const struct dm_module ipping_mtk_module = {
	.name  = "mtk-ipping",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tIPPingMtkObj,
	.paths = ipping_mtk_paths,
};
DM_MODULE_REGISTER(ipping_mtk_module);
