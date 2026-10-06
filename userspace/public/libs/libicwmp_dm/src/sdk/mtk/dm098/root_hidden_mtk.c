/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The HIDDEN objects of the root: the ones functions/tr098/root answers
 *	only when the request names them.
 *
 *	entry_execute_method_root() is a "case" on the requested path.  A
 *	whole-tree request ("" or "InternetGatewayDevice.") takes the first
 *	branch only; SelfTestDiagnostics., WiFi., FaultMgmt., BulkData.,
 *	CaptivePortal., FAP.GPS., User. ... sit in branches reached only when
 *	$1 starts with their own path.  So they never show in a full
 *	GetParameterNames/Values, in inform or in notifications, yet answer a
 *	request addressed to them.  DMOBJ.addressed_only (dmtr098.h) gives the
 *	engine that behaviour.
 *
 *	Here (P5e, 3 parameters):
 *	  SelfTestDiagnostics.DiagnosticsState  writable, reads "None"; the shell
 *	      gave it no setter, and common_set_value_check_param() answers
 *	      9008 for a writable leaf without one -- the engine does the same
 *	      for a DMWRITE leaf with a NULL setter (mparam_set_value);
 *	  SelfTestDiagnostics.Results           reads "";
 *	  WiFi.NeighboringWiFiDiagnostic.DiagnosticsState  reads "".
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

static DMLEAF tSelfTestParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"DiagnosticsState", &DMWRITE, DMT_STRING, get_rh_selftest_state, NULL, NULL, NULL},
{"Results", &DMREAD, DMT_STRING, get_rh_empty, NULL, NULL, NULL},
{0}
};

static DMLEAF tNeighborWiFiParams[] = {
{"DiagnosticsState", &DMREAD, DMT_STRING, get_rh_empty, NULL, NULL, NULL},
{0}
};

static DMOBJ tWiFiChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker, container_leaf, addressed_only */
{"NeighboringWiFiDiagnostic", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tNeighborWiFiParams, NULL},
{0}
};

static DMOBJ tRootHiddenMtkObj[] = {
{"SelfTestDiagnostics", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tSelfTestParams, NULL, NULL, 1},
{"WiFi", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tWiFiChildObj, NULL, NULL, NULL, 1},
{0}
};

static const char *const root_hidden_mtk_paths[] = {
	"InternetGatewayDevice.SelfTestDiagnostics.",
	"InternetGatewayDevice.WiFi.",
	NULL
};

static const struct dm_module root_hidden_mtk_module = {
	.name  = "mtk-root-hidden",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tRootHiddenMtkObj,
	.paths = root_hidden_mtk_paths,
};
DM_MODULE_REGISTER(root_hidden_mtk_module);
