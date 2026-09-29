/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	X_MARUSYS_COM_MloCfg. — Wi-Fi 7 Multi-Link Operation (MLO) access point
 *	configuration, implemented inside icwmpd/libtr098 for Broadcom BDK.
 *
 *	The SDK data model has no MLO parameter.  On this firmware MLO is set up
 *	by the Marusys WebUI page marusys_mlo.asp (router/www/broadcom/cgi/
 *	cgi_marusys_mlo.c): the MLO AP is one secondary BSS per radio (wl<u>.1)
 *	sharing an SSID and a passphrase, the link set is written to the kernel
 *	NVRAM wl_mlo_config (read by the wl driver at module init only: reboot to
 *	apply) and the page keeps its own state in the NVRAM variables wl_mlo_*.
 *
 *	This file gives the ACS the same control through one object and keeps
 *	the WebUI in sync by using the same wl_mlo_* variables (nvram CLI) and
 *	the same per-link TR-181 parameters (Device.WiFi.SSID.{i} /
 *	AccessPoint.{i}.Security through the generic HAL, so the SDK RCLs apply
 *	them and restart Wi-Fi as for any other SPV).  Nothing about the board is
 *	hard coded: radios, their wl unit, band and link BSS come from the MDM
 *	at run time; the only fixed numbers are the wl driver contract for
 *	wl_mlo_config (4 slots, at most 3 links, mlo_ipc.c).
 *
 *	Leaves (same table under InternetGatewayDevice. and Device.WiFi.):
 *	  Enable            RW  MLO AP on/off           nvram wl_mlo_bss_enabled
 *	  LinkRadios        RW  "1,2,3": Device.WiFi.Radio instance numbers of
 *	                        the links, first = main AP   wl_mlo_selected_config
 *	  LinkBssIndex      RW  which BSS of each radio is the link (default 1
 *	                        = wl<u>.1 like the WebUI)   wl_mlo_bss_index
 *	  LinkInterfaces    RO  "wl0.1,wl1.1,wl2.1"
 *	  SelectedConfig    RO  wl_mlo_config string derived from LinkRadios
 *	  RuntimeConfig     RO  kernel NVRAM wl_mlo_config in effect
 *	  Status            RO  Disabled | Enabled | RebootRequired
 *	  SSID              RW                          wl_mlo_ssid
 *	  SecurityMode      RW  wpa | owe               wl_mlo_security_mode
 *	  KeyPassphrase     RW  write only              wl_mlo_wpa_psk
 *	  Description       RW                          wl_mlo_description
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "cms.h"
#include "cms_log.h"
#include "cms_mem.h"
#include "bcm_generic_hal.h"

#include "dmtr098.h"
#include "dmmem.h"
#include "dmcommon.h"
#include "dmbdk.h"
#include "mlo_bdk.h"

/* NVRAM variables shared with cgi_marusys_mlo.c */
#define NV_MLO_ENABLE    "wl_mlo_bss_enabled"
#define NV_MLO_IFACE     "wl_mlo_interface"        /* WebUI selector 0..3 */
#define NV_MLO_SELECTED  "wl_mlo_selected_config"
#define NV_MLO_SSID      "wl_mlo_ssid"
#define NV_MLO_SECMODE   "wl_mlo_security_mode"
#define NV_MLO_PSK       "wl_mlo_wpa_psk"
#define NV_MLO_DESC      "wl_mlo_description"
/* ours */
#define NV_MLO_BSSIDX    "wl_mlo_bss_index"
/* kernel NVRAM, wl driver (mlo_ipc.c) */
#define KNV_MLO_CONFIG   "wl_mlo_config"

/* wl driver contract for wl_mlo_config (mlo_ipc.c:81-129): exactly
 * MLO_CFG_SLOTS values indexed by wl unit, 0 = main AP, 1/2 = auxiliary AP,
 * -1 = not in the MLD; at most MLO_MAX_LINKS radios */
#define MLO_CFG_SLOTS      4
#define MLO_MAX_LINKS      3
#define MLO_DISABLED_CFG   "-1 -1 -1 -1"
#define MLO_MAX_RADIO      8

#define MLO_SSID_OBJ   "Device.WiFi.SSID."
#define MLO_AP_OBJ     "Device.WiFi.AccessPoint."
#define MLO_RADIO_OBJ  "Device.WiFi.Radio."

struct mlo_radio {
	unsigned int inst;        /* Device.WiFi.Radio.{inst} */
	int unit;                 /* wl<unit>, from Radio.{inst}.Name */
	char band[16];            /* OperatingFrequencyBand */
	char linkname[16];        /* wl<unit>.<bssidx> */
	unsigned int ssid_inst;   /* Device.WiFi.SSID.{i} of the link BSS, 0 = none */
	unsigned int ap_inst;     /* Device.WiFi.AccessPoint.{i} of that BSS, 0 = none */
	int link_enabled;         /* SSID.{i}.Enable */
};

struct mlo_topo {
	unsigned int gen;         /* bdk_ctx_generation() it was built for */
	int valid;
	int n;
	unsigned int bssidx;
	struct mlo_radio r[MLO_MAX_RADIO];
};

static struct mlo_topo topo;

/* ------------------------------------------------------------------------ */
/* nvram CLI (unfnvram): user NVRAM lives in the MDM through wlmdm, the      */
/* kernel NVRAM is reached with kget/kset; libnvram itself links the wifi     */
/* callbacks and cannot be linked into a tr69 component process             */
/* ------------------------------------------------------------------------ */

static int nv_run(const char *cmd, char *out, size_t outlen)
{
	FILE *fp;
	size_t len;

	if (out && outlen)
		out[0] = '\0';
	fp = popen(cmd, "r");
	if (!fp) {
		cmsLog_error("popen(%s) failed", cmd);
		return -1;
	}
	if (out && outlen && fgets(out, outlen, fp) == NULL)
		out[0] = '\0';
	pclose(fp);
	if (out && outlen) {
		len = strlen(out);
		while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
			out[--len] = '\0';
	}
	return 0;
}

/* value of a user (get) or kernel (kget) NVRAM variable, dm-allocated, "" if unset */
static char *nv_get_kind(const char *kind, const char *name)
{
	char cmd[128], out[512];

	snprintf(cmd, sizeof(cmd), "nvram %s %s 2>/dev/null", kind, name);
	nv_run(cmd, out, sizeof(out));
	return dmstrdup(out);
}

#define nv_get(name)   nv_get_kind("get", name)
#define nv_kget(name)  nv_get_kind("kget", name)

/* single-quote a value for the shell */
static void nv_quote(const char *in, char *out, size_t outlen)
{
	size_t o = 0;

	if (outlen < 3)
		return;
	out[o++] = '\'';
	for (; *in && o + 5 < outlen; in++) {
		if (*in == '\'') {
			memcpy(out + o, "'\\''", 4);
			o += 4;
		} else {
			out[o++] = *in;
		}
	}
	out[o++] = '\'';
	out[o] = '\0';
}

static int nv_set_kind(const char *kind, const char *name, const char *value)
{
	char q[320], cmd[400];

	nv_quote(value ? value : "", q, sizeof(q));
	snprintf(cmd, sizeof(cmd), "nvram %s %s=%s", kind, name, q);
	cmsLog_notice("%s", cmd);
	return nv_run(cmd, NULL, 0);
}

#define nv_set(name, value)   nv_set_kind("set", name, value)
#define nv_kset(name, value)  nv_set_kind("kset", name, value)
#define nv_commit()           nv_run("nvram commit", NULL, 0)
#define nv_kcommit()          nv_run("nvram kcommit", NULL, 0)

/* ------------------------------------------------------------------------ */
/* topology from the MDM: radios, their wl unit and band, the link BSS       */
/* ------------------------------------------------------------------------ */

/* "Device.WiFi.SSID.12.Name" -> inst 12, leaf "Name"; 0 when not "<obj><n>.<leaf>" */
static unsigned int mlo_split(const char *fullpath, const char *obj, const char **leaf)
{
	size_t ol = strlen(obj);
	char *end;
	unsigned long n;

	if (strncmp(fullpath, obj, ol) != 0)
		return 0;
	n = strtoul(fullpath + ol, &end, 10);
	if (!n || *end != '.')
		return 0;
	*leaf = end + 1;
	return (unsigned int)n;
}

static struct mlo_radio *mlo_radio_by_inst(unsigned int inst)
{
	int i;

	for (i = 0; i < topo.n; i++)
		if (topo.r[i].inst == inst)
			return &topo.r[i];
	return NULL;
}

static struct mlo_radio *mlo_radio_by_unit(int unit)
{
	int i;

	for (i = 0; i < topo.n; i++)
		if (topo.r[i].unit == unit)
			return &topo.r[i];
	return NULL;
}

static int mlo_build_topo(void)
{
	unsigned int *inst = NULL, ninst = 0, i;
	BcmGenericParamInfo *arr = NULL;
	UINT32 n = 0, k;
	char path[96], *v;

	if (topo.valid && topo.gen == bdk_ctx_generation())
		return 0;
	memset(&topo, 0, sizeof(topo));
	topo.gen = bdk_ctx_generation();

	v = nv_get(NV_MLO_BSSIDX);
	topo.bssidx = (v[0] && isdigit((unsigned char)v[0])) ? (unsigned int)atoi(v) : 1;

	if (bdk_get_instances(MLO_RADIO_OBJ, &inst, &ninst) != 0)
		return FAULT_9002;
	for (i = 0; i < ninst && topo.n < MLO_MAX_RADIO; i++) {
		struct mlo_radio *r = &topo.r[topo.n];
		int unit;

		snprintf(path, sizeof(path), MLO_RADIO_OBJ "%u.Name", inst[i]);
		bdk_get_value_default(path, "", &v);
		if (sscanf(v, "wl%d", &unit) != 1)
			continue;               /* not a wl radio, skip */
		r->inst = inst[i];
		r->unit = unit;
		snprintf(path, sizeof(path), MLO_RADIO_OBJ "%u.OperatingFrequencyBand", inst[i]);
		bdk_get_value_default(path, "", &v);
		snprintf(r->band, sizeof(r->band), "%s", v);
		if (topo.bssidx == 0)
			snprintf(r->linkname, sizeof(r->linkname), "wl%d", unit);
		else
			snprintf(r->linkname, sizeof(r->linkname), "wl%d.%u", unit, topo.bssidx);
		topo.n++;
	}
	if (!topo.n) {
		cmsLog_error("no Device.WiFi.Radio instance named wl<n>");
		return FAULT_9002;
	}

	/* link BSS of every radio: the SSID instance whose Name is wl<u>.<bssidx> */
	if (bdk_get_subtree(MLO_SSID_OBJ, &arr, &n) == 0) {
		for (k = 0; k < n; k++) {
			const char *leaf;
			unsigned int si = mlo_split(arr[k].fullpath, MLO_SSID_OBJ, &leaf);
			struct mlo_radio *r;

			if (!si || strcmp(leaf, "Name") != 0 || !arr[k].value)
				continue;
			for (i = 0; i < (unsigned int)topo.n; i++) {
				r = &topo.r[i];
				if (strcmp(arr[k].value, r->linkname) == 0)
					r->ssid_inst = si;
			}
		}
		/* second pass: Enable of the link BSS */
		for (k = 0; k < n; k++) {
			const char *leaf;
			unsigned int si = mlo_split(arr[k].fullpath, MLO_SSID_OBJ, &leaf);

			if (!si || strcmp(leaf, "Enable") != 0)
				continue;
			for (i = 0; i < (unsigned int)topo.n; i++) {
				if (topo.r[i].ssid_inst == si)
					topo.r[i].link_enabled = (arr[k].value && (arr[k].value[0] == '1' ||
					                          strcasecmp(arr[k].value, "true") == 0));
			}
		}
		bcm_generic_freeParamInfoArray(&arr, n);
	}

	/* AccessPoint of the link BSS: SSIDReference == Device.WiFi.SSID.<i> */
	if (bdk_get_subtree(MLO_AP_OBJ, &arr, &n) == 0) {
		for (k = 0; k < n; k++) {
			const char *leaf;
			unsigned int ai = mlo_split(arr[k].fullpath, MLO_AP_OBJ, &leaf);
			unsigned int ref = 0;

			if (!ai || strcmp(leaf, "SSIDReference") != 0 || !arr[k].value)
				continue;
			if (sscanf(arr[k].value, "Device.WiFi.SSID.%u", &ref) != 1)
				continue;
			for (i = 0; i < (unsigned int)topo.n; i++) {
				if (topo.r[i].ssid_inst && topo.r[i].ssid_inst == ref)
					topo.r[i].ap_inst = ai;
			}
		}
		bcm_generic_freeParamInfoArray(&arr, n);
	}
	topo.valid = 1;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* wl_mlo_config <-> radio list                                              */
/* ------------------------------------------------------------------------ */

static int mlo_parse_cfg(const char *cfg, int slot[MLO_CFG_SLOTS])
{
	int i;

	for (i = 0; i < MLO_CFG_SLOTS; i++)
		slot[i] = -1;
	if (!cfg || !cfg[0])
		return -1;
	if (sscanf(cfg, "%d %d %d %d", &slot[0], &slot[1], &slot[2], &slot[3]) != MLO_CFG_SLOTS)
		return -1;
	return 0;
}

static int mlo_cfg_is_disabled(const int slot[MLO_CFG_SLOTS])
{
	int i;

	for (i = 0; i < MLO_CFG_SLOTS; i++)
		if (slot[i] >= 0)
			return 0;
	return 1;
}

/* the config the WebUI/ACS selected, or what the kernel runs when none */
static char *mlo_selected_cfg(void)
{
	char *v = nv_get(NV_MLO_SELECTED);
	int slot[MLO_CFG_SLOTS];

	if (mlo_parse_cfg(v, slot) == 0)
		return v;
	v = nv_kget(KNV_MLO_CONFIG);
	if (mlo_parse_cfg(v, slot) == 0)
		return v;
	return "";
}

/* radio instance list "1,2,3" (main first) from a config string */
static char *mlo_cfg_to_radios(const char *cfg)
{
	int slot[MLO_CFG_SLOTS], role, u;
	char buf[64] = "";
	size_t o = 0;

	if (mlo_parse_cfg(cfg, slot) != 0)
		return "";
	for (role = 0; role < MLO_MAX_LINKS; role++) {
		for (u = 0; u < MLO_CFG_SLOTS; u++) {
			struct mlo_radio *r;

			if (slot[u] != role)
				continue;
			r = mlo_radio_by_unit(u);
			if (!r)
				continue;
			o += snprintf(buf + o, sizeof(buf) - o, "%s%u", o ? "," : "", r->inst);
		}
	}
	return dmstrdup(buf);
}

/* "1,2,3" -> radios[] (main first); 0 ok, else fault */
static int mlo_radios_from_list(const char *list, struct mlo_radio *radios[MLO_MAX_LINKS], int *count)
{
	char *dup = dmstrdup(list), *tok, *sp;
	int c = 0, i;

	for (tok = strtok_r(dup, ", ", &sp); tok; tok = strtok_r(NULL, ", ", &sp)) {
		struct mlo_radio *r;
		unsigned int inst;
		char *end;

		inst = (unsigned int)strtoul(tok, &end, 10);
		if (*end || !inst)
			return FAULT_9007;
		r = mlo_radio_by_inst(inst);
		if (!r || r->unit < 0 || r->unit >= MLO_CFG_SLOTS)
			return FAULT_9007;
		for (i = 0; i < c; i++)
			if (radios[i] == r)
				return FAULT_9007;      /* duplicate */
		if (c >= MLO_MAX_LINKS)
			return FAULT_9007;
		radios[c++] = r;
	}
	if (c < 2)
		return FAULT_9007;              /* an MLD needs at least two links */
	*count = c;
	return 0;
}

static char *mlo_radios_to_cfg(struct mlo_radio *radios[], int count)
{
	int slot[MLO_CFG_SLOTS], i;
	char buf[32];

	for (i = 0; i < MLO_CFG_SLOTS; i++)
		slot[i] = -1;
	for (i = 0; i < count; i++)
		slot[radios[i]->unit] = i;
	snprintf(buf, sizeof(buf), "%d %d %d %d", slot[0], slot[1], slot[2], slot[3]);
	return dmstrdup(buf);
}

/* The WebUI selector (wl_mlo_interface) only knows four presets, by band:
 * 0 = 6G+5G+2.4G, 1 = 6G+5G, 2 = 6G+2.4G, 3 = 5G+2.4G, main = first.  Keep it
 * in sync when the ACS choice is one of them, leave it alone otherwise. */
static const char *mlo_webui_preset(struct mlo_radio *radios[], int count)
{
	static const char *const presets[4][3] = {
		{"6GHz", "5GHz", "2.4GHz"}, {"6GHz", "5GHz", NULL}, {"6GHz", "2.4GHz", NULL}, {"5GHz", "2.4GHz", NULL}
	};
	static const char *const names[4] = {"0", "1", "2", "3"};
	int p, i;

	for (p = 0; p < 4; p++) {
		int want = presets[p][2] ? 3 : 2;

		if (count != want)
			continue;
		for (i = 0; i < count; i++)
			if (strcasecmp(radios[i]->band, presets[p][i]) != 0)
				break;
		if (i == count)
			return names[p];
	}
	return NULL;
}

/* ------------------------------------------------------------------------ */
/* apply helpers (VALUESET): queue the per-link TR-181 writes into the SPV   */
/* batch, write the shared NVRAM state, kernel NVRAM for the link set        */
/* ------------------------------------------------------------------------ */

static int mlo_queue_link(struct dmctx *ctx, const char *refparam, const struct mlo_radio *r,
                          const char *fmt, const char *value)
{
	char path[128];

	snprintf(path, sizeof(path), fmt, r->ap_inst ? r->ap_inst : r->ssid_inst);
	return bdk_queue_set(ctx, refparam, path, value);
}

/* enable the link BSS of the radios in cfg, disable the other link BSS */
static int mlo_apply_link_enable(struct dmctx *ctx, const char *refparam, const char *cfg, int enabled)
{
	int slot[MLO_CFG_SLOTS], i, fault;
	char path[128];

	if (mlo_parse_cfg(cfg, slot) != 0)
		return FAULT_9007;
	for (i = 0; i < topo.n; i++) {
		struct mlo_radio *r = &topo.r[i];
		int on = enabled && r->unit >= 0 && r->unit < MLO_CFG_SLOTS && slot[r->unit] >= 0;

		if (!r->ssid_inst)
			continue;
		snprintf(path, sizeof(path), MLO_SSID_OBJ "%u.Enable", r->ssid_inst);
		fault = bdk_queue_set(ctx, refparam, path, on ? "1" : "0");
		if (fault)
			return fault;
	}
	return 0;
}

static void mlo_set_kernel_cfg(const char *cfg)
{
	nv_kset(KNV_MLO_CONFIG, cfg);
	nv_kcommit();
	cmsLog_notice("wl_mlo_config=\"%s\": takes effect after reboot", cfg);
}

static int mlo_enabled(void)
{
	char *v = nv_get(NV_MLO_ENABLE);

	return (v[0] == '1' || strcasecmp(v, "true") == 0);
}

/* ------------------------------------------------------------------------ */
/* leaves                                                                    */
/* ------------------------------------------------------------------------ */

static int get_mlo_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = mlo_enabled() ? "1" : "0";
	return 0;
}

static int set_mlo_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	bool b;
	int fault;

	(void)data; (void)instance;
	switch (action) {
	case VALUECHECK:
		if (string_to_bool(value, &b))
			return FAULT_9007;
		return mlo_build_topo();
	case VALUESET:
		string_to_bool(value, &b);
		if ((fault = mlo_build_topo()))
			return fault;
		if (b) {
			char *cfg = mlo_selected_cfg();
			int slot[MLO_CFG_SLOTS];

			if (mlo_parse_cfg(cfg, slot) != 0 || mlo_cfg_is_disabled(slot)) {
				cmsLog_error("Enable=true without LinkRadios/SelectedConfig");
				return FAULT_9007;
			}
			mlo_set_kernel_cfg(cfg);
			fault = mlo_apply_link_enable(ctx, refparam, cfg, 1);
		} else {
			mlo_set_kernel_cfg(MLO_DISABLED_CFG);
			fault = mlo_apply_link_enable(ctx, refparam, MLO_DISABLED_CFG, 0);
		}
		if (fault)
			return fault;
		nv_set(NV_MLO_ENABLE, b ? "1" : "0");
		nv_commit();
		return 0;
	}
	return 0;
}

static int get_mlo_link_radios(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int fault;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	if ((fault = mlo_build_topo()))
		return fault;
	*value = mlo_cfg_to_radios(mlo_selected_cfg());
	return 0;
}

static int set_mlo_link_radios(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct mlo_radio *radios[MLO_MAX_LINKS];
	int count = 0, fault;
	char *cfg;
	const char *preset;

	(void)data; (void)instance;
	if ((fault = mlo_build_topo()))
		return fault;
	if ((fault = mlo_radios_from_list(value, radios, &count)))
		return fault;
	if (action == VALUECHECK)
		return 0;

	cfg = mlo_radios_to_cfg(radios, count);
	nv_set(NV_MLO_SELECTED, cfg);
	preset = mlo_webui_preset(radios, count);
	if (preset)
		nv_set(NV_MLO_IFACE, preset);
	/* like validate_wl_mlo_interface(): applied at once when the MLD is on */
	if (mlo_enabled()) {
		mlo_set_kernel_cfg(cfg);
		if ((fault = mlo_apply_link_enable(ctx, refparam, cfg, 1)))
			return fault;
	}
	nv_commit();
	return 0;
}

static int get_mlo_link_bss_index(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int fault;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	if ((fault = mlo_build_topo()))
		return fault;
	dmasprintf(value, "%u", topo.bssidx);
	return 0;
}

static int set_mlo_link_bss_index(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *end;
	unsigned long v;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	v = strtoul(value, &end, 10);
	if (*end || v > 15)
		return FAULT_9007;
	if (action == VALUESET) {
		nv_set(NV_MLO_BSSIDX, value);
		nv_commit();
		topo.valid = 0;
	}
	return 0;
}

static int get_mlo_link_interfaces(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct mlo_radio *radios[MLO_MAX_LINKS];
	int count = 0, i, fault;
	char buf[128] = "";
	size_t o = 0;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = "";
	if ((fault = mlo_build_topo()))
		return fault;
	if (mlo_radios_from_list(mlo_cfg_to_radios(mlo_selected_cfg()), radios, &count))
		return 0;
	for (i = 0; i < count; i++)
		o += snprintf(buf + o, sizeof(buf) - o, "%s%s", o ? "," : "", radios[i]->linkname);
	*value = dmstrdup(buf);
	return 0;
}

static int get_mlo_selected_config(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = nv_get(NV_MLO_SELECTED);
	return 0;
}

static int get_mlo_runtime_config(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = nv_kget(KNV_MLO_CONFIG);
	return 0;
}

static int get_mlo_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int want[MLO_CFG_SLOTS], run[MLO_CFG_SLOTS], i, same = 1;
	char *runtime = nv_kget(KNV_MLO_CONFIG);

	(void)refparam; (void)ctx; (void)data; (void)instance;
	mlo_parse_cfg(runtime, run);
	if (mlo_enabled())
		mlo_parse_cfg(mlo_selected_cfg(), want);
	else
		mlo_parse_cfg(MLO_DISABLED_CFG, want);
	for (i = 0; i < MLO_CFG_SLOTS; i++)
		if (want[i] != run[i])
			same = 0;
	if (!same)
		*value = "RebootRequired";
	else
		*value = mlo_cfg_is_disabled(run) ? "Disabled" : "Enabled";
	return 0;
}

static int get_mlo_ssid(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = nv_get(NV_MLO_SSID);
	return 0;
}

static int set_mlo_ssid(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	size_t len = strlen(value);
	int i, fault;

	(void)data; (void)instance;
	if (len < 1 || len > 32)
		return FAULT_9007;
	if ((fault = mlo_build_topo()))
		return fault;
	if (action == VALUECHECK)
		return 0;
	/* validate_mlo_ssid(): every link BSS gets the SSID */
	for (i = 0; i < topo.n; i++) {
		char path[128];

		if (!topo.r[i].ssid_inst)
			continue;
		snprintf(path, sizeof(path), MLO_SSID_OBJ "%u.SSID", topo.r[i].ssid_inst);
		if ((fault = bdk_queue_set(ctx, refparam, path, value)))
			return fault;
	}
	nv_set(NV_MLO_SSID, value);
	nv_commit();
	return 0;
}

static int get_mlo_security_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	v = nv_get(NV_MLO_SECMODE);
	*value = v[0] ? v : "wpa";
	return 0;
}

/* validate_wl_mlo_security_mode(): akm / crypto / mfp per link.  The WebUI
 * hard codes wl0 as the 6 GHz radio, here the band comes from the MDM. */
static int set_mlo_security_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int i, fault, owe;

	(void)data; (void)instance;
	if (strcasecmp(value, "wpa") != 0 && strcasecmp(value, "owe") != 0)
		return FAULT_9007;
	if ((fault = mlo_build_topo()))
		return fault;
	if (action == VALUECHECK)
		return 0;
	owe = (strcasecmp(value, "owe") == 0);
	for (i = 0; i < topo.n; i++) {
		struct mlo_radio *r = &topo.r[i];
		const char *akm, *mfp;

		if (!r->ap_inst)
			continue;
		if (owe) {
			akm = "owe";
			mfp = "2";
		} else if (strcasecmp(r->band, "6GHz") == 0) {
			akm = "sae";            /* 6 GHz: SAE only, MFP required */
			mfp = "2";
		} else {
			akm = "psk2 sae";       /* WPA2/WPA3 mixed, MFP capable */
			mfp = "1";
		}
		if ((fault = mlo_queue_link(ctx, refparam, r, MLO_AP_OBJ "%u.Security.WlAuthAkm", akm)) ||
		    (fault = mlo_queue_link(ctx, refparam, r, MLO_AP_OBJ "%u.Security.X_BROADCOM_COM_WlWpaEncryption", "aes")) ||
		    (fault = mlo_queue_link(ctx, refparam, r, MLO_AP_OBJ "%u.Security.X_BROADCOM_COM_WlMFP", mfp)))
			return fault;
	}
	nv_set(NV_MLO_SECMODE, owe ? "owe" : "wpa");
	nv_commit();
	return 0;
}

static int get_mlo_key_passphrase(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = "";                    /* write only, like KeyPassphrase elsewhere */
	return 0;
}

static int set_mlo_key_passphrase(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	size_t len = strlen(value), k;
	int i, fault;

	(void)data; (void)instance;
	if (len == 64) {
		for (k = 0; k < len; k++)
			if (!isxdigit((unsigned char)value[k]))
				return FAULT_9007;
	} else if (len < 8 || len > 63) {
		return FAULT_9007;
	}
	if ((fault = mlo_build_topo()))
		return fault;
	if (action == VALUECHECK)
		return 0;
	for (i = 0; i < topo.n; i++) {
		if (!topo.r[i].ap_inst)
			continue;
		if ((fault = mlo_queue_link(ctx, refparam, &topo.r[i], MLO_AP_OBJ "%u.Security.KeyPassphrase", value)))
			return fault;
	}
	nv_set(NV_MLO_PSK, value);
	nv_commit();
	return 0;
}

static int get_mlo_description(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = nv_get(NV_MLO_DESC);
	return 0;
}

static int set_mlo_description(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	if (strlen(value) > 64)
		return FAULT_9007;
	if (action == VALUESET) {
		nv_set(NV_MLO_DESC, value);
		nv_commit();
	}
	return 0;
}

DMLEAF tMloCfgParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable",         &DMWRITE, DMT_BOOL,   get_mlo_enable,          set_mlo_enable,          NULL, NULL},
{"LinkRadios",     &DMWRITE, DMT_STRING, get_mlo_link_radios,     set_mlo_link_radios,     NULL, NULL},
{"LinkBssIndex",   &DMWRITE, DMT_UNINT,  get_mlo_link_bss_index,  set_mlo_link_bss_index,  NULL, NULL},
{"LinkInterfaces", &DMREAD,  DMT_STRING, get_mlo_link_interfaces, NULL,                    NULL, NULL},
{"SelectedConfig", &DMREAD,  DMT_STRING, get_mlo_selected_config, NULL,                    NULL, NULL},
{"RuntimeConfig",  &DMREAD,  DMT_STRING, get_mlo_runtime_config,  NULL,                    NULL, NULL},
{"Status",         &DMREAD,  DMT_STRING, get_mlo_status,          NULL,                    NULL, NULL},
{"SSID",           &DMWRITE, DMT_STRING, get_mlo_ssid,            set_mlo_ssid,            NULL, NULL},
{"SecurityMode",   &DMWRITE, DMT_STRING, get_mlo_security_mode,   set_mlo_security_mode,   NULL, NULL},
{"KeyPassphrase",  &DMWRITE, DMT_STRING, get_mlo_key_passphrase,  set_mlo_key_passphrase,  NULL, NULL},
{"Description",    &DMWRITE, DMT_STRING, get_mlo_description,     set_mlo_description,     NULL, NULL},
{0}
};
