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
#include <time.h>
#include <json-c/json.h>

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

/* the services, comma separated: SupportedServices, and the TR-181
 * Server.{i} rows */
static char *dd_supported(void)
{
	char out[4096];
	const char *file = NULL;

	if (access("/usr/lib/ddns/services", F_OK) == 0)
		file = "/usr/lib/ddns/services";
	if (access("/etc/ddns/services", F_OK) == 0)
		file = "/etc/ddns/services";
	if (!file || dd_service_lines(file, out, sizeof(out)) < 0)
		return "dyndns.org,no-ip.com";
	return dmstrdup(out);
}

static int get_dd_supported(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dd_supported();
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

/* ------------------------------------------------------------------ */
/* TR-181 (cwmp.cpe.datamodel=tr181, T7 S4d)                           */
/* ------------------------------------------------------------------ */

/*
 * The product's branch is TR-181 already, grafted under
 * InternetGatewayDevice.Device.; in the TR-181 tree the same leaves, but:
 *   Server.{i}            one row per service (dd_servers; without a
 *                         services file the product's two, dyndns.org and
 *                         no-ip.com of hal_service.c), SupportedServices
 *                         their names; address, port and protocol
 *                         from the update URL of ddns-scripts
 *                         (/usr/share/ddns/default/<name>.json, a script of
 *                         /usr/lib/ddns, or the services file); the check /
 *                         retry settings are ddns-scripts' defaults (600 s,
 *                         60 s, endless retries: MaxRetries 4294967295).
 *                         Fixed by the product: set-same.
 *   Client.{i}.Server     a reference to that row (the product: the service
 *                         name), written back as service_name
 *   Client.{i}.Status     Updated once ddns-scripts sent an update
 *                         (<sec>.update), Connecting before, Error when the
 *                         updater does not run (the product's "Up"/"Error")
 *   Client.{i}.LastError  the product's ERROR_* names in the TR-181 enum
 *   Client.{i}.Alias      the TR-181 Alias store (cpe-ddns-<n>)
 *   Client.{i}.Interface  "" while the network section has no number (the
 *                         product: Device.IP.Interface.0)
 *   Client.{i}.Password   secured: reads empty (the product reads it back)
 *   Client.{i}.Hostname.1 the one name the product updates: domain, written
 *                         with lookup_host as hal_service.c does; LastUpdate
 *                         from <sec>.update (the uptime of the update)
 */
#define DD_DEFAULT_DIR	"/usr/share/ddns/default"
#define DD_SCRIPT_DIR	"/usr/lib/ddns"
#define DD_SERVER_MAX	64

struct dd_server {
	char *name;
};

/* The services: the product's file (/etc/ddns/services, else
 * /usr/lib/ddns/services), one per line not starting with "#" -- its name
 * the first quoted string ("dyndns.org"<tab>"url", the ddns-scripts 2.7
 * format) or the first word -- else the product's two.  The product's own
 * SupportedServices takes what comes before the first '"' (the shell's
 * cut -d'"' -f1), which is empty in that format: the TR-181 tree lists the
 * names it parses here, in SupportedServices too. */
static int dd_servers(struct dd_server *out, int max)
{
	const char *file = NULL;
	char line[512], name[128];
	int n = 0;
	FILE *f;

	if (access("/usr/lib/ddns/services", F_OK) == 0)
		file = "/usr/lib/ddns/services";
	if (access("/etc/ddns/services", F_OK) == 0)
		file = "/etc/ddns/services";
	f = file ? fopen(file, "r") : NULL;
	if (!f) {
		if (max >= 2) {
			out[n++].name = "dyndns.org";
			out[n++].name = "no-ip.com";
		}
		return n;
	}
	while (fgets(line, sizeof(line), f) && n < max) {
		if (line[0] == '#')
			continue;
		if (sscanf(line, " \"%127[^\"]\"", name) != 1 && sscanf(line, "%127s", name) != 1)
			continue;
		out[n++].name = dmstrdup(name);
	}
	fclose(f);
	return n;
}

static int get_dd181_supported(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct dd_server *list = dmcalloc(DD_SERVER_MAX, sizeof(*list));
	char out[2048] = "";
	int n, i;

	n = list ? dd_servers(list, DD_SERVER_MAX) : 0;
	for (i = 0; i < n; i++) {
		if (*out)
			strncat(out, ",", sizeof(out) - strlen(out) - 1);
		strncat(out, list[i].name, sizeof(out) - strlen(out) - 1);
	}
	*value = dmstrdup(out);
	return 0;
}

/* the first http:// or https:// URL in a file (an update script) */
static char *dd_url_in_file(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[512], *u, *v = "";

	if (!f)
		return "";
	while (fgets(line, sizeof(line), f)) {
		u = strstr(line, "http://");
		if (!u)
			u = strstr(line, "https://");
		if (u) {
			u[strcspn(u, "\"' \t\n")] = '\0';
			v = dmstrdup(u);
			break;
		}
	}
	fclose(f);
	return v;
}

/* the IPv4 update URL of a service, "" when none is found */
static char *dd_service_url(const char *name)
{
	static const char *const files[] = { "/etc/ddns/services", "/usr/lib/ddns/services" };
	char path[256], line[512], q1[128], q2[384];
	json_object *root, *v4, *url;
	const char *u;
	size_t i;
	FILE *f;

	/* "name"<blanks>"url" lines of the ddns-scripts 2.7 services files */
	for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		f = fopen(files[i], "r");
		if (!f)
			continue;
		while (fgets(line, sizeof(line), f)) {
			if (sscanf(line, " \"%127[^\"]\" \"%383[^\"]\"", q1, q2) == 2 && strcmp(q1, name) == 0) {
				fclose(f);
				return dmstrdup(q2);
			}
		}
		fclose(f);
	}
	/* ddns-scripts 2.8: <name>.json, ipv4.url, a script name or a URL */
	snprintf(path, sizeof(path), DD_DEFAULT_DIR "/%s.json", name);
	root = json_object_from_file(path);
	if (!root)
		return "";
	if (!json_object_object_get_ex(root, "ipv4", &v4) || !json_object_object_get_ex(v4, "url", &url) ||
	    !(u = json_object_get_string(url))) {
		json_object_put(root);
		return "";
	}
	if (strstr(u, "://")) {
		char *r = dmstrdup(u);

		json_object_put(root);
		return r;
	}
	snprintf(path, sizeof(path), DD_SCRIPT_DIR "/%s", u);
	json_object_put(root);
	return dd_url_in_file(path);
}

/* scheme, host and port of [scheme://][user:pass@]host[:port][/path] */
static void dd_url_parts(const char *url, char **proto, char **host, int *port)
{
	const char *h = strstr(url, "://"), *end, *at, *colon;
	int https = strncmp(url, "https://", 8) == 0;

	*proto = https ? "HTTPS" : "HTTP";
	*port = https ? 443 : 80;
	*host = "";
	if (!h)
		return;
	h += 3;
	end = h + strcspn(h, "/?");
	for (at = end; at > h && at[-1] != '@'; at--)
		;
	if (at > h)
		h = at;
	colon = memchr(h, ':', end - h);
	*host = dmcalloc(1, (colon ? colon : end) - h + 1);
	if (*host)
		memcpy(*host, h, (colon ? colon : end) - h);
	if (colon)
		*port = atoi(colon + 1);
}

static int browse_dd181_server(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct dd_server *list = dmcalloc(DD_SERVER_MAX, sizeof(*list));
	int n, i;
	char *inst;

	if (!list)
		return 0;
	n = dd_servers(list, DD_SERVER_MAX);
	for (i = 0; i < n; i++) {
		dmasprintf(&inst, "%d", i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, &list[i], inst) == DM_STOP)
			break;
	}
	return 0;
}

#define DD_SRV(data)	(((struct dd_server *)(data))->name)

static int get_dd181_true(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

static int get_dd181_srv_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(DD_SRV(data));
	return 0;
}

static int get_dd181_srv_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *d;

	dmasprintf(&d, "cpe-ddns-server-%s", instance);
	*value = mtk_alias181_get(refparam, d);
	return 0;
}

static int set_dd181_srv_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *d;

	dmasprintf(&d, "cpe-ddns-server-%s", instance);
	return mtk_alias181_set(refparam, d, value, action);
}

static int get_dd181_srv_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *proto, *host;
	int port;

	dd_url_parts(dd_service_url(DD_SRV(data)), &proto, &host, &port);
	*value = host;
	return 0;
}

static int get_dd181_srv_port(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *proto, *host;
	int port;

	dd_url_parts(dd_service_url(DD_SRV(data)), &proto, &host, &port);
	dmasprintf(value, "%d", port);
	return 0;
}

static int get_dd181_srv_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *host;
	int port;

	dd_url_parts(dd_service_url(DD_SRV(data)), value, &host, &port);
	return 0;
}

/* curl of the product has OpenSSL: ddns-scripts' use_https works */
static int get_dd181_srv_protocols(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "HTTP,HTTPS";
	return 0;
}

static int get_dd181_srv_check(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "600";
	return 0;
}

static int get_dd181_srv_retry(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "60";
	return 0;
}

static int get_dd181_srv_maxretries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "4294967295";
	return 0;
}

MTK_SET_SAME_BOOL(dd181_srv_enable, get_dd181_true)
MTK_SET_SAME(dd181_srv_name, get_dd181_srv_name)
MTK_SET_SAME(dd181_srv_address, get_dd181_srv_address)
MTK_SET_SAME(dd181_srv_port, get_dd181_srv_port)
MTK_SET_SAME(dd181_srv_protocol, get_dd181_srv_protocol)
MTK_SET_SAME(dd181_srv_check, get_dd181_srv_check)
MTK_SET_SAME(dd181_srv_retry, get_dd181_srv_retry)
MTK_SET_SAME(dd181_srv_maxretries, get_dd181_srv_maxretries)

static DMLEAF tDd181ServerParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_dd181_true, set_same_dd181_srv_enable, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_dd181_srv_name, set_same_dd181_srv_name, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_dd181_srv_alias, set_dd181_srv_alias, NULL, NULL},
{"ServiceName", &DMWRITE, DMT_STRING, get_dd181_srv_name, set_same_dd181_srv_name, NULL, NULL},
{"ServerAddress", &DMWRITE, DMT_STRING, get_dd181_srv_address, set_same_dd181_srv_address, NULL, NULL},
{"ServerPort", &DMWRITE, DMT_UNINT, get_dd181_srv_port, set_same_dd181_srv_port, NULL, NULL},
{"SupportedProtocols", &DMREAD, DMT_STRING, get_dd181_srv_protocols, NULL, NULL, NULL},
{"Protocol", &DMWRITE, DMT_STRING, get_dd181_srv_protocol, set_same_dd181_srv_protocol, NULL, NULL},
{"CheckInterval", &DMWRITE, DMT_UNINT, get_dd181_srv_check, set_same_dd181_srv_check, NULL, NULL},
{"RetryInterval", &DMWRITE, DMT_UNINT, get_dd181_srv_retry, set_same_dd181_srv_retry, NULL, NULL},
{"MaxRetries", &DMWRITE, DMT_UNINT, get_dd181_srv_maxretries, set_same_dd181_srv_maxretries, NULL, NULL},
{0}
};

/* Client.{i} -------------------------------------------------------- */

/* ddns-scripts runs for the section: its pid file names a live process */
static int dd_running(const char *sec)
{
	char path[160], *pid;
	struct stat st;

	snprintf(path, sizeof(path), DD_RUN "/%s.pid", sec);
	if (access(path, F_OK) != 0)
		return 0;
	pid = mtk_file_line(path);
	snprintf(path, sizeof(path), "/proc/%s", pid);
	return *pid && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *dd181_status(const char *sec)
{
	char path[160];

	if (strcmp(mtk_uci(DD_PKG, sec, "enabled"), "1") != 0)
		return "Disabled";
	if (!dd_running(sec))
		return "Error";
	snprintf(path, sizeof(path), DD_RUN "/%s.update", sec);
	return access(path, F_OK) == 0 ? "Updated" : "Connecting";
}

static int get_dd181_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dd181_status(DD_SEC(data));
	return 0;
}

static int get_dd181_lasterror(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	static const struct { const char *product, *tr181; } map[] = {
		{ "ERROR_NONE", "NO_ERROR" },
		{ "ERROR_AUTHENTICATION", "AUTHENTICATION_ERROR" },
		{ "ERROR_CONNECT", "CONNECTION_ERROR" },
		{ "ERROR_DNS_RESOLVE", "DNS_ERROR" },
		{ "ERROR_MISCONFIGURED", "MISCONFIGURATION_ERROR" },
	};
	char *v = NULL;
	size_t i;

	get_dd_lasterror(refparam, ctx, data, instance, &v);
	for (i = 0; v && i < sizeof(map) / sizeof(map[0]); i++) {
		if (strcmp(v, map[i].product) == 0) {
			*value = (char *)map[i].tr181;
			return 0;
		}
	}
	/* ERROR_UNKNOWN: <what the server answered> */
	*value = "PROTOCOL_ERROR";
	return 0;
}

static int get_dd181_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *d;

	dmasprintf(&d, "cpe-ddns-%s", instance);
	*value = mtk_alias181_get(refparam, d);
	return 0;
}

static int set_dd181_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *d;

	dmasprintf(&d, "cpe-ddns-%s", instance);
	return mtk_alias181_set(refparam, d, value, action);
}

/* Device.DynamicDNS.Server.<n> of the client's service_name, "" if none */
static int get_dd181_server(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct dd_server *list = dmcalloc(DD_SERVER_MAX, sizeof(*list));
	char *svc = mtk_uci(DD_PKG, DD_SEC(data), "service_name");
	int n, i;

	*value = "";
	if (!list)
		return 0;
	n = dd_servers(list, DD_SERVER_MAX);
	for (i = 0; i < n; i++) {
		if (strcmp(list[i].name, svc) == 0) {
			dmasprintf(value, "Device.DynamicDNS.Server.%d", i + 1);
			break;
		}
	}
	return 0;
}

static int set_dd181_server(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct dd_server *list = dmcalloc(DD_SERVER_MAX, sizeof(*list));
	int n, idx;
	char tail;

	if (!list || !value || sscanf(value, "Device.DynamicDNS.Server.%d%c", &idx, &tail) != 1)
		return FAULT_9007;
	n = dd_servers(list, DD_SERVER_MAX);
	if (idx < 1 || idx > n)
		return FAULT_9007;
	return set_dd_server(refparam, ctx, data, instance, list[idx - 1].name, action);
}

static int get_dd181_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	size_t l;

	get_dd_interface(refparam, ctx, data, instance, value);
	l = strlen(*value);
	if (l > 2 && strcmp(*value + l - 2, ".0") == 0)
		*value = "";
	return 0;
}

/* Password is secured in TR-181: it reads empty (the product reads it back) */
static int get_dd181_secured(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int get_dd181_one(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "1";
	return 0;
}

/* Client.{i}.Hostname.1 ------------------------------------------- */

static int browse_dd181_hostname(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	DM_LINK_INST_OBJ(dmctx, parent_node, prev_data, "1");
	return 0;
}

static int get_dd181_host_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *st = dd181_status(DD_SEC(data));

	if (strcmp(st, "Updated") == 0)
		*value = "Registered";
	else if (strcmp(st, "Connecting") == 0)
		*value = "Updating";
	else
		*value = st;	/* Disabled, Error */
	return 0;
}

static int get_dd181_host_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(DD_PKG, DD_SEC(data), "domain");
	return 0;
}

/* the domain and lookup_host, as hal_service.c writes them (1..256, the
 * length X_AIS_DDNS.DomainName takes) */
static int set_dd181_host_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	size_t l = value ? strlen(value) : 0;

	if (l < 1 || l > 256)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(DD_PKG, DD_SEC(data), "domain", value);
	dmuci_set_value(DD_PKG, DD_SEC(data), "lookup_host", value);
	mtk_apply_service_once(DD_RESTART);
	return 0;
}

/* <sec>.update holds the uptime of the last update sent */
static int get_dd181_host_lastupdate(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char path[160], buf[32];
	double up = 0, at;
	time_t t;
	struct tm tm;
	FILE *f;

	*value = "0001-01-01T00:00:00Z";
	snprintf(path, sizeof(path), DD_RUN "/%s.update", DD_SEC(data));
	if (sscanf(mtk_file_line(path), "%lf", &at) != 1)
		return 0;
	f = fopen("/proc/uptime", "r");
	if (!f)
		return 0;
	if (fscanf(f, "%lf", &up) != 1 || up < at) {
		fclose(f);
		return 0;
	}
	fclose(f);
	t = time(NULL) - (time_t)(up - at);
	if (gmtime_r(&t, &tm) && strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm))
		*value = dmstrdup(buf);
	return 0;
}

static int get_dd181_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_dd_enable(refparam, ctx, data, instance, value);
}

MTK_SET_SAME_BOOL(dd181_host_enable, get_dd181_enable)

static DMLEAF tDd181HostnameParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_dd181_enable, set_same_dd181_host_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_dd181_host_status, NULL, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_dd181_host_name, set_dd181_host_name, NULL, NULL},
{"LastUpdate", &DMREAD, DMT_TIME, get_dd181_host_lastupdate, NULL, NULL, NULL},
{0}
};

static DMOBJ tDd181ClientObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Hostname", &DMREAD, NULL, NULL, NULL, browse_dd181_hostname, NULL, NULL, NULL, tDd181HostnameParams, NULL},
{0}
};

static DMLEAF tDd181ClientParams[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_dd_enable, set_dd_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_dd181_status, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_dd181_alias, set_dd181_alias, NULL, NULL},
{"LastError", &DMREAD, DMT_STRING, get_dd181_lasterror, NULL, NULL, NULL},
{"Server", &DMWRITE, DMT_STRING, get_dd181_server, set_dd181_server, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_dd181_interface, set_dd_interface, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_dd_username, set_dd_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_dd181_secured, set_dd_password, NULL, NULL},
{"HostnameNumberOfEntries", &DMREAD, DMT_UNINT, get_dd181_one, NULL, NULL, NULL},
{0}
};

static int get_dd181_server_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct dd_server *list = dmcalloc(DD_SERVER_MAX, sizeof(*list));

	*value = dd_num(list ? dd_servers(list, DD_SERVER_MAX) : 0);
	return 0;
}

static DMLEAF tDd181Params[] = {
{"ClientNumberOfEntries", &DMREAD, DMT_UNINT, get_dd_client_count, NULL, NULL, NULL},
{"ServerNumberOfEntries", &DMREAD, DMT_UNINT, get_dd181_server_count, NULL, NULL, NULL},
{"SupportedServices", &DMREAD, DMT_STRING, get_dd181_supported, NULL, NULL, NULL},
{0}
};

static DMOBJ tDd181Obj[] = {
{"Client", &DMWRITE, add_dd, del_dd, NULL, browse_dd, NULL, NULL, tDd181ClientObj, tDd181ClientParams, NULL},
{"Server", &DMREAD, NULL, NULL, NULL, browse_dd181_server, NULL, NULL, NULL, tDd181ServerParams, NULL},
{0}
};

static DMOBJ tDdDevice181Obj[] = {
{"DynamicDNS", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDd181Obj, tDd181Params, NULL},
{0}
};

/* References to IP.Interface follow the root (mtk_ipif_prefix()). */
static const char *const device_ddns_mtk_paths181[] = {
	"Device.DynamicDNS.",
	NULL
};

static const struct dm_module device_ddns_mtk_module181 = {
	.name  = "mtk-device-dynamicdns-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tDdDevice181Obj,
	.paths = device_ddns_mtk_paths181,
};
DM_MODULE_REGISTER(device_ddns_mtk_module181);
