/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice.1.X_AIS_Mesh. -- the operator's mesh
 *	control object, ported from functions/tr098/X_AIS_Mesh.
 *
 *	All four leaves are forced-inform in the product's tree, so they travel
 *	in every Inform and the ACS notices a mesh state change without polling.
 *
 *	Three different subsystems answer here, exactly as in the shell:
 *	  MeshEnabled            wireless.<radio>.map_mode + the backhaul iface
 *	  MeshMaximumHopNumber   1905d_cfg.map.max_hop
 *	  MeshMode               clay.opermode (the product's operating mode)
 *	  MeshHopNumberStatus    mapd topology dump, live
 *
 *	The LANDevice object and its single instance are browsed in lan_mtk.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define RADIO_DEVICE_2G		"MT7993_1_1"
#define RADIO_DEVICE_5G		"MT7993_1_2"
#define RADIO_DEVICE_BH		"rai4"
#define MAPD_DUMP_FILE		"/tmp/dump.txt"

/* clay.opermode -> the operator's MeshMode numbering */
static int mesh_mode_value(void)
{
	char *mode = mtk_uci("clay", "opermode", "mode");
	char *tagged = mtk_uci("clay", "opermode", "tagged");

	if (!mode || !*mode)
		return -1;
	if (strcmp(mode, "auto") == 0)
		return 0;
	if (strcmp(mode, "ap") == 0)
		return 3;
	if (strcmp(mode, "router") == 0)
		return strcmp(tagged, "1") == 0 ? 1 : 2;
	return 0;
}

/* is_router_meshmode(): mesh may only be driven from a router mode */
static int is_router_meshmode(void)
{
	int m = mesh_mode_value();

	return m == 1 || m == 2;
}

/*
 * The shell returned 9002 when both map_mode options were missing, while still
 * printing "0".  A fault inside the bulk GetParameterValues the ACS runs would
 * fail the whole RPC for one missing option, so the value is reported and the
 * fault is not -- the only intentional behaviour change in this object.
 */
static int get_mesh_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *m2g = mtk_uci("wireless", RADIO_DEVICE_2G, "map_mode");
	char *m5g = mtk_uci("wireless", RADIO_DEVICE_5G, "map_mode");

	*value = (strcmp(m2g, "1") == 0 && strcmp(m5g, "1") == 0) ? "1" : "0";
	return 0;
}

static int set_mesh_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);
	char *current = NULL;

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (!is_router_meshmode())
		return FAULT_9001;
	get_mesh_enabled(refparam, ctx, data, instance, &current);
	if (current && atoi(current) == b)
		return 0;	/* no-op, like the shell */
	dmuci_set_value("wireless", RADIO_DEVICE_2G, "map_mode", b ? "1" : "0");
	dmuci_set_value("wireless", RADIO_DEVICE_5G, "map_mode", b ? "1" : "0");
	dmuci_set_value("wireless", RADIO_DEVICE_BH, "disabled", b ? "0" : "1");
	/* request_reboot(): queued, so it runs after the session and after the
	 * engine has committed wireless */
	mtk_apply_service("sh -c 'sync; reboot -b KERNEL_RESET_ACS'");
	return 0;
}

/*
 * 1 = alone, 2 = one agent joined, 3 = more than one.  Counted from the mapd
 * topology dump: every "Device role" : "02" row is an agent.
 */
static int get_mesh_hop_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *dump_argv[] = { "/usr/bin/mapd_cli", "/tmp/mapd_ctrl", "dump_topology_v1", NULL };
	char *pidof_argv[] = { "/bin/pidof", "mapd", NULL };
	char line[512];
	int agents = 0;
	FILE *f;

	*value = "1";
	if (!is_router_meshmode())
		return 0;
	if (!*mtk_exec_line(pidof_argv)) {
		remove(MAPD_DUMP_FILE);
		return 0;
	}
	mtk_exec(dump_argv);
	f = fopen(MAPD_DUMP_FILE, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		char *p = strstr(line, "\"Device role\"");

		if (p && strstr(p, "\"02\""))
			agents++;
	}
	fclose(f);
	if (agents == 1)
		*value = "2";
	else if (agents > 1)
		*value = "3";
	return 0;
}

static int get_mesh_max_hop(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_uci("1905d_cfg", "map", "max_hop");

	*value = (v && *v) ? v : "0";
	return 0;
}

static int set_mesh_max_hop(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *p;

	if (!value || !*value)
		return FAULT_9007;
	for (p = value; *p; p++) {
		if (*p < '0' || *p > '9')
			return FAULT_9007;
	}
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("1905d_cfg", "map", "max_hop", value);
	mtk_apply_service("/etc/init.d/easymesh start");
	return 0;
}

static int get_mesh_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int m = mesh_mode_value();

	/* same reasoning as get_mesh_enabled: report 0, do not fault a bulk get */
	dmasprintf(value, "%d", m < 0 ? 0 : m);
	return 0;
}

static int set_mesh_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int want, current;

	if (!value || !*value)
		return FAULT_9007;
	want = atoi(value);
	if (want < 0 || want > 3)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	current = mesh_mode_value();
	if (current == want)
		return 0;
	switch (want) {
	case 0:
		dmuci_set_value("clay", "opermode", "mode", "auto");
		dmuci_set_value("clay", "opermode", "uplink", "pon");
		break;
	case 1:
		dmuci_set_value("clay", "opermode", "mode", "router");
		dmuci_set_value("clay", "opermode", "tagged", "1");
		dmuci_set_value("clay", "opermode", "uplink", "pon");
		break;
	case 2:
		dmuci_set_value("clay", "opermode", "mode", "router");
		dmuci_set_value("clay", "opermode", "tagged", "0");
		dmuci_set_value("clay", "opermode", "uplink", "eth1");
		break;
	case 3:
		dmuci_set_value("clay", "opermode", "mode", "ap");
		break;
	}
	/* both queued: the mode switch must see the committed clay config */
	mtk_apply_service("/usr/bin/prolinecmd resetreason set RESTORE_DEFAULT_ACS");
	mtk_apply_service("/etc/init.d/opermode restart");
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tMeshParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"MeshEnabled", &DMWRITE, DMT_BOOL, get_mesh_enabled, set_mesh_enabled, &DMFINFRM, NULL},
{"MeshHopNumberStatus", &DMREAD, DMT_UNINT, get_mesh_hop_status, NULL, &DMFINFRM, NULL},
{"MeshMaximumHopNumber", &DMWRITE, DMT_UNINT, get_mesh_max_hop, set_mesh_max_hop, &DMFINFRM, NULL},
{"MeshMode", &DMWRITE, DMT_UNINT, get_mesh_mode, set_mesh_mode, &DMFINFRM, NULL},
{0}
};

static DMOBJ tLanDeviceMeshObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"X_AIS_Mesh", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tMeshParam, NULL},
{0}
};

/* browseinstobj left NULL on purpose: lan_mtk.c owns the LANDevice instance */
static DMOBJ tLanDeviceMeshRoot[] = {
{"LANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tLanDeviceMeshObj, NULL, NULL},
{0}
};

static const char *const mesh_mtk_paths[] = {
	"InternetGatewayDevice.LANDevice.1.X_AIS_Mesh.",
	NULL
};

static const struct dm_module mesh_mtk_module = {
	.name  = "mtk-x-ais-mesh",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanDeviceMeshRoot,
	.paths = mesh_mtk_paths,
};
DM_MODULE_REGISTER(mesh_mtk_module);
