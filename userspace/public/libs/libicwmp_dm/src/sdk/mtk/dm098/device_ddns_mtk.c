/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Device.DynamicDNS. -- ddns-scripts seen the
 *	TR-181 way, ported from functions/tr098/device_dynamic_dns.  The same
 *	ddns config as X_AIS_DDNS (x_ais_ddns_mtk.c), one client per "service"
 *	section, positional.
 *	  ClientNumberOfEntries  the service sections
 *	  ServerNumberOfEntries  the lines of /usr/lib/ddns/services, else of
 *	                         /etc/ddns/services, that do not start with
 *	                         "#"; "2" without either file
 *	  SupportedServices      of /etc/ddns/services, else /usr/lib/ddns/
 *	                         services (that order, unlike the count): per
 *	                         line not starting with "#", the first word of
 *	                         what comes before the first '"', joined with
 *	                         ","; "dyndns.org,no-ip.com" without a file
 *	  Client.{i}.Enable      enabled "1" read true; a set stores 1 for
 *	                         true/1, else 0
 *	  Status                 Disabled unless enabled; Up when the pid in
 *	                         /var/run/ddns/<sec>.pid runs or <sec>.dat
 *	                         exists; else Error
 *	  Alias                  alias, the section name when unset
 *	  LastError              /var/run/ddns/<sec>.result mapped to the TR-181
 *	                         error names
 *	  Server, Username, Password   service_name, username, password as
 *	                         stored (the password reads back, the shell's
 *	                         choice)
 *	  Interface              "Device.IP.Interface.<n>" (no
 *	                         "InternetGatewayDevice." in front, as the
 *	                         shell wrote it) of ip_network, else interface,
 *	                         ".0" when that network section has no number;
 *	                         a set takes a path whose 4th dot field is a
 *	                         Device.IP number and writes ip_source network,
 *	                         ip_network and interface
 *	Every set but Alias restarts ddns, queued once for the end of the
 *	session.  AddObject adds a service section, disabled, ip_source network
 *	on network wan; DeleteObject removes it.
 *
 *	One difference, on purpose: the shell's AddObject answered instance "1"
 *	whatever the position of the new client, so an ACS that then set
 *	Client.1.* changed the first client already there.  Here the answer is
 *	the new client's own number.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "device_ip_mtk.h"

#define DD_PKG		"ddns"
#define DD_RESTART	"/etc/init.d/ddns restart"
#define DD_RUN		"/var/run/ddns"

struct dd_client {
	char *sec;	/* section name, "@service[n]" for an anonymous one */
};

static struct uci_package *dd_pkg(void)
{
	struct uci_ptr ptr = {0};

	if (dmuci_lookup_ptr(uci_ctx, &ptr, DD_PKG, NULL, NULL, NULL) || !ptr.p)
		return NULL;
	return ptr.p;
}

/* the "=service" lines of "uci show ddns", as section paths */
static int dd_list(struct dd_client *list, int max)
{
	struct uci_package *p = dd_pkg();
	struct uci_element *e;
	int n = 0, idx = 0;

	if (!p)
		return 0;
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		char *sec;

		if (strcmp(s->type, "service") != 0)
			continue;
		if (s->anonymous)
			dmasprintf(&sec, "@service[%d]", idx);
		else
			sec = dmstrdup(section_name(s));
		idx++;
		if (!sec || n >= max)
			continue;
		if (list)
			list[n].sec = sec;
		n++;
	}
	return n;
}

#define DD_MAX	32

static int browse_dd(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct dd_client *list = dmcalloc(DD_MAX, sizeof(*list));
	char *idx, *idx_last = NULL;
	int n, i;

	if (!list)
		return 0;
	n = dd_list(list, DD_MAX);
	for (i = 0; i < n; i++) {
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&list[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

#define DD_SEC(data)	(((struct dd_client *)(data))->sec)

static int add_dd(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	struct uci_section *s = NULL;
	char *name = NULL;

	dmuci_add_section(DD_PKG, "service", &s, &name);
	if (!s)
		return FAULT_9002;
	dmuci_set_value_by_section(s, "enabled", "0");
	dmuci_set_value_by_section(s, "ip_source", "network");
	dmuci_set_value_by_section(s, "ip_network", "wan");
	dmasprintf(instance, "%d", dd_list(NULL, DD_MAX));
	return 0;
}

static int del_dd(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	if (del_action != DEL_INST)
		return FAULT_9005;	/* the shell had no "delete all" */
	if (!data)
		return FAULT_9002;
	dmuci_delete(DD_PKG, DD_SEC(data), NULL, NULL);
	return 0;
}

/* ------------------------------------------------------------------ */
/* DynamicDNS.                                                         */
/* ------------------------------------------------------------------ */

static char *dd_num(int v)
{
	char *s = NULL;

	dmasprintf(&s, "%d", v);
	return s ? s : "0";
}

static int get_dd_client_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dd_num(dd_list(NULL, DD_MAX));
	return 0;
}

/* grep -v "^#" <file> | wc -l: blank lines count too */
static int dd_service_lines(const char *path, char *out, size_t sz)
{
	FILE *f = fopen(path, "r");
	char line[512];
	int n = 0, first = 1;

	if (!f)
		return -1;
	if (out)
		out[0] = '\0';
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#')
			continue;
		n++;
		if (out) {
			/* cut -d'"' -f1 | awk '{printf "%s%s", sep, $1; sep=","}' */
			char word[128] = "";

			line[strcspn(line, "\"\n")] = '\0';
			sscanf(line, "%127s", word);
			if (!first)
				strncat(out, ",", sz - strlen(out) - 1);
			strncat(out, word, sz - strlen(out) - 1);
			first = 0;
		}
	}
	fclose(f);
	return n;
}

static int get_dd_server_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int n = dd_service_lines("/usr/lib/ddns/services", NULL, 0);

	if (n < 0)
		n = dd_service_lines("/etc/ddns/services", NULL, 0);
	*value = n < 0 ? "2" : dd_num(n);
	return 0;
}

static int get_dd_supported(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char out[4096];
	const char *file = NULL;

	if (access("/usr/lib/ddns/services", F_OK) == 0)
		file = "/usr/lib/ddns/services";
	if (access("/etc/ddns/services", F_OK) == 0)
		file = "/etc/ddns/services";
	if (!file || dd_service_lines(file, out, sizeof(out)) < 0) {
		*value = "dyndns.org,no-ip.com";
		return 0;
	}
	*value = dmstrdup(out);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Client.{i}                                                          */
/* ------------------------------------------------------------------ */

static int get_dd_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(mtk_uci(DD_PKG, DD_SEC(data), "enabled"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_dd_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(DD_PKG, DD_SEC(data), "enabled",
			(strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? "1" : "0");
	mtk_apply_service_once(DD_RESTART);
	return 0;
}

static int get_dd_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char path[160], *pid;
	struct stat st;

	if (strcmp(mtk_uci(DD_PKG, DD_SEC(data), "enabled"), "1") != 0) {
		*value = "Disabled";
		return 0;
	}
	snprintf(path, sizeof(path), DD_RUN "/%s.pid", DD_SEC(data));
	if (access(path, F_OK) == 0) {
		pid = mtk_file_line(path);
		snprintf(path, sizeof(path), "/proc/%s", pid);
		if (*pid && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			*value = "Up";
			return 0;
		}
	}
	snprintf(path, sizeof(path), DD_RUN "/%s.dat", DD_SEC(data));
	*value = access(path, F_OK) == 0 ? "Up" : "Error";
	return 0;
}

static int get_dd_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_uci(DD_PKG, DD_SEC(data), "alias");

	*value = *v ? v : DD_SEC(data);
	return 0;
}

static int set_dd_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUESET)
		dmuci_set_value(DD_PKG, DD_SEC(data), "alias", value);
	return 0;
}

/* ddns_get_lasterror: the result file mapped, first match wins */
static int get_dd_lasterror(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char path[160], *status, *p;
	static const struct { const char *const pats[5]; const char *name; } map[] = {
		{ { "NO_ERROR", "UPDATED", "good", NULL }, "ERROR_NONE" },
		{ { "AUTH_FAIL", "badauth", "abuse", NULL }, "ERROR_AUTHENTICATION" },
		{ { "CONNECT_ERROR", "network", NULL }, "ERROR_CONNECT" },
		{ { "DNS_ERROR", NULL }, "ERROR_DNS_RESOLVE" },
		{ { "notfqdn", "CONFIG_ERROR", "badagent", "badparam", NULL }, "ERROR_MISCONFIGURED" },
	};
	size_t i, j;

	snprintf(path, sizeof(path), DD_RUN "/%s.result", DD_SEC(data));
	if (access(path, F_OK) != 0) {
		*value = strcmp(mtk_uci(DD_PKG, DD_SEC(data), "enabled"), "1") != 0 ?
			 "ERROR_MISCONFIGURED" : "ERROR_NONE";
		return 0;
	}
	/* cat <file> | tr -d '\n' */
	{
		char *argv[] = { "cat", path, NULL };

		status = mtk_exec(argv);
	}
	for (p = status; p && *p; ) {
		if (*p == '\n')
			memmove(p, p + 1, strlen(p + 1) + 1);
		else
			p++;
	}
	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		for (j = 0; map[i].pats[j]; j++) {
			if (strstr(status, map[i].pats[j])) {
				*value = (char *)map[i].name;
				return 0;
			}
		}
	}
	if (*status)
		dmasprintf(value, "ERROR_UNKNOWN: %s", status);
	else
		*value = "ERROR_NONE";
	return 0;
}

#define DD_TEXT(name, option)							\
static int get_dd_##name(char *refparam, struct dmctx *ctx, void *data,	\
			 char *instance, char **value)				\
{										\
	*value = mtk_uci(DD_PKG, DD_SEC(data), option);				\
	return 0;								\
}										\
static int set_dd_##name(char *refparam, struct dmctx *ctx, void *data,	\
			 char *instance, char *value, int action)		\
{										\
	if (action == VALUECHECK)						\
		return 0;							\
	dmuci_set_value(DD_PKG, DD_SEC(data), option, value);			\
	mtk_apply_service_once(DD_RESTART);					\
	return 0;								\
}

DD_TEXT(server, "service_name")
DD_TEXT(username, "username")
DD_TEXT(password, "password")

static int get_dd_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *net = mtk_uci(DD_PKG, DD_SEC(data), "ip_network"), *inst;

	if (!*net)
		net = mtk_uci(DD_PKG, DD_SEC(data), "interface");
	if (!*net || strcmp(net, "notused") == 0) {
		*value = "";
		return 0;
	}
	inst = dip_instance_of(net);
	dmasprintf(value, "Device.IP.Interface.%s", *inst ? inst : "0");
	return 0;
}

/* echo "$path" | cut -d. -f4 */
static char *dd_field4(const char *path)
{
	const char *p = path;
	char *out;
	size_t len;
	int i;

	for (i = 0; i < 3; i++) {
		p = strchr(p, '.');
		if (!p)
			return dmstrdup(i == 0 ? path : "");	/* no "." at all: cut prints the line */
		p++;
	}
	len = strcspn(p, ".");
	out = dmcalloc(1, len + 1);
	if (out)
		memcpy(out, p, len);
	return out;
}

static int set_dd_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *idx = dd_field4(value), *net;

	if (!idx || !*idx)
		return FAULT_9007;
	net = dip_section_of_instance(idx);
	if (!net)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(DD_PKG, DD_SEC(data), "ip_source", "network");
	dmuci_set_value(DD_PKG, DD_SEC(data), "ip_network", net);
	dmuci_set_value(DD_PKG, DD_SEC(data), "interface", net);
	mtk_apply_service_once(DD_RESTART);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDdClientParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_dd_enable, set_dd_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_dd_status, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_dd_alias, set_dd_alias, NULL, NULL},
{"LastError", &DMREAD, DMT_STRING, get_dd_lasterror, NULL, NULL, NULL},
{"Server", &DMWRITE, DMT_STRING, get_dd_server, set_dd_server, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_dd_interface, set_dd_interface, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_dd_username, set_dd_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_dd_password, set_dd_password, NULL, NULL},
{0}
};

static DMLEAF tDdParams[] = {
{"ClientNumberOfEntries", &DMREAD, DMT_UNINT, get_dd_client_count, NULL, NULL, NULL},
{"ServerNumberOfEntries", &DMREAD, DMT_UNINT, get_dd_server_count, NULL, NULL, NULL},
{"SupportedServices", &DMREAD, DMT_STRING, get_dd_supported, NULL, NULL, NULL},
{0}
};

static DMOBJ tDdObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Client", &DMWRITE, add_dd, del_dd, NULL, browse_dd, NULL, NULL, NULL, tDdClientParams, NULL},
{0}
};

static DMOBJ tDdDeviceObj[] = {
{"DynamicDNS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDdObj, tDdParams, NULL},
{0}
};

static DMOBJ tDdRoot[] = {
{"Device", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDdDeviceObj, NULL, NULL},
{0}
};

static const char *const device_ddns_mtk_paths[] = {
	"InternetGatewayDevice.Device.DynamicDNS.",
	NULL
};

static const struct dm_module device_ddns_mtk_module = {
	.name  = "mtk-device-dynamicdns",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tDdRoot,
	.paths = device_ddns_mtk_paths,
};
DM_MODULE_REGISTER(device_ddns_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): this branch is TR-181 already, the
 * product grafted it under InternetGatewayDevice.Device.; the same tables at
 * the root (type A of docs/plan/tr181_mtk_design.md).  References to
 * IP.Interface follow the root (mtk_ipif_prefix()). */
static const char *const device_ddns_mtk_paths181[] = {
	"Device.DynamicDNS.",
	NULL
};

static const struct dm_module device_ddns_mtk_module181 = {
	.name  = "mtk-device-dynamicdns-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tDdDeviceObj,
	.paths = device_ddns_mtk_paths181,
};
DM_MODULE_REGISTER(device_ddns_mtk_module181);
