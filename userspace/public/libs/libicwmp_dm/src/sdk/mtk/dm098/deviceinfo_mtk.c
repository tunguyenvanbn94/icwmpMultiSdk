/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.DeviceInfo. for the MTK/Airoha product, native C.
 *
 *	One for one with functions/common/device_info of the easycwmp library
 *	the product shipped, same UCI options and same files:
 *
 *	  identity              easycwmp.@device[0].*  (the init script fills it
 *	                        from system.@devinfo[0], the PON serial and the
 *	                        OUI at every start -- files/mtk/icwmpd.init)
 *	  ProvisioningCode      easycwmp.@local[0].provisioning_code
 *	  UpTime                /proc/uptime
 *	  MemoryStatus          /proc/meminfo MemTotal / MemFree, in kB
 *	  ProcessStatus.CPUUsage /tmp/cpu_avg_usage, written by the product
 *	  X_AIS_CpuUsed/MemUsed  request/response pair in /var/state: the ACS
 *	                        writes 1, the CPE measures and puts the result in
 *	                        ...Response and resets the request to 0
 *	  X_AIS.PonPassword     pon.xpon_auth.sn_ascii_password + pon restart
 *	  X_AIS.PonPasswordState, X_AIS_GPON.Tx/RxPower  /userfs/bin/ponmgr
 *	  TemperatureStatus     thermal_zone0/temp, milli degrees
 *
 *	The Inform DeviceId getters at the bottom read the same options, so the
 *	Inform and a GetParameterValues can never disagree.
 *
 *	Bring-up override, same as the BDK build: a non-empty cwmp.cpe.<option>
 *	(manufacturer, oui, product_class, serial_number, software_version) wins.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <unistd.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "deviceinfo.h"

#define PONMGR	"/userfs/bin/ponmgr"

/* ------------------------------------------------------------------ */
/* identity (also the DeviceId of the Inform)                          */
/* ------------------------------------------------------------------ */

static char *devinfo_get(char *opt)
{
	char *v = NULL;

	dmuci_get_option_value_string("cwmp", "cpe", opt, &v);
	if (v && v[0])
		return v;
	v = NULL;
	dmuci_get_option_value_string("easycwmp", "@device[0]", opt, &v);
	return v ? v : "";
}

char *get_deviceid_manufacturer()
{
	return devinfo_get("manufacturer");
}

char *get_deviceid_manufactureroui()
{
	return devinfo_get("oui");
}

char *get_deviceid_productclass()
{
	return devinfo_get("product_class");
}

char *get_deviceid_serialnumber()
{
	return devinfo_get("serial_number");
}

char *get_softwareversion()
{
	return devinfo_get("software_version");
}

/* Download "3 Vendor Configuration File" with an instance: name of the
 * vendor config file; the product keeps a single one (icwmp.sh
 * apply_download restores whatever was downloaded), so no name */
int lookup_vcf_name(char *instance, char **value)
{
	(void)instance;
	*value = dmstrdup("");
	return 0;
}

#define DEVINFO_RO(fn, opt)							\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	*value = devinfo_get(opt);						\
	return 0;								\
}

DEVINFO_RO(get_manufacturer, "manufacturer")
DEVINFO_RO(get_oui, "oui")
DEVINFO_RO(get_modelname, "modelname")
DEVINFO_RO(get_description, "description")
DEVINFO_RO(get_productclass, "product_class")
DEVINFO_RO(get_serialnumber, "serial_number")
DEVINFO_RO(get_hardwareversion, "hardware_version")
DEVINFO_RO(get_softwareversion_param, "software_version")

static int get_provisioningcode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("easycwmp", "@local[0]", "provisioning_code");
	return 0;
}

static int set_provisioningcode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	switch (action) {
	case VALUECHECK:
		if (value && strlen(value) > 64)
			return FAULT_9007;
		return 0;
	case VALUESET:
		dmuci_set_value("easycwmp", "@local[0]", "provisioning_code", value);
		return 0;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* runtime counters                                                    */
/* ------------------------------------------------------------------ */

static int get_uptime(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[24];

	snprintf(buf, sizeof(buf), "%ld", mtk_uptime());
	*value = dmstrdup(buf);
	return 0;
}

static int get_devicelog(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	/* the old client declared DeviceLog with an empty getter: the ACS gets
	 * an empty string, not the 500 lines of logread.  Kept identical on
	 * purpose, a GPV of DeviceInfo. must stay small */
	*value = "";
	return 0;
}

static int get_cpu_usage(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_file_line("/tmp/cpu_avg_usage");

	*value = (v && v[0]) ? v : "0";
	return 0;
}

static int get_mem_total(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[24];
	long kb = mtk_meminfo_kb("MemTotal");

	snprintf(buf, sizeof(buf), "%ld", kb < 0 ? 0 : kb);
	*value = dmstrdup(buf);
	return 0;
}

static int get_mem_free(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[24];
	long kb = mtk_meminfo_kb("MemFree");

	snprintf(buf, sizeof(buf), "%ld", kb < 0 ? 0 : kb);
	*value = dmstrdup(buf);
	return 0;
}

static int get_temperature(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *raw = mtk_file_line("/sys/devices/virtual/thermal/thermal_zone0/temp");
	char buf[24];
	char *end = NULL;
	long milli;

	*value = "0";
	if (!raw || !raw[0])
		return 0;
	milli = strtol(raw, &end, 10);
	if (end == raw || (end && *end))
		return 0;
	snprintf(buf, sizeof(buf), "%ld", milli / 1000);
	*value = dmstrdup(buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* X_AIS_CpuUsed / X_AIS_MemUsed: ACS writes 1, CPE answers in Response */
/* ------------------------------------------------------------------ */

static int get_varstate_num(char *option, char **value)
{
	char *v = mtk_varstate("easycwmp", "@local[0]", option);

	*value = (v && v[0]) ? v : "0";
	return 0;
}

static int get_cpu_used(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_varstate_num("X_AIS_CpuUsed", value);
}

static int get_cpu_used_response(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_varstate_num("X_AIS_CpuUsedResponse", value);
}

static int get_mem_used(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_varstate_num("X_AIS_MemUsed", value);
}

static int get_mem_used_response(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_varstate_num("X_AIS_MemUsedResponse", value);
}

/* the request value is 0..3 like the old client, anything else is 9007 */
static int check_request_value(char *value)
{
	if (!value || !value[0] || value[1])
		return FAULT_9007;
	if (value[0] < '0' || value[0] > '3')
		return FAULT_9007;
	return 0;
}

/* busy percentage over one second from /proc/stat, the C equivalent of
 * "mpstat -P ALL 1 1 ... 100 - idle" (mpstat is not always installed) */
static int cpu_busy_percent(void)
{
	FILE *f;
	char line[256];
	unsigned long long a[8], b[8];
	unsigned long long tot_a = 0, tot_b = 0, idle_a, idle_b, dt, di;
	int i, n;

	f = fopen("/proc/stat", "r");
	if (!f)
		return 0;
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	memset(a, 0, sizeof(a));
	n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		   &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]);
	if (n < 4)
		return 0;

	sleep(1);

	f = fopen("/proc/stat", "r");
	if (!f)
		return 0;
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	memset(b, 0, sizeof(b));
	n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
	if (n < 4)
		return 0;

	for (i = 0; i < 8; i++) {
		tot_a += a[i];
		tot_b += b[i];
	}
	idle_a = a[3] + a[4];		/* idle + iowait */
	idle_b = b[3] + b[4];
	if (tot_b <= tot_a)
		return 0;
	dt = tot_b - tot_a;
	di = idle_b > idle_a ? idle_b - idle_a : 0;
	if (di > dt)
		return 0;
	return (int)(((dt - di) * 100 + dt / 2) / dt);
}

/* MemUsed(%) = (MemTotal - MemFree - Buffers - Cached) * 100 / MemTotal */
static int mem_used_percent(void)
{
	long total = mtk_meminfo_kb("MemTotal");
	long freem = mtk_meminfo_kb("MemFree");
	long buff = mtk_meminfo_kb("Buffers");
	long cach = mtk_meminfo_kb("Cached");

	if (total <= 0)
		return 0;
	if (freem < 0) freem = 0;
	if (buff < 0) buff = 0;
	if (cach < 0) cach = 0;
	if (total <= freem + buff + cach)
		return 0;
	return (int)(((total - freem - buff - cach) * 100) / total);
}

static int set_usage_request(char *option, char *response_option, char *value,
			     int action, int (*measure)(void))
{
	char buf[16];

	switch (action) {
	case VALUECHECK:
		return check_request_value(value);
	case VALUESET:
		mtk_varstate_set("easycwmp", "@local[0]", option, value);
		if (strcmp(value, "1") != 0)
			return 0;
		snprintf(buf, sizeof(buf), "%d", measure());
		mtk_varstate_set("easycwmp", "@local[0]", response_option, buf);
		mtk_varstate_set("easycwmp", "@local[0]", option, "0");
		return 0;
	}
	return 0;
}

static int set_cpu_used(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_usage_request("X_AIS_CpuUsed", "X_AIS_CpuUsedResponse",
				 value, action, cpu_busy_percent);
}

static int set_mem_used(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_usage_request("X_AIS_MemUsed", "X_AIS_MemUsedResponse",
				 value, action, mem_used_percent);
}

/* ------------------------------------------------------------------ */
/* PON / GPON                                                          */
/* ------------------------------------------------------------------ */

/* "<key> is <value>" row of "ponmgr gpon get <what>", "" when absent */
static char *ponmgr_field(const char *what, const char *key)
{
	char *argv[] = { PONMGR, "gpon", "get", (char *)what, NULL };
	char *out, *p;
	size_t kl = strlen(key);

	if (!check_file(PONMGR))
		return "";
	out = mtk_exec(argv);
	for (p = out; p && *p; ) {
		char *eol = strchr(p, '\n');

		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, key, kl) == 0) {
			char *v = p + kl;

			while (*v == ' ' || *v == '\t')
				v++;
			if (strncmp(v, "is", 2) == 0)
				v += 2;
			while (*v == ' ' || *v == '\t')
				v++;
			if (eol)
				*eol = '\0';
			v[strcspn(v, "\r")] = '\0';
			return dmstrdup(v);
		}
		if (!eol)
			break;
		p = eol + 1;
	}
	return "";
}

/* "ONU State: O5" of "ponmgr gpon get info" */
static char *ponmgr_onu_state(void)
{
	char *argv[] = { PONMGR, "gpon", "get", "info", NULL };
	char *out, *p;

	if (!check_file(PONMGR))
		return "";
	out = mtk_exec(argv);
	p = out ? strstr(out, "ONU State:") : NULL;
	if (!p)
		return "";
	p += strlen("ONU State:");
	while (*p == ' ' || *p == '\t')
		p++;
	p[strcspn(p, "\r\n")] = '\0';
	return p;
}

static int get_pon_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci("pon", "xpon_auth", "sn_ascii_password");
	return 0;
}

static int set_pon_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	switch (action) {
	case VALUECHECK:
		if (value && strlen(value) > 64)
			return FAULT_9007;
		return 0;
	case VALUESET:
		dmuci_set_value("pon", "xpon_auth", "sn_ascii_password", value);
		if (check_file("/etc/init.d/pon"))
			mtk_apply_service("/etc/init.d/pon restart");
		else if (check_file("/etc/init.d/gpon"))
			mtk_apply_service("/etc/init.d/gpon restart");
		return 0;
	}
	return 0;
}

static int get_pon_password_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *state = ponmgr_onu_state();

	*value = (state && strcmp(state, "O5") == 0) ? "0" : "2";
	return 0;
}

/* raw is 0.1 uW: dBm = 10 * log10(raw / 10000) */
static int gpon_power(const char *key, char **value)
{
	char *raw = ponmgr_field("phyTransParams", key);
	char buf[32];
	char *end = NULL;
	long v;
	double mw;

	*value = "";
	if (!raw || !raw[0])
		return 0;
	v = strtol(raw, &end, 10);
	if (end == raw || v <= 0)
		return 0;
	mw = (double)v / 10000.0;
	snprintf(buf, sizeof(buf), "%.1fdBm", 10.0 * log10(mw));
	*value = dmstrdup(buf);
	return 0;
}

static int get_gpon_txpower(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gpon_power("txPower", value);
}

static int get_gpon_rxpower(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gpon_power("rxPower", value);
}

/* the product ships these three as constants, kept as constants */
static int get_dsl_snr(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0dBm";
	return 0;
}

static int get_dsl_atten(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0dBm";
	return 0;
}

static int get_dsl_stream(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0kbps";
	return 0;
}

/* ------------------------------------------------------------------ */
/* tree                                                                */
/* ------------------------------------------------------------------ */

static DMLEAF tProcessStatusParam[] = {
{"CPUUsage", &DMREAD, DMT_UNINT, get_cpu_usage, NULL, NULL, NULL},
{0}
};

static DMOBJ tProcessStatusObj[] = {
/* the old client advertised the object with no instances, keep it visible */
{"Process", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
{0}
};

static DMLEAF tMemoryStatusParam[] = {
{"Total", &DMREAD, DMT_UNINT, get_mem_total, NULL, NULL, NULL},
{"Free", &DMREAD, DMT_UNINT, get_mem_free, NULL, NULL, NULL},
{0}
};

static DMLEAF tXAisParam[] = {
{"PonPassword", &DMWRITE, DMT_STRING, get_pon_password, set_pon_password, &DMFINFRM, NULL},
{"PonPasswordState", &DMREAD, DMT_UNINT, get_pon_password_state, NULL, &DMFINFRM, NULL},
{0}
};

static DMLEAF tXAisDslParam[] = {
{"SNR", &DMREAD, DMT_STRING, get_dsl_snr, NULL, NULL, NULL},
{"Attenuation", &DMREAD, DMT_STRING, get_dsl_atten, NULL, NULL, NULL},
{"UpDownStream", &DMREAD, DMT_STRING, get_dsl_stream, NULL, NULL, NULL},
{0}
};

static DMLEAF tXAisGponParam[] = {
{"TxPower", &DMREAD, DMT_STRING, get_gpon_txpower, NULL, &DMFINFRM, NULL},
{"RxPower", &DMREAD, DMT_STRING, get_gpon_rxpower, NULL, &DMFINFRM, NULL},
{0}
};

static DMLEAF tTempSensorParam[] = {
{"Value", &DMREAD, DMT_INT, get_temperature, NULL, NULL, NULL},
{0}
};

/* the product exposes exactly one sensor, at instance 1 */
static DMOBJ tTempSensorObj[] = {
{"1", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tTempSensorParam, NULL},
{0}
};

static DMOBJ tTemperatureStatusObj[] = {
{"TemperatureSensor", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tTempSensorObj, NULL, NULL},
{0}
};

static DMOBJ tDeviceInfoMtkObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"ProcessStatus", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tProcessStatusObj, tProcessStatusParam, NULL},
{"MemoryStatus", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tMemoryStatusParam, NULL},
{"TemperatureStatus", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tTemperatureStatusObj, NULL, NULL},
{"X_AIS", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, NULL, NULL, tXAisParam, NULL},
{"X_AIS_DSL", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tXAisDslParam, NULL},
{"X_AIS_GPON", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, NULL, NULL, tXAisGponParam, NULL},
{0}
};

static DMLEAF tDeviceInfoMtkParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Manufacturer", &DMREAD, DMT_STRING, get_manufacturer, NULL, &DMFINFRM, NULL},
{"ManufacturerOUI", &DMREAD, DMT_STRING, get_oui, NULL, &DMFINFRM, NULL},
{"ModelName", &DMREAD, DMT_STRING, get_modelname, NULL, &DMFINFRM, NULL},
{"Description", &DMREAD, DMT_STRING, get_description, NULL, &DMFINFRM, NULL},
{"ProductClass", &DMREAD, DMT_STRING, get_productclass, NULL, &DMFINFRM, NULL},
{"SerialNumber", &DMREAD, DMT_STRING, get_serialnumber, NULL, &DMFINFRM, NULL},
{"HardwareVersion", &DMREAD, DMT_STRING, get_hardwareversion, NULL, &DMFINFRM, NULL},
{"SoftwareVersion", &DMREAD, DMT_STRING, get_softwareversion_param, NULL, &DMFINFRM, NULL},
{"ProvisioningCode", &DMWRITE, DMT_STRING, get_provisioningcode, set_provisioningcode, &DMFINFRM, NULL},
{"UpTime", &DMREAD, DMT_UNINT, get_uptime, NULL, NULL, NULL},
{"DeviceLog", &DMREAD, DMT_STRING, get_devicelog, NULL, NULL, NULL},
{"X_AIS_CpuUsedResponse", &DMREAD, DMT_UNINT, get_cpu_used_response, NULL, &DMFINFRM, NULL},
{"X_AIS_CpuUsed", &DMWRITE, DMT_UNINT, get_cpu_used, set_cpu_used, &DMFINFRM, NULL},
{"X_AIS_MemUsedResponse", &DMREAD, DMT_UNINT, get_mem_used_response, NULL, &DMFINFRM, NULL},
{"X_AIS_MemUsed", &DMWRITE, DMT_UNINT, get_mem_used, set_mem_used, &DMFINFRM, NULL},
{"X_AIS_reuseCPE_cycles", &DMREAD, DMT_STRING, get_empty, NULL, &DMFINFRM, NULL},
{"X_AIS_reuseCPE_status", &DMREAD, DMT_STRING, get_empty, NULL, &DMFINFRM, NULL},
{0}
};

static DMOBJ tDeviceInfoMtkRoot[] = {
{"DeviceInfo", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, NULL, tDeviceInfoMtkObj, tDeviceInfoMtkParam, NULL},
{0}
};

static const char *const deviceinfo_mtk_paths[] = {
	"InternetGatewayDevice.DeviceInfo.",
	NULL
};

static const struct dm_module deviceinfo_mtk_module = {
	.name  = "mtk-deviceinfo",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tDeviceInfoMtkRoot,
	.paths = deviceinfo_mtk_paths,
};
DM_MODULE_REGISTER(deviceinfo_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): Device.DeviceInfo., the same getters
 * and sub-objects (type A of docs/plan/tr181_mtk_design.md) without
 * DeviceLog, which TR-181 does not have (it lists VendorLogFile.{i}) */
static DMLEAF tDeviceInfo181Param[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Manufacturer", &DMREAD, DMT_STRING, get_manufacturer, NULL, &DMFINFRM, NULL},
{"ManufacturerOUI", &DMREAD, DMT_STRING, get_oui, NULL, &DMFINFRM, NULL},
{"ModelName", &DMREAD, DMT_STRING, get_modelname, NULL, &DMFINFRM, NULL},
{"Description", &DMREAD, DMT_STRING, get_description, NULL, &DMFINFRM, NULL},
{"ProductClass", &DMREAD, DMT_STRING, get_productclass, NULL, &DMFINFRM, NULL},
{"SerialNumber", &DMREAD, DMT_STRING, get_serialnumber, NULL, &DMFINFRM, NULL},
{"HardwareVersion", &DMREAD, DMT_STRING, get_hardwareversion, NULL, &DMFINFRM, NULL},
{"SoftwareVersion", &DMREAD, DMT_STRING, get_softwareversion_param, NULL, &DMFINFRM, NULL},
{"ProvisioningCode", &DMWRITE, DMT_STRING, get_provisioningcode, set_provisioningcode, &DMFINFRM, NULL},
{"UpTime", &DMREAD, DMT_UNINT, get_uptime, NULL, NULL, NULL},
{"X_AIS_CpuUsedResponse", &DMREAD, DMT_UNINT, get_cpu_used_response, NULL, &DMFINFRM, NULL},
{"X_AIS_CpuUsed", &DMWRITE, DMT_UNINT, get_cpu_used, set_cpu_used, &DMFINFRM, NULL},
{"X_AIS_MemUsedResponse", &DMREAD, DMT_UNINT, get_mem_used_response, NULL, &DMFINFRM, NULL},
{"X_AIS_MemUsed", &DMWRITE, DMT_UNINT, get_mem_used, set_mem_used, &DMFINFRM, NULL},
{"X_AIS_reuseCPE_cycles", &DMREAD, DMT_STRING, get_empty, NULL, &DMFINFRM, NULL},
{"X_AIS_reuseCPE_status", &DMREAD, DMT_STRING, get_empty, NULL, &DMFINFRM, NULL},
{0}
};

static DMOBJ tDeviceInfo181Root[] = {
{"DeviceInfo", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, NULL, tDeviceInfoMtkObj, tDeviceInfo181Param, NULL},
{0}
};

static const char *const deviceinfo181_mtk_paths[] = {
	"Device.DeviceInfo.",
	NULL
};

static const struct dm_module deviceinfo181_mtk_module = {
	.name  = "mtk-deviceinfo-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tDeviceInfo181Root,
	.paths = deviceinfo181_mtk_paths,
};
DM_MODULE_REGISTER(deviceinfo181_mtk_module);
