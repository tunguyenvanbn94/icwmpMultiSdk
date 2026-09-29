/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	X_MARUSYS_COM_Sample. — TEMPLATE for developers: one file that shows
 *	every way a parameter can be implemented in icwmpd on BDK, under both
 *	roots.  Copy the case you need, rename, delete the rest.
 *
 *	    TR-098  InternetGatewayDevice.X_MARUSYS_COM_Sample.
 *	    TR-181  Device.X_MARUSYS_COM_Sample.
 *
 *	Cases (docs/icwmp_parameter_development_guide.md explains each one):
 *	  1. UciText            RW  value owned by icwmpd, stored in UCI cwmp.sample.text
 *	  2. LoadAverage        RO  value read from the system (/proc/loadavg)
 *	  3. MdmUpTime          RO  value read from the MDM (Device.DeviceInfo.UpTime)
 *	  4. MdmPeriodicInformInterval
 *	                       RW  value read from / written to the MDM in the SPV
 *	                           batch (Device.ManagementServer.PeriodicInformInterval)
 *	  5. CallCount          RO  value computed at run time (dmasprintf)
 *	  6. Radio.{i}.         instance object joined to Device.WiFi.Radio.{i}
 *	                           through a bdk_leafmap table (browse + objctx)
 *	  7. DeviceInfo.X_MARUSYS_COM_SampleUpTimeMinutes
 *	                       RO  a NEW leaf added to an EXISTING object of both
 *	                           models (TR-098 table row in deviceinfo_bdk.c,
 *	                           TR-181 static leaf inside the HAL object through
 *	                           proxy_static_leaves[] in dmproxy_bdk.c)
 *
 *	Compiled only with -DBDK_SAMPLE_OBJECT (bin/Makefile.am, remove it for a
 *	production build) and shown to the ACS only when UCI cwmp.sample.enable='1'
 *	(checkobj sample_enabled), so a dev image can keep it without polluting
 *	the data model seen by the ACS.
 *
 *	Rules that apply to every case:
 *	  - strings handed back through **value must be dm-allocated (dmstrdup,
 *	    dmasprintf, dmuci_get_*) or literals: they are freed at dm_ctx_clean;
 *	    never return malloc()/strdup() memory and never free them yourself
 *	  - a setter is called twice: action == VALUECHECK (validate only, return
 *	    FAULT_9007 on a bad value, touch nothing) then VALUESET (apply)
 *	  - MDM writes go through bdk_queue_set() in VALUESET so they join the
 *	    single batch SPV of the session (dm_platform_commit); the SDK RCL
 *	    applies them, no restart of anything from here
 *	  - MDM reads: bdk_get_value_default() (dm-allocated); one call = one
 *	    generic HAL GPV, so read an object once (bdk_get_subtree) when a getter
 *	    needs several leaves
 *	  - anything that must happen after the ACS session (reload config,
 *	    reboot) is requested with cwmp_set_end_session(END_SESSION_*)
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmcommon.h"
#include "dmbdk.h"
#include "sample_bdk.h"

#define SAMPLE_UCI_SECTION  "sample"                  /* cwmp.sample.* */
#define SAMPLE_TR098_OBJ    "InternetGatewayDevice." CUSTOM_PREFIX "Sample."
#define SAMPLE_TR181_OBJ    "Device." CUSTOM_PREFIX "Sample."

/* ------------------------------------------------------------------------ */
/* gate: checkobj of the object in both roots                                */
/* ------------------------------------------------------------------------ */

/* DMOBJ.checkobj: false = the engine skips the object (GPN does not list
 * it, GPV/SPV on it fault 9005).  Runs on every walk, keep it cheap. */
bool sample_enabled(struct dmctx *ctx, void *data)
{
	char *v = NULL;

	(void)ctx; (void)data;
	dmuci_get_option_value_string("cwmp", SAMPLE_UCI_SECTION, "enable", &v);
	return v && (v[0] == '1' || strcasecmp(v, "true") == 0);
}

/* ------------------------------------------------------------------------ */
/* case 1: value owned by icwmpd, kept in UCI                                */
/* ------------------------------------------------------------------------ */

static int get_sample_uci_text(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	/* dmuci_get_option_value_string always sets *value ("" when absent) */
	dmuci_get_option_value_string("cwmp", SAMPLE_UCI_SECTION, "text", value);
	return 0;
}

static int set_sample_uci_text(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	switch (action) {
	case VALUECHECK:
		/* validate only; FAULT_9007 = "invalid parameter value" */
		if (strlen(value) > 64)
			return FAULT_9007;
		return 0;
	case VALUESET:
		/* dmuci_set_value + dmuci_commit is done by the engine at the end of
		 * dm_entry_apply (CMD_SET_VALUE) — a plain set is enough here */
		dmuci_set_value("cwmp", SAMPLE_UCI_SECTION, "text", value);
		/* if icwmpd must re-read its config for the value to take effect:
		 * cwmp_set_end_session(END_SESSION_RELOAD); */
		return 0;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* case 2: value from the system                                             */
/* ------------------------------------------------------------------------ */

static int get_sample_loadavg(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[64] = "";
	FILE *f = fopen("/proc/loadavg", "r");

	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = "";
	if (!f)
		return 0;                       /* unreadable = empty, never a fault */
	if (fgets(buf, sizeof(buf), f)) {
		char *sp = strchr(buf, ' ');
		if (sp)
			*sp = '\0';                 /* first field: 1 minute average */
		*value = dmstrdup(buf);
	}
	fclose(f);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* case 3: value read from the MDM (generic HAL)                             */
/* ------------------------------------------------------------------------ */

static int get_sample_mdm_uptime(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	/* "0" when the MDM has no value / the GPV fails; the fault is logged by
	 * dmplatform_bdk.c, the ACS still gets a value for the rest of the object */
	bdk_get_value_default("Device.DeviceInfo.UpTime", "0", value);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* case 4: value read from and written to the MDM (batch SPV)                */
/* ------------------------------------------------------------------------ */

#define SAMPLE_MDM_RW_PATH "Device.ManagementServer.PeriodicInformInterval"

static int get_sample_mdm_interval(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	bdk_get_value_default(SAMPLE_MDM_RW_PATH, "", value);
	return 0;
}

static int set_sample_mdm_interval(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *end;
	unsigned long v = strtoul(value, &end, 10);

	(void)data; (void)instance;
	if (*end || v < 1)
		return FAULT_9007;
	switch (action) {
	case VALUECHECK:
		/* exists and writable in the MDM?  FAULT_9005 / FAULT_9008 otherwise */
		return bdk_check_writable(SAMPLE_MDM_RW_PATH);
	case VALUESET:
		/* refparam = the name the ACS used, only for the per-parameter
		 * fault list; the write itself goes into the session batch */
		return bdk_queue_set(ctx, refparam, SAMPLE_MDM_RW_PATH, value);
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* case 5: value computed at run time                                        */
/* ------------------------------------------------------------------------ */

static unsigned int sample_calls;

static int get_sample_call_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	sample_calls++;
	dmasprintf(value, "%u", sample_calls);      /* dm-allocated */
	return 0;
}

/* ------------------------------------------------------------------------ */
/* case 6: instance object joined to MDM instances through a map table       */
/* ------------------------------------------------------------------------ */

/* Radio.{i}.: one instance per Device.WiFi.Radio.{i}.  The table is keyed by
 * TR-098 leaf name; tr181 paths are relative to bdk_objctx.tr181_base that
 * the browse function sets ("Device.WiFi.Radio.<n>."), "Device...." would be
 * absolute, {aux0}..{aux3} expand bdk_objctx.aux[] (see dmbdk.h). */
static const struct bdk_leafmap sample_radio_map[] = {
	{"Name",     "Name",                   BDK_MAP_RO},
	{"Band",     "OperatingFrequencyBand", BDK_MAP_RO},
	{"Enable",   "Enable",                 BDK_MAP_RW | BDK_MAP_BOOL},
	{"Channel",  "Channel",                BDK_MAP_RW},
	{"Kind",     "sample",                 BDK_MAP_CONST},    /* literal value */
	{NULL, NULL, 0}
};

static int browseSampleRadioInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	unsigned int *inst = NULL, num = 0, i;
	char *sinst;

	(void)prev_data; (void)prev_instance;
	/* instance numbers of the MDM object (dm-allocated array) */
	if (bdk_get_instances("Device.WiFi.Radio.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		/* base every relative map entry hangs from; aux[] for joins */
		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.WiFi.Radio.%u.", inst[i]);
		oc->aux[0] = inst[i];
		/* TR-098 instance number shown to the ACS = MDM instance (stable
		 * across reboots because the MDM keeps it) */
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

static DMLEAF tSampleRadioParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Name",    &DMREAD,  DMT_STRING, bdk_map_get, NULL,        NULL, NULL},
{"Band",    &DMREAD,  DMT_STRING, bdk_map_get, NULL,        NULL, NULL},
{"Enable",  &DMWRITE, DMT_BOOL,   bdk_map_get, bdk_map_set, NULL, NULL},
{"Channel", &DMWRITE, DMT_UNINT,  bdk_map_get, bdk_map_set, NULL, NULL},
{"Kind",    &DMREAD,  DMT_STRING, bdk_map_get, NULL,        NULL, NULL},
{0}
};

/* ------------------------------------------------------------------------ */
/* case 7: new leaf in an existing object of both models                     */
/* ------------------------------------------------------------------------ */

/* DeviceInfo.X_MARUSYS_COM_SampleUpTimeMinutes: a derived value.  Wired in
 * tDeviceInfoParams (deviceinfo_bdk.c, TR-098) and tDeviceInfo181Params
 * (root181_bdk.c, TR-181 static leaf of the HAL object Device.DeviceInfo.). */
int get_sample_uptime_minutes(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = NULL;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	bdk_get_value_default("Device.DeviceInfo.UpTime", "0", &v);
	dmasprintf(value, "%lu", strtoul(v, NULL, 10) / 60);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* tables                                                                    */
/* ------------------------------------------------------------------------ */

DMLEAF tSampleParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"UciText",                   &DMWRITE, DMT_STRING, get_sample_uci_text,     set_sample_uci_text,     NULL, NULL},
{"LoadAverage",               &DMREAD,  DMT_STRING, get_sample_loadavg,      NULL,                    NULL, NULL},
{"MdmUpTime",                 &DMREAD,  DMT_UNINT,  get_sample_mdm_uptime,   NULL,                    NULL, NULL},
{"MdmPeriodicInformInterval", &DMWRITE, DMT_UNINT,  get_sample_mdm_interval, set_sample_mdm_interval, NULL, NULL},
{"CallCount",                 &DMREAD,  DMT_UNINT,  get_sample_call_count,   NULL,                    NULL, NULL},
/* forced_inform = &DMFINFRM puts the leaf in every Inform, notification =
 * &DMACTIVE gives it active notification by default (see managementserver.c) */
{0}
};

DMOBJ tSampleObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Radio", &DMREAD, NULL, NULL, NULL, browseSampleRadioInst, NULL, &DMNONE, NULL, tSampleRadioParam, NULL},
{0}
};

/* The row each root adds to its object list (a DMOBJ array cannot include
 * another one, so it is written out in root_bdk.c and root181_bdk.c):
 * {CUSTOM_PREFIX"Sample", &DMREAD, NULL, NULL, sample_enabled, NULL, NULL, &DMNONE, tSampleObj, tSampleParam, NULL}
 */

void sample_bdk_register(void)
{
	/* the map table is looked up by the TR-098-style object path of the
	 * parameter the ACS used (instance numbers -> "{i}"): register it under
	 * both roots so the same table serves TR-098 and TR-181 */
	bdk_register_objmap(SAMPLE_TR098_OBJ "Radio.{i}.", sample_radio_map, NULL);
	bdk_register_objmap(SAMPLE_TR181_OBJ "Radio.{i}.", sample_radio_map, NULL);
}
