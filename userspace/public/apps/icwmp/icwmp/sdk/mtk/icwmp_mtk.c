/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	icwmpd on the MediaTek/Airoha OpenWrt product tree (HNI) — see icwmp_mtk.h.
 *
 *	Mirror table easycwmp <-> cwmp.  The easycwmp side is the config of
 *	record (what easycwmpd read); the cwmp side is what config.c
 *	global_conf_init() reads.  Same pattern as bdk/icwmp_bdk.c ms_maps
 *	(MDM <-> cwmp), with the value transforms easycwmp needs:
 *	  bool          "1"/"0" both sides
 *	  ssl_verify    "disable" -> cwmp.acs.insecure_enable=1
 *	  logging_level 0..4 (Critic..Debug) -> cwmp.cpe.log_severity name
 *
 *	NOT BUILD-TESTED YET.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <uci.h>

#include "cwmp.h"
#include "log.h"
#include "config.h"
#include "icwmp_mtk.h"
#include "sdk/sdk.h"

enum mtk_xform {
	XF_NONE,
	XF_BOOL,        /* true/1 -> "1", else "0" */
	XF_SSL_VERIFY,  /* "disable" -> "1" (insecure), else "0" */
	XF_LOG_LEVEL,   /* 0..4 -> CRITIC..DEBUG */
};

struct mtk_map {
	const char *easy;      /* easycwmp.<section>.<option> */
	const char *cwmp;      /* cwmp.<section>.<option> */
	enum mtk_xform xform;
	int to_easy;           /* pushed back cwmp -> easycwmp at end of session */
};

static const struct mtk_map maps[] = {
	{"easycwmp.@acs[0].url",                         "cwmp.acs.url",                        XF_NONE,       0},
	{"easycwmp.@acs[0].username",                    "cwmp.acs.userid",                     XF_NONE,       0},
	{"easycwmp.@acs[0].password",                    "cwmp.acs.passwd",                     XF_NONE,       0},
	{"easycwmp.@acs[0].periodic_enable",             "cwmp.acs.periodic_inform_enable",     XF_BOOL,       0},
	{"easycwmp.@acs[0].periodic_interval",           "cwmp.acs.periodic_inform_interval",   XF_NONE,       0},
	{"easycwmp.@acs[0].periodic_time",               "cwmp.acs.periodic_inform_time",       XF_NONE,       0},
	{"easycwmp.@acs[0].cwmpretryinterval",           "cwmp.acs.retry_min_wait_interval",    XF_NONE,       0},
	{"easycwmp.@acs[0].cwmpretryintervalmultiplier", "cwmp.acs.retry_interval_multiplier",  XF_NONE,       0},
	{"easycwmp.@acs[0].ssl_verify",                  "cwmp.acs.insecure_enable",            XF_SSL_VERIFY, 0},
	{"easycwmp.@acs[0].parameter_key",               "cwmp.acs.ParameterKey",               XF_NONE,       1},
	{"easycwmp.@local[0].interface",                 "cwmp.cpe.interface",                  XF_NONE,       0},
	{"easycwmp.@local[0].port",                      "cwmp.cpe.port",                       XF_NONE,       0},
	{"easycwmp.@local[0].username",                  "cwmp.cpe.userid",                     XF_NONE,       0},
	{"easycwmp.@local[0].password",                  "cwmp.cpe.passwd",                     XF_NONE,       0},
	{"easycwmp.@local[0].provisioning_code",         "cwmp.cpe.provisioning_code",          XF_NONE,       0},
	{"easycwmp.@local[0].logging_level",             "cwmp.cpe.log_severity",               XF_LOG_LEVEL,  0},
	{NULL, NULL, XF_NONE, 0}
};

static const char *const log_levels[] = { "CRITIC", "WARNING", "NOTICE", "INFO", "DEBUG" };

/* ------------------------------------------------------------------------ */
/* UCI helpers (plain libuci, usable from any thread)                        */
/* ------------------------------------------------------------------------ */

static int uci_get_str(struct uci_context *c, const char *key, char *out, size_t outlen)
{
	struct uci_ptr ptr;
	char *k = strdup(key);
	int rc = -1;

	out[0] = '\0';
	if (!k)
		return -1;
	if (uci_lookup_ptr(c, &ptr, k, true) == UCI_OK && (ptr.flags & UCI_LOOKUP_COMPLETE) &&
	    ptr.o && ptr.o->type == UCI_TYPE_STRING) {
		snprintf(out, outlen, "%s", ptr.o->v.string);
		rc = 0;
	}
	free(k);
	return rc;
}

static int uci_set_str(struct uci_context *c, const char *key, const char *value)
{
	struct uci_ptr ptr;
	char *kv;
	int rc = -1;

	if (asprintf(&kv, "%s=%s", key, value ? value : "") < 0)
		return -1;
	if (uci_lookup_ptr(c, &ptr, kv, true) == UCI_OK && uci_set(c, &ptr) == UCI_OK &&
	    uci_save(c, ptr.p) == UCI_OK)
		rc = 0;
	free(kv);
	return rc;
}

static int uci_commit_pkg(struct uci_context *c, const char *package)
{
	struct uci_ptr ptr;
	char *k = strdup(package);
	int rc = -1;

	if (!k)
		return -1;
	if (uci_lookup_ptr(c, &ptr, k, true) == UCI_OK && ptr.p && uci_commit(c, &ptr.p, false) == UCI_OK)
		rc = 0;
	free(k);
	return rc;
}

/* value as stored on the easycwmp side -> value for the cwmp side */
static const char *to_cwmp(const struct mtk_map *m, const char *v, char *buf, size_t buflen)
{
	switch (m->xform) {
	case XF_BOOL:
		return (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0) ? "1" : "0";
	case XF_SSL_VERIFY:
		return strcasecmp(v, "disable") == 0 ? "1" : "0";
	case XF_LOG_LEVEL: {
		int l = atoi(v);

		if (l < 0 || l > 4 || !v[0])
			return NULL;                           /* unknown: keep cwmp's */
		return log_levels[l];
	}
	default:
		snprintf(buf, buflen, "%s", v);
		return buf;
	}
}

static int is_secret(const char *key)
{
	return strstr(key, "passwd") != NULL || strstr(key, "password") != NULL;
}

/* ------------------------------------------------------------------------ */
/* mirror                                                                    */
/* ------------------------------------------------------------------------ */

int icwmp_mtk_sync_easycwmp_to_cwmp(void)
{
	struct uci_context *c;
	char ev[512], cur[512], buf[512];
	const struct mtk_map *m;
	const char *v;
	int changed = 0;

	c = uci_alloc_context();
	if (!c)
		return 0;
	for (m = maps; m->easy; m++) {
		if (m->to_easy)
			continue;                              /* icwmpd owns it */
		if (uci_get_str(c, m->easy, ev, sizeof(ev)) != 0)
			continue;                              /* not set on the product: keep cwmp's */
		v = to_cwmp(m, ev, buf, sizeof(buf));
		if (!v)
			continue;
		if (uci_get_str(c, m->cwmp, cur, sizeof(cur)) == 0 && strcmp(cur, v) == 0)
			continue;
		if (uci_set_str(c, m->cwmp, v) == 0) {
			changed = 1;
			if (is_secret(m->cwmp))
				CWMP_LOG(INFO, "sync easycwmp->cwmp %s (masked)", m->cwmp);
			else
				CWMP_LOG(INFO, "sync easycwmp->cwmp %s=%s", m->cwmp, v);
		}
	}
	if (changed)
		uci_commit_pkg(c, "cwmp");
	uci_free_context(c);
	return changed;
}

int icwmp_mtk_sync_cwmp_to_easycwmp(void)
{
	struct uci_context *c;
	char ev[512], cur[512];
	const struct mtk_map *m;
	int changed = 0;

	c = uci_alloc_context();
	if (!c)
		return 0;
	for (m = maps; m->easy; m++) {
		if (!m->to_easy)
			continue;
		if (uci_get_str(c, m->cwmp, cur, sizeof(cur)) != 0)
			continue;
		if (uci_get_str(c, m->easy, ev, sizeof(ev)) == 0 && strcmp(ev, cur) == 0)
			continue;
		if (uci_set_str(c, m->easy, cur) == 0) {
			changed = 1;
			CWMP_LOG(INFO, "sync cwmp->easycwmp %s=%s", m->easy, cur);
		}
	}
	/* connection request address the script uses for ConnectionRequestURL
	 * (libtr098 overrides the value on the wire from the same source, this
	 * keeps the WebUI / stuncd view consistent) */
	if (cwmp_main.conf.ip && cwmp_main.conf.ip[0] &&
	    (uci_get_str(c, "easycwmp.@local[0].ip", ev, sizeof(ev)) != 0 || strcmp(ev, cwmp_main.conf.ip) != 0) &&
	    uci_set_str(c, "easycwmp.@local[0].ip", cwmp_main.conf.ip) == 0) {
		changed = 1;
		CWMP_LOG(INFO, "sync cwmp->easycwmp easycwmp.@local[0].ip=%s", cwmp_main.conf.ip);
	}
	if (changed)
		uci_commit_pkg(c, "easycwmp");
	uci_free_context(c);
	return changed;
}

/* ------------------------------------------------------------------------ */
/* sdk/sdk.h hooks                                                */
/* ------------------------------------------------------------------------ */

int icwmp_platform_init(void)
{
	struct stat st;

	/* backup session file and boot flag live here (bin/Makefile.am CWMP_BKP_FILE) */
	if (stat(ICWMP_MTK_STATE_DIR, &st) != 0 && mkdir(ICWMP_MTK_STATE_DIR, 0755) != 0)
		CWMP_LOG(ERROR, "cannot create %s: %s", ICWMP_MTK_STATE_DIR, strerror(errno));
	icwmp_mtk_sync_easycwmp_to_cwmp();
	return 0;
}

int icwmp_platform_config_reload(void)
{
	return icwmp_mtk_sync_easycwmp_to_cwmp();
}

void icwmp_platform_config_reloaded(struct cwmp *cwmp)
{
	/* easycwmp.@device[0].* (identity) may have been refreshed by the init
	 * script or overridden in cwmp.cpe.*: rebuild the cached DeviceId */
	FREE(cwmp->deviceid.manufacturer);
	FREE(cwmp->deviceid.oui);
	FREE(cwmp->deviceid.serialnumber);
	FREE(cwmp->deviceid.productclass);
	FREE(cwmp->deviceid.softwareversion);
	cwmp_get_deviceid(cwmp);
}

int icwmp_platform_uloop_register(void)
{
	return 0;
}

void icwmp_platform_end_session(void)
{
	icwmp_mtk_sync_cwmp_to_easycwmp();
	/* the ACS may have written ManagementServer.* through the script
	 * (easycwmp.@acs[0]/@local[0]); libtr098 then asked for
	 * END_SESSION_RELOAD and run_session_end_func() already reloaded through
	 * icwmp_platform_config_reload().  A change that slipped past (WebUI
	 * during the session) is picked up here. */
	if (icwmp_mtk_sync_easycwmp_to_cwmp()) {
		CWMP_LOG(INFO, "easycwmp config changed during the session: reloading icwmpd config");
		cwmp_apply_acs_changes();
	}
}

void icwmp_platform_cleanup(void)
{
}
