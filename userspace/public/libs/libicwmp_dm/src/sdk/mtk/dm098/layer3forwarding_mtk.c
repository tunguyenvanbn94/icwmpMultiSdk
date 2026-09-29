/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Layer3Forwarding., 12 parameters: IPv4 static
 *	routes (network.@route[]) and the default WAN.
 *
 *	Ported from functions/tr098/layer3_forwarding.
 *
 *	THE PATHS ARE THE VENDOR'S, NOT TR-098: Enable, ForwardNumberOfEntries
 *	and DefaultConnectionService sit on Layer3Forwarding.Forwarding. itself,
 *	next to its instances Forwarding.{i}. -- TR-098 puts the last two on
 *	Layer3Forwarding.  The ACS has always seen these paths; they are kept,
 *	through DMOBJ.container_leaf (dmtr098.h), since the engine gave the leaves
 *	of a multi-instance object to its instances only.
 *
 *	Instances are the ANONYMOUS "route" sections of network, in file order:
 *	the shell counts `uci show network | grep "@route\[" | grep "=route"`,
 *	and uci show prints a named section by its name, not as @route[n].
 *
 *	WHAT THE TWO WAN-PATH SETTERS REALLY CHECK ON THE PRODUCT (busybox
 *	1.33.1 ash + test, read in shell/ash.c and coreutils/test.c): inside
 *	[[ ]] ash hands "&&" and "||" to test as plain arguments, so
 *
 *	    [[ ! "$node" =~ ^WAN[A-Za-z0-9_]+$ || ! is_integer "$index" ]]
 *
 *	never calls is_integer: test reads the word "is_integer" as a non-empty
 *	string, "$index" is left over, test_main() says "unknown operand" and
 *	exits 2 -- for every value -- so "if" never rejects.  The line after,
 *	"[ "$index" -le 1 ] && [[ ... ] || [ ... ]]", also ends with an operand
 *	left over and never rejects either.  What does decide:
 *
 *	  1. the prefix "InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1.";
 *	  2. awk -F '.' '{ print $(NF-2) "." $(NF-1) }' on the rest (a one
 *	     character FS is literal in busybox awk, a negative field is an
 *	     error and prints nothing): node = field NF-2, index = field NF-1;
 *	  3. wan_if = $((index - 1)), ash arithmetic: 0x and leading-0 octal
 *	     numbers, "08" is an error;
 *	  4. an entry whose id is wan_if and whose conn_type is 0 for
 *	     WANIPConnection, 2 for WANPPPConnection -- or equal to node
 *	     verbatim for any other node.
 *
 *	NOT ESTABLISHED, approximated: ash evaluates a non-numeric index as the
 *	name of a shell variable (recursively).  Here it reads as 0, what an
 *	unset variable gives.
 *
 *	Forwarding.{i}.Interface reads and writes WAN routes through the UCI
 *	package "hniwan" (@wan[], id, conn_type).  That package exists nowhere
 *	in the 2025q3 SDK -- the only other mention is a comment in
 *	hal_unify/src/hal_network.c -- so on the product the getter reads "" for
 *	a WAN route and the setter accepts only InternetGatewayDevice.LANDevice.
 *	Ported literally, so it behaves the same if the package ever appears.
 *
 *	Other quirks kept: Forwarding.Enable reads "1"/"0" (not true/false) and
 *	accepts 1/true/True/TRUE/yes/on and their opposites; Forwarding.{i}.Enable
 *	accepts only "true"/"false"; Status reads "Enable"/"Disable"; Type is
 *	writable and ignores what is written; Dest/Mask/Gateway take any
 *	non-empty string; ForwardingMetric 0..255 as busybox "[" parses numbers
 *	(" 5" and "+5" pass and are stored as written); a new route is disabled
 *	with metric -1, which ForwardingMetric then refuses to write back; the
 *	common Enable lives in network.route4_common.active while AddObject reads
 *	its limit from network.routev4Common.max_rules -- two different sections.
 *
 *	Types: the shell's "xsd:Int" becomes xsd:int, "xsd:IPv4Address" becomes
 *	xsd:string -- the engine has neither spelling, as in phase 4.
 *
 *	Every write queues /usr/sbin/hni_wan_reload.sh (wan_device_network_reload),
 *	the per-route ones only when the route is enabled.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"

#define L3F_WAN_PREFIX	"InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1."
#define L3F_LAN_PATH	"InternetGatewayDevice.LANDevice."
#define L3F_MAX_ROUTES	64

/* ------------------------------------------------------------------ */
/* shell emulation                                                     */
/* ------------------------------------------------------------------ */

/* is_integer of functions/common: optional "-", then digits, not empty */
static int l3f_is_integer(const char *v)
{
	if (*v == '-')
		v++;
	if (!*v)
		return 0;
	for (; *v; v++)
		if (!isdigit((unsigned char)*v))
			return 0;
	return 1;
}

/* $((v)) of busybox ash for one operand: 0x.. hex, 0.. octal, decimal,
 * blanks around.  "" and a name read as 0 (see the header).  Returns -1 on
 * an arithmetic error ("08"), where ash aborts the setter. */
static int l3f_arith(const char *v, long long *out)
{
	char *end;
	const char *p = v;

	while (*p == ' ' || *p == '\t')
		p++;
	if (!*p || isalpha((unsigned char)*p) || *p == '_') {
		*out = 0;
		return 0;
	}
	errno = 0;
	*out = strtoll(p, &end, 0);
	if (end == p || errno)
		return -1;
	while (*end == ' ' || *end == '\t')
		end++;
	return *end ? -1 : 0;
}

/* busybox "[ x -ge a ] && [ x -le b ]": getn() -> strtoll base 10, leading
 * blanks and sign allowed, trailing blanks allowed, anything else an error
 * (the test fails) */
static int l3f_test_range(const char *v, long long lo, long long hi)
{
	char *end;
	long long n;

	errno = 0;
	n = strtoll(v, &end, 10);
	if (end == v || errno)
		return 0;
	while (isspace((unsigned char)*end))
		end++;
	if (*end)
		return 0;
	return n >= lo && n <= hi;
}

/* the sections the shell's "uci show <pkg> | grep ']=<type>'" counts: the
 * anonymous ones; the loop then walks @<type>[0 .. count-1] */
static int l3f_anon_count(char *pkg, char *type)
{
	struct uci_section *s;
	int n = 0;

	uci_foreach_sections(pkg, type, s)
		if (s->anonymous)
			n++;
	return n;
}

static struct uci_section *l3f_nth(char *pkg, char *type, int idx)
{
	struct uci_section *s;
	int i = 0;

	uci_foreach_sections(pkg, type, s)
		if (i++ == idx)
			return s;
	return NULL;
}

static char *l3f_opt(struct uci_section *s, char *option)
{
	char *v = NULL;

	dmuci_get_value_by_section_string(s, option, &v);
	return v ? v : "";
}

/*
 * The common path of set_Forwarding_DefaultConnectionService and
 * set_Forwarding_Interface, as busybox runs it (see the header).  pkg/type
 * are "wan"/"entry" for the first, "hniwan"/"wan" for the second.  Returns
 * the matching section and its "id" into *wan_if, NULL when the shell would
 * have answered 9007.
 */
static struct uci_section *l3f_resolve_wan_path(const char *v, char *pkg, char *type,
						 long long *wan_if)
{
	const char *suffix, *fs[64];
	char buf[256], node[64], index[64], want[16], *p;
	int nf = 0, i, n;
	long long idx;

	if (strncmp(v, L3F_WAN_PREFIX, strlen(L3F_WAN_PREFIX)) != 0)
		return NULL;
	suffix = v + strlen(L3F_WAN_PREFIX);
	if (strlen(suffix) >= sizeof(buf))
		return NULL;
	strcpy(buf, suffix);

	/* awk -F '.': "" has no field, otherwise one per '.', plus one */
	if (buf[0]) {
		fs[nf++] = buf;
		for (p = buf; *p && nf < (int)(sizeof(fs) / sizeof(fs[0])); p++) {
			if (*p == '.') {
				*p = '\0';
				fs[nf++] = p + 1;
			}
		}
	}
	if (nf < 2)
		return NULL;	/* $(-1): awk error, empty node and index */
	if (nf == 2) {
		/* $0 "." $1: node and index are both the first field */
		snprintf(node, sizeof(node), "%s", fs[0]);
		snprintf(index, sizeof(index), "%s", fs[0]);
	} else {
		snprintf(node, sizeof(node), "%s", fs[nf - 3]);
		snprintf(index, sizeof(index), "%s", fs[nf - 2]);
	}

	if (strcmp(node, "WANIPConnection") == 0)
		strcpy(node, "0");
	else if (strcmp(node, "WANPPPConnection") == 0)
		strcpy(node, "2");

	if (l3f_arith(index, &idx) != 0)
		return NULL;
	*wan_if = idx - 1;
	snprintf(want, sizeof(want), "%lld", *wan_if);

	n = l3f_anon_count(pkg, type);
	for (i = 0; i < n; i++) {
		struct uci_section *s = l3f_nth(pkg, type, i);
		const char *mode;

		if (!s || strcmp(l3f_opt(s, "id"), want) != 0)
			continue;
		mode = l3f_opt(s, "conn_type");
		if (!*mode || strcmp(mode, node) != 0)
			return NULL;
		return s;
	}
	return NULL;
}

/* "InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1.<kind>.<id+1>."
 * of the first @<type>[i] satisfying the caller; "" when conn_type is
 * neither 0 nor 2 */
static char *l3f_wan_path(struct uci_section *s)
{
	const char *mode = l3f_opt(s, "conn_type");
	const char *kind;
	char *r = NULL;
	long long id;

	if (strcmp(mode, "0") == 0)
		kind = "WANIPConnection";
	else if (strcmp(mode, "2") == 0)
		kind = "WANPPPConnection";
	else
		return "";
	if (l3f_arith(l3f_opt(s, "id"), &id) != 0)
		return "";	/* ash dies in the command substitution */
	dmasprintf(&r, L3F_WAN_PREFIX "%s.%lld.", kind, id + 1);
	return r ? r : "";
}

/* ------------------------------------------------------------------ */
/* routes                                                              */
/* ------------------------------------------------------------------ */

static int l3f_routes(struct uci_section **list, int max)
{
	struct uci_section *s;
	int n = 0;

	uci_foreach_sections("network", "route", s) {
		if (!s->anonymous)
			continue;
		if (n < max)
			list[n] = s;
		n++;
	}
	return n;
}

static int l3f_route_enabled(struct uci_section *s)
{
	const char *d = l3f_opt(s, "disabled");

	return !*d || strcmp(d, "0") == 0;
}

/* "reload only if the route is enabled" tail of the per-route setters */
static void l3f_reload_if_enabled(struct uci_section *s)
{
	if (l3f_route_enabled(s))
		wan_reload();
}

/* ------------------------------------------------------------------ */
/* Forwarding. (container)                                             */
/* ------------------------------------------------------------------ */

static int get_l3f_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *a = NULL;

	dmuci_get_option_value_string("network", "route4_common", "active", &a);
	*value = (a && strcmp(a, "0") == 0) ? "0" : "1";
	return 0;
}

static int set_l3f_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	static const char *const on[]  = { "1", "true", "True", "TRUE", "yes", "on", NULL };
	static const char *const off[] = { "0", "false", "False", "FALSE", "no", "off", NULL };
	const char *b = NULL;
	int i;

	for (i = 0; on[i]; i++)
		if (strcmp(value, on[i]) == 0)
			b = "1";
	for (i = 0; off[i]; i++)
		if (strcmp(value, off[i]) == 0)
			b = "0";
	if (!b)
		return FAULT_9007;
	if (action == VALUESET) {
		dmuci_set_value("network", "route4_common", "active", (char *)b);
		wan_reload();
	}
	return 0;
}

static int get_l3f_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%d", l3f_routes(NULL, 0));
	return 0;
}

static int get_l3f_default_service(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int n = l3f_anon_count("wan", "entry"), i;

	*value = "";
	for (i = 0; i < n; i++) {
		struct uci_section *s = l3f_nth("wan", "entry", i);

		if (s && strcmp(l3f_opt(s, "default_gw"), "1") == 0) {
			*value = l3f_wan_path(s);
			return 0;
		}
	}
	return 0;
}

static int set_l3f_default_service(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct uci_section *target, *s;
	long long wan_if;

	target = l3f_resolve_wan_path(value, "wan", "entry", &wan_if);
	if (!target)
		return FAULT_9007;
	if (action != VALUESET)
		return 0;
	/* reset every default_gw of the anonymous entries, then set this one */
	uci_foreach_sections("wan", "entry", s)
		if (s->anonymous && strcmp(l3f_opt(s, "default_gw"), "1") == 0)
			dmuci_set_value_by_section(s, "default_gw", "0");
	dmuci_set_value_by_section(target, "default_gw", "1");
	wan_reload();
	return 0;
}

/* static_route_add_rule */
static int add_l3f_route(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	struct uci_section *added = NULL;
	char *added_name = NULL;	/* dmuci_add_section() writes it on every path */
	char *max = NULL, *end;
	long long limit;
	int n;

	dmuci_get_option_value_string("network", "routev4Common", "max_rules", &max);
	if (!max || !l3f_is_integer(max))
		return FAULT_9002;		/* E_INTERNAL_ERROR */
	n = l3f_routes(NULL, 0);
	errno = 0;
	limit = strtoll(max, &end, 10);
	if (errno != ERANGE && n >= limit)	/* out of range: "[" fails, add goes on */
		return FAULT_9004;		/* E_RESOURCES_EXCEEDED */
	dmuci_add_section("network", "route", &added, &added_name);
	if (!added)
		return FAULT_9002;
	dmuci_set_value_by_section(added, "disabled", "1");
	dmuci_set_value_by_section(added, "metric", "-1");
	dmasprintf(instance, "%d", l3f_routes(NULL, 0));
	return 0;
}

/* static_route_delete_entry */
static int del_l3f_route(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	struct uci_section *s = (struct uci_section *)data;

	if (del_action != DEL_INST)
		return FAULT_9005;	/* the shell had no "delete all" here */
	if (!s)
		return FAULT_9002;
	dmuci_delete_by_section(s, NULL, NULL);
	wan_reload();
	return 0;
}

static int browseL3fRouteInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct uci_section *list[L3F_MAX_ROUTES];
	char *idx, *idx_last = NULL;
	int n, i;

	n = l3f_routes(list, L3F_MAX_ROUTES);
	if (n > L3F_MAX_ROUTES)
		n = L3F_MAX_ROUTES;
	for (i = 0; i < n; i++) {
		idx = handle_update_instance(4, dmctx, &idx_last, update_instance_without_section,
					     1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)list[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Forwarding.{i}.                                                     */
/* ------------------------------------------------------------------ */

static int get_rt_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = l3f_route_enabled((struct uci_section *)data) ? "true" : "false";
	return 0;
}

static int set_rt_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct uci_section *s = (struct uci_section *)data;
	int was_unset_metric;

	if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0)
		return FAULT_9007;
	if (action != VALUESET)
		return 0;
	was_unset_metric = strcmp(l3f_opt(s, "metric"), "-1") == 0;
	dmuci_set_value_by_section(s, "disabled", strcmp(value, "true") == 0 ? "0" : "1");
	if (was_unset_metric)
		dmuci_set_value_by_section(s, "metric", "0");
	wan_reload();
	return 0;
}

static int get_rt_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = l3f_route_enabled((struct uci_section *)data) ? "Enable" : "Disable";
	return 0;
}

static int get_rt_static(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

static int get_rt_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct uci_section *s = (struct uci_section *)data;

	if (!*l3f_opt(s, "target"))
		*value = "Default";
	else if (strcmp(l3f_opt(s, "netmask"), "255.255.255.255") == 0)
		*value = "Host";
	else
		*value = "Network";
	return 0;
}

/* set_Forwarding_Type: return 0 */
static int set_rt_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return 0;
}

#define RT_GET_OR(fn, option, def)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char **value)					\
{										\
	char *v = l3f_opt((struct uci_section *)data, option);			\
										\
	*value = *v ? v : def;							\
	return 0;								\
}

RT_GET_OR(get_rt_dest,    "target",  "0.0.0.0")
RT_GET_OR(get_rt_mask,    "netmask", "0.0.0.0")
RT_GET_OR(get_rt_gateway, "gateway", "")
RT_GET_OR(get_rt_metric,  "metric",  "0")

/* set_Forwarding_DestIPAddress / DestSubnetMask / GatewayIPAddress: any
 * non-empty value, reload when the route is enabled */
#define RT_SET_NONEMPTY(fn, option)						\
static int fn(char *refparam, struct dmctx *ctx, void *data,			\
	      char *instance, char *value, int action)				\
{										\
	struct uci_section *s = (struct uci_section *)data;			\
										\
	if (!*value)								\
		return FAULT_9007;						\
	if (action == VALUESET) {						\
		dmuci_set_value_by_section(s, option, value);			\
		l3f_reload_if_enabled(s);					\
	}									\
	return 0;								\
}

RT_SET_NONEMPTY(set_rt_dest,    "target")
RT_SET_NONEMPTY(set_rt_mask,    "netmask")
RT_SET_NONEMPTY(set_rt_gateway, "gateway")

static int set_rt_metric(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct uci_section *s = (struct uci_section *)data;

	if (!*value || !l3f_test_range(value, 0, 255))
		return FAULT_9007;
	if (action == VALUESET) {
		dmuci_set_value_by_section(s, "metric", value);
		l3f_reload_if_enabled(s);
	}
	return 0;
}

/* get_Forwarding_Interface: through package hniwan, see the header */
static int get_rt_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *itf = l3f_opt((struct uci_section *)data, "interface");
	const char *index;
	int n, i;

	*value = "";
	if (!*itf)
		return 0;	/* $E_INTERNAL_ERROR, which the getter's caller ignores */
	if (strcmp(itf, "lan") == 0) {
		*value = L3F_LAN_PATH;
		return 0;
	}
	index = strncmp(itf, "if", 2) == 0 ? itf + 2 : itf;	/* ${interface#if} */
	n = l3f_anon_count("hniwan", "wan");
	for (i = 0; i < n; i++) {
		struct uci_section *w = l3f_nth("hniwan", "wan", i);

		if (w && strcmp(l3f_opt(w, "id"), index) == 0) {
			*value = l3f_wan_path(w);
			return 0;
		}
	}
	return 0;
}

static int set_rt_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct uci_section *s = (struct uci_section *)data;
	long long wan_if;
	char buf[32];

	if (strcmp(value, L3F_LAN_PATH) == 0) {
		if (action == VALUESET) {
			dmuci_set_value_by_section(s, "interface", "lan");
			dmuci_set_value_by_section(s, "onlink", "1");
			l3f_reload_if_enabled(s);
		}
		return 0;
	}
	if (!l3f_resolve_wan_path(value, "hniwan", "wan", &wan_if))
		return FAULT_9007;
	if (action == VALUESET) {
		snprintf(buf, sizeof(buf), "if%lld", wan_if);
		dmuci_set_value_by_section(s, "interface", buf);
		dmuci_set_value_by_section(s, "gateway", "");
		dmuci_set_value_by_section(s, "onlink", "0");
		l3f_reload_if_enabled(s);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tL3fRouteParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_rt_enable, set_rt_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_rt_status, NULL, NULL, NULL},
{"StaticRoute", &DMREAD, DMT_BOOL, get_rt_static, NULL, NULL, NULL},
{"Type", &DMWRITE, DMT_STRING, get_rt_type, set_rt_type, NULL, NULL},
{"DestIPAddress", &DMWRITE, DMT_STRING, get_rt_dest, set_rt_dest, NULL, NULL},
{"DestSubnetMask", &DMWRITE, DMT_STRING, get_rt_mask, set_rt_mask, NULL, NULL},
{"GatewayIPAddress", &DMWRITE, DMT_STRING, get_rt_gateway, set_rt_gateway, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_rt_interface, set_rt_interface, NULL, NULL},
{"ForwardingMetric", &DMWRITE, DMT_INT, get_rt_metric, set_rt_metric, NULL, NULL},
{0}
};

/* the container's own leaves: DMOBJ.container_leaf */
static DMLEAF tL3fForwardingParams[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_l3f_enable, set_l3f_enable, NULL, NULL},
{"ForwardNumberOfEntries", &DMREAD, DMT_INT, get_l3f_count, NULL, NULL, NULL},
{"DefaultConnectionService", &DMWRITE, DMT_STRING, get_l3f_default_service, set_l3f_default_service, NULL, NULL},
{0}
};

static DMOBJ tL3fChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker, container_leaf */
{"Forwarding", &DMWRITE, add_l3f_route, del_l3f_route, NULL, browseL3fRouteInst, NULL, &DMNONE,
 NULL, tL3fRouteParams, NULL, tL3fForwardingParams},
{0}
};

static DMOBJ tL3fMtkObj[] = {
{"Layer3Forwarding", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tL3fChildObj, NULL, NULL},
{0}
};

static const char *const l3f_mtk_paths[] = {
	"InternetGatewayDevice.Layer3Forwarding.",
	NULL
};

static const struct dm_module l3f_mtk_module = {
	.name  = "mtk-layer3forwarding",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tL3fMtkObj,
	.paths = l3f_mtk_paths,
};
DM_MODULE_REGISTER(l3f_mtk_module);
