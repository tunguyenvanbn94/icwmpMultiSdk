/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.ManagementServer. of the MTK/Airoha product.
 *
 *	The portable module tr098/managementserver.c serves the object against
 *	icwmpd's own UCI "cwmp" config.  On this product that is the wrong store
 *	for every leaf the product had: easycwmp stays the config of record
 *	(WebUI, hal_gateway, DHCP option 43, the next boot), icwmpd copies it
 *	over "cwmp" at start, at every config reload and at the end of every
 *	session (icwmp sdk/mtk/icwmp_mtk.c), and STUN belongs to the product's
 *	stunclient (stun.@stun[0], stuncd), not to icwmp_stund/cwmp_stun.  A value
 *	the ACS wrote into "cwmp" came back as the old easycwmp one at the end of
 *	the session (tests/host run.sh msrv).
 *
 *	So this module, merged after the portable one (a leaf declared twice
 *	keeps the later module's row, dm_registry.h), reads and writes the
 *	option the shell function of the product used, per
 *	docs/issue/tr098_coverage_matrix.tsv:
 *
 *	  URL, Username, Password                 easycwmp.@acs[0].url/username/password
 *	  PeriodicInformEnable/Interval/Time      easycwmp.@acs[0].periodic_enable/_interval/_time
 *	  ConnectionRequestUsername/Password      easycwmp.@local[0].username/password
 *	  CWMPRetryMinimumWaitInterval            easycwmp.@acs[0].cwmpretryinterval
 *	  CWMPRetryIntervalMultiplier             easycwmp.@acs[0].cwmpretryintervalmultiplier
 *	  EnableCWMP, UpgradesManaged             easycwmp.@acs[0].enablecwmp/upgradesmanaged
 *	  STUN*, NATDetected,                     stun.@stun[0].stun_enable/serveraddress/
 *	  UDPConnectionRequestAddress               serverport/username/password/min_keepalive/
 *	                                            max_keepalive/natdetect/udpcontnreqaddr
 *
 *	An easycwmp write ends the session with END_SESSION_RELOAD: icwmpd then
 *	mirrors the new value into "cwmp" and uses it.  A STUN write raises
 *	/tmp/stunclient_reload_needed like the shell setter did and queues one
 *	"stuncd reload" for the end of the session, what the product's ucitrack
 *	restart of the stun package did (to be confirmed on the board, gate G7).
 *	Passwords read back empty, as in the shell.  Value ranges are the
 *	TR-098 ones; the shell's own type check (is_safe_input, xsd type) runs
 *	in front of every setter already (input_contract_mtk.c).
 *
 *	ParameterKey, ConnectionRequestURL and the leaves icwmp adds on top of
 *	the product tree (HTTPCompression, InstanceMode, ...) stay with the
 *	portable module: they are icwmpd's own settings.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define STUN_RELOAD_FLAG	"/tmp/stunclient_reload_needed"
#define STUN_RELOAD_CMD		"[ -x /etc/init.d/stuncd ] && /etc/init.d/stuncd reload"
#define UNKNOWN_TIME		"0001-01-01T00:00:00Z"

enum ms_kind {
	MS_STRING,	/* string(256) */
	MS_BOOL,
	MS_UINT,	/* min .. max */
	MS_INT,		/* min .. max */
	MS_TIME,	/* dateTime, stored as written */
};

#define MS_RELOAD	0x1	/* easycwmp: icwmpd reloads its config after the session */
#define MS_STUN		0x2	/* stunclient: flag + stuncd reload */
#define MS_SECRET	0x4	/* reads back empty */

struct ms_opt {
	const char *package;
	const char *section;
	const char *option;
	enum ms_kind kind;
	long long min, max;
	int flags;
};

static int ms_2digits(const char *p)
{
	return (p[0] - '0') * 10 + (p[1] - '0');
}

static int ms_days_in_month(int y, int m)
{
	static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
		return 29;
	return days[m - 1];
}

/* xsd:dateTime: "2026-10-04T03:00:00", optional fraction, optional Z or
 * +hh:mm/-hh:mm, and a real instant: month 1-12, the day within its month
 * (29 February in leap years only), hh 0-23, mm and ss 0-59, a zone of at
 * most 14:00.  The unknown time 0001-01-01T00:00:00Z passes.  The shell ran
 * busybox "date -d" on the value with T made a space: that refused every
 * value ending in Z except the unknown time, and took +25:00 (K14). */
static int ms_valid_datetime(const char *v)
{
	static const char shape[] = "dddd-dd-ddTdd:dd:dd";
	int i, y, mo, d, zh, zm;

	for (i = 0; shape[i]; i++) {
		if (shape[i] == 'd' ? !isdigit((unsigned char)v[i]) : v[i] != shape[i])
			return 0;
	}
	y = ms_2digits(v) * 100 + ms_2digits(v + 2);
	mo = ms_2digits(v + 5);
	d = ms_2digits(v + 8);
	if (y < 1 || mo < 1 || mo > 12 || d < 1 || d > ms_days_in_month(y, mo) ||
	    ms_2digits(v + 11) > 23 || ms_2digits(v + 14) > 59 || ms_2digits(v + 17) > 59)
		return 0;
	v += i;
	if (*v == '.') {
		v++;
		if (!isdigit((unsigned char)*v))
			return 0;
		while (isdigit((unsigned char)*v))
			v++;
	}
	if (*v == 'Z')
		v++;
	else if (*v == '+' || *v == '-') {
		if (!isdigit((unsigned char)v[1]) || !isdigit((unsigned char)v[2]) || v[3] != ':' ||
		    !isdigit((unsigned char)v[4]) || !isdigit((unsigned char)v[5]))
			return 0;
		zh = ms_2digits(v + 1);
		zm = ms_2digits(v + 4);
		if (zh > 14 || zm > 59 || (zh == 14 && zm != 0))
			return 0;
		v += 6;
	}
	return *v == '\0';
}

static int ms_valid_number(const char *v, int is_signed, long long min, long long max)
{
	const char *p = v;
	long long n;
	char *end;

	if (is_signed && *p == '-')
		p++;
	if (!isdigit((unsigned char)*p))
		return 0;
	errno = 0;
	n = strtoll(v, &end, 10);
	return errno == 0 && *end == '\0' && n >= min && n <= max;
}

static int ms_get(const struct ms_opt *o, char **value)
{
	char *v;

	if (o->flags & MS_SECRET) {
		*value = "";
		return 0;
	}
	v = mtk_uci(o->package, o->section, o->option);
	switch (o->kind) {
	case MS_BOOL:
		*value = mtk_bool(v) ? "1" : "0";
		break;
	case MS_TIME:
		*value = v[0] ? v : UNKNOWN_TIME;
		break;
	default:
		*value = v;
		break;
	}
	return 0;
}

static int ms_check(const struct ms_opt *o, const char *value)
{
	switch (o->kind) {
	case MS_STRING:
		return strlen(value) <= 256 ? 0 : FAULT_9007;
	case MS_BOOL:
		return mtk_parse_bool(value) < 0 ? FAULT_9007 : 0;
	case MS_UINT:
		return ms_valid_number(value, 0, o->min, o->max) ? 0 : FAULT_9007;
	case MS_INT:
		return ms_valid_number(value, 1, o->min, o->max) ? 0 : FAULT_9007;
	case MS_TIME:
		return ms_valid_datetime(value) ? 0 : FAULT_9007;
	}
	return FAULT_9007;
}

static void ms_stun_changed(void)
{
	FILE *f = fopen(STUN_RELOAD_FLAG, "w");

	if (f)
		fclose(f);
	mtk_apply_service_once(STUN_RELOAD_CMD);
}

static int ms_set(const struct ms_opt *o, char *value, int action)
{
	switch (action) {
	case VALUECHECK:
		return ms_check(o, value);
	case VALUESET:
		if (o->kind == MS_BOOL)
			value = mtk_parse_bool(value) ? "1" : "0";
		dmuci_set_value((char *)o->package, (char *)o->section, (char *)o->option, value);
		if (o->flags & MS_RELOAD)
			cwmp_set_end_session(END_SESSION_RELOAD);
		if (o->flags & MS_STUN)
			ms_stun_changed();
		return 0;
	}
	return 0;
}

#define MS_OPT(name, pkg, sec, opt, kind, min, max, flags) \
static const struct ms_opt ms_##name = { pkg, sec, opt, kind, min, max, flags }; \
static int get_##name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value) \
{ \
	return ms_get(&ms_##name, value); \
} \
static int __attribute__((unused)) set_##name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action) \
{ \
	return ms_set(&ms_##name, value, action); \
}

#define MS_ACS(name, opt, kind, min, max, flags)	MS_OPT(name, "easycwmp", "@acs[0]", opt, kind, min, max, flags)
#define MS_LOCAL(name, opt, kind, flags)		MS_OPT(name, "easycwmp", "@local[0]", opt, kind, 0, 0, flags)
#define MS_STUNOPT(name, opt, kind, min, max, flags)	MS_OPT(name, "stun", "@stun[0]", opt, kind, min, max, flags)

MS_ACS(url,                "url",                          MS_STRING, 0, 0,               MS_RELOAD)
MS_ACS(username,           "username",                     MS_STRING, 0, 0,               MS_RELOAD)
MS_ACS(password,           "password",                     MS_STRING, 0, 0,               MS_RELOAD | MS_SECRET)
MS_ACS(periodic_en,        "periodic_enable",              MS_BOOL,   0, 0,               MS_RELOAD)
MS_ACS(periodic_int,       "periodic_interval",            MS_UINT,   1, 4294967295LL,    MS_RELOAD)
MS_ACS(periodic_time,      "periodic_time",                MS_TIME,   0, 0,               MS_RELOAD)
MS_ACS(retry_min,          "cwmpretryinterval",            MS_UINT,   1, 65535,           MS_RELOAD)
MS_ACS(retry_mult,         "cwmpretryintervalmultiplier",  MS_UINT,   1000, 65535,        MS_RELOAD)
MS_LOCAL(cr_username,      "username",                     MS_STRING,                     MS_RELOAD)
MS_LOCAL(cr_password,      "password",                     MS_STRING,                     MS_RELOAD | MS_SECRET)
MS_STUNOPT(stun_enable,    "stun_enable",                  MS_BOOL,   0, 0,               MS_STUN)
MS_STUNOPT(stun_server,    "serveraddress",                MS_STRING, 0, 0,               MS_STUN)
MS_STUNOPT(stun_port,      "serverport",                   MS_UINT,   0, 65535,           MS_STUN)
MS_STUNOPT(stun_user,      "username",                     MS_STRING, 0, 0,               MS_STUN)
MS_STUNOPT(stun_pass,      "password",                     MS_STRING, 0, 0,               MS_STUN | MS_SECRET)
MS_STUNOPT(stun_max_ka,    "max_keepalive",                MS_INT,    -1, 2147483647,     MS_STUN)
MS_STUNOPT(stun_min_ka,    "min_keepalive",                MS_UINT,   0, 4294967295LL,    MS_STUN)
MS_STUNOPT(nat_detected,   "natdetect",                    MS_BOOL,   0, 0,               0)
MS_STUNOPT(udp_cr_addr,    "udpcontnreqaddr",              MS_STRING, 0, 0,               0)

static int get_enablecwmp(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_uci("easycwmp", "@acs[0]", "enablecwmp");

	/* absent means enabled: the daemon is running, after all */
	*value = (v && v[0]) ? (mtk_bool(v) ? "1" : "0") : "1";
	return 0;
}

static int set_enablecwmp(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b;

	switch (action) {
	case VALUECHECK:
		if (mtk_parse_bool(value) < 0)
			return FAULT_9007;
		return 0;
	case VALUESET:
		b = mtk_parse_bool(value);
		dmuci_set_value("easycwmp", "@acs[0]", "enablecwmp", b ? "1" : "0");
		return 0;
	}
	return 0;
}

static int get_upgradesmanaged(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_bool(mtk_uci("easycwmp", "@acs[0]", "upgradesmanaged")) ? "1" : "0";
	return 0;
}

static int set_upgradesmanaged(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b;

	switch (action) {
	case VALUECHECK:
		if (mtk_parse_bool(value) < 0)
			return FAULT_9007;
		return 0;
	case VALUESET:
		b = mtk_parse_bool(value);
		dmuci_set_value("easycwmp", "@acs[0]", "upgradesmanaged", b ? "1" : "0");
		return 0;
	}
	return 0;
}

static DMLEAF tManagementServerMtkParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"EnableCWMP", &DMWRITE, DMT_BOOL, get_enablecwmp, set_enablecwmp, NULL, NULL},
{"UpgradesManaged", &DMWRITE, DMT_BOOL, get_upgradesmanaged, set_upgradesmanaged, NULL, NULL},
{"URL", &DMWRITE, DMT_STRING, get_url, set_url, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_username, set_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_password, set_password, NULL, NULL},
{"PeriodicInformEnable", &DMWRITE, DMT_BOOL, get_periodic_en, set_periodic_en, NULL, NULL},
{"PeriodicInformInterval", &DMWRITE, DMT_UNINT, get_periodic_int, set_periodic_int, NULL, NULL},
{"PeriodicInformTime", &DMWRITE, DMT_TIME, get_periodic_time, set_periodic_time, NULL, NULL},
{"ConnectionRequestUsername", &DMWRITE, DMT_STRING, get_cr_username, set_cr_username, NULL, NULL},
{"ConnectionRequestPassword", &DMWRITE, DMT_STRING, get_cr_password, set_cr_password, NULL, NULL},
{"CWMPRetryMinimumWaitInterval", &DMWRITE, DMT_UNINT, get_retry_min, set_retry_min, NULL, NULL},
{"CWMPRetryIntervalMultiplier", &DMWRITE, DMT_UNINT, get_retry_mult, set_retry_mult, NULL, NULL},
{"UDPConnectionRequestAddress", &DMREAD, DMT_STRING, get_udp_cr_addr, NULL, NULL, &DMACTIVE},
{"STUNEnable", &DMWRITE, DMT_BOOL, get_stun_enable, set_stun_enable, NULL, NULL},
{"STUNServerAddress", &DMWRITE, DMT_STRING, get_stun_server, set_stun_server, NULL, NULL},
{"STUNServerPort", &DMWRITE, DMT_UNINT, get_stun_port, set_stun_port, NULL, NULL},
{"STUNUsername", &DMWRITE, DMT_STRING, get_stun_user, set_stun_user, NULL, NULL},
{"STUNPassword", &DMWRITE, DMT_STRING, get_stun_pass, set_stun_pass, NULL, NULL},
{"STUNMaximumKeepAlivePeriod", &DMWRITE, DMT_INT, get_stun_max_ka, set_stun_max_ka, NULL, NULL},
{"STUNMinimumKeepAlivePeriod", &DMWRITE, DMT_UNINT, get_stun_min_ka, set_stun_min_ka, NULL, NULL},
{"NATDetected", &DMREAD, DMT_BOOL, get_nat_detected, NULL, NULL, NULL},
{0}
};

static DMOBJ tManagementServerMtkRoot[] = {
{"ManagementServer", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, &DMNONE, NULL, tManagementServerMtkParam, NULL},
{0}
};

static const char *const ms_mtk_paths[] = {
	"InternetGatewayDevice.ManagementServer.",
	NULL
};

static const struct dm_module ms_mtk_module = {
	.name  = "mtk-managementserver",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_LATE,	/* merged after the portable module */
	.objs  = tManagementServerMtkRoot,
	.paths = ms_mtk_paths,
};
DM_MODULE_REGISTER(ms_mtk_module);
