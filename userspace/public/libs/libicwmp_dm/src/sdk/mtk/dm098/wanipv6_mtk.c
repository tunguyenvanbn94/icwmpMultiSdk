/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The IPv6 leaves of both WAN connection objects:
 *	  WANIPConnection.{i}.X_AIS_IPv6.*  + WANIPConnection.{i}.X_AIS_IPv6*
 *	  WANPPPConnection.{i}.X_AIS_IPv6.* + WANPPPConnection.{i}.X_AIS_IPv6*
 *
 *	Ported from functions/tr098/wan_device: the wan_device_v6_* helpers for
 *	the X_AIS_IPv6 subtree and the wan_device_{get,set}_x_ais_ipv6_* ones for
 *	the flat leaves, plus json_get_gw() and is_ipv6_public() of
 *	functions/common/common.
 *
 *	Two shapes for one feature, and they are NOT the same set:
 *
 *	  - the "X_AIS_IPv6." subtree is identical on both objects: 13 leaves,
 *	    AddressingType / IPAddress / PrefixLength / DefaultGateway /
 *	    ConnectionStatus / Pd.{Enable,Address,PrefixLength,PLtime,VLtime} /
 *	    ManualDNS / DNSServers / Uptime,
 *	  - the flat "X_AIS_IPv6<Name>" leaves differ.  WANIPConnection has 12
 *	    of them and calls the status one X_AIS_IPv6ConnStatus;
 *	    WANPPPConnection has 8 and calls it X_AIS_IPv6ConnectionStatus.  The
 *	    PPP object has no GatewayType / GatewayAddress / DNSType /
 *	    PrefixDelegationType / GUAFromPrefixEnable at all.  That asymmetry is
 *	    the product's, kept exactly: an ACS template written against one
 *	    object must not suddenly find new parameters on the other.
 *
 *	Where the two shapes overlap they are NOT aliases of each other:
 *
 *	  X_AIS_IPv6.AddressingType   reads wan.@entry[i].v6_mode alone,
 *	  X_AIS_IPv6AddressingType    reads v6_active first and answers "None"
 *	                              when IPv6 is off, v6_mode otherwise,
 *	  X_AIS_IPv6.Pd.Enable and X_AIS_IPv6PdEnable are the same option but
 *	  only the subtree one is refused in Static mode by the same function --
 *	  they happen to share an implementation in the shell too.
 *
 *	The setters keep the product's guard rails, which are the part an ACS
 *	will actually hit:
 *
 *	  - IPAddress, PrefixLength, DefaultGateway and ExternalAddress are
 *	    refused (9001) unless v6_mode is Static,
 *	  - Pd.Enable is refused in Static mode -- the opposite direction,
 *	  - DNSServers is refused while v6_static_dns is 0,
 *	  - ManualDNS cannot be turned off while the mode is Static (9007).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>

#include "dmuci.h"
#include "dmubus.h"
#include "dmcommon.h"
#include "dmjson.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"

#define V6_MODE_SLAAC	"0"
#define V6_MODE_DHCP	"1"
#define V6_MODE_STATIC	"2"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static struct wan_entry *v6_entry(void *data)
{
	return (struct wan_entry *)data;
}

/* every getter below works on the IPv6 interface of the entry, if<id>_6 */
static const char *v6_iface(void *data)
{
	struct wan_entry *e = v6_entry(data);

	return (e && e->if6[0]) ? e->if6 : "";
}

static char *v6_status_str(void *data, const char *key)
{
	json_object *res = wan_iface_status(v6_iface(data));

	return res ? dmjson_get_value(res, 1, (char *)key) : "";
}

/* $["<array>"][0].<key> */
static char *v6_array0(void *data, const char *array, const char *key)
{
	json_object *res = wan_iface_status(v6_iface(data)), *a;

	if (!res)
		return "";
	a = dmjson_select_obj_in_array_idx(res, 0, 1, (char *)array);
	return a ? dmjson_get_value(a, 1, (char *)key) : "";
}

/*
 * The fallback of wan_device_v6_get_ipaddr(): when ubus reports no
 * ipv6-address, the shell ran
 *   ip -6 addr show dev <l3_device> | grep inet6 | awk '{print $2}' | head -1
 * and split it on '/'.  Same source without spawning a process:
 * /proc/net/if_inet6 is <32 hex> <ifindex> <prefixlen hex> <scope> <flags> <dev>.
 */
static int v6_proc_first(const char *dev, char *addr, size_t alen, long *plen)
{
	char line[160], hex[40], name[40];
	unsigned char raw[16];
	unsigned idx, pfx, scope, flags;
	FILE *fp;
	int i;

	if (!dev || !dev[0])
		return 0;
	fp = fopen("/proc/net/if_inet6", "r");
	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "%39s %x %x %x %x %39s",
			   hex, &idx, &pfx, &scope, &flags, name) != 6)
			continue;
		if (strcmp(name, dev) != 0 || strlen(hex) != 32)
			continue;
		for (i = 0; i < 16; i++) {
			char b[3] = { hex[i * 2], hex[i * 2 + 1], 0 };

			raw[i] = (unsigned char)strtol(b, NULL, 16);
		}
		if (!inet_ntop(AF_INET6, raw, addr, alen))
			continue;
		if (plen)
			*plen = (long)pfx;
		fclose(fp);
		return 1;
	}
	fclose(fp);
	return 0;
}

/* "up" is false or absent -> the shell answered empty without looking further */
static int v6_is_up(void *data)
{
	char *up = v6_status_str(data, "up");

	return (up && strcmp(up, "true") == 0);
}

/*
 * json_get_gw(): the nexthop of the "::" route.  network.<iface>.defaultroute
 * set to "0" moves the routes under "inactive", which is where the shell
 * looked in that case.
 */
static char *v6_gateway(void *data)
{
	json_object *res = wan_iface_status(v6_iface(data)), *r;
	const char *iface = v6_iface(data);
	char *array = "route";
	int i;

	if (!res)
		return "";
	if (strcmp(mtk_uci("network", iface, "defaultroute"), "0") == 0)
		array = "inactive";
	for (i = 0; i < 16; i++) {
		char *target;

		if (strcmp(array, "inactive") == 0) {
			json_object *inact = dmjson_get_obj(res, 1, "inactive");

			if (!inact)
				return "";
			r = dmjson_select_obj_in_array_idx(inact, i, 1, "route");
		} else {
			r = dmjson_select_obj_in_array_idx(res, i, 1, "route");
		}
		if (!r)
			break;
		target = dmjson_get_value(r, 1, "target");
		if (target && strcmp(target, "::") == 0)
			return dmjson_get_value(r, 1, "nexthop");
	}
	return "";
}

/* is_ipv6_public(): not ::1, not fe80::/10, and it has to look like IPv6 */
static int v6_is_public(const char *ip)
{
	if (!ip || !ip[0] || !strchr(ip, ':'))
		return 0;
	if (strcmp(ip, "::1") == 0)
		return 0;
	if (strncasecmp(ip, "fe80:", 5) == 0)
		return 0;
	return 1;
}

static char *v6_mode(void *data)
{
	return wan_entry_opt(data, "v6_mode");
}

static int v6_is_static(void *data)
{
	return strcmp(v6_mode(data), V6_MODE_STATIC) == 0;
}

/* wan.@entry[i] + network.if<id>_6, then the reload -- the shell's pair */
static void v6_write_pair(struct wan_entry *e, const char *eopt, const char *evalue,
			  const char *nopt, const char *nvalue)
{
	if (eopt)
		dmuci_set_value_by_section(e->s, (char *)eopt, (char *)evalue);
	if (nopt && e->if6[0])
		dmuci_set_value("network", e->if6, (char *)nopt, (char *)nvalue);
	wan_reload();
}

/* ------------------------------------------------------------------ */
/* X_AIS_IPv6. -- the subtree                                          */
/* ------------------------------------------------------------------ */

static int get_v6_address_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *m = v6_mode(data);

	if (strcmp(m, V6_MODE_DHCP) == 0)
		*value = "DHCP";
	else if (strcmp(m, V6_MODE_STATIC) == 0)
		*value = "Static";
	else
		*value = "SLAAC";	/* 0 and anything unset */
	return 0;
}

static const char *v6_mode_of(const char *name)
{
	if (strcmp(name, "SLAAC") == 0)
		return V6_MODE_SLAAC;
	if (strcmp(name, "DHCP") == 0)
		return V6_MODE_DHCP;
	if (strcmp(name, "Static") == 0)
		return V6_MODE_STATIC;
	return NULL;
}

static int set_v6_address_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	const char *m = value ? v6_mode_of(value) : NULL;

	if (!e)
		return FAULT_9002;
	if (!m)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "v6_mode", m);
}

static int get_v6_ipaddr(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char addr[64];

	*value = "";
	if (!v6_is_up(data))
		return 0;
	*value = v6_array0(data, "ipv6-address", "address");
	if ((*value)[0])
		return 0;
	if (v6_proc_first(wan_iface_l3_device(v6_iface(data)), addr, sizeof(addr), NULL))
		*value = dmstrdup(addr);
	return 0;
}

/* Static only; the address and the mask go out together as ip6addr */
static int set_v6_ipaddr(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	char buf[80];

	if (!e)
		return FAULT_9002;
	if (!v6_is_static(data))
		return FAULT_9001;	/* E_REQUEST_DENIED */
	if (action == VALUECHECK)
		return 0;
	snprintf(buf, sizeof(buf), "%s/%s", value ? value : "",
		 wan_entry_opt(data, "v6_mask"));
	v6_write_pair(e, "v6_ip", value ? value : "", "ip6addr", buf);
	return 0;
}

static int get_v6_prefix_len(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char addr[64];
	char buf[16];
	long plen = 0;

	*value = "0";
	if (!v6_is_up(data))
		return 0;
	*value = v6_array0(data, "ipv6-address", "mask");
	if ((*value)[0])
		return 0;
	if (v6_proc_first(wan_iface_l3_device(v6_iface(data)), addr, sizeof(addr), &plen)) {
		snprintf(buf, sizeof(buf), "%ld", plen);
		*value = dmstrdup(buf);
	}
	return 0;
}

static int set_v6_prefix_len(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	char buf[80];
	long mask = 0;

	if (!e)
		return FAULT_9002;
	if (!wan_str_is_uint(value, &mask) || mask > 128)
		return FAULT_9005;	/* the fault the shell returned here */
	if (!v6_is_static(data))
		return FAULT_9001;
	if (action == VALUECHECK)
		return 0;
	snprintf(buf, sizeof(buf), "%s/%s", wan_entry_opt(data, "v6_ip"), value);
	v6_write_pair(e, "v6_mask", value, "ip6addr", buf);
	return 0;
}

static int get_v6_gateway(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = v6_gateway(data);
	return 0;
}

static int set_v6_gateway(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);

	if (!e)
		return FAULT_9002;
	if (!v6_is_static(data))
		return FAULT_9001;
	if (action == VALUECHECK)
		return 0;
	v6_write_pair(e, "v6_gw", value ? value : "", "ip6gw", value ? value : "");
	return 0;
}

/*
 * wan_device_v6_get_conn_status(): connected when there is a public address,
 * or a default gateway, or a delegated prefix.  Three chances, in that order.
 */
static int get_v6_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v;

	v = v6_array0(data, "ipv6-address", "address");
	if (v6_is_public(v)) {
		*value = "Connected";
		return 0;
	}
	if (v6_gateway(data)[0]) {
		*value = "Connected";
		return 0;
	}
	if (v6_array0(data, "ipv6-prefix", "address")[0]) {
		*value = "Connected";
		return 0;
	}
	*value = "Disconnected";
	return 0;
}

static int get_v6_pd_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(wan_entry_opt(data, "v6_pd"), "1") == 0 ? "true" : "false";
	return 0;
}

/* the opposite guard of the address setters: prefix delegation in Static is refused */
static int set_v6_pd_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (v6_is_static(data))
		return FAULT_9001;
	if (action == VALUECHECK)
		return 0;
	v6_write_pair(e, "v6_pd", b ? "1" : "0", "reqprefix", b ? "auto" : "no");
	return 0;
}

static int get_v6_pd_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = v6_array0(data, "ipv6-prefix", "address");
	return 0;
}

#define V6_PREFIX_GETTER(fn, key)						\
static int fn(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)	\
{										\
	char *v = v6_array0(data, "ipv6-prefix", key);				\
										\
	*value = v[0] ? v : "0";						\
	return 0;								\
}

V6_PREFIX_GETTER(get_v6_pd_prefix_len, "mask")
V6_PREFIX_GETTER(get_v6_pd_pltime, "preferred")
V6_PREFIX_GETTER(get_v6_pd_vltime, "valid")

static int get_v6_manual_dns(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(wan_entry_opt(data, "v6_static_dns"), "1") == 0 ? "true" : "false";
	return 0;
}

/* "Static mode with dynamic DNS" is the one combination the shell rejected */
static int set_v6_manual_dns(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (b == 0 && v6_is_static(data))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "v6_static_dns", b ? "1" : "0");
}

/* the first two of "dns-server", comma separated, exactly like the v4 leaf */
static int get_v6_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = wan_iface_status(v6_iface(data));
	char buf[160];
	char *d1, *d2;

	*value = "";
	if (!res)
		return 0;
	d1 = dmjson_get_value_in_array_idx(res, 0, 1, "dns-server");
	d2 = dmjson_get_value_in_array_idx(res, 1, 1, "dns-server");
	if (!d1[0])
		return 0;
	if (d2[0])
		snprintf(buf, sizeof(buf), "%s,%s", d1, d2);
	else
		snprintf(buf, sizeof(buf), "%s", d1);
	*value = dmstrdup(buf);
	return 0;
}

static int set_v6_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);

	if (!e)
		return FAULT_9002;
	if (strcmp(wan_entry_opt(data, "v6_static_dns"), "0") == 0)
		return FAULT_9001;	/* dynamic DNS: denied */
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "v6_dns", value ? value : "");
}

static int get_v6_uptime(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = v6_status_str(data, "uptime");

	*value = v[0] ? v : "0";
	return 0;
}

/* ------------------------------------------------------------------ */
/* X_AIS_IPv6<Name> -- the flat leaves                                 */
/* ------------------------------------------------------------------ */

/* v6_mode 0 or 1 is automatic, Static is not */
static int get_x_auto_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *m = v6_mode(data);

	*value = (strcmp(m, V6_MODE_SLAAC) == 0 || strcmp(m, V6_MODE_DHCP) == 0)
		 ? "true" : "false";
	return 0;
}

/*
 * Asymmetric on purpose, like the shell: true means DHCP (1), false means
 * Static (2).  It can never put the entry back into SLAAC -- only
 * X_AIS_IPv6AddressingType can.
 */
static int set_x_auto_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "v6_mode", b ? V6_MODE_DHCP : V6_MODE_STATIC);
	wan_reload();
	return 0;
}

/* v6_active 0 -> "None"; otherwise the mode, same names as the subtree leaf */
static int get_x_addressing_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	if (strcmp(wan_entry_opt(data, "v6_active"), "0") == 0) {
		*value = "None";
		return 0;
	}
	return get_v6_address_type(refparam, ctx, data, instance, value);
}

static int set_x_addressing_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	const char *m;

	if (!e)
		return FAULT_9002;
	if (value && strcmp(value, "None") == 0) {
		if (action == VALUECHECK)
			return 0;
		dmuci_set_value_by_section(e->s, "v6_active", "0");
		wan_reload();
		return 0;
	}
	m = value ? v6_mode_of(value) : NULL;
	if (!m)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "v6_active", "1");
	dmuci_set_value_by_section(e->s, "v6_mode", (char *)m);
	wan_reload();
	return 0;
}

/* same body as X_AIS_IPv6.IPAddress, which is what the shell wired up */
static int get_x_external_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_v6_ipaddr(refparam, ctx, data, instance, value);
}

static int set_x_external_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_v6_ipaddr(refparam, ctx, data, instance, value, action);
}

/*
 * The two DNS leaves are NOT per WAN entry: they read and write the space
 * separated list dhcp.lan.dns, the DNS the LAN hands out.  Every instance of
 * every connection object therefore shows the same pair, and writing one
 * keeps the other entries of that list.  That is the product's behaviour and
 * the reason these are not part of the X_AIS_IPv6 subtree.
 */
static char *lan_dns_field(int want)
{
	char *list = mtk_uci("dhcp", "lan", "dns");
	char buf[256], *p, *save = NULL;
	int i = 0;

	snprintf(buf, sizeof(buf), "%s", list);
	for (p = strtok_r(buf, " \t", &save); p; p = strtok_r(NULL, " \t", &save)) {
		if (i++ == want)
			return dmstrdup(p);
	}
	return "";
}

static int set_lan_dns_field(int want, const char *value)
{
	char *list = mtk_uci("dhcp", "lan", "dns");
	char buf[256], out[256], *p, *save = NULL;
	int i = 0;
	size_t len = 0;

	snprintf(buf, sizeof(buf), "%s", list);
	out[0] = '\0';
	for (p = strtok_r(buf, " \t", &save); p || i <= want; p = strtok_r(NULL, " \t", &save)) {
		const char *use = (i == want) ? value : p;

		if (use && use[0])
			len += snprintf(out + len, sizeof(out) - len, "%s%s",
					len ? " " : "", use);
		i++;
		if (!p)
			break;
	}
	dmuci_set_value("dhcp", "lan", "dns", out);
	return 0;
}

static int get_x_dns1(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = lan_dns_field(0);
	return 0;
}

static int set_x_dns1(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	return set_lan_dns_field(0, value ? value : "");
}

static int get_x_dns2(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = lan_dns_field(1);
	return 0;
}

static int set_x_dns2(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	return set_lan_dns_field(1, value ? value : "");
}

static int get_x_pd_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return get_v6_pd_enable(refparam, ctx, data, instance, value);
}

static int set_x_pd_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return set_v6_pd_enable(refparam, ctx, data, instance, value, action);
}

static int get_x_pd_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = v6_array0(data, "ipv6-prefix", "address");
	return 0;
}

/* Static only, and it only records the value: v6_pd_address is not pushed to network */
static int set_x_pd_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);

	if (!e)
		return FAULT_9002;
	if (!v6_is_static(data))
		return FAULT_9001;
	if (action == VALUECHECK)
		return 0;
	v6_write_pair(e, "v6_pd_address", value ? value : "", NULL, NULL);
	return 0;
}

/*
 * Three "type" leaves with the same shape and three different encodings --
 * copied one by one because the numbers do not line up:
 *   v6_gw_type   0 SLAAC, 1 Static, else None   (set None writes 2)
 *   v6_dns_type  0 SLAAC, 1 Static, 2 DHCP, else None (set None writes 3)
 *   v6_pd_type   0 Static, 1 DHCP, else None    (set None writes 2)
 */
static int get_x_gateway_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = wan_entry_opt(data, "v6_gw_type");

	*value = strcmp(v, "0") == 0 ? "SLAAC" : (strcmp(v, "1") == 0 ? "Static" : "None");
	return 0;
}

static int set_x_gateway_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	const char *v;

	if (!e || !value)
		return FAULT_9002;
	if (strcmp(value, "SLAAC") == 0)
		v = "0";
	else if (strcmp(value, "Static") == 0)
		v = "1";
	else if (strcmp(value, "None") == 0)
		v = "2";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "v6_gw_type", (char *)v);
	wan_reload();
	return 0;
}

/* $["route"][0].nexthop -- the first route, not the "::" one v6_gateway finds */
static int get_x_gateway_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = v6_array0(data, "route", "nexthop");
	return 0;
}

/* no mode guard here, unlike X_AIS_IPv6.DefaultGateway: the shell had none */
static int set_x_gateway_address(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);

	if (!e)
		return FAULT_9002;
	if (action == VALUECHECK)
		return 0;
	v6_write_pair(e, "v6_gw", value ? value : "", NULL, NULL);
	return 0;
}

static int get_x_dns_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = wan_entry_opt(data, "v6_dns_type");

	if (strcmp(v, "0") == 0)
		*value = "SLAAC";
	else if (strcmp(v, "1") == 0)
		*value = "Static";
	else if (strcmp(v, "2") == 0)
		*value = "DHCP";
	else
		*value = "None";
	return 0;
}

static int set_x_dns_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	const char *v;

	if (!e || !value)
		return FAULT_9002;
	if (strcmp(value, "SLAAC") == 0)
		v = "0";
	else if (strcmp(value, "Static") == 0)
		v = "1";
	else if (strcmp(value, "DHCP") == 0)
		v = "2";
	else if (strcmp(value, "None") == 0)
		v = "3";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "v6_dns_type", (char *)v);
	wan_reload();
	return 0;
}

static int get_x_pd_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = wan_entry_opt(data, "v6_pd_type");

	*value = strcmp(v, "0") == 0 ? "Static" : (strcmp(v, "1") == 0 ? "DHCP" : "None");
	return 0;
}

static int set_x_pd_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	const char *v;

	if (!e || !value)
		return FAULT_9002;
	if (strcmp(value, "Static") == 0)
		v = "0";
	else if (strcmp(value, "DHCP") == 0)
		v = "1";
	else if (strcmp(value, "None") == 0)
		v = "2";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "v6_pd_type", (char *)v);
	wan_reload();
	return 0;
}

static int get_x_gua_from_prefix(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(wan_entry_opt(data, "v6_gua_from_prefix"), "1") == 0 ? "true" : "false";
	return 0;
}

static int set_x_gua_from_prefix(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "v6_gua_from_prefix", b ? "1" : "0");
	wan_reload();
	return 0;
}

/*
 * X_AIS_IPv6ConnStatus / X_AIS_IPv6ConnectionStatus.  Richer than the subtree
 * leaf: it reports Unconfigured and Connecting too, from "autostart",
 * "pending" and data.v6_active.
 */
static int get_x_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = wan_iface_status(v6_iface(data));
	char *up, *pending, *autostart, *v6_active, *v;

	*value = "Disconnected";
	if (!res)
		return 0;
	up = dmjson_get_value(res, 1, "up");
	pending = dmjson_get_value(res, 1, "pending");
	autostart = dmjson_get_value(res, 1, "autostart");
	v6_active = dmjson_get_value(res, 2, "data", "v6_active");
	if (strcmp(v6_active, "0") == 0 || strcmp(autostart, "false") == 0) {
		*value = "Unconfigured";
		return 0;
	}
	if (strcmp(pending, "true") == 0) {
		*value = strcmp(up, "false") == 0 ? "Connecting" : "Disconnecting";
		return 0;
	}
	if (strcmp(up, "true") != 0)
		return 0;
	v = v6_array0(data, "ipv6-address", "address");
	if (v6_is_public(v) || v6_gateway(data)[0] ||
	    v6_array0(data, "ipv6-prefix", "address")[0])
		*value = "Connected";
	else
		*value = "Connecting";		/* up, but nothing routable yet */
	return 0;
}

/*
 * Writable in the product: an enum check, then ifup or ifdown.
 *
 * DIFFERENCE, the same one as WANPPPConnection.Reset: the shell ran ifup /
 * ifdown inline and cut the session it was answering on.  Queued here for the
 * end of the session instead.
 */
static int set_x_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = v6_entry(data);
	static const char *const up_states[] = { "Connected", "Connecting", NULL };
	static const char *const down_states[] = { "Disconnected", "Disconnecting",
						   "Unconfigured", NULL };
	static const char *const other[] = { "Authenticating", "PendingDisconnect", NULL };
	char cmd[96];
	int i;

	if (!e || !value)
		return FAULT_9002;
	if (strlen(value) > 64)
		return FAULT_9007;
	for (i = 0; other[i]; i++)
		if (strcmp(value, other[i]) == 0)
			return FAULT_9001;
	for (i = 0; up_states[i]; i++) {
		if (strcmp(value, up_states[i]) != 0)
			continue;
		if (action == VALUECHECK)
			return 0;
		snprintf(cmd, sizeof(cmd), "ifup %s", e->if6);
		mtk_apply_service(cmd);
		return 0;
	}
	for (i = 0; down_states[i]; i++) {
		if (strcmp(value, down_states[i]) != 0)
			continue;
		if (action == VALUECHECK)
			return 0;
		snprintf(cmd, sizeof(cmd), "ifdown %s", e->if6);
		mtk_apply_service(cmd);
		return 0;
	}
	return FAULT_9007;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tWanV6PdParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_v6_pd_enable, set_v6_pd_enable, NULL, NULL},
{"Address", &DMREAD, DMT_STRING, get_v6_pd_address, NULL, NULL, NULL},
{"PrefixLength", &DMREAD, DMT_UNINT, get_v6_pd_prefix_len, NULL, NULL, NULL},
{"PLtime", &DMREAD, DMT_UNINT, get_v6_pd_pltime, NULL, NULL, NULL},
{"VLtime", &DMREAD, DMT_UNINT, get_v6_pd_vltime, NULL, NULL, NULL},
{0}
};

static DMOBJ tWanV6Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Pd", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWanV6PdParam, NULL},
{0}
};

static DMLEAF tWanV6Param[] = {
{"AddressingType", &DMWRITE, DMT_STRING, get_v6_address_type, set_v6_address_type, NULL, NULL},
{"IPAddress", &DMWRITE, DMT_STRING, get_v6_ipaddr, set_v6_ipaddr, NULL, NULL},
{"PrefixLength", &DMWRITE, DMT_UNINT, get_v6_prefix_len, set_v6_prefix_len, NULL, NULL},
{"DefaultGateway", &DMWRITE, DMT_STRING, get_v6_gateway, set_v6_gateway, NULL, NULL},
{"ConnectionStatus", &DMREAD, DMT_STRING, get_v6_conn_status, NULL, NULL, NULL},
{"ManualDNS", &DMWRITE, DMT_BOOL, get_v6_manual_dns, set_v6_manual_dns, NULL, NULL},
{"DNSServers", &DMWRITE, DMT_STRING, get_v6_dns_servers, set_v6_dns_servers, NULL, NULL},
{"Uptime", &DMREAD, DMT_UNINT, get_v6_uptime, NULL, NULL, NULL},
{0}
};

/* the subtree is identical on both objects, so one pair of tables serves both */
static DMOBJ tWanConnV6Obj[] = {
{"X_AIS_IPv6", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWanV6Obj, tWanV6Param, NULL},
{0}
};

/* WANIPConnection: twelve flat leaves, and the status one is ConnStatus */
static DMLEAF tWanIpConnV6Param[] = {
{"X_AIS_IPv6AutoModeEnable", &DMWRITE, DMT_BOOL, get_x_auto_mode, set_x_auto_mode, NULL, NULL},
{"X_AIS_IPv6AddressingType", &DMWRITE, DMT_STRING, get_x_addressing_type, set_x_addressing_type, NULL, NULL},
{"X_AIS_IPv6ExternalAddress", &DMWRITE, DMT_STRING, get_x_external_address, set_x_external_address, NULL, NULL},
{"X_AIS_IPv6GatewayType", &DMWRITE, DMT_STRING, get_x_gateway_type, set_x_gateway_type, NULL, NULL},
{"X_AIS_IPv6GatewayAddress", &DMWRITE, DMT_STRING, get_x_gateway_address, set_x_gateway_address, NULL, NULL},
{"X_AIS_IPv6DNSType", &DMWRITE, DMT_STRING, get_x_dns_type, set_x_dns_type, NULL, NULL},
{"X_AIS_IPv6DNSServers1", &DMWRITE, DMT_STRING, get_x_dns1, set_x_dns1, NULL, NULL},
{"X_AIS_IPv6DNSServers2", &DMWRITE, DMT_STRING, get_x_dns2, set_x_dns2, NULL, NULL},
{"X_AIS_IPv6PrefixDelegationType", &DMWRITE, DMT_STRING, get_x_pd_type, set_x_pd_type, NULL, NULL},
{"X_AIS_IPv6PrefixDelegationAddress", &DMWRITE, DMT_STRING, get_x_pd_address, set_x_pd_address, NULL, NULL},
{"X_AIS_IPv6GUAFromPrefixEnable", &DMWRITE, DMT_BOOL, get_x_gua_from_prefix, set_x_gua_from_prefix, NULL, NULL},
{"X_AIS_IPv6ConnStatus", &DMWRITE, DMT_STRING, get_x_conn_status, set_x_conn_status, NULL, NULL},
{0}
};

/* WANPPPConnection: eight, and the status one is spelled out in full */
static DMLEAF tWanPppConnV6Param[] = {
{"X_AIS_IPv6AutoModeEnable", &DMWRITE, DMT_BOOL, get_x_auto_mode, set_x_auto_mode, NULL, NULL},
{"X_AIS_IPv6AddressingType", &DMWRITE, DMT_STRING, get_x_addressing_type, set_x_addressing_type, NULL, NULL},
{"X_AIS_IPv6ExternalAddress", &DMWRITE, DMT_STRING, get_x_external_address, set_x_external_address, NULL, NULL},
{"X_AIS_IPv6DNSServers1", &DMWRITE, DMT_STRING, get_x_dns1, set_x_dns1, NULL, NULL},
{"X_AIS_IPv6DNSServers2", &DMWRITE, DMT_STRING, get_x_dns2, set_x_dns2, NULL, NULL},
{"X_AIS_IPv6PrefixDelegationAddress", &DMWRITE, DMT_STRING, get_x_pd_address, set_x_pd_address, NULL, NULL},
{"X_AIS_IPv6PdEnable", &DMWRITE, DMT_BOOL, get_x_pd_enable, set_x_pd_enable, NULL, NULL},
{"X_AIS_IPv6ConnectionStatus", &DMWRITE, DMT_STRING, get_x_conn_status, set_x_conn_status, NULL, NULL},
{0}
};

/*
 * browseinstobj NULL all the way down: wanip_mtk.c owns the instances of both
 * connection objects, dm_registry merges this subtree into them by name.  The
 * data pointer an instance carries is its struct wan_entry, which is what
 * every getter here reads.
 */
static DMOBJ tWanCxDevV6Obj[] = {
{"WANIPConnection", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tWanConnV6Obj, tWanIpConnV6Param, NULL},
{"WANPPPConnection", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tWanConnV6Obj, tWanPppConnV6Param, NULL},
{0}
};

static DMOBJ tWanDeviceV6Obj[] = {
{"WANConnectionDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tWanCxDevV6Obj, NULL, NULL},
{0}
};

static DMOBJ tWanDeviceV6Root[] = {
{"WANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWanDeviceV6Obj, NULL, NULL},
{0}
};

/*
 * No .paths: wan_mtk.c claims the whole WANDevice branch (K8), dm_registry
 * merges this tree into it.
 */
static const struct dm_module wanipv6_mtk_module = {
	.name  = "mtk-wanipv6",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWanDeviceV6Root,
};
DM_MODULE_REGISTER(wanipv6_mtk_module);
