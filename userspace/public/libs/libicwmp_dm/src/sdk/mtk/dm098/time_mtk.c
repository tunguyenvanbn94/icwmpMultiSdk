/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Time. for the MTK/Airoha product, native C.
 *
 *	Replaces functions/tr098/time of the easycwmp library one for one:
 *	  Enable                 system.ntp.enabled        + sysntpd restart
 *	  NTPServer1..5          system.ntp.server (list)  + sysntpd restart
 *	  LocalTimeZoneName      system.@system[0].zonename, city part only,
 *	                         set goes through the product's city table
 *	                         (tz_table_mtk.h, generated from the same file)
 *	  LocalTimeZone          offset derived from system.@system[0].timezone,
 *	                         sign inverted like the shell did (POSIX TZ says
 *	                         "west positive", TR-098 wants UTC offset)
 *	  DaylightSavingsStart   nth weekday of the ",Mm.w.d" rules of that TZ
 *	  DaylightSavingsEnd     string, read only, same as the old client
 *	  DaylightSavingsUsed    system.@system[0].timezone_dst
 *	  CurrentLocalTime       local time of the box
 *	  Status                 constant "Synchronized", as before
 *
 *	This module overrides the portable tr098/times.c: it is registered with
 *	a higher .order, so the registry keeps these rows when both are linked.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>

#include <uci.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "tz_table_mtk.h"

#define NTP_SERVER_MAX	5

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* system.ntp.server is a UCI list; the shell read it as one space
 * separated string and indexed words, this indexes the list itself.
 * A copy: the engine keeps the getter's pointer until the reply is
 * written (add_list_paramameter), and e->name lives in the UCI package,
 * which a reload during the walk frees (garbage NTPServer3 on the board,
 * analysis 65). */
static char *ntp_server_get(int index)
{
	struct uci_list *list = NULL;
	struct uci_element *e;
	int i = 0;

	dmuci_get_option_value_list("system", "ntp", "server", &list);
	if (!list)
		return "";
	uci_foreach_element(list, e) {
		if (i++ == index)
			return e->name ? dmstrdup(e->name) : "";
	}
	return "";
}

static void ntp_server_set(int index, char *value)
{
	struct uci_list *list = NULL;
	struct uci_element *e;
	char *keep[NTP_SERVER_MAX + 1];
	int n = 0, i;

	memset(keep, 0, sizeof(keep));
	dmuci_get_option_value_list("system", "ntp", "server", &list);
	if (list) {
		uci_foreach_element(list, e) {
			if (n >= NTP_SERVER_MAX)
				break;
			keep[n++] = dmstrdup(e->name ? e->name : "");
		}
	}
	/* pad up to the index the ACS addressed, as the shell did */
	while (n <= index && n < NTP_SERVER_MAX)
		keep[n++] = dmstrdup("");
	if (index < n)
		keep[index] = value ? value : "";

	dmuci_delete("system", "ntp", "server", NULL);
	for (i = 0; i < n; i++) {
		if (keep[i] && keep[i][0])
			dmuci_add_list_value("system", "ntp", "server", keep[i]);
	}
	mtk_apply_service("/etc/init.d/sysntpd restart");
}

/* POSIX TZ of system.@system[0].timezone, without the DST rules */
static void tz_base(char *out, size_t len)
{
	char *tz = mtk_uci("system", "@system[0]", "timezone");
	size_t n;

	if (!tz || !tz[0])
		tz = "UTC";
	n = strcspn(tz, ",");
	if (n >= len)
		n = len - 1;
	memcpy(out, tz, n);
	out[n] = '\0';
}

/* day of month of the nth weekday, Zeller, same arithmetic as the shell */
static int nth_weekday(int year, int month, int dow, int nth)
{
	int q = 1, y = year, m = month, K, J, h, first_dow, offset;

	if (m < 3) {
		m += 12;
		y--;
	}
	K = y % 100;
	J = y / 100;
	h = (q + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 - 2 * J) % 7;
	if (h < 0)
		h += 7;
	first_dow = (h + 6) % 7;		/* 0 = Sunday */
	offset = (dow - first_dow + 7) % 7;
	return 1 + offset + (nth - 1) * 7;
}

/* "M3.2.0" -> month/week/dow; returns 0 when the field is not a rule */
static int parse_dst_rule(const char *rule, int *month, int *week, int *dow)
{
	if (!rule || rule[0] != 'M')
		return 0;
	if (sscanf(rule + 1, "%d.%d.%d", month, week, dow) != 3)
		return 0;
	return 1;
}

static int dst_date(int field, char **value)
{
	char *tz = mtk_uci("system", "@system[0]", "timezone");
	char buf[32], rule[32];
	const char *p;
	int month = 0, week = 0, dow = 0, day, i;
	time_t now;
	struct tm *tm_now;

	*value = "0001-01-01T00:00:00";
	if (!tz || !strstr(tz, ",M"))
		return 0;

	/* field 2 = start, field 3 = end of "TZ,start,end" */
	p = tz;
	for (i = 1; i < field && p; i++) {
		p = strchr(p, ',');
		if (p)
			p++;
	}
	if (!p)
		return 0;
	snprintf(rule, sizeof(rule), "%.*s", (int)strcspn(p, ","), p);
	if (!parse_dst_rule(rule, &month, &week, &dow))
		return 0;

	now = time(NULL);
	tm_now = localtime(&now);
	if (!tm_now)
		return 0;
	day = nth_weekday(tm_now->tm_year + 1900, month, dow, week);
	snprintf(buf, sizeof(buf), "%04d-%02d-%02dT02:00:00",
		 tm_now->tm_year + 1900, month, day);
	*value = dmstrdup(buf);
	return 0;
}

/* ------------------------------------------------------------------ */
/* parameters                                                          */
/* ------------------------------------------------------------------ */

static int get_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci("system", "ntp", "enabled"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b;

	switch (action) {
	case VALUECHECK:
		if (mtk_parse_bool(value) < 0)
			return FAULT_9007;
		return 0;
	case VALUESET:
		b = mtk_parse_bool(value);
		dmuci_set_value("system", "ntp", "enabled", b ? "1" : "0");
		mtk_apply_service("/etc/init.d/sysntpd restart");
		return 0;
	}
	return 0;
}

static int get_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Synchronized";
	return 0;
}

#define NTP_GETSET(n)								\
static int get_ntp##n(char *refparam, struct dmctx *ctx, void *data,		\
		      char *instance, char **value)				\
{										\
	*value = ntp_server_get(n - 1);						\
	return 0;								\
}										\
static int set_ntp##n(char *refparam, struct dmctx *ctx, void *data,		\
		      char *instance, char *value, int action)			\
{										\
	if (action == VALUECHECK) {						\
		if (value && strlen(value) > 64)				\
			return FAULT_9007;					\
		return 0;							\
	}									\
	ntp_server_set(n - 1, value);						\
	return 0;								\
}

NTP_GETSET(1)
NTP_GETSET(2)
NTP_GETSET(3)
NTP_GETSET(4)
NTP_GETSET(5)

static int get_zonename(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *zone = mtk_uci("system", "@system[0]", "zonename");
	char *slash;

	if (!zone || !zone[0])
		zone = "UTC";
	slash = strrchr(zone, '/');
	*value = slash ? dmstrdup(slash + 1) : zone;
	return 0;
}

static const struct mtk_tz_entry *tz_lookup(const char *city)
{
	int i;

	if (!city)
		return NULL;
	for (i = 0; mtk_tz_table[i].city; i++) {
		if (strcmp(mtk_tz_table[i].city, city) == 0)
			return &mtk_tz_table[i];
	}
	return NULL;
}

static int set_zonename(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const struct mtk_tz_entry *e = tz_lookup(value);

	switch (action) {
	case VALUECHECK:
		/* the old client answered 9005 (invalid parameter name) here;
		 * 9007 is the correct fault for a value the CPE cannot accept */
		if (!e)
			return FAULT_9007;
		return 0;
	case VALUESET:
		if (!e)
			return FAULT_9007;
		dmuci_set_value("system", "@system[0]", "zonename", (char *)e->zonename);
		dmuci_set_value("system", "@system[0]", "timezone", (char *)e->tz);
		if (!strchr(e->tz, ','))
			dmuci_delete("system", "@system[0]", "timezone_dst", NULL);
		mtk_apply_service("/etc/init.d/system restart");
		if (mtk_bool(mtk_uci("system", "ntp", "enabled")))
			mtk_apply_service("/etc/init.d/sysntpd restart");
		return 0;
	}
	return 0;
}

/* TR-098 wants the UTC offset of local time, POSIX TZ counts west as
 * positive: the sign is inverted, exactly as get_local_time_zone() did */
static int get_localtimezone(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char tz[64], buf[16];
	const char *rest;
	int sign = 1, hours = 0, minutes = 0, total;

	*value = "+00:00";
	tz_base(tz, sizeof(tz));
	if (!strcmp(tz, "UTC") || !strcmp(tz, "GMT") ||
	    !strcmp(tz, "GMT0") || !strcmp(tz, "UTC0"))
		return 0;

	if (tz[0] == '<') {
		rest = strchr(tz, '>');
		if (!rest)
			return 0;
		rest++;
	} else {
		rest = tz;
		while (*rest && isalpha((unsigned char)*rest))
			rest++;
	}
	if (*rest == '-') {
		sign = -1;
		rest++;
	} else if (*rest == '+') {
		rest++;
	}
	if (!isdigit((unsigned char)*rest))
		return 0;
	hours = (int)strtol(rest, (char **)&rest, 10);
	if (*rest == ':')
		minutes = (int)strtol(rest + 1, NULL, 10);

	total = sign * (hours * 60 + minutes);
	total = -total;				/* POSIX west-positive -> UTC offset */
	snprintf(buf, sizeof(buf), "%c%02d:%02d",
		 total < 0 ? '-' : '+', abs(total) / 60, abs(total) % 60);
	*value = dmstrdup(buf);
	return 0;
}

static int get_currentlocaltime(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[40];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	*value = "0001-01-01T00:00:00Z";
	if (!t)
		return 0;
	if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", t) == 0)
		return 0;
	/* "+0700" -> "+07:00" */
	if (strlen(buf) >= 5) {
		size_t l = strlen(buf);

		buf[l + 1] = '\0';
		buf[l] = buf[l - 1];
		buf[l - 1] = buf[l - 2];
		buf[l - 2] = ':';
	}
	*value = dmstrdup(buf);
	return 0;
}

static int get_dst_start(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return dst_date(2, value);
}

static int get_dst_end(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return dst_date(3, value);
}

static int get_dst_used(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *tz = mtk_uci("system", "@system[0]", "timezone");

	if (!tz || !strchr(tz, ',')) {
		*value = "false";
		return 0;
	}
	*value = strcmp(mtk_uci("system", "@system[0]", "timezone_dst"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_dst_used(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *tz = mtk_uci("system", "@system[0]", "timezone");

	switch (action) {
	case VALUECHECK:
		if (mtk_parse_bool(value) < 0)
			return FAULT_9007;
		/* the current zone has no DST rules: nothing to enable */
		if (!tz || !strchr(tz, ','))
			return FAULT_9007;
		return 0;
	case VALUESET:
		if (mtk_parse_bool(value) == 1)
			dmuci_set_value("system", "@system[0]", "timezone_dst", "1");
		else
			dmuci_delete("system", "@system[0]", "timezone_dst", NULL);
		mtk_apply_service("/etc/init.d/system restart");
		return 0;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tree                                                                */
/* ------------------------------------------------------------------ */

static DMLEAF tTimeMtkParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_enable, set_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_status, NULL, NULL, NULL},
{"NTPServer1", &DMWRITE, DMT_STRING, get_ntp1, set_ntp1, NULL, NULL},
{"NTPServer2", &DMWRITE, DMT_STRING, get_ntp2, set_ntp2, NULL, NULL},
{"NTPServer3", &DMWRITE, DMT_STRING, get_ntp3, set_ntp3, NULL, NULL},
{"NTPServer4", &DMWRITE, DMT_STRING, get_ntp4, set_ntp4, NULL, NULL},
{"NTPServer5", &DMWRITE, DMT_STRING, get_ntp5, set_ntp5, NULL, NULL},
{"CurrentLocalTime", &DMREAD, DMT_TIME, get_currentlocaltime, NULL, NULL, NULL},
{"LocalTimeZone", &DMREAD, DMT_STRING, get_localtimezone, NULL, NULL, NULL},
{"LocalTimeZoneName", &DMWRITE, DMT_STRING, get_zonename, set_zonename, NULL, NULL},
{"DaylightSavingsStart", &DMREAD, DMT_TIME, get_dst_start, NULL, NULL, NULL},
{"DaylightSavingsEnd", &DMREAD, DMT_TIME, get_dst_end, NULL, NULL, NULL},
{"DaylightSavingsUsed", &DMWRITE, DMT_BOOL, get_dst_used, set_dst_used, NULL, NULL},
{0}
};

static DMOBJ tTimeMtkObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Time", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tTimeMtkParams, NULL},
{0}
};

static const char *const time_mtk_paths[] = {
	"InternetGatewayDevice.Time.",
	NULL
};

static const struct dm_module time_mtk_module = {
	.name  = "mtk-time",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tTimeMtkObj,
	.paths = time_mtk_paths,
};
DM_MODULE_REGISTER(time_mtk_module);

/* ------------------------------------------------------------------ */
/* TR-181 (cwmp.cpe.datamodel=tr181): Device.Time.                     */
/* ------------------------------------------------------------------ */

/* Device.Time.LocalTimeZone is the POSIX TZ string itself,
 * system.@system[0].timezone ("<+07>-7").  A write must be the TZ of one of
 * the product's cities (tz_table_mtk.h, what the WebUI offers): it goes
 * through set_zonename() with the first such city, so zonename and
 * timezone_dst follow exactly as for TR-098 LocalTimeZoneName.
 * TR-181 has no LocalTimeZoneName, no offset form of LocalTimeZone and no
 * DaylightSavings* (the rules are part of the TZ string). */
static const struct mtk_tz_entry *tz_lookup_posix(const char *tz)
{
	int i;

	if (!tz || !tz[0])
		return NULL;
	for (i = 0; mtk_tz_table[i].city; i++) {
		if (strcmp(mtk_tz_table[i].tz, tz) == 0)
			return &mtk_tz_table[i];
	}
	return NULL;
}

static int get_localtimezone181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *tz = mtk_uci("system", "@system[0]", "timezone");

	*value = (tz && tz[0]) ? tz : "UTC";
	return 0;
}

static int set_localtimezone181(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const struct mtk_tz_entry *e = tz_lookup_posix(value);

	if (!e)
		return FAULT_9007;
	return set_zonename(refparam, ctx, data, instance, (char *)e->city, action);
}

/*
 * TR-181 2.19 deleted NTPServer1..5: the client is Time.Client.{i} (2.16).
 * One client, sysntpd:
 *   Enable           system.ntp.enabled, as Time.Enable
 *   Status           Disabled when not enabled; Synchronized once ntpd has
 *                    reported a stratum since boot -- the product's
 *                    /etc/hotplug.d/ntp/25-dnsmasqsec then writes
 *                    /var/state/dnsmasqsec (tmpfs) -- else Unsynchronized.
 *                    Time.Status the same (TR-098 keeps the product's
 *                    constant "Synchronized")
 *   Servers          system.ntp.server, comma separated; a set replaces the
 *                    list (at most 5, each at most 64 characters, as the 5
 *                    TR-098 NTPServer leaves) and restarts sysntpd
 *   Mode Unicast, Port 123, Version 4, Interface "" (any): what busybox ntpd
 *                    does; another value is 9007
 */
#define NTP_VALID_FILE	"/var/state/dnsmasqsec"

static int get_status181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	if (strcmp(mtk_uci("system", "ntp", "enabled"), "1") != 0)
		*value = "Disabled";
	else
		*value = check_file(NTP_VALID_FILE) ? "Synchronized" : "Unsynchronized";
	return 0;
}

static int browse_time_client(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	DM_LINK_INST_OBJ(dmctx, parent_node, prev_data, dmstrdup("1"));
	return 0;
}

static int get_time_one(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "1";
	return 0;
}

static int get_client_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct uci_list *list = NULL;
	struct uci_element *e;
	char buf[512];
	size_t n = 0;

	buf[0] = '\0';
	dmuci_get_option_value_list("system", "ntp", "server", &list);
	if (list) {
		uci_foreach_element(list, e) {
			if (!e->name || !e->name[0] || n >= sizeof(buf) - 1)
				continue;
			n += snprintf(buf + n, sizeof(buf) - n, "%s%s", n ? "," : "", e->name);
		}
	}
	*value = dmstrdup(buf);
	return 0;
}

static int set_client_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *copy = dmstrdup(value ? value : ""), *tok, *save = NULL, *end;
	int n = 0;

	if (action == VALUESET)
		dmuci_delete("system", "ntp", "server", NULL);
	for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		while (*tok == ' ')
			tok++;
		end = tok + strlen(tok);
		while (end > tok && end[-1] == ' ')
			*--end = '\0';
		if (!*tok)
			continue;
		if (strlen(tok) > 64 || ++n > NTP_SERVER_MAX)
			return FAULT_9007;
		if (action == VALUESET)
			dmuci_add_list_value("system", "ntp", "server", tok);
	}
	if (action == VALUESET)
		mtk_apply_service("/etc/init.d/sysntpd restart");
	return 0;
}

#define TIME_FIXED(name, text)							\
static int get_client_##name(char *refparam, struct dmctx *ctx, void *data,	\
			     char *instance, char **value)			\
{										\
	*value = text;								\
	return 0;								\
}										\
static int set_client_##name(char *refparam, struct dmctx *ctx, void *data,	\
			     char *instance, char *value, int action)		\
{										\
	return strcmp(value ? value : "", text) == 0 ? 0 : FAULT_9007;		\
}

TIME_FIXED(mode, "Unicast")
TIME_FIXED(port, "123")
TIME_FIXED(version, "4")
TIME_FIXED(interface, "")

static DMLEAF tTimeClient181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_enable, set_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_status181, NULL, NULL, NULL},
{"Mode", &DMWRITE, DMT_STRING, get_client_mode, set_client_mode, NULL, NULL},
{"Port", &DMWRITE, DMT_UNINT, get_client_port, set_client_port, NULL, NULL},
{"Version", &DMWRITE, DMT_UNINT, get_client_version, set_client_version, NULL, NULL},
{"Servers", &DMWRITE, DMT_STRING, get_client_servers, set_client_servers, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_client_interface, set_client_interface, NULL, NULL},
{0}
};

static DMLEAF tTime181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_enable, set_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_status181, NULL, NULL, NULL},
{"CurrentLocalTime", &DMREAD, DMT_TIME, get_currentlocaltime, NULL, NULL, NULL},
{"LocalTimeZone", &DMWRITE, DMT_STRING, get_localtimezone181, set_localtimezone181, NULL, NULL},
{"ClientNumberOfEntries", &DMREAD, DMT_UNINT, get_time_one, NULL, NULL, NULL},
{0}
};

static DMOBJ tTime181ChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Client", &DMREAD, NULL, NULL, NULL, browse_time_client, NULL, NULL, NULL, tTimeClient181Params, NULL},
{0}
};

static DMOBJ tTime181Obj[] = {
{"Time", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tTime181ChildObj, tTime181Params, NULL},
{0}
};

static const char *const time181_mtk_paths[] = {
	"Device.Time.",
	NULL
};

static const struct dm_module time181_mtk_module = {
	.name  = "mtk-time-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tTime181Obj,
	.paths = time181_mtk_paths,
};
DM_MODULE_REGISTER(time181_mtk_module);
