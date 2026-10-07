/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.UserInterface.CarrierLocking. -- the operator's
 *	ISP locking (isplocking service), ported from
 *	functions/tr098/X_AIS_CarrierLocking.
 *
 *	Every leaf is an option of the first isplocking section:
 *	  X_AIS_LockingEnable            enabled          "1" or "0", only those
 *	  X_AIS_LockingStatus            locking_status   read only, the service's
 *	                                                  UNKNOWN_STATE reads "unknown"
 *	  X_AIS_DelayBeforeStartCheckISP delay_before_start  digits
 *	  X_AIS_DelayToCheckAgain        delay_recheck       digits
 *	  X_AIS_RoundNum                 round_num           digits
 *	  X_AIS_RoundDelay               round_time          digits
 *	  X_AIS_Guard_URL                ais_guard_url       any string
 *
 *	A setter adds the section when there is none (ensure_isplocking_section),
 *	writes only when the value changes or the section is new, and queues one
 *	"isplocking restart" for the end of the session.  The shell committed and
 *	queued per leaf; the commit is the engine's here, so an SPV that fails on
 *	another leaf leaves isplocking untouched.
 *
 *	The UserInterface object itself stays unclaimed: X_AIS_WebUserInfo below
 *	it is still the shell's until ported, and the row both sides print for
 *	UserInterface. is merged by add_list_paramameter().
 */
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define IL_PACKAGE	"isplocking"
#define IL_TYPE		"isplocking"
#define IL_SECTION	"@isplocking[0]"
#define IL_RESTART	"/etc/init.d/isplocking restart &"

static char *il_get(const char *option)
{
	return mtk_uci(IL_PACKAGE, IL_SECTION, option);
}

/* ensure_isplocking_section(): the first isplocking section, added when there
 * is none.  NULL when it cannot be added (the shell's "uci add" failed). */
static struct uci_section *il_section(int *created)
{
	struct uci_section *s, *added = NULL;
	char *name = NULL;	/* dmuci_add_section() writes it on every path */

	*created = 0;
	s = dmuci_walk_section(IL_PACKAGE, IL_TYPE, NULL, NULL, CMP_SECTION, NULL, NULL, GET_FIRST_SECTION);
	if (s)
		return s;
	dmuci_add_section(IL_PACKAGE, IL_TYPE, &added, &name);
	if (added)
		*created = 1;
	return added;
}

/* the VALUESET half every setter of the shell repeated */
static int il_set(const char *option, const char *value)
{
	char *current = il_get(option);
	struct uci_section *s;
	int created;

	s = il_section(&created);
	if (!s)
		return FAULT_9002;	/* E_INTERNAL_ERROR */
	if (!created && strcmp(current, value) == 0)
		return 0;
	dmuci_set_value_by_section(s, (char *)option, (char *)value);
	mtk_apply_service_once(IL_RESTART);
	return 0;
}

/* case "$v" in ''|*[!0-9]*) -> E_INVALID_PARAMETER_VALUE */
static int il_digits(const char *value)
{
	const char *p;

	if (!value || !*value)
		return 0;
	for (p = value; *p; p++) {
		if (*p < '0' || *p > '9')
			return 0;
	}
	return 1;
}

static int get_cl_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(il_get("enabled"), "1") == 0 ? "1" : "0";
	return 0;
}

static int set_cl_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	/* the boolean check in front took "true"/"false" too; the shell's own
	 * case then refused them */
	if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return il_set("enabled", value);
}

static int get_cl_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = il_get("locking_status");

	/* X_AIS_LockingStatus is at most 10 characters; UNKNOWN_STATE is the
	 * only state of the service longer than that */
	*value = strcmp(v, "UNKNOWN_STATE") == 0 ? "unknown" : v;
	return 0;
}

#define CL_DIGITS(name, option)							\
static int get_cl_##name(char *refparam, struct dmctx *ctx, void *data,	\
			 char *instance, char **value)				\
{										\
	*value = il_get(option);						\
	return 0;								\
}										\
static int set_cl_##name(char *refparam, struct dmctx *ctx, void *data,	\
			 char *instance, char *value, int action)		\
{										\
	if (!il_digits(value))							\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	return il_set(option, value);						\
}

CL_DIGITS(delay_before_start, "delay_before_start")
CL_DIGITS(delay_recheck, "delay_recheck")
CL_DIGITS(round_num, "round_num")
CL_DIGITS(round_time, "round_time")

static int get_cl_guard_url(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = il_get("ais_guard_url");
	return 0;
}

static int set_cl_guard_url(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	return il_set("ais_guard_url", value ? value : "");
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tCarrierLockingParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"X_AIS_LockingEnable", &DMWRITE, DMT_BOOL, get_cl_enable, set_cl_enable, NULL, NULL},
{"X_AIS_LockingStatus", &DMREAD, DMT_STRING, get_cl_status, NULL, NULL, NULL},
{"X_AIS_DelayBeforeStartCheckISP", &DMWRITE, DMT_STRING, get_cl_delay_before_start, set_cl_delay_before_start, NULL, NULL},
{"X_AIS_DelayToCheckAgain", &DMWRITE, DMT_STRING, get_cl_delay_recheck, set_cl_delay_recheck, NULL, NULL},
{"X_AIS_RoundNum", &DMWRITE, DMT_STRING, get_cl_round_num, set_cl_round_num, NULL, NULL},
{"X_AIS_RoundDelay", &DMWRITE, DMT_STRING, get_cl_round_time, set_cl_round_time, NULL, NULL},
{"X_AIS_Guard_URL", &DMWRITE, DMT_STRING, get_cl_guard_url, set_cl_guard_url, NULL, NULL},
{0}
};

static DMOBJ tUserInterfaceClObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"CarrierLocking", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tCarrierLockingParams, NULL},
{0}
};

static DMOBJ tCarrierLockingRoot[] = {
{"UserInterface", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tUserInterfaceClObj, NULL, NULL},
{0}
};

static const char *const carrierlocking_mtk_paths[] = {
	"InternetGatewayDevice.UserInterface.CarrierLocking.",
	NULL
};

static const struct dm_module carrierlocking_mtk_module = {
	.name  = "mtk-x-ais-carrierlocking",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tCarrierLockingRoot,
	.paths = carrierlocking_mtk_paths,
};
DM_MODULE_REGISTER(carrierlocking_mtk_module);
