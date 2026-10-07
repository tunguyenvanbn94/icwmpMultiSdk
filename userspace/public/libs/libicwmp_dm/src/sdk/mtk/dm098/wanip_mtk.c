/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1.WANIPConnection.
 *	of the MTK/Airoha product.
 *
 *	Ported from functions/tr098/wan_device:
 *	  sub_entry_wandevice_wanconnectiondevice_ip()      (routed IPoE entries)
 *	  sub_entry_wandevice_wanconnectiondevice_bridge()  (bridged entries)
 *	  wan_device_browse_instances_wancxdev_ip(), wan_device_get_total_entry(),
 *	  wan_device_get_total_bridge_entry(), wan_device_get_entry_num_ip()
 *
 *	The instance model, which is the part that is easy to get wrong:
 *
 *	  - the entries are the anonymous sections "config entry" of UCI package
 *	    "wan", scanned in file order and stopped at the first one without an
 *	    "id" option, exactly like the shell's "while :; do ... wan.@entry[$i].id
 *	    ... done" loop,
 *	  - the CWMP instance number is "id" + 1, NOT the position of the section.
 *	    An ACS that has provisioned WANIPConnection.3 keeps talking to the
 *	    same entry after another one is deleted,
 *	  - this one object carries TWO kinds of entry: routed IPoE
 *	    (switch_mode 0 and conn_type 0) and bridged (switch_mode 1).  PPPoE
 *	    (conn_type 2) lives under WANPPPConnection and is not here,
 *	  - the netdevs follow the id: routed uses network.if<id> / if<id>_6,
 *	    bridged uses network.if_wanbr<id> with device section dev_wanbr<id>.
 *
 *	Kept verbatim from the product, each one deliberate:
 *
 *	1. Bridged entries answer constants for the IP leaves (0.0.0.0 subnet and
 *	   gateway, NAT off, no DNS) instead of reading the interface.  That is
 *	   what the shell did, and an ACS template reading a bridged WAN expects
 *	   those exact strings.
 *	2. ExternalIPAddress of a bridged entry is 0.0.0.0 except on entry id 0
 *	   when clay.opermode.mode is "ap", where it reports the LAN address --
 *	   and only in that case is it forced-inform.  On a routed entry it is
 *	   forced-inform when the entry carries the TR-069 service bit
 *	   (service_type & 2).  Both conditions are evaluated per instance
 *	   through the forced_inform callback, not hardcoded on the leaf.
 *	3. Setting X_AIS_VLAN8021P on a BRIDGED entry fails.  The shell passes
 *	   "$iface4" there, a variable its bridge function never sets, so
 *	   wan_device_set_vlan_priority() looks for an empty device name, never
 *	   finds it and returns an internal error.  Mirrored on purpose: making
 *	   it work would start writing ingress/egress QoS mapping on a path the
 *	   product has never exercised.
 *	4. A bad MaxMTUSize answers 9005 ("invalid parameter name"), not 9007.
 *	   That is the fault the shell returned for an out-of-range MTU.
 *
 *	Three deliberate DIFFERENCES, all additive, none changes a value:
 *
 *	a. Bridged instances also expose Alias, X_AIS_DefaultRoute and
 *	   X_AIS_IPMode.  A static C tree has one leaf table per object while the
 *	   shell registered a different set per entry kind.  The getters answer
 *	   what the same UCI options say, so nothing is invented: Alias is
 *	   "cpe-other" for a bridge, exactly what wan_device_get_alias() returns.
 *	b. Stats.* are xsd:unsignedInt for every instance.  The shell passed the
 *	   type in the setter slot for routed entries (so they went out as
 *	   xsd:string) but in the right slot for bridged ones.  One leaf cannot
 *	   be two types -- this takes the spelling that is both TR-098 correct
 *	   and already used by half the instances.
 *	c. A non-numeric MaxMTUSize / X_AIS_VLANID / X_AIS_VLAN8021P is refused
 *	   with 9007 instead of being written to UCI.  The shell's "[ $v -lt 1 ]"
 *	   errors out on a non-number and falls through to the write.
 *
 *	X_AIS_ServiceList is NOT here: its setter is a state machine over
 *	easycwmp.@acs[0].enablecwmp, the firewall internet-access rules and
 *	easycwmpd (re)configuration, ported on its own in servicelist_mtk.c.
 *
 *	AddObject / DeleteObject of both connection objects are here too, and
 *	reach this file since wan_mtk.c claims the WANDevice branch (K8).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"

#define WAN_MAX_LAN_PORTS	4	/* MAX_LAN_PORTS of the shell */
#define WAN_MAX_WLAN_PORTS	8	/* MAX_WLAN_PORTS */

/* struct wan_entry and the entry enumeration: wanconn_mtk.h */

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */

char *wan_sect_opt(struct uci_section *s, char *option)
{
	char *v = NULL;

	if (!s)
		return "";
	dmuci_get_value_by_section_string(s, option, &v);
	return v ? v : "";
}

char *wan_entry_opt(void *data, char *option)
{
	struct wan_entry *e = (struct wan_entry *)data;

	return e ? wan_sect_opt(e->s, option) : "";
}

/* the shell compared strings, so an unset option is never "0" */
static int entry_opt_is(void *data, char *option, const char *val)
{
	return strcmp(wan_entry_opt(data, option), val) == 0;
}

int wan_str_is_uint(const char *s, long *out)
{
	char *end;
	long v;

	if (!s || !*s)
		return 0;
	v = strtol(s, &end, 10);
	if (*end != '\0' || v < 0)
		return 0;
	if (out)
		*out = v;
	return 1;
}

/* ------------------------------------------------------------------ */
/* ubus                                                                */
/* ------------------------------------------------------------------ */

json_object *wan_iface_status(const char *iface)
{
	json_object *res = NULL;
	char obj[64];

	if (!iface || !iface[0])
		return NULL;
	snprintf(obj, sizeof(obj), "network.interface.%s", iface);
	dmubus_call(obj, "status", UBUS_ARGS{}, 0, &res);
	return res;
}

static char *iface_ipv4(const char *iface)
{
	json_object *res = wan_iface_status(iface), *a;

	if (!res)
		return "";
	a = dmjson_select_obj_in_array_idx(res, 0, 1, "ipv4-address");
	if (!a)
		return "";
	return dmjson_get_value(a, 1, "address");
}

/* "@.l3_device" of the shell: for PPPoE the counters and the MTU live on the
 * ppp netdev the daemon created, not on the ethernet below it */
char *wan_iface_l3_device(const char *iface)
{
	json_object *res = wan_iface_status(iface);

	return res ? dmjson_get_value(res, 1, "l3_device") : "";
}

/* '$["ipv4-address"][0].ptpaddress': the peer address of the PPP link */
static char *iface_ptp_peer(const char *iface)
{
	json_object *res = wan_iface_status(iface), *a;

	if (!res)
		return "";
	a = dmjson_select_obj_in_array_idx(res, 0, 1, "ipv4-address");
	if (!a)
		return "";
	return dmjson_get_value(a, 1, "ptpaddress");
}

/* what the shell queued with common_execute_command_in_apply_service(): the
 * reload runs once at the end of the session, never between two SetParameterValues */
void wan_reload(void)
{
	mtk_apply_service("/usr/sbin/hni_wan_reload.sh");
}

/*
 * ubus call hni.wan set '{ "index": N, "action": ..., "param": ..., "value": ... }'
 * The shell only accepted "result":"SUCCESS"; anything else was an internal
 * error and the reload was NOT queued.  Returns 0 on success.
 */
int wan_ubus_set(int index, const char *action, const char *param, const char *value)
{
	json_object *res = NULL;
	char idx[16];
	char *result;

	snprintf(idx, sizeof(idx), "%d", index);
	if (param)
		dmubus_call("hni.wan", "set",
			    UBUS_ARGS{{"index", idx, Integer},
				      {"action", (char *)action, String},
				      {"param", (char *)param, String},
				      {"value", (char *)value, String}}, 4, &res);
	else
		dmubus_call("hni.wan", "set",
			    UBUS_ARGS{{"index", idx, Integer},
				      {"action", (char *)action, String}}, 2, &res);
	if (!res)
		return -1;
	result = dmjson_get_value(res, 1, "result");
	return (result && strcmp(result, "SUCCESS") == 0) ? 0 : -1;
}

/* every "modify" of the shell: ubus first, reload only when it succeeded */
int wan_modify(struct wan_entry *e, const char *param, const char *value)
{
	if (wan_ubus_set(e->idx, "modify", param, value) != 0)
		return FAULT_9002;
	wan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* entries                                                             */
/* ------------------------------------------------------------------ */

static void entry_fill(struct wan_entry *e)
{
	if (e->bridge) {
		snprintf(e->if4, sizeof(e->if4), "if_wanbr%d", e->id);
		snprintf(e->dev, sizeof(e->dev), "dev_wanbr%d", e->id);
		e->if6[0] = '\0';
	} else {
		snprintf(e->if4, sizeof(e->if4), "if%d", e->id);
		snprintf(e->if6, sizeof(e->if6), "if%d_6", e->id);
		e->dev[0] = '\0';
	}
}

/*
 * One pass over wan.@entry[], stopping at the first section without an id --
 * the shell's loop condition.  Returns the number of instances the object
 * asked for carries and, when out is not NULL, fills it with them.
 *
 * "kind" is the shell's $targe_conn_type of wan_device_get_total_entry():
 * WANIPConnection takes the routed IPoE and the bridged entries,
 * WANPPPConnection takes conn_type 2.  Every entry belongs to exactly one of
 * the two objects, which is why one instance number can be reused on both.
 */
int wan_entries_kind(struct wan_entry **out, int max, int kind)
{
	struct uci_section *s;
	int idx = 0, n = 0;

	uci_foreach_sections("wan", "entry", s) {
		char *id = wan_sect_opt(s, "id");
		char *sw = wan_sect_opt(s, "switch_mode");
		long idv = 0, svc = 0;
		int bridge = 0, ppp = 0;

		if (!id[0])
			break;
		if (kind == WAN_KIND_PPP) {
			if (strcmp(sw, "0") != 0 ||
			    strcmp(wan_sect_opt(s, "conn_type"), "2") != 0) {
				idx++;
				continue;
			}
			ppp = 1;
		} else if (strcmp(sw, "1") == 0) {
			bridge = 1;
		} else if (strcmp(sw, "0") == 0 && strcmp(wan_sect_opt(s, "conn_type"), "0") == 0) {
			bridge = 0;
		} else {
			idx++;
			continue;	/* PPPoE, or an entry with no mode at all */
		}
		if (!wan_str_is_uint(id, &idv)) {
			idx++;
			continue;
		}
		if (out && n < max) {
			struct wan_entry *e = &out[0][n];

			memset(e, 0, sizeof(*e));
			e->s = s;
			e->idx = idx;
			e->id = (int)idv;
			e->bridge = bridge;
			e->ppp = ppp;
			if (wan_str_is_uint(wan_sect_opt(s, "service_type"), &svc))
				e->tr069 = (svc & 2) ? 1 : 0;
			entry_fill(e);
		}
		n++;
		idx++;
	}
	return n;
}

static int wan_entries(struct wan_entry **out, int max)
{
	return wan_entries_kind(out, max, WAN_KIND_IP);
}

/* ------------------------------------------------------------------ */
/* leaves -- identity and state                                        */
/* ------------------------------------------------------------------ */

static int get_conn_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = entry_opt_is(data, "active", "1") ? "true" : "false";
	return 0;
}

static int set_conn_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "active", b ? "1" : "0");
}

/* routed: an address on either family means connected.  bridged: the shell
 * only looked at the "active" flag, there is no L3 to ask */
static int get_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char *v4, *v6 = NULL;
	json_object *res, *a;

	if (!e)
		return 0;
	if (e->bridge) {
		*value = entry_opt_is(data, "active", "1") ? "Connected" : "Disconnected";
		return 0;
	}
	v4 = iface_ipv4(e->if4);
	res = wan_iface_status(e->if6);
	if (res) {
		a = dmjson_select_obj_in_array_idx(res, 0, 1, "ipv6-address");
		if (a)
			v6 = dmjson_get_value(a, 1, "address");
	}
	*value = ((v4 && v4[0]) || (v6 && v6[0])) ? "Connected" : "Disconnected";
	return 0;
}

static int get_possible_types(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "IP_Routed,IP_Bridged";
	return 0;
}

static int get_conn_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;

	*value = (e && e->bridge) ? "IP_Bridged" : "IP_Routed";
	return 0;
}

/* moves the entry between routed and bridged -- the object keeps its
 * instance number because that number comes from "id", not from the mode */
static int set_conn_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return FAULT_9002;
	if (!value || (strcmp(value, "IP_Routed") != 0 && strcmp(value, "IP_Bridged") != 0))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "sw_mode", (strcmp(value, "IP_Bridged") == 0) ? "1" : "0");
}

static int get_conn_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wan_entry_opt(data, "name");
	return 0;
}

/* the one setter of this object that writes UCI directly: the shell did the
 * same, there is no hni.wan action for the display name */
static int set_conn_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return FAULT_9002;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "name", value ? value : "");
	return 0;
}

/* service_type -> the operator's alias; a bridge is always "cpe-other" */
static int get_conn_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char *svc = wan_entry_opt(data, "service_type");

	if (e && e->bridge) {
		*value = "cpe-other";
		return 0;
	}
	if (strcmp(svc, "1") == 0)
		*value = "cpe-internet";
	else if (strcmp(svc, "2") == 0)
		*value = "cpe-tr069";
	else if (strcmp(svc, "3") == 0)
		*value = "cpe-internet-tr069";
	else
		*value = "cpe-other";
	return 0;
}

static int get_conn_uptime(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	json_object *res;
	char *v;

	*value = "0";
	if (!e)
		return 0;
	res = wan_iface_status(e->if4);
	if (!res)
		return 0;
	v = dmjson_get_value(res, 1, "uptime");
	if (v && v[0])
		*value = v;
	return 0;
}

static int get_last_error(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "ERROR_NONE";
	return 0;
}

/* ------------------------------------------------------------------ */
/* leaves -- IPv4                                                      */
/* ------------------------------------------------------------------ */

static int get_nat_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (e && e->bridge)
		*value = "false";
	else
		*value = entry_opt_is(data, "nat_disabled", "1") ? "false" : "true";
	return 0;
}

static int set_nat_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (e->bridge)		/* the shell's setter here was the literal "true" */
		return 0;
	return wan_modify(e, "nat_enable", b ? "1" : "0");
}

static int get_addressing_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (e && e->bridge)
		*value = "";
	else
		*value = entry_opt_is(data, "v4_mode", "0") ? "DHCP" : "Static";
	return 0;
}

static int set_addressing_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return FAULT_9002;
	if (e && e->bridge) {
		if (action == VALUECHECK)
			return 0;
		return 0;	/* accepted and dropped, like the shell */
	}
	if (!value || (strcmp(value, "Static") != 0 && strcmp(value, "DHCP") != 0))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "v4_mode", (strcmp(value, "Static") == 0) ? "1" : "0");
}

/* on a bridge this is 0.0.0.0, except entry 0 in AP opermode which reports
 * the LAN address -- see the header comment */
static int get_external_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return 0;
	if (!e->bridge) {
		*value = iface_ipv4(e->if4);
		return 0;
	}
	*value = "0.0.0.0";
	if (e->id == 0 && strcmp(mtk_uci("clay", "opermode", "mode"), "ap") == 0) {
		char *ip = iface_ipv4("lan");

		if (ip && ip[0])
			*value = ip;
	}
	return 0;
}

/* v4_mode 0 is DHCP: the shell refused to write a static address over it */
static int set_external_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	unsigned int tmp;

	if (!e)
		return FAULT_9002;
	if (e && e->bridge)
		return 0;
	if (mtk_ipv4_parse(value, &tmp) != 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (entry_opt_is(data, "v4_mode", "0"))
		return FAULT_9001;
	dmuci_set_value_by_section(e->s, "v4_ip", value);
	dmuci_set_value("network", e->if4, "ipaddr", value);
	wan_reload();
	return 0;
}

/* ipv4-address[0].mask is a prefix length, cidr_to_string() of the shell */
static int get_subnet_mask(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	json_object *res, *a;
	char *mask;
	long cidr = 0;

	if (!e)
		return 0;
	if (e->bridge) {
		*value = "0.0.0.0";
		return 0;
	}
	*value = "";
	res = wan_iface_status(e->if4);
	if (!res)
		return 0;
	a = dmjson_select_obj_in_array_idx(res, 0, 1, "ipv4-address");
	if (!a)
		return 0;
	mask = dmjson_get_value(a, 1, "mask");
	if (!wan_str_is_uint(mask, &cidr) || cidr > 32)
		return 0;
	*value = mtk_ipv4_str(cidr ? (0xffffffffu << (32 - (unsigned int)cidr)) : 0u);
	return 0;
}

static int set_subnet_mask(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	unsigned int tmp;

	if (!e)
		return FAULT_9002;
	if (e && e->bridge)
		return 0;
	if (mtk_ipv4_parse(value, &tmp) != 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (entry_opt_is(data, "v4_mode", "0"))
		return FAULT_9001;
	dmuci_set_value_by_section(e->s, "v4_mask", value);
	dmuci_set_value("network", e->if4, "netmask", value);
	wan_reload();
	return 0;
}

/* the active route first, then the inactive one -- a WAN that is down still
 * reports the gateway it was configured with */
static int get_default_gateway(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	json_object *res, *r;
	char *gw = NULL;

	if (!e)
		return 0;
	if (e->bridge) {
		*value = "0.0.0.0";
		return 0;
	}
	*value = "";
	res = wan_iface_status(e->if4);
	if (!res)
		return 0;
	r = dmjson_select_obj_in_array_idx(res, 0, 1, "route");
	if (r)
		gw = dmjson_get_value(r, 1, "nexthop");
	if (!gw || !gw[0]) {
		r = dmjson_select_obj_in_array_idx(res, 0, 2, "inactive", "route");
		if (r)
			gw = dmjson_get_value(r, 1, "nexthop");
	}
	if (gw)
		*value = gw;
	return 0;
}

static int set_default_gateway(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	unsigned int tmp;

	if (!e)
		return FAULT_9002;
	if (e && e->bridge)
		return 0;
	if (mtk_ipv4_parse(value, &tmp) != 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (entry_opt_is(data, "v4_mode", "0"))
		return FAULT_9001;
	dmuci_set_value_by_section(e->s, "v4_gw", value);
	dmuci_set_value("network", e->if4, "gateway", value);
	wan_reload();
	return 0;
}

/* ------------------------------------------------------------------ */
/* leaves -- DNS                                                       */
/* ------------------------------------------------------------------ */

static int get_dns_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

static int set_accept_and_drop(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return 0;
}

static int get_dns_override(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (e && e->bridge)
		*value = "false";
	else
		*value = entry_opt_is(data, "v4_static_dns", "1") ? "true" : "false";
	return 0;
}

/* static addressing with dynamic DNS is refused by the product */
static int set_dns_override(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (e && e->bridge)
		return 0;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (!b && entry_opt_is(data, "v4_mode", "1"))
		return FAULT_9007;
	return wan_modify(e, "v4_static_dns", b ? "1" : "0");
}

/* the first three dns-server entries of the interface, comma separated */
static int get_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	json_object *res, *arr = NULL;
	char buf[256];
	size_t len = 0;
	int i, n;

	*value = "";
	if (!e || e->bridge)
		return 0;
	res = wan_iface_status(e->if4);
	if (!res)
		return 0;
	arr = dmjson_get_obj(res, 1, "dns-server");
	if (!arr || json_object_get_type(arr) != json_type_array)
		return 0;
	buf[0] = '\0';
	n = (int)json_object_array_length(arr);
	if (n > 3)
		n = 3;
	for (i = 0; i < n; i++) {
		json_object *o = json_object_array_get_idx(arr, i);
		const char *s = o ? json_object_get_string(o) : NULL;

		if (!s || !*s)
			continue;
		len += snprintf(buf + len, sizeof(buf) - len, "%s%s", len ? "," : "", s);
		if (len >= sizeof(buf))
			break;
	}
	*value = dmstrdup(buf);
	return 0;
}

static int set_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return FAULT_9002;
	if (e && e->bridge)
		return 0;
	if (action == VALUECHECK)
		return 0;
	if (entry_opt_is(data, "v4_static_dns", "0"))
		return FAULT_9001;
	return wan_modify(e, "v4_dns", value ? value : "");
}

/* ------------------------------------------------------------------ */
/* leaves -- device: MTU and MAC                                       */
/* ------------------------------------------------------------------ */

/* network.<iface>.device is the netdev the logical interface runs on */
static char *entry_netdev(struct wan_entry *e)
{
	if (!e)
		return "";
	return mtk_uci("network", e->if4, "device");
}

/* the "config device" section whose name is that netdev */
static struct uci_section *netdev_section(const char *name)
{
	struct uci_section *s;

	if (!name || !name[0])
		return NULL;
	uci_foreach_sections("network", "device", s) {
		if (strcmp(wan_sect_opt(s, "name"), name) == 0)
			return s;
	}
	return NULL;
}

static int get_max_mtu(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char path[128];
	char *dev = entry_netdev(e);

	*value = "";
	if (!dev[0])
		return 0;
	snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", dev);
	*value = mtk_file_line(path);
	return 0;
}

/*
 * Routed: find the "config device" section carrying the netdev and set its
 * mtu.  Bridged: the shell wrote network.dev_wanbr<id>.mtu directly, that
 * named section is the bridge device of the entry.
 */
static int set_max_mtu(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	struct uci_section *d;
	long mtu = 0;

	if (!e)
		return FAULT_9002;
	if (!wan_str_is_uint(value, &mtu))
		return FAULT_9007;
	if (mtu < 1 || mtu > 1540)
		return FAULT_9005;	/* the fault the shell returned here */
	if (action == VALUECHECK)
		return 0;
	if (e->bridge) {
		dmuci_set_value("network", e->dev, "mtu", value);
		wan_reload();
		return 0;
	}
	d = netdev_section(entry_netdev(e));
	if (!d)
		return FAULT_9002;
	dmuci_set_value_by_section(d, "mtu", value);
	wan_reload();
	return 0;
}

static int is_valid_mac(const char *m)
{
	int i;

	if (!m)
		return 0;
	for (i = 0; i < 17; i++) {
		if ((i % 3) == 2) {
			if (m[i] != ':')
				return 0;
		} else if (!isxdigit((unsigned char)m[i]))
			return 0;
	}
	return m[17] == '\0';
}

static int get_conn_mac(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char path[128];
	char *dev = entry_netdev(e);

	*value = "";
	if (!dev[0])
		return 0;
	snprintf(path, sizeof(path), "/sys/class/net/%s/address", dev);
	*value = mtk_file_line(path);
	return 0;
}

/* refused unless the entry has mac_override set -- cloning the WAN MAC is an
 * explicit opt-in on this product */
static int set_conn_mac(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	struct uci_section *d;

	if (!e)
		return FAULT_9002;
	if (!is_valid_mac(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (entry_opt_is(data, "mac_override", "") || entry_opt_is(data, "mac_override", "0"))
		return FAULT_9001;
	if (e->bridge) {
		dmuci_set_value("network", e->dev, "macaddr", value);
		wan_reload();
		return 0;
	}
	d = netdev_section(entry_netdev(e));
	if (!d)
		return FAULT_9002;
	dmuci_set_value_by_section(d, "macaddr", value);
	wan_reload();
	return 0;
}

static int get_mac_override(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = wan_entry_opt(data, "mac_override");

	*value = (!v[0] || strcmp(v, "0") == 0) ? "false" : "true";
	return 0;
}

static int set_mac_override(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (b)
		dmuci_set_value_by_section(e->s, "mac_override", "1");
	else
		dmuci_set_value_by_section(e->s, "mac_override", "");
	return 0;
}

/* ------------------------------------------------------------------ */
/* leaves -- operator extensions                                       */
/* ------------------------------------------------------------------ */

static int get_vlan_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = entry_opt_is(data, "vlan_active", "1") ? "true" : "false";
	return 0;
}

static int set_vlan_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "vlan_enable", b ? "1" : "0");
}

static int get_vlan_id(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = wan_entry_opt(data, "vlan_id");

	*value = v[0] ? v : "1";
	return 0;
}

static int set_vlan_id(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	long id = 0;

	if (!e)
		return FAULT_9002;
	if (!wan_str_is_uint(value, &id))
		return FAULT_9007;
	if (id < 1 || id > 4094)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "vlan_id", value);
}

static int get_vlan_priority(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = wan_entry_opt(data, "vlan_priority");

	*value = v[0] ? v : "0";
	return 0;
}

/*
 * Writes the priority on the entry and the matching QoS mapping on the netdev
 * section.  On a BRIDGED entry it fails with 9002: see point 3 of the header
 * comment, the shell could never do this either.
 */
static int set_vlan_priority(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	struct uci_section *d;
	char map[16];
	long prio = 0;

	if (!e)
		return FAULT_9002;
	if (!wan_str_is_uint(value, &prio))
		return FAULT_9007;
	if (prio > 7)
		return FAULT_9005;	/* the fault the shell returned here */
	if (action == VALUECHECK)
		return 0;
	if (e->bridge)
		return FAULT_9002;
	d = netdev_section(entry_netdev(e));
	if (!d)
		return FAULT_9002;
	dmuci_set_value_by_section(e->s, "vlan_priority", value);
	snprintf(map, sizeof(map), "0:%ld", prio);
	dmuci_set_value_by_section(d, "ingress_qos_mapping", map);
	dmuci_set_value_by_section(d, "egress_qos_mapping", map);
	wan_reload();
	return 0;
}

static int get_x_default_route(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = entry_opt_is(data, "default_gw", "1") ? "true" : "false";
	return 0;
}

static int set_x_default_route(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "default_route", b ? "1" : "0");
}

static int get_x_ip_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int v4 = entry_opt_is(data, "v4_active", "1");
	int v6 = entry_opt_is(data, "v6_active", "1");

	if (v4 && v6)
		*value = "Both";
	else if (v6)
		*value = "IPv6";
	else if (v4)
		*value = "IPv4";
	else
		*value = "N/A";
	return 0;
}

static int set_x_ip_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	const char *version;

	if (!e)
		return FAULT_9002;
	if (!value)
		return FAULT_9007;
	if (strcmp(value, "Both") == 0)
		version = "3";
	else if (strcmp(value, "IPv4") == 0)
		version = "1";
	else if (strcmp(value, "IPv6") == 0)
		version = "2";
	else
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	return wan_modify(e, "ip_protocol", version);
}

/*
 * The LAN ports and SSIDs bound to this WAN, as TR-098 paths.  lan1..4 map to
 * LANEthernetInterfaceConfig.1..4, ssid1..8 to WLANConfiguration.1..8, and the
 * "mlo" flag stands for the two MLO SSIDs 11 and 12.
 */
static int get_x_lan_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[1024];		/* 4 LAN + 8 SSID + 2 MLO paths ~ 790 bytes */
	char opt[16];
	size_t len = 0;
	int i;

	buf[0] = '\0';
	for (i = 1; i <= WAN_MAX_LAN_PORTS; i++) {
		snprintf(opt, sizeof(opt), "lan%d", i);
		if (entry_opt_is(data, opt, "1"))
			len += snprintf(buf + len, sizeof(buf) - len,
					"%sInternetGatewayDevice.LANDevice.1.LANEthernetInterfaceConfig.%d",
					len ? "," : "", i);
		if (len >= sizeof(buf))
			break;
	}
	for (i = 1; len < sizeof(buf) && i <= WAN_MAX_WLAN_PORTS; i++) {
		snprintf(opt, sizeof(opt), "ssid%d", i);
		if (entry_opt_is(data, opt, "1"))
			len += snprintf(buf + len, sizeof(buf) - len,
					"%sInternetGatewayDevice.LANDevice.1.WLANConfiguration.%d",
					len ? "," : "", i);
	}
	if (len < sizeof(buf) && entry_opt_is(data, "mlo", "1"))
		len += snprintf(buf + len, sizeof(buf) - len,
				"%sInternetGatewayDevice.LANDevice.1.WLANConfiguration.11,"
				"InternetGatewayDevice.LANDevice.1.WLANConfiguration.12",
				len ? "," : "");
	*value = dmstrdup(buf);
	return 0;
}

/*
 * The reverse: a comma separated list of the same paths becomes the
 * "binding_ports" string hni.wan expects ("lan1=1 ssid3=1 ,").  Unknown or
 * out-of-range entries are skipped exactly like the shell's case statement --
 * they are not an error.
 */
static int set_x_lan_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char list[512];
	char *copy, *tok, *save = NULL;
	size_t len = 0;

	if (!e)
		return FAULT_9002;
	if (action == VALUECHECK)
		return 0;
	list[0] = '\0';
	copy = dmstrdup(value ? value : "");
	for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		char *last = strrchr(tok, '.');
		long n = 0;

		if (!last || !wan_str_is_uint(last + 1, &n))
			continue;
		if (strstr(tok, "LANEthernetInterfaceConfig.")) {
			if (n >= 1 && n <= WAN_MAX_LAN_PORTS)
				len += snprintf(list + len, sizeof(list) - len, " lan%ld=1", n);
		} else if (strstr(tok, "WLANConfiguration.")) {
			if ((n >= 1 && n <= WAN_MAX_WLAN_PORTS) || n == 11 || n == 12)
				len += snprintf(list + len, sizeof(list) - len, " ssid%ld=1", n);
		}
		if (len >= sizeof(list))
			break;
	}
	if (len >= sizeof(list) - 1)
		return FAULT_9007;
	snprintf(list + len, sizeof(list) - len, ",");
	return wan_modify(e, "binding_ports", list);
}

/* ------------------------------------------------------------------ */
/* Stats                                                               */
/* ------------------------------------------------------------------ */

/*
 * The three cases of wan_device_get_eth_stats(): routed entries count on
 * network.if<id>.device, bridged ones on network.dev_wanbr<id>.name, and
 * PPPoE on the l3_device that ubus reports -- the ppp netdev, not the
 * ethernet carrying it.
 */
static char *stat_of(void *data, const char *counter)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char path[128];
	char *dev;

	if (!e)
		return "";
	if (e->ppp)
		dev = wan_iface_l3_device(e->if4);
	else
		dev = e->bridge ? mtk_uci("network", e->dev, "name") : entry_netdev(e);
	if (!dev[0])
		return "";
	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", dev, counter);
	return mtk_file_line(path);
}

#define WAN_STAT_GETTER(fn, counter)						\
static int fn(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)	\
{										\
	*value = stat_of(data, counter);					\
	return 0;								\
}

WAN_STAT_GETTER(get_stat_tx_bytes, "tx_bytes")
WAN_STAT_GETTER(get_stat_rx_bytes, "rx_bytes")
WAN_STAT_GETTER(get_stat_tx_packets, "tx_packets")
WAN_STAT_GETTER(get_stat_rx_packets, "rx_packets")
WAN_STAT_GETTER(get_stat_tx_errors, "tx_errors")
WAN_STAT_GETTER(get_stat_rx_errors, "rx_errors")
WAN_STAT_GETTER(get_stat_tx_dropped, "tx_dropped")
WAN_STAT_GETTER(get_stat_rx_dropped, "rx_dropped")
WAN_STAT_GETTER(get_stat_rx_nohandler, "rx_nohandler")

/* ------------------------------------------------------------------ */
/* object level                                                        */
/* ------------------------------------------------------------------ */

static int get_ipconn_entries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%d", wan_entries(NULL, 0));
	*value = dmstrdup(buf);
	return 0;
}

/*
 * AddObject / DeleteObject, wan_device_add_instance_ip / _ppp and
 * wan_device_del_instance of the shell.  hni.wan writes the entry; the add
 * queues no reload (the shell did not), the delete does.
 *
 * The instance the product reports after an add is the NUMBER OF ENTRIES, not
 * id+1 -- that is what the shell echoed and what the ACS has been told.  It
 * is counted like the shell's `uci show wan | grep -c '=entry$'`, on a fresh
 * UCI context: hni is another process, and the engine's context may hold the
 * package as it was before the add.
 */
static int wan_entry_count_fresh(void)
{
	struct uci_context *c = uci_alloc_context();
	struct uci_package *p = NULL;
	struct uci_element *e;
	int n = 0;

	if (!c)
		return 0;
	if (uci_load(c, "wan", &p) == UCI_OK && p) {
		uci_foreach_element(&p->sections, e) {
			if (strcmp(uci_to_section(e)->type, "entry") == 0)
				n++;
		}
	}
	uci_free_context(c);
	return n;
}

static int add_conn_instance(const char *kind, char **instance)
{
	json_object *res = NULL;
	char *result;
	int n;

	dmubus_call("hni.wan", "set",
		    UBUS_ARGS{{"action", "add", String}, {"param", (char *)kind, String}}, 2, &res);
	result = res ? dmjson_get_value(res, 1, "result") : NULL;
	if (!result || strcmp(result, "SUCCESS") != 0)
		return FAULT_9002;
	n = wan_entry_count_fresh();
	if (n <= 0)
		return FAULT_9002;	/* the shell's "echo 0" */
	dmasprintf(instance, "%d", n);
	return 0;
}

static int add_ipconn_instance(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	return add_conn_instance("IP", instance);
}

static int del_conn_instance(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (del_action != DEL_INST)
		return FAULT_9005;	/* the shell had no "delete all" here */
	if (!e)
		return FAULT_9002;
	if (wan_ubus_set(e->idx, "delete", NULL, NULL) != 0)
		return FAULT_9002;
	wan_reload();
	return 0;
}

/*
 * ExternalIPAddress is forced-inform per instance, not per leaf: a routed
 * entry carrying the TR-069 service bit, or the bridged entry 0 while the
 * product runs as an AP.  Both are the conditions the shell passed as the
 * last argument of common_execute_method_param().
 */
static unsigned char extip_forced_inform(char *refparam, struct dmctx *dmctx, void *data, char *instance)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return 0;
	if (e->bridge)
		return (e->id == 0 && strcmp(mtk_uci("clay", "opermode", "mode"), "ap") == 0) ? 1 : 0;
	return e->tr069 ? 1 : 0;
}

static struct dm_forced_inform_s DMFINFRM_EXTIP = { 0, extip_forced_inform };

/* ------------------------------------------------------------------ */
/* instances                                                           */
/* ------------------------------------------------------------------ */

static int browseWanIpConnInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct wan_entry *list;
	char *idx, *idx_last = NULL;
	int n, i;

	list = dmcalloc(WAN_MAX_ENTRIES, sizeof(*list));
	if (!list)
		return 0;
	n = wan_entries(&list, WAN_MAX_ENTRIES);
	if (n > WAN_MAX_ENTRIES)
		n = WAN_MAX_ENTRIES;
	for (i = 0; i < n; i++) {
		idx = handle_update_instance(3, dmctx, &idx_last, update_instance_without_section,
					     1, list[i].id + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&list[i], idx) == DM_STOP)
			break;
	}
	return 0;
}


/* ------------------------------------------------------------------ */
/* WANPPPConnection -- the leaves that are not shared with the IP object */
/* ------------------------------------------------------------------ */

/*
 * Everything above this point is shared: sub_entry_wandevice_wanconnectiondevice_ppp()
 * calls the same wan_device_get_* helpers as the IP entry for Enable,
 * ConnectionStatus, Name, Alias, Uptime, NAT, addressing, DNS, MAC, VLAN and
 * the X_AIS_* pair.  Only the leaves below differ, so they live here instead
 * of in a second file that would have to duplicate thirty getters.
 */

/* the PPP branch answered "echo IP_Routed" for both connection-type leaves */
static int get_ip_routed(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "IP_Routed";
	return 0;
}

static int get_transport_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "PPPoE";
	return 0;
}

static int get_ppp_username(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wan_entry_opt(data, "ppp_username");
	return 0;
}

/* wan.@entry[i].ppp_username AND network.if<id>.username, like the shell */
static int set_ppp_username(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return FAULT_9002;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "ppp_username", value ? value : "");
	dmuci_set_value("network", e->if4, "username", value ? value : "");
	wan_reload();
	return 0;
}

/*
 * DIFFERENCE from the product, deliberate: the shell answered
 * "uci get wan.@entry[i].ppp_password", handing the PPP password to whoever
 * asked.  TR-098 says this parameter reads back as an empty string, and an
 * ACS never needs the value it wrote.  Writing still works.
 */
static int get_ppp_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int set_ppp_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;

	if (!e)
		return FAULT_9002;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value_by_section(e->s, "ppp_password", value ? value : "");
	dmuci_set_value("network", e->if4, "password", value ? value : "");
	wan_reload();
	return 0;
}

static int get_ppp_remote_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;

	*value = e ? iface_ptp_peer(e->if4) : "";
	return 0;
}

/* network.if<id>.mtu, and 1492 when it was never set -- the shell's default */
static int get_ppp_max_mru(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char *v;

	if (!e)
		return 0;
	v = mtk_uci("network", e->if4, "mtu");
	*value = v[0] ? v : "1492";
	return 0;
}

static int set_ppp_max_mru(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	long mru = 0;

	if (!e)
		return FAULT_9002;
	if (!wan_str_is_uint(value, &mru))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value("network", e->if4, "mtu", value);
	wan_reload();
	return 0;
}

/* the MTU the ppp daemon actually negotiated, read off the l3 device */
static int get_ppp_current_mru(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char path[128];
	char *dev;

	*value = "";
	if (!e)
		return 0;
	dev = wan_iface_l3_device(e->if4);
	if (!dev[0])
		return 0;
	snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", dev);
	*value = mtk_file_line(path);
	return 0;
}

/*
 * MaxMTUSize on a PPP entry, mirrored bug.  The shell wrote
 * "wan_device_get_mtu $iface" / "wan_device_set_br_mtu $device" in a function
 * that never sets either variable, so the getter read
 * /sys/class/net//mtu and answered empty, and the setter range-checked the
 * value and then wrote the invalid path "network..mtu", i.e. it reported
 * success and changed nothing.  Same here: making it work would start moving
 * the MTU of a PPP link the product has never exposed.
 */
static int get_ppp_max_mtu(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int set_ppp_max_mtu(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	long mtu = 0;

	if (!wan_str_is_uint(value, &mtu))
		return FAULT_9007;
	if (mtu < 1 || mtu > 1540)
		return FAULT_9005;	/* the fault the shell returned here */
	return 0;
}

/*
 * Reset: wan_device_get_reset() is a constant "false".
 *
 * DIFFERENCE, deliberate: the shell ran "ifdown; sleep 1; ifup" inline, which
 * on the WAN carrying the session kills the connection before the response is
 * sent -- the ACS sees a timeout instead of a SetParameterValuesResponse.
 * Queued here through the apply-service list, so it runs after the session
 * ends, the same place every other change of this object lands.
 */
static int get_reset(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "false";
	return 0;
}

static int set_reset(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct wan_entry *e = (struct wan_entry *)data;
	char cmd[128];
	int b = mtk_parse_bool(value);

	if (!e)
		return FAULT_9002;
	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK || b == 0)
		return 0;
	snprintf(cmd, sizeof(cmd), "ifdown %s; sleep 1; ifup %s", e->if4, e->if4);
	mtk_apply_service(cmd);
	return 0;
}

static int get_pppconn_entries(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%d", wan_entries_kind(NULL, 0, WAN_KIND_PPP));
	*value = dmstrdup(buf);
	return 0;
}

static int add_pppconn_instance(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	return add_conn_instance("PPP", instance);
}

static int browseWanPppConnInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct wan_entry *list;
	char *idx, *idx_last = NULL;
	int n, i;

	list = dmcalloc(WAN_MAX_ENTRIES, sizeof(*list));
	if (!list)
		return 0;
	n = wan_entries_kind(&list, WAN_MAX_ENTRIES, WAN_KIND_PPP);
	if (n > WAN_MAX_ENTRIES)
		n = WAN_MAX_ENTRIES;
	for (i = 0; i < n; i++) {
		idx = handle_update_instance(3, dmctx, &idx_last, update_instance_without_section,
					     1, list[i].id + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&list[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tWanIpStatsParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"EthernetBytesSent", &DMREAD, DMT_UNINT, get_stat_tx_bytes, NULL, NULL, NULL},
{"EthernetBytesReceived", &DMREAD, DMT_UNINT, get_stat_rx_bytes, NULL, NULL, NULL},
{"EthernetPacketsSent", &DMREAD, DMT_UNINT, get_stat_tx_packets, NULL, NULL, NULL},
{"EthernetPacketsReceived", &DMREAD, DMT_UNINT, get_stat_rx_packets, NULL, NULL, NULL},
{"EthernetErrorsSent", &DMREAD, DMT_UNINT, get_stat_tx_errors, NULL, NULL, NULL},
{"EthernetErrorsReceived", &DMREAD, DMT_UNINT, get_stat_rx_errors, NULL, NULL, NULL},
{"EthernetDiscardPacketsSent", &DMREAD, DMT_UNINT, get_stat_tx_dropped, NULL, NULL, NULL},
{"EthernetDiscardPacketsReceived", &DMREAD, DMT_UNINT, get_stat_rx_dropped, NULL, NULL, NULL},
{"EthernetUnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_stat_rx_nohandler, NULL, NULL, NULL},
{0}
};

static DMOBJ tWanIpConnObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWanIpStatsParam, NULL},
{0}
};

static DMLEAF tWanIpConnParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_conn_enable, set_conn_enable, NULL, NULL},
{"ConnectionStatus", &DMREAD, DMT_STRING, get_conn_status, NULL, NULL, NULL},
{"PossibleConnectionTypes", &DMREAD, DMT_STRING, get_possible_types, NULL, NULL, NULL},
{"ConnectionType", &DMWRITE, DMT_STRING, get_conn_type, set_conn_type, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_conn_name, set_conn_name, NULL, NULL},
{"Alias", &DMREAD, DMT_STRING, get_conn_alias, NULL, NULL, NULL},
{"Uptime", &DMREAD, DMT_UNINT, get_conn_uptime, NULL, NULL, NULL},
{"LastConnectionError", &DMREAD, DMT_STRING, get_last_error, NULL, NULL, NULL},
{"NATEnabled", &DMWRITE, DMT_BOOL, get_nat_enabled, set_nat_enabled, NULL, NULL},
{"AddressingType", &DMWRITE, DMT_STRING, get_addressing_type, set_addressing_type, NULL, NULL},
{"ExternalIPAddress", &DMWRITE, DMT_STRING, get_external_ip, set_external_ip, &DMFINFRM_EXTIP, NULL},
{"SubnetMask", &DMWRITE, DMT_STRING, get_subnet_mask, set_subnet_mask, NULL, NULL},
{"DefaultGateway", &DMWRITE, DMT_STRING, get_default_gateway, set_default_gateway, NULL, NULL},
{"DNSEnabled", &DMWRITE, DMT_BOOL, get_dns_enabled, set_accept_and_drop, NULL, NULL},
{"DNSOverrideAllowed", &DMWRITE, DMT_BOOL, get_dns_override, set_dns_override, NULL, NULL},
{"DNSServers", &DMWRITE, DMT_STRING, get_dns_servers, set_dns_servers, NULL, NULL},
{"MaxMTUSize", &DMWRITE, DMT_UNINT, get_max_mtu, set_max_mtu, NULL, NULL},
{"MACAddress", &DMWRITE, DMT_STRING, get_conn_mac, set_conn_mac, NULL, NULL},
{"MACAddressOverride", &DMWRITE, DMT_BOOL, get_mac_override, set_mac_override, NULL, NULL},
{"X_AIS_VLANEnable", &DMWRITE, DMT_BOOL, get_vlan_enable, set_vlan_enable, NULL, NULL},
{"X_AIS_VLANID", &DMWRITE, DMT_UNINT, get_vlan_id, set_vlan_id, NULL, NULL},
{"X_AIS_VLAN8021P", &DMWRITE, DMT_UNINT, get_vlan_priority, set_vlan_priority, NULL, NULL},
{"X_AIS_DefaultRoute", &DMWRITE, DMT_BOOL, get_x_default_route, set_x_default_route, NULL, NULL},
{"X_AIS_IPMode", &DMWRITE, DMT_STRING, get_x_ip_mode, set_x_ip_mode, NULL, NULL},
{"X_AIS_LanInterface", &DMWRITE, DMT_STRING, get_x_lan_interface, set_x_lan_interface, NULL, NULL},
{0}
};

/* the same nine counters: stat_of() picks the netdev per entry kind */
static DMLEAF tWanPppStatsParam[] = {
{"EthernetBytesSent", &DMREAD, DMT_UNINT, get_stat_tx_bytes, NULL, NULL, NULL},
{"EthernetBytesReceived", &DMREAD, DMT_UNINT, get_stat_rx_bytes, NULL, NULL, NULL},
{"EthernetPacketsSent", &DMREAD, DMT_UNINT, get_stat_tx_packets, NULL, NULL, NULL},
{"EthernetPacketsReceived", &DMREAD, DMT_UNINT, get_stat_rx_packets, NULL, NULL, NULL},
{"EthernetErrorsSent", &DMREAD, DMT_UNINT, get_stat_tx_errors, NULL, NULL, NULL},
{"EthernetErrorsReceived", &DMREAD, DMT_UNINT, get_stat_rx_errors, NULL, NULL, NULL},
{"EthernetDiscardPacketsSent", &DMREAD, DMT_UNINT, get_stat_tx_dropped, NULL, NULL, NULL},
{"EthernetDiscardPacketsReceived", &DMREAD, DMT_UNINT, get_stat_rx_dropped, NULL, NULL, NULL},
{"EthernetUnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_stat_rx_nohandler, NULL, NULL, NULL},
{0}
};

static DMOBJ tWanPppConnObj[] = {
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWanPppStatsParam, NULL},
{0}
};

/*
 * Permissions follow the PPP branch, which is not the IP one everywhere:
 * DefaultGateway and RemoteIPAddress are read-only here, and
 * ExternalIPAddress / DNSEnabled take the shell's "true" setter -- accept the
 * write and change nothing.
 */
static DMLEAF tWanPppConnParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_conn_enable, set_conn_enable, NULL, NULL},
{"ConnectionStatus", &DMREAD, DMT_STRING, get_conn_status, NULL, NULL, NULL},
{"PossibleConnectionTypes", &DMREAD, DMT_STRING, get_ip_routed, NULL, NULL, NULL},
{"ConnectionType", &DMWRITE, DMT_STRING, get_ip_routed, set_conn_type, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_conn_name, set_conn_name, NULL, NULL},
{"Alias", &DMREAD, DMT_STRING, get_conn_alias, NULL, NULL, NULL},
{"Uptime", &DMREAD, DMT_UNINT, get_conn_uptime, NULL, NULL, NULL},
{"LastConnectionError", &DMREAD, DMT_STRING, get_last_error, NULL, NULL, NULL},
{"NATEnabled", &DMWRITE, DMT_BOOL, get_nat_enabled, set_nat_enabled, NULL, NULL},
{"AddressingType", &DMWRITE, DMT_STRING, get_addressing_type, set_addressing_type, NULL, NULL},
{"TransportType", &DMREAD, DMT_STRING, get_transport_type, NULL, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_ppp_username, set_ppp_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_ppp_password, set_ppp_password, NULL, NULL},
{"ExternalIPAddress", &DMWRITE, DMT_STRING, get_external_ip, set_accept_and_drop, &DMFINFRM_EXTIP, NULL},
{"SubnetMask", &DMWRITE, DMT_STRING, get_subnet_mask, set_subnet_mask, NULL, NULL},
{"DefaultGateway", &DMREAD, DMT_STRING, get_default_gateway, NULL, NULL, NULL},
{"RemoteIPAddress", &DMREAD, DMT_STRING, get_ppp_remote_ip, NULL, NULL, NULL},
{"DNSEnabled", &DMWRITE, DMT_BOOL, get_dns_enabled, set_accept_and_drop, NULL, NULL},
{"DNSOverrideAllowed", &DMWRITE, DMT_BOOL, get_dns_override, set_dns_override, NULL, NULL},
{"DNSServers", &DMWRITE, DMT_STRING, get_dns_servers, set_dns_servers, NULL, NULL},
{"MaxMTUSize", &DMWRITE, DMT_UNINT, get_ppp_max_mtu, set_ppp_max_mtu, NULL, NULL},
{"MaxMRUSize", &DMWRITE, DMT_UNINT, get_ppp_max_mru, set_ppp_max_mru, NULL, NULL},
{"CurrentMRUSize", &DMREAD, DMT_UNINT, get_ppp_current_mru, NULL, NULL, NULL},
{"MACAddress", &DMWRITE, DMT_STRING, get_conn_mac, set_conn_mac, NULL, NULL},
{"MACAddressOverride", &DMWRITE, DMT_BOOL, get_mac_override, set_mac_override, NULL, NULL},
{"X_AIS_VLANEnable", &DMWRITE, DMT_BOOL, get_vlan_enable, set_vlan_enable, NULL, NULL},
{"X_AIS_VLANID", &DMWRITE, DMT_UNINT, get_vlan_id, set_vlan_id, NULL, NULL},
{"X_AIS_VLAN8021P", &DMWRITE, DMT_UNINT, get_vlan_priority, set_vlan_priority, NULL, NULL},
{"X_AIS_DefaultRoute", &DMWRITE, DMT_BOOL, get_x_default_route, set_x_default_route, NULL, NULL},
{"X_AIS_IPMode", &DMWRITE, DMT_STRING, get_x_ip_mode, set_x_ip_mode, NULL, NULL},
{"X_AIS_LanInterface", &DMWRITE, DMT_STRING, get_x_lan_interface, set_x_lan_interface, NULL, NULL},
{"Reset", &DMWRITE, DMT_BOOL, get_reset, set_reset, NULL, NULL},
{0}
};

static DMLEAF tWanCxDevIpParam[] = {
{"WANIPConnectionNumberOfEntries", &DMREAD, DMT_UNINT, get_ipconn_entries, NULL, NULL, NULL},
{"WANPPPConnectionNumberOfEntries", &DMREAD, DMT_UNINT, get_pppconn_entries, NULL, NULL, NULL},
{0}
};

static DMOBJ tWanCxDevIpObj[] = {
{"WANIPConnection", &DMWRITE, add_ipconn_instance, del_conn_instance, NULL, browseWanIpConnInst,
 NULL, NULL, tWanIpConnObj, tWanIpConnParam, NULL},
{"WANPPPConnection", &DMWRITE, add_pppconn_instance, del_conn_instance, NULL, browseWanPppConnInst,
 NULL, NULL, tWanPppConnObj, tWanPppConnParam, NULL},
{0}
};

/* browseinstobj left NULL twice on purpose: wan_mtk.c owns the WANDevice and
 * WANConnectionDevice instances, dm_registry merges this subtree into them */
static DMOBJ tWanDeviceIpObj[] = {
{"WANConnectionDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL,
 tWanCxDevIpObj, tWanCxDevIpParam, NULL},
{0}
};

static DMOBJ tWanDeviceIpRoot[] = {
{"WANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWanDeviceIpObj, NULL, NULL},
{0}
};

/*
 * No .paths: wan_mtk.c claims the whole WANDevice branch (K8), dm_registry
 * merges this tree into it.
 */
static const struct dm_module wanip_mtk_module = {
	.name  = "mtk-wanip",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWanDeviceIpRoot,
};
DM_MODULE_REGISTER(wanip_mtk_module);
