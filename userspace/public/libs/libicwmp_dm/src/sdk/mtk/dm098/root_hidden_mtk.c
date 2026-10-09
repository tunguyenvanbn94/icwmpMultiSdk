/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	functions/tr098/root of the product: InternetGatewayDevice.DeviceSummary
 *	and the HIDDEN objects of the root, the ones it answers only when the
 *	request names them.
 *
 *	entry_execute_method_root() is a "case" on the requested path.  A
 *	whole-tree request ("" or "InternetGatewayDevice.") takes the first
 *	branch only, which is DeviceSummary (forced inform).  SelfTestDiagnostics.,
 *	WiFi., FaultMgmt., BulkData., CaptivePortal., FAP.GPS., User. ... sit in
 *	branches reached only when $1 starts with their own path.  So they never
 *	show in a full GetParameterNames/Values, in inform or in notifications,
 *	yet answer a request addressed to them.  DMOBJ.addressed_only
 *	(dmtr098.h) gives the engine that behaviour.
 *
 *	P5e, 3 parameters:
 *	  SelfTestDiagnostics.DiagnosticsState  writable, reads "None"; the shell
 *	      gave it no setter, and common_set_value_check_param() answers
 *	      9008 for a writable leaf without one -- the engine does the same
 *	      for a DMWRITE leaf with a NULL setter (mparam_set_value);
 *	  SelfTestDiagnostics.Results           reads "";
 *	  WiFi.NeighboringWiFiDiagnostic.DiagnosticsState  reads "".
 *
 *	P6a, 10 parameters, all constants of the shell (no UCI, no system call):
 *	  DeviceSummary                    the product's summary string;
 *	  BulkData.Enable/Status           "false" / "";
 *	  CaptivePortal.*                  Enable "false", Status "Enabled",
 *	      AllowedList "", CaptivePortalURL; the three writable ones take any
 *	      value of their type and store nothing (captive_portal_set_fake);
 *	  FAP.GPS.*                        zero coordinates, scan time of
 *	      0001-01-01;
 *	and the empty objects FaultMgmt.CurrentAlarm., BulkData.Profile.,
 *	SoftwareModules.DeploymentUnit., Layer2Bridging.AvailableInterface./
 *	Bridge., USBHosts.Host., User. (no instances, as in the shell).
 *
 *	One difference, on purpose: the shell's branch is "FAP.GPS."*, so a
 *	request for "InternetGatewayDevice.FAP." itself matched no branch and
 *	faulted 9005; here FAP. answers with its GPS child, like every other
 *	addressed object.
 */
#include <stdio.h>
#include <string.h>

#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"

static int get_rh_selftest_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "None";
	return 0;
}

static int get_rh_empty(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int get_rh_false(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

static int get_rh_device_summary(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "InternetGatewayDevice:1.0[](Baseline:1, EthernetLAN:1, WiFiLAN:1)";
	return 0;
}

static int get_rh_captive_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Enabled";
	return 0;
}

static int get_rh_captive_url(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "http://192.168.1.1/captiveportal";
	return 0;
}

/* captive_portal_set_fake: the type check in front of every setter
 * (mtk_input_contract) is all the shell did; nothing is stored */
static int set_rh_captive_fake(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return 0;
}

static int get_rh_gps_coordinate(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0.000000";
	return 0;
}

static int get_rh_gps_scan_time(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0001-01-01T00:00:00Z";
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tRootHiddenParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DeviceSummary", &DMREAD, DMT_STRING, get_rh_device_summary, NULL, &DMFINFRM, NULL},
{0}
};

static DMLEAF tSelfTestParams[] = {
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_rh_selftest_state, NULL, NULL, NULL},
{"Results", &DMREAD, DMT_STRING, get_rh_empty, NULL, NULL, NULL},
{0}
};

static DMLEAF tNeighborWiFiParams[] = {
{"DiagnosticsState", &DMREAD, DMT_STRING, get_rh_empty, NULL, NULL, NULL},
{0}
};

static DMLEAF tBulkDataParams[] = {
{"Enable", &DMREAD, DMT_BOOL, get_rh_false, NULL, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_rh_empty, NULL, NULL, NULL},
{0}
};

static DMLEAF tCaptivePortalParams[] = {
{"AllowedList", &DMWRITE, DMT_STRING, get_rh_empty, set_rh_captive_fake, NULL, NULL},
{"CaptivePortalURL", &DMWRITE, DMT_STRING, get_rh_captive_url, set_rh_captive_fake, NULL, NULL},
{"Enable", &DMWRITE, DMT_BOOL, get_rh_false, set_rh_captive_fake, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_rh_captive_status, NULL, NULL, NULL},
{0}
};

static DMLEAF tGpsParams[] = {
{"LastSuccessfulScanTime", &DMREAD, DMT_TIME, get_rh_gps_scan_time, NULL, NULL, NULL},
{"LockedLatitude", &DMREAD, DMT_STRING, get_rh_gps_coordinate, NULL, NULL, NULL},
{"LockedLongitude", &DMREAD, DMT_STRING, get_rh_gps_coordinate, NULL, NULL, NULL},
{0}
};

static DMOBJ tWiFiChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker, container_leaf, addressed_only */
{"NeighboringWiFiDiagnostic", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tNeighborWiFiParams, NULL},
{0}
};

static DMOBJ tFaultMgmtObj[] = {
{"CurrentAlarm", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tBulkDataObj[] = {
{"Profile", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tSoftwareModulesObj[] = {
{"DeploymentUnit", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tLayer2BridgingObj[] = {
{"AvailableInterface", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{"Bridge", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tUSBHostsObj[] = {
{"Host", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMOBJ tFAPObj[] = {
{"GPS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tGpsParams, NULL},
{0}
};

static DMOBJ tRootHiddenMtkObj[] = {
{"SelfTestDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tSelfTestParams, NULL, NULL, 1},
{"WiFi", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWiFiChildObj, NULL, NULL, NULL, 1},
{"FaultMgmt", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tFaultMgmtObj, NULL, NULL, NULL, 1},
{"BulkData", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tBulkDataObj, tBulkDataParams, NULL, NULL, 1},
{"SoftwareModules", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tSoftwareModulesObj, NULL, NULL, NULL, 1},
{"Layer2Bridging", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tLayer2BridgingObj, NULL, NULL, NULL, 1},
{"USBHosts", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tUSBHostsObj, NULL, NULL, NULL, 1},
{"CaptivePortal", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tCaptivePortalParams, NULL, NULL, 1},
{"FAP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tFAPObj, NULL, NULL, NULL, 1},
{"User", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 1},
{0}
};

static const char *const root_hidden_mtk_paths[] = {
	"InternetGatewayDevice.DeviceSummary",
	"InternetGatewayDevice.SelfTestDiagnostics.",
	"InternetGatewayDevice.WiFi.",
	"InternetGatewayDevice.FaultMgmt.",
	"InternetGatewayDevice.BulkData.",
	"InternetGatewayDevice.SoftwareModules.",
	"InternetGatewayDevice.Layer2Bridging.",
	"InternetGatewayDevice.USBHosts.",
	"InternetGatewayDevice.CaptivePortal.",
	"InternetGatewayDevice.FAP.",
	"InternetGatewayDevice.User.",
	NULL
};

static const struct dm_module root_hidden_mtk_module = {
	.name   = "mtk-root-hidden",
	.model  = DM_MODEL_TR098,
	.order  = DM_ORDER_SDK,
	.objs   = tRootHiddenMtkObj,
	.params = tRootHiddenParams,
	.paths  = root_hidden_mtk_paths,
};
DM_MODULE_REGISTER(root_hidden_mtk_module);

/* Not in the TR-181 tree (cwmp.cpe.datamodel=tr181, docs/plan/tr181_mtk_design.md
 * T7): every object here is a placeholder -- constants, writes that store
 * nothing, empty tables, diagnostics that run nothing -- which the standard
 * does not allow for an object the CPE lists.  SelfTestDiagnostics,
 * FaultMgmt, BulkData, SoftwareModules, USB.USBHosts, CaptivePortal, FAP,
 * Users.User and WiFi.NeighboringWiFiDiagnostic come back when the product
 * implements them, or when the operator's ACS needs them. */
