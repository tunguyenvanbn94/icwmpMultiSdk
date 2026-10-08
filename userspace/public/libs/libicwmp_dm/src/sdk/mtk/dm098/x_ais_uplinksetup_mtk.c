/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.X_AIS_UplinkSetup. -- single/dual uplink (PON and
 *	the LAN ports as WAN), ported from functions/tr098/X_AIS_UplinkSetup.
 *
 *	The dualuplink config:
 *	  mode                      common.enabled "1" -> "1" (dual), else "0"
 *	  CurrentUplinkType         clay.opermode.uplink as lan1..lan4/optic
 *	  UplinkStatus              up/down of that uplink: the LAN port's state
 *	                            from blapi_cmd, or the PON activation state
 *	  AllowAdmin                common.allow_admin, "0"/"1"
 *	  DualUplink.mode           common.mode, 0 PON-Eth, 1 Eth-PON, 2 Eth-Eth
 *	  DualUplink.mode1.BackupUplink  @uplink[0].backup   lan1..lan4
 *	  DualUplink.mode2.MainUplink    @uplink[1].main     lan1..lan4
 *	  DualUplink.mode3.MainUplink    @uplink[2].main     lan1..lan4, never
 *	  DualUplink.mode3.BackupUplink  @uplink[2].backup   the other one's
 *	  DualUplink.BackupOver, NoWANIPTime, DelayBeforeSwitchUplink,
 *	      IncreaseTime          timer.backup_over_time, no_wanip_time,
 *	                            delay_before_switch, increase_time: digits, > 0
 *	  DualUplink.DualUplinkFlag common.flag, read only
 *	  DualUplink.VlanTaggingEnable  common.tagged, "0"/"1"
 *	An uplink is stored as eth1..eth4 or pon, and reads lan1..lan4, or
 *	"optic" for anything else.
 *
 *	hni owns most of it: mode, DualUplink.mode, the mode1..3 uplinks and
 *	VlanTaggingEnable go through "ubus call hni.dualuplink set {param,value}"
 *	-- only when the value changes -- and a reply whose result is not
 *	"SUCCESS" is E_INTERNAL_ERROR.  Those calls are made in the setter, at
 *	VALUESET, as the shell did: the fault depends on the reply.  hni writes
 *	and commits dualuplink itself, so the "current value" these setters
 *	compare with is read with "uci get" from the files, as the shell read
 *	it, not from this session's copy of the package, which an hni write
 *	earlier in the same SPV has made stale (mode3 Main then Backup).
 *
 *	AllowAdmin and the four timers are written here; a changed timer
 *	signals dualuplink (USR1) to reload them, queued for the end of the
 *	session.
 */
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define UL_PACKAGE	"dualuplink"
#define UL_TIMER_RELOAD	"killall -USR1 dualuplink"
#define UL_BLAPI	"/userfs/bin/blapi_cmd"
#define UL_PON_INFO	"/proc/xpon/ponInfo"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* uplink_to_tr069_lower */
static const char *ul_to_tr069(const char *uplink)
{
	static const char *const map[][2] = {
		{ "eth1", "lan1" }, { "eth2", "lan2" }, { "eth3", "lan3" }, { "eth4", "lan4" },
	};
	size_t i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (strcmp(uplink, map[i][0]) == 0)
			return map[i][1];
	}
	return "optic";
}

/* is_uplink_tr069_eth; tr069_to_uplink of those four is "eth<n>" */
static int ul_is_eth(const char *v)
{
	return v && strncmp(v, "lan", 3) == 0 && v[3] >= '1' && v[3] <= '4' && !v[4];
}

/* "$(uci -q get <path>)" from the files, see the header */
static char *ul_uci_now(const char *path)
{
	char *argv[] = { "uci", "-q", "get", (char *)path, NULL };

	return mtk_exec_line(argv);
}

/* get_uplink_cfg <idx> <main|backup>, from the files */
static const char *ul_uplink_now(int idx, const char *which)
{
	char path[64];

	snprintf(path, sizeof(path), UL_PACKAGE ".@uplink[%d].%s", idx, which);
	return ul_to_tr069(ul_uci_now(path));
}

static const char *ul_uplink(int idx, const char *which)
{
	char sec[24];

	snprintf(sec, sizeof(sec), "@uplink[%d]", idx);
	return ul_to_tr069(mtk_uci(UL_PACKAGE, sec, which));
}

/* ubus call hni.dualuplink set {"param": ..., "value": ...} + the
 * jsonfilter -e "@.result" = SUCCESS test */
static int ul_hni_set(const char *param, const char *value)
{
	char payload[96];
	char *argv[] = { "ubus", "call", "hni.dualuplink", "set", payload, NULL };
	json_object *reply, *result;
	const char *res;
	char *out;
	int ok = 0;

	snprintf(payload, sizeof(payload), "{\"param\": \"%s\", \"value\": \"%s\"}", param, value);
	out = mtk_exec(argv);
	reply = *out ? json_tokener_parse(out) : NULL;
	if (reply) {
		ok = json_object_object_get_ex(reply, "result", &result) &&
		     (res = json_object_get_string(result)) != NULL && strcmp(res, "SUCCESS") == 0;
		json_object_put(reply);
	}
	return ok ? 0 : FAULT_9002;
}

static int ul_01(const char *v)
{
	return v && (strcmp(v, "0") == 0 || strcmp(v, "1") == 0);
}

/* ------------------------------------------------------------------ */
/* X_AIS_UplinkSetup                                                   */
/* ------------------------------------------------------------------ */

static int get_ul_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci(UL_PACKAGE, "common", "enabled"), "1") == 0 ? "1" : "0";
	return 0;
}

/* 0: single uplink, 1: dual uplink */
static int set_ul_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!ul_01(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(ul_uci_now(UL_PACKAGE ".common.enabled"), value) == 0)
		return 0;
	return ul_hni_set("enable", value);
}

static int get_ul_current_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (char *)ul_to_tr069(mtk_uci("clay", "opermode", "uplink"));
	return 0;
}

/* "cut -d<d> -f<n>" of one line: the whole line when it has no <d> */
static void ul_cut(const char *line, char d, int n, char *out, size_t sz)
{
	const char *p = line, *end;
	size_t len;

	if (!strchr(line, d)) {
		snprintf(out, sz, "%s", line);
		return;
	}
	while (--n > 0) {
		p = strchr(p, d);
		if (!p) {
			out[0] = '\0';
			return;
		}
		p++;
	}
	end = strchr(p, d);
	len = end ? (size_t)(end - p) : strlen(p);
	if (len >= sz)
		len = sz - 1;
	memcpy(out, p, len);
	out[len] = '\0';
}

/* "<cmd output> | grep <pattern> | cut ... | cut ...": the one matching
 * line's field.  No match, or more than one (the shell then held several
 * lines and compared them as one string), gives NULL. */
static int ul_one_line(const char *text, int (*match)(const char *line, const void *arg),
		       const void *arg, char *line, size_t sz)
{
	const char *p = text;
	int found = 0;

	while (p && *p) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		char buf[512];

		if (len >= sizeof(buf))
			len = sizeof(buf) - 1;
		memcpy(buf, p, len);
		buf[len] = '\0';
		if (match(buf, arg)) {
			if (found++)
				return 0;
			snprintf(line, sz, "%s", buf);
		}
		p = nl ? nl + 1 : NULL;
	}
	return found == 1;
}

static int ul_starts(const char *line, const void *prefix)
{
	return strncmp(line, (const char *)prefix, strlen((const char *)prefix)) == 0;
}

static int ul_contains(const char *line, const void *needle)
{
	return strstr(line, (const char *)needle) != NULL;
}

static int get_ul_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *uplink = mtk_uci("clay", "opermode", "uplink");
	char line[512], f[128], g[128];

	*value = "down";
	if (ul_is_eth(ul_to_tr069(uplink))) {
		/* Ethernet uplink: "LAN<n>=UP,..." of blapi_cmd */
		char *argv[] = { UL_BLAPI, "traffic", "get_lanport_info", NULL };
		char prefix[8];

		snprintf(prefix, sizeof(prefix), "LAN%c=", uplink[3]);
		if (ul_one_line(mtk_exec(argv), ul_starts, prefix, line, sizeof(line))) {
			ul_cut(line, '=', 2, f, sizeof(f));
			ul_cut(f, ',', 1, g, sizeof(g));
			if (strcmp(g, "UP") == 0)
				*value = "up";
		}
	} else {
		/* PON uplink: the G_ACTIVATION state, O2..O5 count as up */
		char *argv[] = { "cat", UL_PON_INFO, NULL };

		if (ul_one_line(mtk_exec(argv), ul_contains, "G_ACTIVATION", line, sizeof(line))) {
			ul_cut(line, ':', 2, f, sizeof(f));
			ul_cut(f, ' ', 2, g, sizeof(g));
			if (!strcmp(g, "0x5") || !strcmp(g, "0x4") || !strcmp(g, "0x3") || !strcmp(g, "0x2"))
				*value = "up";
		}
	}
	return 0;
}

static int get_ul_allow_admin(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci(UL_PACKAGE, "common", "allow_admin"), "1") == 0 ? "1" : "0";
	return 0;
}

static int set_ul_allow_admin(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!ul_01(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(mtk_uci(UL_PACKAGE, "common", "allow_admin"), value) != 0)
		dmuci_set_value(UL_PACKAGE, "common", "allow_admin", value);
	return 0;
}

/* ------------------------------------------------------------------ */
/* DualUplink                                                          */
/* ------------------------------------------------------------------ */

static int get_dul_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(UL_PACKAGE, "common", "mode");
	return 0;
}

/* 0: PON - Ethernet, 1: Ethernet - PON, 2: Ethernet - Ethernet */
static int set_dul_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || (strcmp(value, "0") && strcmp(value, "1") && strcmp(value, "2")))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(ul_uci_now(UL_PACKAGE ".common.mode"), value) == 0)
		return 0;
	return ul_hni_set("mode", value);
}

/* the four uplink leaves: idx/which of @uplink, hni's param, and the leaf
 * of the same mode that must differ (mode3 only) */
static int ul_set_uplink(int idx, const char *which, const char *param,
			 const char *other, const char *value, int action)
{
	char eth[8];

	if (!ul_is_eth(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	/* main and backup uplink cannot be the same: the other leaf as it is
	 * now, an hni write of this SPV included -- hence at VALUESET */
	if (other && strcmp(value, ul_uplink_now(idx, other)) == 0)
		return FAULT_9007;
	if (strcmp(ul_uplink_now(idx, which), value) == 0)
		return 0;
	snprintf(eth, sizeof(eth), "eth%c", value[3]);
	return ul_hni_set(param, eth);
}

static int get_dul_mode1_backup(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (char *)ul_uplink(0, "backup");
	return 0;
}

static int set_dul_mode1_backup(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ul_set_uplink(0, "backup", "backup1", NULL, value, action);
}

static int get_dul_mode2_main(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (char *)ul_uplink(1, "main");
	return 0;
}

static int set_dul_mode2_main(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ul_set_uplink(1, "main", "main2", NULL, value, action);
}

static int get_dul_mode3_main(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (char *)ul_uplink(2, "main");
	return 0;
}

static int set_dul_mode3_main(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ul_set_uplink(2, "main", "main3", "backup", value, action);
}

static int get_dul_mode3_backup(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (char *)ul_uplink(2, "backup");
	return 0;
}

static int set_dul_mode3_backup(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ul_set_uplink(2, "backup", "backup3", "main", value, action);
}

/* verify_digit + [ "$v" -gt 0 ]; written, and dualuplink signalled, when
 * it changes */
#define DUL_TIMER(name, option)							\
static int get_dul_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	*value = mtk_uci(UL_PACKAGE, "timer", option);				\
	return 0;								\
}										\
static int set_dul_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char *value, int action)		\
{										\
	const char *p;								\
	long long n;								\
										\
	if (!value || !*value)							\
		return FAULT_9007;						\
	for (p = value; *p; p++) {						\
		if (*p < '0' || *p > '9')					\
			return FAULT_9007;					\
	}									\
	if (mtk_shell_getn(value, &n) != 0 || n <= 0)				\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	if (strcmp(mtk_uci(UL_PACKAGE, "timer", option), value) == 0)		\
		return 0;							\
	dmuci_set_value(UL_PACKAGE, "timer", option, value);			\
	mtk_apply_service_once(UL_TIMER_RELOAD);				\
	return 0;								\
}

DUL_TIMER(backup_over, "backup_over_time")
DUL_TIMER(no_wanip_time, "no_wanip_time")
DUL_TIMER(delay_before_switch, "delay_before_switch")
DUL_TIMER(increase_time, "increase_time")

static int get_dul_flag(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(UL_PACKAGE, "common", "flag");
	return 0;
}

static int get_dul_vlan(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci(UL_PACKAGE, "common", "tagged"), "1") == 0 ? "1" : "0";
	return 0;
}

static int set_dul_vlan(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!ul_01(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(ul_uci_now(UL_PACKAGE ".common.tagged"), value) == 0)
		return 0;
	return ul_hni_set("vlan", value);
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDulMode1Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"BackupUplink", &DMWRITE, DMT_STRING, get_dul_mode1_backup, set_dul_mode1_backup, NULL, NULL},
{0}
};

static DMLEAF tDulMode2Params[] = {
{"MainUplink", &DMWRITE, DMT_STRING, get_dul_mode2_main, set_dul_mode2_main, NULL, NULL},
{0}
};

static DMLEAF tDulMode3Params[] = {
{"MainUplink", &DMWRITE, DMT_STRING, get_dul_mode3_main, set_dul_mode3_main, NULL, NULL},
{"BackupUplink", &DMWRITE, DMT_STRING, get_dul_mode3_backup, set_dul_mode3_backup, NULL, NULL},
{0}
};

static DMLEAF tDualUplinkParams[] = {
{"mode", &DMWRITE, DMT_STRING, get_dul_mode, set_dul_mode, NULL, NULL},
{"BackupOver", &DMWRITE, DMT_STRING, get_dul_backup_over, set_dul_backup_over, NULL, NULL},
{"NoWANIPTime", &DMWRITE, DMT_STRING, get_dul_no_wanip_time, set_dul_no_wanip_time, NULL, NULL},
{"DelayBeforeSwitchUplink", &DMWRITE, DMT_STRING, get_dul_delay_before_switch, set_dul_delay_before_switch, NULL, NULL},
{"IncreaseTime", &DMWRITE, DMT_STRING, get_dul_increase_time, set_dul_increase_time, NULL, NULL},
{"DualUplinkFlag", &DMREAD, DMT_STRING, get_dul_flag, NULL, NULL, NULL},
{"VlanTaggingEnable", &DMWRITE, DMT_STRING, get_dul_vlan, set_dul_vlan, NULL, NULL},
{0}
};

static DMOBJ tDualUplinkObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"mode1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDulMode1Params, NULL},
{"mode2", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDulMode2Params, NULL},
{"mode3", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDulMode3Params, NULL},
{0}
};

static DMLEAF tUplinkSetupParams[] = {
{"mode", &DMWRITE, DMT_STRING, get_ul_mode, set_ul_mode, NULL, NULL},
{"CurrentUplinkType", &DMREAD, DMT_STRING, get_ul_current_type, NULL, NULL, NULL},
{"UplinkStatus", &DMREAD, DMT_STRING, get_ul_status, NULL, NULL, NULL},
{"AllowAdmin", &DMWRITE, DMT_STRING, get_ul_allow_admin, set_ul_allow_admin, NULL, NULL},
{0}
};

static DMOBJ tUplinkSetupObj[] = {
{"DualUplink", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDualUplinkObj, tDualUplinkParams, NULL},
{0}
};

static DMOBJ tUplinkSetupRoot[] = {
{"X_AIS_UplinkSetup", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tUplinkSetupObj, tUplinkSetupParams, NULL},
{0}
};

static const char *const uplinksetup_mtk_paths[] = {
	"InternetGatewayDevice.X_AIS_UplinkSetup.",
	NULL
};

static const struct dm_module uplinksetup_mtk_module = {
	.name  = "mtk-x-ais-uplinksetup",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tUplinkSetupRoot,
	.paths = uplinksetup_mtk_paths,
};
DM_MODULE_REGISTER(uplinksetup_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): the same tables under Device., type C
 * of docs/plan/tr181_mtk_design.md */
static const char *const uplinksetup_mtk_paths181[] = {
	"Device.X_AIS_UplinkSetup.",
	NULL
};

static const struct dm_module uplinksetup_mtk_module181 = {
	.name  = "mtk-x-ais-uplinksetup-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tUplinkSetupRoot,
	.paths = uplinksetup_mtk_paths181,
};
DM_MODULE_REGISTER(uplinksetup_mtk_module181);
