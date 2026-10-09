/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Device.IP. -- the TR-181 IP branch the product
 *	grafted into its TR-098 tree, ported from functions/tr098/device_ip and
 *	functions/tr098/ip_ipv4v6.  The odd path is what the ACS provisions
 *	against and is kept as it is.  Diagnostics.TraceRoute is in
 *	device_traceroute_mtk.c.
 *
 *	Globals (ip_ipv4v6):
 *	  IPv4Capable/IPv4Enable "true", IPv4Status "Enabled" -- constants
 *	  IPv6Capable  /proc/net/if_inet6 exists
 *	  IPv6Enable   true when any wan section has v6_active "1"; a set writes
 *	               v6_active of every anonymous wan "entry" and queues
 *	               hni_wan_reload.sh (nothing at all when there is none)
 *	  IPv6Status   Enabled/Disabled after IPv6Enable
 *
 *	Interface.{i} is every network "interface" section but "loopback", in
 *	the order of the config, numbered by network.<sec>.ip_int_instance and
 *	listed by that number.  A section without one gets the first free number
 *	the moment it is seen -- on a GET too -- and it is committed at once,
 *	as the shell did (mtk_uci_set_persist), so the numbers the ACS has seen
 *	never move.
 *	  Name         the section name
 *	  Enable       network.<sec>.auto != "0"; a set writes auto and, for a
 *	               WAN section (if<n>, if<n>_6), wan.@entry[<n>].active, then
 *	               queues hni_wan_reload.sh
 *	  IPv4Enable   network.<sec>.ipv4 != "0"; a set writes ipv4 and
 *	               wan.@entry[<n>].v4_active, adds/removes pppd_options
 *	               "noip" for pppoe, and restarts the interface (a static
 *	               one switched off only flushes its IPv4 addresses/routes)
 *	  IPv6Enable   network.<sec>.ipv6 != "0"; ipv6, v6_active, ifdown/ifup
 *	  Status, LastChange, IPv4/IPv6AddressNumberOfEntries,
 *	  IPv6PrefixNumberOfEntries   "ubus call network.interface.<sec> status"
 *	  LowerLayers  network.<sec>.device without a leading "@"
 *	  Stats.*      "network.device status" of that device, sysfs for the
 *	               counters ubus does not have, unicast = total - multicast
 *	               - broadcast (0 when negative)
 *	AddObject adds network.if<n> (first free name) as a static interface
 *	with auto 0 and the next free number; DeleteObject removes the section.
 *
 *	Differences from the shell, on purpose:
 *	  - the interface restarts (ifdown/ifup, the flush) are queued for the
 *	    end of the session.  The shell ran them inside the setter, with a
 *	    "sleep 1", and could take down the WAN the session was running on;
 *	  - the counters are xsd:string on the wire, as they were through the
 *	    shell bridge: the engine has no xsd:unsignedLong (analysis 17.5).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <json-c/json.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dmmem.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "device_ip_mtk.h"
#include "wanconn_mtk.h"
#include "stack181_mtk.h"
#include "ipv6_181_mtk.h"

#define IP_PKG		"network"
#define WAN_RELOAD	"/usr/sbin/hni_wan_reload.sh"

struct dip_iface {
	char *sec;	/* section name, "@interface[n]" for an anonymous one */
	char *inst;	/* its Interface.{i} */
};

/* ------------------------------------------------------------------ */
/* numbering                                                           */
/* ------------------------------------------------------------------ */

static struct uci_package *dip_pkg(const char *name)
{
	struct uci_ptr ptr = {0};

	if (dmuci_lookup_ptr(uci_ctx, &ptr, (char *)name, NULL, NULL, NULL) || !ptr.p)
		return NULL;
	return ptr.p;
}

/* ip_list_used_instances: every network section's ip_int_instance with its
 * non-digits removed; ip_get_next_free_instance: the first number from 1 up
 * that is not among them */
static int dip_next_free(void)
{
	struct uci_package *p = dip_pkg(IP_PKG);
	struct uci_element *e;
	unsigned char used[256] = {0};
	int i;

	if (p) {
		uci_foreach_element(&p->sections, e) {
			char *v = NULL, digits[16];
			size_t n = 0;
			const char *c;

			dmuci_get_value_by_section_string(uci_to_section(e), "ip_int_instance", &v);
			for (c = v ? v : ""; *c && n < sizeof(digits) - 1; c++) {
				if (isdigit((unsigned char)*c))
					digits[n++] = *c;
			}
			digits[n] = '\0';
			if (n && atoi(digits) < (int)sizeof(used))
				used[atoi(digits)] = 1;
		}
	}
	for (i = 1; i < (int)sizeof(used) && used[i]; i++)
		;
	return i;
}

char *dip_instance_of(const char *nsec)
{
	return mtk_uci(IP_PKG, nsec, "ip_int_instance");
}

char *dip_update_instance(const char *nsec)
{
	char *v = dip_instance_of(nsec), *t = NULL, buf[16];

	if (*v)
		return v;
	dmuci_get_section_type(IP_PKG, (char *)nsec, &t);
	if (!t || !*t)
		return "";
	snprintf(buf, sizeof(buf), "%d", dip_next_free());
	mtk_uci_set_persist(IP_PKG, nsec, "ip_int_instance", buf);
	return dmstrdup(buf);
}

char *dip_section_of_instance(const char *inst)
{
	struct uci_package *p = dip_pkg(IP_PKG);
	struct uci_element *e;

	if (!p || !inst || !*inst)
		return NULL;
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		char *v = NULL;

		dmuci_get_value_by_section_string(s, "ip_int_instance", &v);
		if (v && strcmp(v, inst) == 0)
			return section_name(s);
	}
	return NULL;
}

/* _ip_list_sections: "network.<name>=interface" of "uci show", loopback out.
 * number: give the sections without one their number (the browse did, the
 * InterfaceNumberOfEntries count did not).  An anonymous section is
 * "@interface[<i>]", <i> counting every interface section as uci does. */
static int dip_list(struct dip_iface *list, int max, int number)
{
	struct uci_package *p = dip_pkg(IP_PKG);
	struct uci_element *e;
	int n = 0, anon = 0;

	if (!p)
		return 0;
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		char *sec;

		if (strcmp(s->type, "interface") != 0)
			continue;
		if (s->anonymous)
			dmasprintf(&sec, "@interface[%d]", anon);
		else
			sec = dmstrdup(section_name(s));
		anon++;
		if (!sec || strcmp(sec, "loopback") == 0 || n >= max)
			continue;
		list[n].sec = sec;
		list[n].inst = number ? dip_update_instance(sec) : dip_instance_of(sec);
		n++;
	}
	return n;
}

/* sort -n -k1,1 of "<inst> <sec>": numerically, then the whole line */
static int dip_cmp(const void *a, const void *b)
{
	const struct dip_iface *x = a, *y = b;
	long nx = strtol(x->inst, NULL, 10), ny = strtol(y->inst, NULL, 10);
	int c;

	if (nx != ny)
		return nx < ny ? -1 : 1;
	c = strcmp(x->inst, y->inst);
	return c ? c : strcmp(x->sec, y->sec);
}

#define DIP_MAX	64

static int browse_dip(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct dip_iface *list = dmcalloc(DIP_MAX, sizeof(*list));
	int n, i;

	if (!list)
		return 0;
	n = dip_list(list, DIP_MAX, 1);
	qsort(list, n, sizeof(*list), dip_cmp);
	for (i = 0; i < n; i++) {
		if (!*list[i].inst)
			continue;
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&list[i], list[i].inst) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* AddObject / DeleteObject                                            */
/* ------------------------------------------------------------------ */

static int add_dip(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	char name[16], inst[16];
	char *t;
	int n;

	/* _ip_generate_new_ifname: the first if<n> no section is named */
	for (n = 0; n < 10000; n++) {
		snprintf(name, sizeof(name), "if%d", n);
		t = NULL;
		dmuci_get_section_type(IP_PKG, name, &t);
		if (!t || !*t)
			break;
	}
	if (n == 10000)
		return FAULT_9004;
	dmuci_set_value(IP_PKG, name, "", "interface");
	t = NULL;
	dmuci_get_section_type(IP_PKG, name, &t);
	if (!t || !*t)
		return FAULT_9002;
	dmuci_set_value(IP_PKG, name, "proto", "static");
	dmuci_set_value(IP_PKG, name, "auto", "0");
	snprintf(inst, sizeof(inst), "%d", dip_next_free());
	dmuci_set_value(IP_PKG, name, "ip_int_instance", inst);
	*instance = dmstrdup(inst);
	return 0;
}

static int del_dip(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	struct dip_iface *f = data;

	if (del_action != DEL_INST)
		return FAULT_9005;	/* the shell had no "delete all" */
	if (!f)
		return FAULT_9002;
	dmuci_delete(IP_PKG, f->sec, NULL, NULL);
	return 0;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static json_object *dip_status(const char *sec)
{
	json_object *res = NULL;
	char obj[96];

	snprintf(obj, sizeof(obj), "network.interface.%s", sec);
	dmubus_call(obj, "status", UBUS_ARGS{}, 0, &res);
	return res;
}

/* how many entries of <array> (of <outer>[*].<inner> when inner is set)
 * have an "address", excluding those that start with skip */
static int dip_count(json_object *res, const char *array, const char *inner, const char *skip)
{
	json_object *arr, *it, *sub, *addr;
	int i, n = 0;

	if (!res || !json_object_object_get_ex(res, array, &arr) ||
	    !json_object_is_type(arr, json_type_array))
		return 0;
	for (i = 0; i < (int)json_object_array_length(arr); i++) {
		const char *a;

		it = json_object_array_get_idx(arr, i);
		if (inner) {
			if (!json_object_object_get_ex(it, inner, &sub))
				continue;
			it = sub;
		}
		if (!json_object_object_get_ex(it, "address", &addr))
			continue;
		a = json_object_get_string(addr);
		if (skip && a && strncmp(a, skip, strlen(skip)) == 0)
			continue;
		n++;
	}
	return n;
}

static char *dip_num(long long v)
{
	char *s = NULL;

	dmasprintf(&s, "%lld", v);
	return s ? s : "0";
}

/* "if<n>" or "if<n>_6": the WAN connection <n>, -1 otherwise */
static int dip_wan_index(const char *sec)
{
	const char *p;

	if (strncmp(sec, "if", 2) != 0 || !isdigit((unsigned char)sec[2]))
		return -1;
	for (p = sec + 2; isdigit((unsigned char)*p); p++)
		;
	if (*p && strcmp(p, "_6") != 0)
		return -1;
	return atoi(sec + 2);
}

static void dip_wan_entry(const char *sec, const char *option, const char *value)
{
	char entry[32];
	int idx = dip_wan_index(sec);

	if (idx < 0)
		return;
	snprintf(entry, sizeof(entry), "@entry[%d]", idx);
	dmuci_set_value("wan", entry, (char *)option, (char *)value);
}

/* a word the queued shell line can carry inside single quotes */
static int dip_quotable(const char *v)
{
	return v && *v && !strchr(v, '\'') && !strchr(v, '\n');
}

static void dip_queue_restart(const char *sec, int pause)
{
	char cmd[160];

	if (!dip_quotable(sec))
		return;
	snprintf(cmd, sizeof(cmd), pause ? "ifdown '%s'; sleep 1; ifup '%s'" : "ifdown '%s'; ifup '%s'", sec, sec);
	mtk_apply_service_once(cmd);
}

/* ------------------------------------------------------------------ */
/* globals                                                             */
/* ------------------------------------------------------------------ */

static int get_dip_true(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

static int get_dip_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Enabled";
	return 0;
}

static int get_dip_v6_capable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = access("/proc/net/if_inet6", F_OK) == 0 ? "true" : "false";
	return 0;
}

/* any "wan.<section>.v6_active" of "uci show wan" equal to 1 */
static int dip_v6_enabled(void)
{
	struct uci_package *p = dip_pkg("wan");
	struct uci_element *e;

	if (!p)
		return 0;
	uci_foreach_element(&p->sections, e) {
		char *v = NULL;

		dmuci_get_value_by_section_string(uci_to_section(e), "v6_active", &v);
		if (v && strcmp(v, "1") == 0)
			return 1;
	}
	return 0;
}

static int get_dip_v6_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_v6_enabled() ? "true" : "false";
	return 0;
}

static int set_dip_v6_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct uci_package *p;
	struct uci_element *e;
	const char *target;
	int any = 0;

	if (action == VALUECHECK)
		return 0;
	/* case 0|false|False|no|off -> 0, anything else 1 */
	target = (!strcmp(value, "0") || !strcmp(value, "false") || !strcmp(value, "False") ||
		  !strcmp(value, "no") || !strcmp(value, "off")) ? "0" : "1";
	p = dip_pkg("wan");
	if (!p)
		return 0;
	/* "wan.@entry[<i>]=entry" lines: the anonymous entry sections */
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);

		if (!s->anonymous || strcmp(s->type, "entry") != 0)
			continue;
		dmuci_set_value_by_section(s, "v6_active", (char *)target);
		any = 1;
	}
	if (any)
		mtk_apply_service_once(WAN_RELOAD);
	return 0;
}

static int get_dip_v6_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_v6_enabled() ? "Enabled" : "Disabled";
	return 0;
}

static int get_dip_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct dip_iface *list = dmcalloc(DIP_MAX, sizeof(*list));

	*value = dip_num(list ? dip_list(list, DIP_MAX, 0) : 0);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Interface.{i}                                                       */
/* ------------------------------------------------------------------ */

#define DIP_SEC(data)	(((struct dip_iface *)(data))->sec)

static int get_dip_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = DIP_SEC(data);
	return 0;
}

/* network.<sec>.<option> != "0" */
static char *dip_not_zero(void *data, const char *option)
{
	return strcmp(mtk_uci(IP_PKG, DIP_SEC(data), option), "0") == 0 ? "0" : "1";
}

static int get_dip_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_not_zero(data, "auto");
	return 0;
}

static int set_dip_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(IP_PKG, DIP_SEC(data), "auto", b ? "1" : "0");
	dip_wan_entry(DIP_SEC(data), "active", b ? "1" : "0");
	mtk_apply_service_once(WAN_RELOAD);
	return 0;
}

static int get_dip_v4(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_not_zero(data, "ipv4");
	return 0;
}

static int set_dip_v4(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *sec = DIP_SEC(data);
	char *proto, *dev;
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(IP_PKG, (char *)sec, "ipv4", b ? "1" : "0");
	dip_wan_entry(sec, "v4_active", b ? "1" : "0");
	proto = mtk_uci(IP_PKG, sec, "proto");
	if (strcmp(proto, "pppoe") == 0) {
		if (b)
			dmuci_delete(IP_PKG, (char *)sec, "pppd_options", NULL);
		else
			dmuci_set_value(IP_PKG, (char *)sec, "pppd_options", "noip");
		dip_queue_restart(sec, 1);
	} else if (strcmp(proto, "static") == 0 && !b) {
		char cmd[200];

		dev = mtk_uci(IP_PKG, sec, "device");
		if (!*dev)
			dev = (char *)sec;
		if (dip_quotable(dev)) {
			snprintf(cmd, sizeof(cmd), "ip -4 addr flush dev '%s' 2>/dev/null; ip -4 route flush dev '%s' 2>/dev/null",
				 dev, dev);
			mtk_apply_service_once(cmd);
		}
	} else {
		dip_queue_restart(sec, 1);	/* dhcp, static on, anything else */
	}
	return 0;
}

static int get_dip_v6(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_not_zero(data, "ipv6");
	return 0;
}

static int set_dip_v6(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(IP_PKG, DIP_SEC(data), "ipv6", b ? "1" : "0");
	dip_wan_entry(DIP_SEC(data), "v6_active", b ? "1" : "0");
	dip_queue_restart(DIP_SEC(data), 0);
	return 0;
}

static int get_dip_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = dip_status(DIP_SEC(data));

	*value = (res && strcmp(dmjson_get_value(res, 1, "up"), "true") == 0) ? "Up" : "Down";
	return 0;
}

static int get_dip_lastchange(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = dip_status(DIP_SEC(data));
	char *v = res ? dmjson_get_value(res, 1, "uptime") : "";

	*value = (v && *v) ? v : "0";
	return 0;
}

/* ip_get_lowerlayers: network.<sec>.device, a leading "@" dropped */
static char *dip_device(void *data)
{
	char *dev = mtk_uci(IP_PKG, DIP_SEC(data), "device");

	return *dev == '@' ? dev + 1 : dev;
}

static int get_dip_lowerlayers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_device(data);
	return 0;
}

static int get_dip_v4_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_num(dip_count(dip_status(DIP_SEC(data)), "ipv4-address", NULL, NULL));
	return 0;
}

/* global and ULA addresses (not fe80:) plus the local addresses of the
 * prefix assignments */
static int get_dip_v6_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = dip_status(DIP_SEC(data));

	*value = dip_num(dip_count(res, "ipv6-address", NULL, "fe80:") +
			 dip_count(res, "ipv6-prefix-assignment", "local-address", NULL));
	return 0;
}

static int get_dip_prefix_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = dip_status(DIP_SEC(data));

	*value = dip_num(dip_count(res, "ipv6-prefix", NULL, NULL) +
			 dip_count(res, "ipv6-prefix-assignment", NULL, NULL));
	return 0;
}

/* ------------------------------------------------------------------ */
/* Interface.{i}.Stats                                                 */
/* ------------------------------------------------------------------ */

/* ip_stats_get_ubus: statistics.<stat> of "network.device status" */
static long long dip_stat_ubus(const char *dev, const char *stat)
{
	json_object *res = NULL;
	char *v;

	if (!*dev)
		return 0;
	dmubus_call("network.device", "status", UBUS_ARGS{{"name", (char *)dev, String}}, 1, &res);
	if (!res)
		return 0;
	v = dmjson_get_value(res, 2, "statistics", stat);
	return (v && *v) ? strtoll(v, NULL, 10) : 0;
}

/* ip_stats_get_sys: /sys/class/net/<dev>/statistics/<stat>, 0 when absent */
static long long dip_stat_sys(const char *dev, const char *stat)
{
	char path[160], *v;

	if (!*dev)
		return 0;
	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", dev, stat);
	v = mtk_file_line(path);
	return *v ? strtoll(v, NULL, 10) : 0;
}

#define DIP_STAT(name, how, stat)						\
static int get_dip_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	*value = dip_num(dip_stat_##how(dip_device(data), stat));		\
	return 0;								\
}

DIP_STAT(bytes_sent, ubus, "tx_bytes")
DIP_STAT(bytes_received, ubus, "rx_bytes")
DIP_STAT(packets_sent, ubus, "tx_packets")
DIP_STAT(packets_received, ubus, "rx_packets")
DIP_STAT(errors_sent, ubus, "tx_errors")
DIP_STAT(errors_received, ubus, "rx_errors")
DIP_STAT(discard_sent, ubus, "tx_dropped")
DIP_STAT(discard_received, ubus, "rx_dropped")
DIP_STAT(multicast_sent, sys, "tx_multicast")
DIP_STAT(multicast_received, ubus, "multicast")
DIP_STAT(broadcast_sent, sys, "tx_broadcast")
DIP_STAT(broadcast_received, sys, "rx_broadcast")
DIP_STAT(unknown_proto, sys, "rx_nohandler")

static int get_dip_unicast_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *dev = dip_device(data);
	long long uni = dip_stat_ubus(dev, "tx_packets") - dip_stat_sys(dev, "tx_multicast") -
			dip_stat_sys(dev, "tx_broadcast");

	*value = dip_num(uni >= 0 ? uni : 0);
	return 0;
}

static int get_dip_unicast_received(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	const char *dev = dip_device(data);
	long long uni = dip_stat_ubus(dev, "rx_packets") - dip_stat_ubus(dev, "multicast") -
			dip_stat_sys(dev, "rx_broadcast");

	*value = dip_num(uni >= 0 ? uni : 0);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tDipStatsParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"BytesSent", &DMREAD, DMT_STRING, get_dip_bytes_sent, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_STRING, get_dip_bytes_received, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_STRING, get_dip_packets_sent, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_STRING, get_dip_packets_received, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_dip_errors_sent, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_dip_errors_received, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, get_dip_discard_sent, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, get_dip_discard_received, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_STRING, get_dip_multicast_sent, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_STRING, get_dip_multicast_received, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_STRING, get_dip_broadcast_sent, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_STRING, get_dip_broadcast_received, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_STRING, get_dip_unicast_sent, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_STRING, get_dip_unicast_received, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_dip_unknown_proto, NULL, NULL, NULL},
{0}
};

static DMOBJ tDipInterfaceChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDipStatsParams, NULL},
{0}
};

static DMLEAF tDipInterfaceParams[] = {
{"Name", &DMREAD, DMT_STRING, get_dip_name, NULL, NULL, NULL},
{"Enable", &DMWRITE, DMT_BOOL, get_dip_enable, set_dip_enable, NULL, NULL},
{"IPv4Enable", &DMWRITE, DMT_BOOL, get_dip_v4, set_dip_v4, NULL, NULL},
{"IPv6Enable", &DMWRITE, DMT_BOOL, get_dip_v6, set_dip_v6, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_dip_status, NULL, NULL, NULL},
{"LastChange", &DMREAD, DMT_UNINT, get_dip_lastchange, NULL, NULL, NULL},
{"LowerLayers", &DMREAD, DMT_STRING, get_dip_lowerlayers, NULL, NULL, NULL},
{"IPv4AddressNumberOfEntries", &DMREAD, DMT_UNINT, get_dip_v4_count, NULL, NULL, NULL},
{"IPv6AddressNumberOfEntries", &DMREAD, DMT_UNINT, get_dip_v6_count, NULL, NULL, NULL},
{"IPv6PrefixNumberOfEntries", &DMREAD, DMT_UNINT, get_dip_prefix_count, NULL, NULL, NULL},
{0}
};

static DMLEAF tDipParams[] = {
{"InterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_dip_count, NULL, NULL, NULL},
{"IPv4Capable", &DMREAD, DMT_BOOL, get_dip_true, NULL, NULL, NULL},
{"IPv4Enable", &DMREAD, DMT_BOOL, get_dip_true, NULL, NULL, NULL},
{"IPv4Status", &DMREAD, DMT_STRING, get_dip_enabled, NULL, NULL, NULL},
{"IPv6Capable", &DMREAD, DMT_BOOL, get_dip_v6_capable, NULL, NULL, NULL},
{"IPv6Enable", &DMWRITE, DMT_BOOL, get_dip_v6_enable, set_dip_v6_enable, NULL, NULL},
{"IPv6Status", &DMREAD, DMT_STRING, get_dip_v6_status, NULL, NULL, NULL},
{0}
};

static DMOBJ tDipObj[] = {
{"Interface", &DMWRITE, add_dip, del_dip, NULL, browse_dip, NULL, NULL, tDipInterfaceChildObj, tDipInterfaceParams, NULL},
{0}
};

static DMOBJ tDeviceIpObj[] = {
{"IP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDipObj, tDipParams, NULL},
{0}
};

static DMOBJ tDeviceIpRoot[] = {
{"Device", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDeviceIpObj, NULL, NULL},
{0}
};

static const char *const device_ip_mtk_paths[] = {
	"InternetGatewayDevice.Device.IP.Interface.",
	"InternetGatewayDevice.Device.IP.InterfaceNumberOfEntries",
	"InternetGatewayDevice.Device.IP.IPv4Capable",
	"InternetGatewayDevice.Device.IP.IPv4Enable",
	"InternetGatewayDevice.Device.IP.IPv4Status",
	"InternetGatewayDevice.Device.IP.IPv6Capable",
	"InternetGatewayDevice.Device.IP.IPv6Enable",
	"InternetGatewayDevice.Device.IP.IPv6Status",
	NULL
};

static const struct dm_module device_ip_mtk_module = {
	.name  = "mtk-device-ip",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tDeviceIpRoot,
	.paths = device_ip_mtk_paths,
};
DM_MODULE_REGISTER(device_ip_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): this branch is TR-181 already, the
 * product grafted it under InternetGatewayDevice.Device.; the same tables at
 * the root (type A of docs/plan/tr181_mtk_design.md).  References to
 * IP.Interface follow the root (mtk_ipif_prefix()). */
static const char *const device_ip_mtk_paths181[] = {
	"Device.IP.Interface.",
	"Device.IP.InterfaceNumberOfEntries",
	"Device.IP.IPv4Capable",
	"Device.IP.IPv4Enable",
	"Device.IP.IPv4Status",
	"Device.IP.IPv6Capable",
	"Device.IP.IPv6Enable",
	"Device.IP.IPv6Status",
	NULL
};

/*
 * TR-181 leaves of Interface.{i} the TR-098 branch does not have, from the
 * WAN connection whose network section this interface is (wanip_mtk.c,
 * wan181_get/set): Alias (cpe-internet ...), MaxMTUSize.  Empty, and not
 * writable, on an interface that carries no WAN connection.
 */
const char *dip_section(void *data)
{
	return data ? DIP_SEC(data) : "";
}

/*
 * The Interface of a TR-181 diagnostic (TraceRoute, Download, Upload,
 * NSLookup): the product stores a layer 3 device name ("ifconfig $val"),
 * TR-181 a Device.IP.Interface reference.  The device of an interface is
 * netifd's l3_device, else network.<sec>.device without a leading "@" (what
 * LowerLayers reads).
 */
static char *dip_netdev(const char *sec)
{
	json_object *res = dip_status(sec);
	char *v = res ? dmjson_get_value(res, 1, "l3_device") : "";
	char *dev;

	if (v && *v)
		return v;
	dev = mtk_uci(IP_PKG, sec, "device");
	return *dev == '@' ? dev + 1 : dev;
}

char *dip_netdev_of_ref(const char *ref)
{
	static const char pfx[] = "Device.IP.Interface.";
	char inst[16];
	const char *p;
	size_t n = 0;
	char *sec;

	if (!ref || strncmp(ref, pfx, sizeof(pfx) - 1) != 0)
		return NULL;
	for (p = ref + sizeof(pfx) - 1; *p >= '0' && *p <= '9' && n < sizeof(inst) - 1; p++)
		inst[n++] = *p;
	inst[n] = '\0';
	/* "<n>" or "<n>." only */
	if (!n || (*p && strcmp(p, ".") != 0))
		return "";
	sec = dip_section_of_instance(inst);
	return sec ? dip_netdev(sec) : "";
}

/* every numbered Interface.{i}: its network section and number, in instance
 * order, at most max; how many */
int dip_all(const char **sec, const char **inst, int max)
{
	struct dip_iface list[DIP_MAX];
	int i, n, m = 0;

	n = dip_list(list, DIP_MAX, 0);
	qsort(list, n, sizeof(list[0]), dip_cmp);
	for (i = 0; i < n && m < max; i++) {
		if (!*list[i].inst)
			continue;
		sec[m] = list[i].sec;
		inst[m] = list[i].inst;
		m++;
	}
	return m;
}

char *dip_ref_of_netdev(const char *dev)
{
	struct dip_iface list[DIP_MAX];
	int i, n;
	char *r;

	if (!dev || !*dev)
		return NULL;
	n = dip_list(list, DIP_MAX, 0);
	/* the lowest Interface.{i} first: if<id> before its if<id>_6 alias,
	 * both on the same layer 3 device */
	qsort(list, n, sizeof(list[0]), dip_cmp);
	for (i = 0; i < n; i++) {
		if (!*list[i].inst || strcmp(dip_netdev(list[i].sec), dev) != 0)
			continue;
		dmasprintf(&r, "Device.IP.Interface.%s", list[i].inst);
		return r;
	}
	return NULL;
}

static struct wan_entry *dip_wan(void *data)
{
	return wan181_of_ipif(data);
}

static int get_dip181_wan(void *data, const char *leaf, char **value)
{
	struct wan_entry *e = dip_wan(data);

	*value = "";
	return e ? wan181_get(e, leaf, value) : 0;
}

/* TR-181 Alias (T7 S3): what an ACS set (dmmtk.h mtk_alias181_*), else the
 * CPE's own "cpe-<network section>", unique in the table -- the product's
 * connection alias (cpe-internet, cpe-tr069 ... from service_type) can be
 * the same on two connections, and the LAN had none */
static int get_dip181_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *dflt;

	dmasprintf(&dflt, "cpe-%s", DIP_SEC(data));
	*value = mtk_alias181_get(refparam, dflt);
	return 0;
}

static int set_dip181_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *dflt;

	dmasprintf(&dflt, "cpe-%s", DIP_SEC(data));
	return mtk_alias181_set(refparam, dflt, value, action);
}

static int get_dip181_mtu(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_dip181_wan(data, "MaxMTUSize", value);
}

static int set_dip181_mtu(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = dip_wan(data);

	return e ? wan181_set(e, "MaxMTUSize", value, action) : FAULT_9008;
}

/* TR-181 Stats: the counters are xsd:unsignedLong there (the product's
 * grafted branch says xsd:string); copies of tDipStatsParams and
 * tDipInterfaceChildObj, keep in step (T7 S2) */
static DMLEAF tDip181StatsParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"BytesSent", &DMREAD, DMT_UNLONG, get_dip_bytes_sent, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNLONG, get_dip_bytes_received, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNLONG, get_dip_packets_sent, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNLONG, get_dip_packets_received, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_dip_errors_sent, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_dip_errors_received, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, get_dip_discard_sent, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, get_dip_discard_received, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_UNLONG, get_dip_multicast_sent, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_UNLONG, get_dip_multicast_received, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_UNLONG, get_dip_broadcast_sent, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_UNLONG, get_dip_broadcast_received, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_UNLONG, get_dip_unicast_sent, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_UNLONG, get_dip_unicast_received, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_dip_unknown_proto, NULL, NULL, NULL},
{0}
};

static DMOBJ tDip181InterfaceChildObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tDip181StatsParams, NULL},
{0}
};

/* T7 S3: standard readWrite leaves the product cannot change (dmmtk.h MTK_SET_SAME) */
/* TR-181: the reference of the layer under the interface (stack181_mtk.c),
 * not the product's device name */
static int get_dip181_lowerlayers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = stack181_ipif_lower(DIP_SEC(data));
	return 0;
}

MTK_SET_SAME(dip181_lowerlayers, get_dip181_lowerlayers)

/* T7 S4c: the IPv6Address / IPv6Prefix rows of ipv6_181_mtk.c (the same
 * netifd arrays as get_dip_v6_count / get_dip_prefix_count) */
static int get_dip181_v6_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_num(ipv6181_count(DIP_SEC(data), 0));
	return 0;
}

static int get_dip181_prefix_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_num(ipv6181_count(DIP_SEC(data), 1));
	return 0;
}

/* T7 S4b: the IPv4Address rows lan_mtk.c (browseLanIpv4Inst) gives the
 * interface -- one on the LAN and on a routed connection, configured even
 * while down -- not the addresses netifd reports up right now */
static int get_dip181_v4_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry e;

	if (strcmp(DIP_SEC(data), "lan") == 0)
		*value = "1";
	else
		*value = (wan_entry_of_sec(DIP_SEC(data), &e) && !e.bridge) ? "1" : "0";
	return 0;
}

static DMLEAF tDip181InterfaceParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Name", &DMREAD, DMT_STRING, get_dip_name, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_dip181_alias, set_dip181_alias, NULL, NULL},
{"Enable", &DMWRITE, DMT_BOOL, get_dip_enable, set_dip_enable, NULL, NULL},
{"IPv4Enable", &DMWRITE, DMT_BOOL, get_dip_v4, set_dip_v4, NULL, NULL},
{"IPv6Enable", &DMWRITE, DMT_BOOL, get_dip_v6, set_dip_v6, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_dip_status, NULL, NULL, NULL},
{"LastChange", &DMREAD, DMT_UNINT, get_dip_lastchange, NULL, NULL, NULL},
{"LowerLayers", &DMWRITE, DMT_STRING, get_dip181_lowerlayers, set_same_dip181_lowerlayers, NULL, NULL},
{"MaxMTUSize", &DMWRITE, DMT_UNINT, get_dip181_mtu, set_dip181_mtu, NULL, NULL},
{"IPv4AddressNumberOfEntries", &DMREAD, DMT_UNINT, get_dip181_v4_count, NULL, NULL, NULL},
{"IPv6AddressNumberOfEntries", &DMREAD, DMT_UNINT, get_dip181_v6_count, NULL, NULL, NULL},
{"IPv6PrefixNumberOfEntries", &DMREAD, DMT_UNINT, get_dip181_prefix_count, NULL, NULL, NULL},
{0}
};

static DMOBJ tDip181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Interface", &DMWRITE, add_dip, del_dip, NULL, browse_dip, NULL, NULL, tDip181InterfaceChildObj, tDip181InterfaceParams, NULL},
{0}
};

/* TR-181 IP.IPv4Enable is readWrite: IPv4 is always on here, true is taken,
 * false is 9007 (T7 S3); a copy of tDipParams, keep in step */
MTK_SET_SAME_BOOL(ipv4_enable, get_dip_true)

static DMLEAF tDip181Params[] = {
{"InterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_dip_count, NULL, NULL, NULL},
{"IPv4Capable", &DMREAD, DMT_BOOL, get_dip_true, NULL, NULL, NULL},
{"IPv4Enable", &DMWRITE, DMT_BOOL, get_dip_true, set_same_ipv4_enable, NULL, NULL},
{"IPv4Status", &DMREAD, DMT_STRING, get_dip_enabled, NULL, NULL, NULL},
{"IPv6Capable", &DMREAD, DMT_BOOL, get_dip_v6_capable, NULL, NULL, NULL},
{"IPv6Enable", &DMWRITE, DMT_BOOL, get_dip_v6_enable, set_dip_v6_enable, NULL, NULL},
{"IPv6Status", &DMREAD, DMT_STRING, get_dip_v6_status, NULL, NULL, NULL},
{0}
};

static DMOBJ tDeviceIp181Obj[] = {
{"IP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tDip181Obj, tDip181Params, NULL},
{0}
};

static const struct dm_module device_ip_mtk_module181 = {
	.name  = "mtk-device-ip-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tDeviceIp181Obj,
	.paths = device_ip_mtk_paths181,
};
DM_MODULE_REGISTER(device_ip_mtk_module181);
