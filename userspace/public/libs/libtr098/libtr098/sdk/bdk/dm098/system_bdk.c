/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice. root leaves, Time., IPPingDiagnostics. and
 *	TraceRouteDiagnostics. on Broadcom BDK.
 *
 *	  InternetGatewayDevice.DeviceSummary, *NumberOfEntries, *DiagnosticsSupported
 *	  Time.                   <- Device.Time.  (1:1, Broadcom keeps the TR-098
 *	                             DST/zone-name leaves as X_BROADCOM_COM_*)
 *	  IPPingDiagnostics.      <- Device.IP.Diagnostics.IPPing.
 *	  TraceRouteDiagnostics.  <- Device.IP.Diagnostics.TraceRoute. (+ RouteHops.{i})
 *	  Layer3Forwarding.       <- Device.Routing.Router.1. (Forwarding.{i} <-
 *	                             IPv4Forwarding.{i}, Add/Delete = static routes)
 *
 *	DiagnosticsState uses the same words in both models: the ACS sets
 *	"Requested" (SPV through the generic HAL -> diag_md runs the test), diag_md
 *	publishes PUBSUB_KEY_PING/TRACERT_DIAG_COMPLETE, tr69_md forwards
 *	CMS_MSG_PING_STATE_CHANGED / CMS_MSG_TRACERT_STATE_CHANGED to EID_TR69C
 *	(icwmpd, bdk/icwmp_bdk.c) which raises "8 DIAGNOSTICS COMPLETE".
 *	The Interface leaf is translated between the TR-098 connection path and
 *	the TR-181 Device.IP.Interface.{i} reference.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "dmtr098.h"
#include "dmcommon.h"
#include "system_bdk.h"
#include "dmbdk.h"

#define TR181_TIME      "Device.Time."
#define TR181_IPPING    "Device.IP.Diagnostics.IPPing."
#define TR181_TRACERT   "Device.IP.Diagnostics.TraceRoute."
#define TR098_WANIP     "InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1.WANIPConnection."
#define TR098_WANPPP    "InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1.WANPPPConnection."
#define TR098_LANIP     "InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.IPInterface."

/* ---------------------------------------------------------------------- */
/* InternetGatewayDevice. root leaves                                      */
/* ---------------------------------------------------------------------- */

/* profiles served by tr098/bdk/*: keep in sync when an object is added */
static const struct bdk_leafmap root_map[] = {
	{"DeviceSummary", "InternetGatewayDevice:1.4[](Baseline:1, EthernetLAN:1, WiFiLAN:1, WiFiLAN:2, EthernetWAN:1, Time:1, IPPing:1, TraceRoute:1)", BDK_MAP_CONST},
	{"LANDeviceNumberOfEntries", "1", BDK_MAP_CONST},
	{"WANDeviceNumberOfEntries", "1", BDK_MAP_CONST},
	/* TR-143 not built on MO77300EB (no Device.IP.Diagnostics.Download/Upload
	 * in the MDM dump), UDPEchoConfig not mapped */
	{"IPv4DownloadDiagnosticsSupported", "0", BDK_MAP_CONST},
	{"IPv6DownloadDiagnosticsSupported", "0", BDK_MAP_CONST},
	{"IPv4UploadDiagnosticsSupported",   "0", BDK_MAP_CONST},
	{"IPv6UploadDiagnosticsSupported",   "0", BDK_MAP_CONST},
	{"IPv4UDPEchoDiagnosticsSupported",  "0", BDK_MAP_CONST},
	{"IPv6UDPEchoDiagnosticsSupported",  "0", BDK_MAP_CONST},
	{0}
};

DMLEAF tRoot_098_Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DeviceSummary", &DMREAD, DMT_STRING, bdk_map_get, NULL, &DMFINFRM, NULL},
{"LANDeviceNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"WANDeviceNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"IPv4DownloadDiagnosticsSupported", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"IPv6DownloadDiagnosticsSupported", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"IPv4UploadDiagnosticsSupported", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"IPv6UploadDiagnosticsSupported", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"IPv4UDPEchoDiagnosticsSupported", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"IPv6UDPEchoDiagnosticsSupported", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* ---------------------------------------------------------------------- */
/* Time.  <- Device.Time.                                                  */
/* ---------------------------------------------------------------------- */

static const struct bdk_leafmap time_map[] = {
	{"Enable",               "Enable",           BDK_MAP_RW | BDK_MAP_BOOL},
	{"Status",               "Status",           BDK_MAP_RO},
	{"NTPServer1",           "NTPServer1",       BDK_MAP_RW},
	{"NTPServer2",           "NTPServer2",       BDK_MAP_RW},
	{"NTPServer3",           "NTPServer3",       BDK_MAP_RW},
	{"NTPServer4",           "NTPServer4",       BDK_MAP_RW},
	{"NTPServer5",           "NTPServer5",       BDK_MAP_RW},
	{"CurrentLocalTime",     "CurrentLocalTime", BDK_MAP_RO},
	{"LocalTimeZone",        "LocalTimeZone",    BDK_MAP_RW},
	{"LocalTimeZoneName",    "X_BROADCOM_COM_LocalTimeZoneName",    BDK_MAP_RW},
	{"DaylightSavingsUsed",  "X_BROADCOM_COM_DaylightSavingsUsed",  BDK_MAP_RW | BDK_MAP_BOOL},
	{"DaylightSavingsStart", "X_BROADCOM_COM_DaylightSavingsStart", BDK_MAP_RW},
	{"DaylightSavingsEnd",   "X_BROADCOM_COM_DaylightSavingsEnd",   BDK_MAP_RW},
	{0}
};

DMLEAF tTimeParams[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"NTPServer1", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"NTPServer2", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"NTPServer3", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"NTPServer4", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"NTPServer5", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"CurrentLocalTime", &DMREAD, DMT_TIME, bdk_map_get, NULL, NULL, NULL},
{"LocalTimeZone", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"LocalTimeZoneName", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"DaylightSavingsUsed", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"DaylightSavingsStart", &DMWRITE, DMT_TIME, bdk_map_get, bdk_map_set, NULL, NULL},
{"DaylightSavingsEnd", &DMWRITE, DMT_TIME, bdk_map_get, bdk_map_set, NULL, NULL},
{0}
};

/* ---------------------------------------------------------------------- */
/* Diagnostics Interface: TR-098 connection path <-> Device.IP.Interface.N */
/* ---------------------------------------------------------------------- */

static int ipif_on_ppp(unsigned int inst)
{
	char path[96], *v;

	snprintf(path, sizeof(path), "Device.IP.Interface.%u.LowerLayers", inst);
	bdk_get_value_default(path, "", &v);
	return strncmp(v, "Device.PPP.Interface.", 21) == 0;
}

/* "Device.IP.Interface.2" -> TR-098 path of the connection ("" stays "") */
static int get_diag_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *path, *v;
	unsigned int inst;

	(void)instance; (void)ctx;
	*value = "";
	path = bdk_map_resolve(refparam, data);
	if (!path)
		return 0;
	bdk_get_value_default(path, "", &v);
	if (strncmp(v, "Device.IP.Interface.", 20) != 0)
		return 0;
	inst = (unsigned int)strtoul(v + 20, NULL, 10);
	if (inst == 0)
		return 0;
	if (inst == 1)
		dmasprintf(value, "%s1.", TR098_LANIP);
	else
		dmasprintf(value, "%s%u.", ipif_on_ppp(inst) ? TR098_WANPPP : TR098_WANIP, inst);
	return 0;
}

/* TR-098 connection path -> "Device.IP.Interface.N"; "" = let the CPE choose */
static int diag_interface_to_tr181(const char *value, char *out, size_t outlen)
{
	const char *num = NULL;
	unsigned int inst;

	if (!value || !*value) {
		out[0] = '\0';
		return 0;
	}
	if (strncmp(value, TR098_WANIP, strlen(TR098_WANIP)) == 0)
		num = value + strlen(TR098_WANIP);
	else if (strncmp(value, TR098_WANPPP, strlen(TR098_WANPPP)) == 0)
		num = value + strlen(TR098_WANPPP);
	else if (strncmp(value, TR098_LANIP, strlen(TR098_LANIP)) == 0)
		num = "1";
	else if (strncmp(value, "Device.IP.Interface.", 20) == 0)
		num = value + 20;   /* ACS already speaks TR-181 */
	if (!num)
		return -1;
	inst = (unsigned int)strtoul(num, NULL, 10);
	if (inst == 0)
		return -1;
	snprintf(out, outlen, "Device.IP.Interface.%u", inst);
	return 0;
}

static int set_diag_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *path, ref[64];

	(void)instance;
	path = bdk_map_resolve(refparam, data);
	if (!path)
		return FAULT_9002;
	if (diag_interface_to_tr181(value, ref, sizeof(ref)))
		return FAULT_9007;
	switch (action) {
	case VALUECHECK:
		return bdk_check_writable(path);
	case VALUESET:
		return bdk_queue_set(ctx, refparam, path, ref);
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* IPPingDiagnostics.  <- Device.IP.Diagnostics.IPPing.                     */
/* ---------------------------------------------------------------------- */

static const struct bdk_leafmap ipping_map[] = {
	{"DiagnosticsState",    "DiagnosticsState",    BDK_MAP_RW},
	{"Interface",           "Interface",           BDK_MAP_RW},   /* via get/set_diag_interface */
	{"Host",                "Host",                BDK_MAP_RW},
	{"NumberOfRepetitions", "NumberOfRepetitions", BDK_MAP_RW},
	{"Timeout",             "Timeout",             BDK_MAP_RW},
	{"DataBlockSize",       "DataBlockSize",       BDK_MAP_RW},
	{"DSCP",                "DSCP",                BDK_MAP_RW},
	{"SuccessCount",        "SuccessCount",        BDK_MAP_RO},
	{"FailureCount",        "FailureCount",        BDK_MAP_RO},
	{"AverageResponseTime", "AverageResponseTime", BDK_MAP_RO},
	{"MinimumResponseTime", "MinimumResponseTime", BDK_MAP_RO},
	{"MaximumResponseTime", "MaximumResponseTime", BDK_MAP_RO},
	{0}
};

DMLEAF tIPPingDiagnosticsParam[] = {
{"DiagnosticsState", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_diag_interface, set_diag_interface, NULL, NULL},
{"Host", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"NumberOfRepetitions", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"Timeout", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"DataBlockSize", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"DSCP", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"SuccessCount", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"FailureCount", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"AverageResponseTime", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"MinimumResponseTime", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"MaximumResponseTime", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* ---------------------------------------------------------------------- */
/* TraceRouteDiagnostics.  <- Device.IP.Diagnostics.TraceRoute.             */
/* ---------------------------------------------------------------------- */

static const struct bdk_leafmap tracert_map[] = {
	{"DiagnosticsState",  "DiagnosticsState",        BDK_MAP_RW},
	{"Interface",         "Interface",               BDK_MAP_RW},   /* via get/set_diag_interface */
	{"Host",              "Host",                    BDK_MAP_RW},
	{"NumberOfTries",     "NumberOfTries",           BDK_MAP_RW},
	{"Timeout",           "Timeout",                 BDK_MAP_RW},
	{"DataBlockSize",     "DataBlockSize",           BDK_MAP_RW},
	{"DSCP",              "DSCP",                    BDK_MAP_RW},
	{"MaxHopCount",       "MaxHopCount",             BDK_MAP_RW},
	{"ResponseTime",      "ResponseTime",            BDK_MAP_RO},
	{"NumberOfRouteHops", "RouteHopsNumberOfEntries", BDK_MAP_RO},
	{0}
};

DMLEAF tTraceRouteDiagnosticsParam[] = {
{"DiagnosticsState", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_diag_interface, set_diag_interface, NULL, NULL},
{"Host", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"NumberOfTries", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"Timeout", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"DataBlockSize", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"DSCP", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"MaxHopCount", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"ResponseTime", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"NumberOfRouteHops", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* RouteHops.{i}. <- Device.IP.Diagnostics.TraceRoute.RouteHops.{i}. */
static const struct bdk_leafmap routehops_map[] = {
	{"HopHost",        "Host",        BDK_MAP_RO},
	{"HopHostAddress", "HostAddress", BDK_MAP_RO},
	{"HopErrorCode",   "ErrorCode",   BDK_MAP_RO},
	{"HopRTTimes",     "RTTimes",     BDK_MAP_RO},
	{0}
};

DMLEAF tRouteHopsParam[] = {
{"HopHost", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"HopHostAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"HopErrorCode", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"HopRTTimes", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMOBJ tTraceRouteDiagnosticsObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"RouteHops", &DMREAD, NULL, NULL, NULL, browseRouteHopsInst, NULL, &DMNONE, NULL, tRouteHopsParam, NULL},
{0}
};

int browseRouteHopsInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	unsigned int *inst = NULL, num = 0, i;
	char *sinst;

	(void)prev_data; (void)prev_instance;
	if (bdk_get_instances(TR181_TRACERT "RouteHops.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		snprintf(oc->tr181_base, sizeof(oc->tr181_base), TR181_TRACERT "RouteHops.%u.", inst[i]);
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* Layer3Forwarding.  <- Device.Routing.Router.1.                           */
/* ---------------------------------------------------------------------- */

#define TR181_ROUTER "Device.Routing.Router.1."
#define TR181_FWD    TR181_ROUTER "IPv4Forwarding."

static int get_l3_default_connection(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_l3_fwd_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_l3_fwd_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

static const struct bdk_leafmap l3fwd_root_map[] = {
	{"ForwardNumberOfEntries", "IPv4ForwardingNumberOfEntries", BDK_MAP_RO},
	{0}
};

/* Forwarding.{i}. <- IPv4Forwarding.{i}.  Source* / policy / MTU are not in
 * the TR-181 route entry: reported empty / -1 / 0 */
static const struct bdk_leafmap l3fwd_map[] = {
	{"Enable",           "Enable",           BDK_MAP_RW | BDK_MAP_BOOL},
	{"DestIPAddress",    "DestIPAddress",    BDK_MAP_RW},
	{"DestSubnetMask",   "DestSubnetMask",   BDK_MAP_RW},
	{"GatewayIPAddress", "GatewayIPAddress", BDK_MAP_RW},
	{"Interface",        "Interface",        BDK_MAP_RW},   /* via get/set_diag_interface */
	{"ForwardingMetric", "ForwardingMetric", BDK_MAP_RW},
	{"SourceIPAddress",  "",                 BDK_MAP_CONST},
	{"SourceSubnetMask", "",                 BDK_MAP_CONST},
	{"ForwardingPolicy", "-1",               BDK_MAP_CONST},
	{"MTU",              "0",                BDK_MAP_CONST},
	{CUSTOM_PREFIX"StaticRoute", "StaticRoute", BDK_MAP_RO | BDK_MAP_BOOL},
	{CUSTOM_PREFIX"Origin",      "Origin",      BDK_MAP_RO},
	{0}
};

DMLEAF tLayer3ForwardingParam[] = {
{"DefaultConnectionService", &DMREAD, DMT_STRING, get_l3_default_connection, NULL, NULL, NULL},
{"ForwardNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMLEAF tLayer3ForwardingEntryParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_l3_fwd_status, NULL, NULL, NULL},
{"Type", &DMREAD, DMT_STRING, get_l3_fwd_type, NULL, NULL, NULL},
{"DestIPAddress", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"DestSubnetMask", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"SourceIPAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"SourceSubnetMask", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ForwardingPolicy", &DMREAD, DMT_INT, bdk_map_get, NULL, NULL, NULL},
{"GatewayIPAddress", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_diag_interface, set_diag_interface, NULL, NULL},
{"ForwardingMetric", &DMWRITE, DMT_INT, bdk_map_get, bdk_map_set, NULL, NULL},
{"MTU", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{CUSTOM_PREFIX"StaticRoute", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{CUSTOM_PREFIX"Origin", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMOBJ tLayer3ForwardingObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Forwarding", &DMWRITE, add_l3_forwarding, delete_l3_forwarding, NULL, browseLayer3ForwardingInst, NULL, &DMNONE, NULL, tLayer3ForwardingEntryParam, NULL},
{0}
};

/* TR-098 path of the connection carrying the active default route */
static int get_l3_default_connection(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	unsigned int *inst = NULL, num = 0, i, ipif;
	char path[128], *v;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = "";
	if (bdk_get_instances(TR181_FWD, &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), TR181_FWD "%u.DestIPAddress", inst[i]);
		bdk_get_value_default(path, "", &v);
		if (v[0] && strcmp(v, "0.0.0.0") != 0)
			continue;
		snprintf(path, sizeof(path), TR181_FWD "%u.Interface", inst[i]);
		bdk_get_value_default(path, "", &v);
		if (strncmp(v, "Device.IP.Interface.", 20) != 0)
			continue;
		ipif = (unsigned int)strtoul(v + 20, NULL, 10);
		if (!ipif)
			continue;
		dmasprintf(value, "%s%u.", ipif_on_ppp(ipif) ? TR098_WANPPP : TR098_WANIP, ipif);
		return 0;
	}
	return 0;
}

static int get_l3_fwd_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[160], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "%sStatus", oc ? oc->tr181_base : "");
	bdk_get_value_default(path, "Disabled", &v);
	*value = (strcmp(v, "Enabled") == 0) ? "Enabled" : (strncmp(v, "Error", 5) == 0 ? "Error" : "Disabled");
	return 0;
}

/* Default (no destination) / Host (/32) / Network */
static int get_l3_fwd_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[160], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "%sDestIPAddress", oc ? oc->tr181_base : "");
	bdk_get_value_default(path, "", &v);
	if (!v[0] || strcmp(v, "0.0.0.0") == 0) {
		*value = "Default";
		return 0;
	}
	snprintf(path, sizeof(path), "%sDestSubnetMask", oc ? oc->tr181_base : "");
	bdk_get_value_default(path, "", &v);
	*value = (strcmp(v, "255.255.255.255") == 0) ? "Host" : "Network";
	return 0;
}

int browseLayer3ForwardingInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	unsigned int *inst = NULL, num = 0, i;
	char *sinst;

	(void)prev_data; (void)prev_instance;
	if (bdk_get_instances(TR181_FWD, &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		snprintf(oc->tr181_base, sizeof(oc->tr181_base), TR181_FWD "%u.", inst[i]);
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/* AddObject = a new static route (StaticRoute=1, the ACS fills the rest) */
int add_l3_forwarding(char *refparam, struct dmctx *ctx, void *data, char **instancepara)
{
	unsigned int inst = 0;
	char path[128];
	int fault;

	(void)refparam; (void)ctx; (void)data;
	fault = bdk_add_object(TR181_FWD, &inst);
	if (fault)
		return fault;
	snprintf(path, sizeof(path), TR181_FWD "%u.StaticRoute", inst);
	bdk_set_value_now(path, NULL, "1");
	dmasprintf(instancepara, "%u", inst);
	return 0;
}

/* DeleteObject: one entry, or (DEL_ALL) every static route — routes learnt
 * from DHCP/RA are never removed here, that would cut the WAN */
int delete_l3_forwarding(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	struct bdk_objctx *oc = data;
	unsigned int *inst = NULL, num = 0, i;
	char path[128], *v;
	int fault = 0;

	(void)refparam; (void)ctx; (void)instance;
	if (del_action == DEL_INST)
		return (oc && oc->tr181_base[0]) ? bdk_del_object(oc->tr181_base) : FAULT_9005;
	if (bdk_get_instances(TR181_FWD, &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), TR181_FWD "%u.StaticRoute", inst[i]);
		bdk_get_value_default(path, "0", &v);
		if (!(strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0))
			continue;
		snprintf(path, sizeof(path), TR181_FWD "%u.", inst[i]);
		if (bdk_del_object(path) && !fault)
			fault = FAULT_9002;
	}
	return fault;
}

/* ---------------------------------------------------------------------- */

void system_bdk_register(void)
{
	bdk_register_objmap("InternetGatewayDevice.Layer3Forwarding.", l3fwd_root_map, TR181_ROUTER);
	bdk_register_objmap("InternetGatewayDevice.Layer3Forwarding.Forwarding.{i}.", l3fwd_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.", root_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.Time.", time_map, TR181_TIME);
	bdk_register_objmap("InternetGatewayDevice.IPPingDiagnostics.", ipping_map, TR181_IPPING);
	bdk_register_objmap("InternetGatewayDevice.TraceRouteDiagnostics.", tracert_map, TR181_TRACERT);
	bdk_register_objmap("InternetGatewayDevice.TraceRouteDiagnostics.RouteHops.{i}.", routehops_map, NULL);
}
