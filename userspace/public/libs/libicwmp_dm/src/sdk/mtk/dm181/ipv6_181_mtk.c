/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-181 Device.IP.Interface.{i}.IPv6Address.{i} and IPv6Prefix.{i} of
 *	the MTK / Airoha product (docs/plan/tr181_mtk_design.md T7 S4c): the
 *	rows of netifd's "network.interface.<section> status", the same arrays
 *	IPv6AddressNumberOfEntries / IPv6PrefixNumberOfEntries count
 *	(ipv6181_count, used by device_ip_mtk.c):
 *	  IPv6Address  "ipv6-address" but link-local (fe80:), then the CPE's own
 *	               address of each "ipv6-prefix-assignment" (local-address)
 *	  IPv6Prefix   "ipv6-prefix" (delegated to the CPE), then
 *	               "ipv6-prefix-assignment" (given out on this interface)
 *	numbered 1..n in that order.  The addresses and prefixes are learnt
 *	(odhcp6c, odhcpd) or derived; the product has no static IPv6 of its
 *	own, so every writable leaf takes only the value it has (dmmtk.h
 *	MTK_SET_SAME) and the tables have no AddObject/DeleteObject.
 *
 *	Origin: an assignment's local address is AutoConfigured (the CPE built
 *	it from a delegated prefix), any other address Static on a static
 *	interface, else DHCPv6 -- netifd does not tell an IA_NA address from a
 *	SLAAC one.  A delegated prefix is PrefixDelegation (Static on a static
 *	interface), an assignment Child, its ParentPrefix the delegated prefix
 *	of another interface that holds it.  Lifetimes: netifd gives seconds
 *	left and leaves them out when infinite (9999-12-31T23:59:59Z).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <json-c/json.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmubus.h"
#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "device_ip_mtk.h"
#include "ipv6_181_mtk.h"

#define V6_MAX		16
#define V6_INFINITE	-1LL

struct v6row {
	const char *sec;	/* network section of the interface */
	char addr[64];		/* address or prefix, no length */
	int len;		/* prefix length */
	long long pref;		/* seconds left, V6_INFINITE when not given */
	long long valid;
	int assign;		/* from ipv6-prefix-assignment */
	int is_static;		/* the interface is proto static */
};

static long long v6_secs(json_object *o, const char *key)
{
	json_object *v;

	if (!o || !json_object_object_get_ex(o, key, &v))
		return V6_INFINITE;
	return json_object_get_int64(v);
}

static int v6_add(struct v6row *out, int n, int max, const char *sec, json_object *o,
		  json_object *timing, int assign, int is_static)
{
	json_object *a, *m;
	const char *s;

	if (n >= max || !o || !json_object_object_get_ex(o, "address", &a))
		return n;
	s = json_object_get_string(a);
	if (!s || !*s)
		return n;
	memset(&out[n], 0, sizeof(out[n]));
	out[n].sec = sec;
	snprintf(out[n].addr, sizeof(out[n].addr), "%s", s);
	out[n].len = json_object_object_get_ex(o, "mask", &m) ? json_object_get_int(m) : 128;
	out[n].pref = v6_secs(timing, "preferred");
	out[n].valid = v6_secs(timing, "valid");
	out[n].assign = assign;
	out[n].is_static = is_static;
	return n + 1;
}

/* the rows of one interface: addresses, or (prefixes) its prefixes */
static int v6_rows(const char *sec, int prefixes, struct v6row *out, int max)
{
	json_object *res = NULL, *arr, *it, *loc, *proto;
	char obj[96];
	int i, n = 0, is_static;

	if (!sec || !*sec)
		return 0;
	snprintf(obj, sizeof(obj), "network.interface.%s", sec);
	dmubus_call(obj, "status", UBUS_ARGS{}, 0, &res);
	if (!res)
		return 0;
	is_static = json_object_object_get_ex(res, "proto", &proto) &&
		    strcmp(json_object_get_string(proto), "static") == 0;
	if (json_object_object_get_ex(res, prefixes ? "ipv6-prefix" : "ipv6-address", &arr) &&
	    json_object_is_type(arr, json_type_array)) {
		for (i = 0; i < (int)json_object_array_length(arr); i++) {
			json_object *a;

			it = json_object_array_get_idx(arr, i);
			/* the same rule as device_ip_mtk.c's count: no link-local */
			if (!prefixes && json_object_object_get_ex(it, "address", &a) &&
			    strncmp(json_object_get_string(a), "fe80:", 5) == 0)
				continue;
			n = v6_add(out, n, max, sec, it, it, 0, is_static);
		}
	}
	if (json_object_object_get_ex(res, "ipv6-prefix-assignment", &arr) &&
	    json_object_is_type(arr, json_type_array)) {
		for (i = 0; i < (int)json_object_array_length(arr); i++) {
			it = json_object_array_get_idx(arr, i);
			if (prefixes)
				n = v6_add(out, n, max, sec, it, it, 1, is_static);
			else if (json_object_object_get_ex(it, "local-address", &loc))
				n = v6_add(out, n, max, sec, loc, it, 1, is_static);
		}
	}
	return n;
}

int ipv6181_count(const char *sec, int prefixes)
{
	struct v6row *r = dmcalloc(V6_MAX, sizeof(*r));

	return r ? v6_rows(sec, prefixes, r, V6_MAX) : 0;
}

char *ipv6181_child_refs(const char *sec, const char *ipif_inst)
{
	struct v6row *r = dmcalloc(V6_MAX, sizeof(*r));
	char out[512] = "", one[96];
	int n, i;

	if (!r || !ipif_inst || !*ipif_inst)
		return "";
	n = v6_rows(sec, 1, r, V6_MAX);
	for (i = 0; i < n; i++) {
		if (!r[i].assign)
			continue;
		snprintf(one, sizeof(one), "%sDevice.IP.Interface.%s.IPv6Prefix.%d", *out ? "," : "", ipif_inst, i + 1);
		strncat(out, one, sizeof(out) - strlen(out) - 1);
	}
	return dmstrdup(out);
}

/* <addr> lies in <pfx>/<len> */
static int v6_in(const char *addr, const char *pfx, int len)
{
	struct in6_addr a, p;
	int i;

	if (len < 0 || len > 128 || inet_pton(AF_INET6, addr, &a) != 1 || inet_pton(AF_INET6, pfx, &p) != 1)
		return 0;
	for (i = 0; i < len; i++) {
		int byte = i / 8, bit = 7 - i % 8;

		if (((a.s6_addr[byte] >> bit) & 1) != ((p.s6_addr[byte] >> bit) & 1))
			return 0;
	}
	return 1;
}

/* "Device.IP.Interface.<n>." of the row's interface: the leaf's own path
 * up to the table name */
static char *v6_ipif(const char *refparam)
{
	char *s = refparam ? dmstrdup(refparam) : NULL;
	char *t = s ? strstr(s, ".IPv6") : NULL;

	if (!t)
		return "";
	t[1] = '\0';
	return s;
}

static char *v6_time(long long secs)
{
	char buf[32];
	time_t t;
	struct tm tm;

	if (secs == V6_INFINITE)
		return "9999-12-31T23:59:59Z";
	t = time(NULL) + (secs > 0 ? secs : 0);
	if (!gmtime_r(&t, &tm) || !strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm))
		return "0001-01-01T00:00:00Z";
	return dmstrdup(buf);
}

static char *v6_status(struct v6row *r)
{
	if (r->pref == V6_INFINITE || r->pref > 0)
		return "Preferred";
	if (r->valid == V6_INFINITE || r->valid > 0)
		return "Deprecated";
	return "Invalid";
}

#define ROW(data)	((struct v6row *)(data))

static int browse_v6(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, int prefixes)
{
	struct v6row *r = dmcalloc(V6_MAX, sizeof(*r));
	int n, i;
	char *inst;

	if (!r || !prev_data)
		return 0;
	n = v6_rows(dip_section(prev_data), prefixes, r, V6_MAX);
	for (i = 0; i < n; i++) {
		dmasprintf(&inst, "%d", i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, &r[i], inst) == DM_STOP)
			break;
	}
	return 0;
}

static int browse_v6addr181(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	return browse_v6(dmctx, parent_node, prev_data, 0);
}

static int browse_v6pfx181(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	return browse_v6(dmctx, parent_node, prev_data, 1);
}

/* ------------------------------------------------------------------ */
/* leaves of both tables                                               */
/* ------------------------------------------------------------------ */

static int get_v6_true(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

static int get_v6_false(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

static int get_v6_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Enabled";
	return 0;
}

static int get_v6_rowstatus(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = v6_status(ROW(data));
	return 0;
}

static int get_v6_pref(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = v6_time(ROW(data)->pref);
	return 0;
}

static int get_v6_valid(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = v6_time(ROW(data)->valid);
	return 0;
}

static int get_v6_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *d;

	dmasprintf(&d, "cpe-%s-%s", strstr(refparam, ".IPv6Prefix.") ? "v6prefix" : "v6addr", instance);
	*value = mtk_alias181_get(refparam, d);
	return 0;
}

static int set_v6_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *d;

	dmasprintf(&d, "cpe-%s-%s", strstr(refparam, ".IPv6Prefix.") ? "v6prefix" : "v6addr", instance);
	return mtk_alias181_set(refparam, d, value, action);
}

/* ------------------------------------------------------------------ */
/* IPv6Address                                                         */
/* ------------------------------------------------------------------ */

static int get_v6a_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(ROW(data)->addr);
	return 0;
}

static int get_v6a_origin(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	if (ROW(data)->assign)
		*value = "AutoConfigured";
	else
		*value = ROW(data)->is_static ? "Static" : "DHCPv6";
	return 0;
}

/* the IPv6Prefix row of the same interface that holds the address */
static int get_v6a_prefix(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct v6row *p = dmcalloc(V6_MAX, sizeof(*p));
	int n, i;

	*value = "";
	if (!p)
		return 0;
	n = v6_rows(ROW(data)->sec, 1, p, V6_MAX);
	for (i = 0; i < n; i++) {
		if (v6_in(ROW(data)->addr, p[i].addr, p[i].len)) {
			dmasprintf(value, "%sIPv6Prefix.%d", v6_ipif(refparam), i + 1);
			break;
		}
	}
	return 0;
}

MTK_SET_SAME_BOOL(v6_enable, get_v6_true)
MTK_SET_SAME_BOOL(v6_anycast, get_v6_false)
MTK_SET_SAME(v6a_ip, get_v6a_ip)
MTK_SET_SAME(v6a_prefix, get_v6a_prefix)
MTK_SET_SAME(v6_pref, get_v6_pref)
MTK_SET_SAME(v6_valid, get_v6_valid)

static DMLEAF tV6Addr181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_v6_true, set_same_v6_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_v6_enabled, NULL, NULL, NULL},
{"IPAddressStatus", &DMREAD, DMT_STRING, get_v6_rowstatus, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_v6_alias, set_v6_alias, NULL, NULL},
{"IPAddress", &DMWRITE, DMT_STRING, get_v6a_ip, set_same_v6a_ip, NULL, NULL},
{"Origin", &DMREAD, DMT_STRING, get_v6a_origin, NULL, NULL, NULL},
{"Prefix", &DMWRITE, DMT_STRING, get_v6a_prefix, set_same_v6a_prefix, NULL, NULL},
{"PreferredLifetime", &DMWRITE, DMT_TIME, get_v6_pref, set_same_v6_pref, NULL, NULL},
{"ValidLifetime", &DMWRITE, DMT_TIME, get_v6_valid, set_same_v6_valid, NULL, NULL},
{"Anycast", &DMWRITE, DMT_BOOL, get_v6_false, set_same_v6_anycast, NULL, NULL},
{0}
};

/* ------------------------------------------------------------------ */
/* IPv6Prefix                                                          */
/* ------------------------------------------------------------------ */

static int get_v6p_prefix(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%s/%d", ROW(data)->addr, ROW(data)->len);
	return 0;
}

static int get_v6p_origin(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	if (ROW(data)->assign)
		*value = "Child";
	else
		*value = ROW(data)->is_static ? "Static" : "PrefixDelegation";
	return 0;
}

static int get_v6p_statictype(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (!ROW(data)->assign && ROW(data)->is_static) ? "Static" : "Inapplicable";
	return 0;
}

/* an assignment: the delegated prefix of another interface that holds it */
static int get_v6p_parent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *secs[64], *insts[64];
	struct v6row *p = dmcalloc(V6_MAX, sizeof(*p));
	int c, i, j, n;

	*value = "";
	if (!ROW(data)->assign || !p)
		return 0;
	c = dip_all(secs, insts, 64);
	for (i = 0; i < c; i++) {
		if (strcmp(secs[i], ROW(data)->sec) == 0)
			continue;
		n = v6_rows(secs[i], 1, p, V6_MAX);
		for (j = 0; j < n; j++) {
			if (!p[j].assign && p[j].len <= ROW(data)->len &&
			    v6_in(ROW(data)->addr, p[j].addr, p[j].len)) {
				dmasprintf(value, "Device.IP.Interface.%s.IPv6Prefix.%d", insts[i], j + 1);
				return 0;
			}
		}
	}
	return 0;
}

static int get_v6_empty(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

/* given out on the LAN: on-link, and autonomous unless odhcpd's SLAAC is
 * off (dhcp.<section>.ra_slaac 0); a delegated prefix is neither */
static int get_v6p_onlink(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ROW(data)->assign ? "true" : "false";
	return 0;
}

static int get_v6p_autonomous(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (ROW(data)->assign && strcmp(mtk_uci("dhcp", ROW(data)->sec, "ra_slaac"), "0") != 0) ? "true" : "false";
	return 0;
}

MTK_SET_SAME(v6p_prefix, get_v6p_prefix)
MTK_SET_SAME(v6p_statictype, get_v6p_statictype)
MTK_SET_SAME(v6p_parent, get_v6p_parent)
MTK_SET_SAME(v6p_childbits, get_v6_empty)
MTK_SET_SAME_BOOL(v6p_onlink, get_v6p_onlink)
MTK_SET_SAME_BOOL(v6p_autonomous, get_v6p_autonomous)

static DMLEAF tV6Prefix181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_v6_true, set_same_v6_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_v6_enabled, NULL, NULL, NULL},
{"PrefixStatus", &DMREAD, DMT_STRING, get_v6_rowstatus, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_v6_alias, set_v6_alias, NULL, NULL},
{"Prefix", &DMWRITE, DMT_STRING, get_v6p_prefix, set_same_v6p_prefix, NULL, NULL},
{"Origin", &DMREAD, DMT_STRING, get_v6p_origin, NULL, NULL, NULL},
{"StaticType", &DMWRITE, DMT_STRING, get_v6p_statictype, set_same_v6p_statictype, NULL, NULL},
{"ParentPrefix", &DMWRITE, DMT_STRING, get_v6p_parent, set_same_v6p_parent, NULL, NULL},
{"ChildPrefixBits", &DMWRITE, DMT_STRING, get_v6_empty, set_same_v6p_childbits, NULL, NULL},
{"OnLink", &DMWRITE, DMT_BOOL, get_v6p_onlink, set_same_v6p_onlink, NULL, NULL},
{"Autonomous", &DMWRITE, DMT_BOOL, get_v6p_autonomous, set_same_v6p_autonomous, NULL, NULL},
{"PreferredLifetime", &DMWRITE, DMT_TIME, get_v6_pref, set_same_v6_pref, NULL, NULL},
{"ValidLifetime", &DMWRITE, DMT_TIME, get_v6_valid, set_same_v6_valid, NULL, NULL},
{0}
};

static DMOBJ tV6IpIf181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"IPv6Address", &DMREAD, NULL, NULL, NULL, browse_v6addr181, NULL, NULL, NULL, tV6Addr181Params, NULL},
{"IPv6Prefix", &DMREAD, NULL, NULL, NULL, browse_v6pfx181, NULL, NULL, NULL, tV6Prefix181Params, NULL},
{0}
};

/* browseinstobj left NULL: device_ip_mtk.c makes the Interface instances */
static DMOBJ tV6Ip181Obj[] = {
{"Interface", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tV6IpIf181Obj, NULL, NULL},
{0}
};

static DMOBJ tV6Root181[] = {
{"IP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tV6Ip181Obj, NULL, NULL},
{0}
};

/* Device.IP. is device_ip_mtk.c's claim: the two tables join its Interface
 * by the merge (no .paths), the way lan_mtk.c adds IPv4Address */
static const struct dm_module ipv6_181_mtk_module = {
	.name  = "mtk-ipv6-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tV6Root181,
};
DM_MODULE_REGISTER(ipv6_181_mtk_module);
