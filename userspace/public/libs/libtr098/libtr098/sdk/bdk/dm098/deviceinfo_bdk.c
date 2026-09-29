/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.DeviceInfo. on Broadcom BDK.
 *
 *	Pure 1:1 projection of Device.DeviceInfo.* (TR-181) so the whole object is
 *	table driven (bdk_map_get / bdk_map_set).  The helper functions at the end
 *	(get_deviceid_* / get_softwareversion) are what icwmpd itself uses to build
 *	the Inform DeviceId, they are declared in tr098/deviceinfo.h.
 *
 *	Source of the TR-181 side: devinfo_md owns Device.DeviceInfo. (see
 *	docs/tr069_cwmp_request_flow.md, namespace table).  The values come from
 *	the same MDM tr69c reads in updateTr69cCfgInfo_dev2().
 */
#include <stdio.h>
#include <string.h>
#include "dmtr098.h"
#include "dmcommon.h"
#include "deviceinfo.h"
#include "dmbdk.h"
#ifdef BDK_SAMPLE_OBJECT
#include "sample_bdk.h"
#endif

#define TR098_DEVINFO "InternetGatewayDevice.DeviceInfo."
#define TR181_DEVINFO "Device.DeviceInfo."

static const struct bdk_leafmap devinfo_map[] = {
	{"Manufacturer",              "Manufacturer",              BDK_MAP_RO},
	{"ManufacturerOUI",           "ManufacturerOUI",           BDK_MAP_RO},
	{"ModelName",                 "ModelName",                 BDK_MAP_RO},
	{"Description",               "Description",               BDK_MAP_RO},
	{"ProductClass",              "ProductClass",              BDK_MAP_RO},
	{"SerialNumber",              "SerialNumber",              BDK_MAP_RO},
	{"HardwareVersion",           "HardwareVersion",           BDK_MAP_RO},
	{"SoftwareVersion",           "SoftwareVersion",           BDK_MAP_RO},
	{"AdditionalHardwareVersion", "AdditionalHardwareVersion", BDK_MAP_RO},
	{"AdditionalSoftwareVersion", "AdditionalSoftwareVersion", BDK_MAP_RO},
	/* TR-098 only: TR-181 dropped SpecVersion, ACS expects "1.0" */
	{"SpecVersion",               "1.0",                       BDK_MAP_CONST},
	{"ProvisioningCode",          "ProvisioningCode",          BDK_MAP_RW},
	{"UpTime",                    "UpTime",                    BDK_MAP_RO},
	{"FirstUseDate",              "FirstUseDate",              BDK_MAP_RO},
	{"DeviceLog",                 "",                          BDK_MAP_CONST},
	{"VendorConfigFileNumberOfEntries", "VendorConfigFileNumberOfEntries", BDK_MAP_RO},
	/* no modem / optional features on this platform */
	{"ModemFirmwareVersion",      "",                          BDK_MAP_CONST},
	{"EnabledOptions",            "",                          BDK_MAP_CONST},
	{0}
};

/* DeviceInfo.VendorConfigFile.{i}. <- Device.DeviceInfo.VendorConfigFile.{i}. */
static const struct bdk_leafmap vcf_map[] = {
	{"Name",        "Name",        BDK_MAP_RO},
	{"Version",     "Version",     BDK_MAP_RO},
	{"Date",        "Date",        BDK_MAP_RO},
	{"Description", "Description", BDK_MAP_RO},
	{0}
};

/* DeviceInfo.MemoryStatus. / ProcessStatus. (TR-098 Amd 2) <- same names in
 * TR-181; the reference dump shows 0 for all of them, i.e. devinfo_md may not
 * fill them on this SDK */
static const struct bdk_leafmap memstatus_map[] = {
	{"Total", "Total", BDK_MAP_RO},
	{"Free",  "Free",  BDK_MAP_RO},
	{0}
};

static const struct bdk_leafmap procstatus_map[] = {
	{"CPUUsage",              "CPUUsage",              BDK_MAP_RO},
	{"ProcessNumberOfEntries", "ProcessNumberOfEntries", BDK_MAP_RO},
	{0}
};

DMLEAF tVendorConfigFileParam[] = {
{"Name", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Version", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Date", &DMREAD, DMT_TIME, bdk_map_get, NULL, NULL, NULL},
{"Description", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMLEAF tMemoryStatusParam[] = {
{"Total", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"Free", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMLEAF tProcessStatusParam[] = {
{"CPUUsage", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"ProcessNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

static int browseVendorConfigFileInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	unsigned int *inst = NULL, num = 0, i;
	char *sinst;

	(void)prev_data; (void)prev_instance;
	if (bdk_get_instances(TR181_DEVINFO "VendorConfigFile.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		snprintf(oc->tr181_base, sizeof(oc->tr181_base), TR181_DEVINFO "VendorConfigFile.%u.", inst[i]);
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/*
 * Identity override from UCI: cwmp.cpe.<option> in /data/icwmp/config/cwmp,
 * when non-empty, replaces the MDM value both in the Inform DeviceId
 * (get_deviceid_* below) and in DeviceInfo.* parameters.  Same option names
 * as the stock iopsys tr098/deviceinfo.c so a config from an OpenWrt icwmp
 * can be copied.  Empty / absent option = MDM value (production default).
 * Use: make a bring-up board show up on the ACS as a device that already has
 * a record there (GenieACS device id = OUI-ProductClass-SerialNumber).
 */
static const struct {
	const char *leaf;
	const char *uci;
} devinfo_override[] = {
	{"Manufacturer",    "manufacturer"},
	{"ManufacturerOUI", "oui"},
	{"ProductClass",    "product_class"},
	{"SerialNumber",    "serial_number"},
	{"SoftwareVersion", "software_version"},
	{"HardwareVersion", "hardware_version"},
	{"ModelName",       "model_name"},
	{"Description",     "description"},
	{NULL, NULL}
};

int devinfo_uci_override(const char *leaf, char **value)
{
	unsigned int i;
	char *v = NULL;

	for (i = 0; devinfo_override[i].leaf; i++) {
		if (strcmp(devinfo_override[i].leaf, leaf) != 0)
			continue;
		dmuci_get_option_value_string("cwmp", "cpe", (char *)devinfo_override[i].uci, &v);
		if (v && *v) {
			*value = v;              /* dm-allocated by dmuci */
			return 1;
		}
		return 0;
	}
	return 0;
}

static int get_devinfo(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *leaf = strrchr(refparam, '.');

	leaf = leaf ? leaf + 1 : refparam;
	if (devinfo_uci_override(leaf, value))
		return 0;
	return bdk_map_get(refparam, ctx, data, instance, value);
}

/*** DeviceInfo. ***/
DMLEAF tDeviceInfoParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Manufacturer", &DMREAD, DMT_STRING, get_devinfo, NULL, &DMFINFRM, NULL},
{"ManufacturerOUI", &DMREAD, DMT_STRING, get_devinfo, NULL, &DMFINFRM, NULL},
{"ModelName", &DMREAD, DMT_STRING, get_devinfo, NULL, &DMFINFRM, NULL},
{"Description", &DMREAD, DMT_STRING, get_devinfo, NULL, NULL, NULL},
{"ProductClass", &DMREAD, DMT_STRING, get_devinfo, NULL, &DMFINFRM, NULL},
{"SerialNumber", &DMREAD, DMT_STRING, get_devinfo, NULL, &DMFINFRM, NULL},
{"HardwareVersion", &DMREAD, DMT_STRING, get_devinfo, NULL, &DMFINFRM, NULL},
{"SoftwareVersion", &DMREAD, DMT_STRING, get_devinfo, NULL, &DMFINFRM, &DMACTIVE},
{"AdditionalHardwareVersion", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"AdditionalSoftwareVersion", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"SpecVersion", &DMREAD, DMT_STRING, bdk_map_get, NULL, &DMFINFRM, NULL},
{"ProvisioningCode", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, &DMFINFRM, &DMACTIVE},
{"UpTime", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"FirstUseDate", &DMREAD, DMT_TIME, bdk_map_get, NULL, NULL, NULL},
{"DeviceLog", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"VendorConfigFileNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"ModemFirmwareVersion", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"EnabledOptions", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
#ifdef BDK_SAMPLE_OBJECT
/* case 7 of tr098/bdk/sample_bdk.c: a new leaf in an existing object */
{CUSTOM_PREFIX"SampleUpTimeMinutes", &DMREAD, DMT_UNINT, get_sample_uptime_minutes, NULL, NULL, NULL},
#endif
{0}
};

DMOBJ tDeviceInfoObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"VendorConfigFile", &DMREAD, NULL, NULL, NULL, browseVendorConfigFileInst, NULL, &DMNONE, NULL, tVendorConfigFileParam, NULL},
{"MemoryStatus", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tMemoryStatusParam, NULL},
{"ProcessStatus", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tProcessStatusParam, NULL},
{0}
};

void deviceinfo_bdk_register(void)
{
	bdk_register_objmap(TR098_DEVINFO, devinfo_map, TR181_DEVINFO);
	bdk_register_objmap(TR098_DEVINFO "VendorConfigFile.{i}.", vcf_map, NULL);
	bdk_register_objmap(TR098_DEVINFO "MemoryStatus.", memstatus_map, TR181_DEVINFO "MemoryStatus.");
	bdk_register_objmap(TR098_DEVINFO "ProcessStatus.", procstatus_map, TR181_DEVINFO "ProcessStatus.");
}

/*
 * Helpers used by icwmpd (config.c cwmp_get_deviceid, event.c) to build the
 * Inform DeviceId.  Called inside a dm context, returned strings are
 * dm-allocated (freed at dm_ctx_clean) and copied by the caller.
 */
static char *devinfo_get(const char *leaf)
{
	char *v = "";
	char path[128];

	if (devinfo_uci_override(leaf, &v))
		return v;
	snprintf(path, sizeof(path), TR181_DEVINFO "%s", leaf);
	bdk_get_value_default(path, "", &v);
	return v;
}

int lookup_vcf_name(char *instance, char **value)
{
	(void)instance;
	*value = dmstrdup("");
	return 0;
}

char *get_deviceid_manufacturer()
{
	return devinfo_get("Manufacturer");
}

char *get_deviceid_manufactureroui()
{
	return devinfo_get("ManufacturerOUI");
}

char *get_deviceid_productclass()
{
	return devinfo_get("ProductClass");
}

char *get_deviceid_serialnumber()
{
	return devinfo_get("SerialNumber");
}

char *get_softwareversion()
{
	return devinfo_get("SoftwareVersion");
}
