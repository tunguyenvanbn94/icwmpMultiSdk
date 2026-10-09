/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Firewall. -- ported from functions/tr098/firewall.
 *	All of it lives in the product's firewall_clay config; hni applies it.
 *
 *	  Config, Enable                  constants "High" / "true"
 *	  X_AIS_DisablePort.{i}           disable_port sections, at most 32
 *	  X_AIS_ServiceControl.IPV4ServiceControl.{i}, .IPV6ServiceControl.{i}
 *	                                  packetfilter sections, by ipversion
 *	                                  ("ipv4" / "ipv6"), at most 64 each
 *	  X_AIS_IPFilter.{i}              ipfilter2 sections, at most 20
 *
 *	Instance numbers are positions, as in the shell: instance N is the N-th
 *	section of its kind in the file, so deleting one renumbers the ones
 *	after it.  AddObject writes the shell's defaults and answers the next
 *	position; DeleteObject removes the section.  Every write queues the
 *	reload the shell queued (one per session per service):
 *	  DisablePort     ubus call hni.service commit {DisablePort, ApplyRule},
 *	                  plus hni.service set {DisablePort, ruleIdx} on add and
 *	                  when Interface changes
 *	  ServiceControl  ubus call hni.service commit {ServiceControl, ApplyRule}
 *	  IPFilter        ubus call hni reloadIpFilter2
 *
 *	Checks that depend on another value of the same rule or of the other
 *	rules (an IPFilter address or mask against the rule's IPVersion, an
 *	Order already taken) run at VALUESET: the shell ran its setters one by
 *	one in request order, so "IPVersion=6, SourceIP=<v6>" in one SPV was
 *	accepted.  The engine runs every VALUECHECK first; a fault at VALUESET
 *	still faults the RPC and reverts its UCI writes (dm_entry_apply).
 *	Everything else is checked at VALUECHECK.
 *
 *	Differences from the shell, on purpose:
 *	  - the commit is the engine's (the shell committed per leaf, so an SPV
 *	    failing on a later leaf kept the earlier ones);
 *	  - an instance number past the last rule is an unknown object, where
 *	    the shell printed empty leaves for any DisablePort instance 1..32;
 *	  - "...WANPPPConnection.0" is refused: the shell turned it into
 *	    "pppoe-if-1";
 *	  - DestIP=NULL on a rule without dst_addr succeeds: the shell's failed
 *	    "uci delete" made it 9002 (SourceIP=NULL never did).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"

#define FW_PKG		"firewall_clay"
#define DP_TYPE		"disable_port"
#define DP_MAX		32
#define PF_TYPE		"packetfilter"
#define SC_MAX		64
#define IPF_TYPE	"ipfilter2"
#define IPF_MAX		20

#define DP_APPLY	"ubus call hni.service commit '{ \"param\": \"DisablePort\", \"action\": \"ApplyRule\" }'"
#define SC_APPLY	"ubus call hni.service commit '{ \"param\": \"ServiceControl\", \"action\": \"ApplyRule\" }'"
#define IPF_APPLY	"ubus call hni reloadIpFilter2 &"

#define LAN_IPIF_PATH	"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.IPInterface.1"
#define WAN_CD_PATH	"InternetGatewayDevice.WANDevice.1.WANConnectionDevice.1."

/* ------------------------------------------------------------------ */
/* common helpers                                                      */
/* ------------------------------------------------------------------ */

static char *fw_opt(struct uci_section *s, const char *option)
{
	char *v = NULL;

	if (!s)
		return "";
	dmuci_get_value_by_section_string(s, (char *)option, &v);
	return v ? v : "";
}

static void fw_set(struct uci_section *s, const char *option, const char *value)
{
	dmuci_set_value_by_section(s, (char *)option, (char *)value);
}

/* position (0 based) of s among the sections of its type, -1 if absent */
static int fw_position(const char *type, struct uci_section *target)
{
	struct uci_section *s;
	int n = 0;

	uci_foreach_sections(FW_PKG, (char *)type, s) {
		if (s == target)
			return n;
		n++;
	}
	return -1;
}

static int fw_count(const char *type)
{
	struct uci_section *s;
	int n = 0;

	uci_foreach_sections(FW_PKG, (char *)type, s)
		n++;
	return n;
}

/* is_integer: an optional "-" then digits */
static int fw_integer(const char *v, long long *out)
{
	const char *p = v;

	if (!v || !*v || strlen(v) > 18)
		return 0;
	if (*p == '-')
		p++;
	if (!*p)
		return 0;
	for (; *p; p++) {
		if (*p < '0' || *p > '9')
			return 0;
	}
	*out = strtoll(v, NULL, 10);
	return 1;
}

/* is_in_range */
static int fw_in_range(const char *v, long long min, long long max)
{
	long long n;

	return fw_integer(v, &n) && n >= min && n <= max;
}

/* case "$v" in 0|1) */
static int fw_01(const char *v)
{
	return v && (strcmp(v, "0") == 0 || strcmp(v, "1") == 0);
}

/* is_nullable_value */
static int fw_nullable(const char *v)
{
	return !v || !*v || strcmp(v, "NULL") == 0 || strcmp(v, "null") == 0 || strcmp(v, "Null") == 0;
}

/* network.if<id> exists and has a device: that device, else NULL */
static char *fw_wan_device(long long wan_id)
{
	char sec[24], *type = NULL, *dev;

	if (wan_id < 0 || wan_id > 9999)
		return NULL;
	snprintf(sec, sizeof(sec), "if%lld", wan_id);
	dmuci_get_section_type("network", sec, &type);
	if (!type || !*type)
		return NULL;
	dev = mtk_uci("network", sec, "device");
	return *dev ? dev : NULL;
}

/* device -> WAN connection path: pppoe-if<n> is WANPPPConnection.<n+1>,
 * the device of network.if<n> (n < 8) is WANIPConnection.<n+1>.  NULL when
 * neither (iface_to_tr069 / service_control_interface_to_trpath). */
static char *fw_iface_to_wan_path(const char *iface)
{
	long long id;
	char *out = NULL;
	int i;

	if (strncmp(iface, "pppoe-if", 8) == 0) {
		const char *n = iface + 8;

		if (!*n || strspn(n, "0123456789") != strlen(n) || !fw_integer(n, &id))
			return NULL;
		dmasprintf(&out, WAN_CD_PATH "WANPPPConnection.%lld", id + 1);
		return out;
	}
	for (i = 0; i < 8; i++) {
		char *dev = fw_wan_device(i);

		if (dev && strcmp(dev, iface) == 0) {
			dmasprintf(&out, WAN_CD_PATH "WANIPConnection.%d", i + 1);
			return out;
		}
	}
	return NULL;
}

/* WAN connection path -> device; the object number after the last "." */
static char *fw_wan_path_to_iface(const char *path, int ppp)
{
	const char *last = strrchr(path, '.');
	long long n;
	char *out = NULL;

	if (!last || !fw_integer(last + 1, &n) || last[1] == '-' || n < 1)
		return NULL;
	if (ppp) {
		dmasprintf(&out, "pppoe-if%lld", n - 1);
		return out;
	}
	return fw_wan_device(n - 1);
}

/* the shell's "<prefix>"*.WANConnectionDevice.*.<kind>.* glob */
static int fw_glob_wan(const char *v, const char *kind)
{
	const char *cd;

	if (strncmp(v, "InternetGatewayDevice.WANDevice.", 32) != 0)
		return 0;
	cd = strstr(v + 32, ".WANConnectionDevice.");
	return cd && strstr(cd + 21, kind) != NULL;
}

/* positional browse over sections; match() filters, NULL = all */
static int fw_browse(struct dmctx *dmctx, DMNODE *parent_node, const char *type,
		     int (*match)(struct uci_section *, const void *), const void *arg)
{
	struct uci_section *s;
	char *idx, *idx_last = NULL;
	int n = 0;

	uci_foreach_sections(FW_PKG, (char *)type, s) {
		if (match && !match(s, arg))
			continue;
		idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, ++n);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)s, idx) == DM_STOP)
			break;
	}
	return 0;
}

static int get_fw_config(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "High";
	return 0;
}

static int get_fw_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

/* ------------------------------------------------------------------ */
/* X_AIS_DisablePort                                                   */
/* ------------------------------------------------------------------ */

static void dp_sync(int index)
{
	char cmd[128];

	snprintf(cmd, sizeof(cmd), "ubus call hni.service set '{ \"param\": \"DisablePort\", \"ruleIdx\": \"%d\" }'", index);
	mtk_apply_service_once(cmd);
}

static int browse_dp(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	return fw_browse(dmctx, parent_node, DP_TYPE, NULL, NULL);
}

static int add_dp(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	struct uci_section *added = NULL;
	char *name = NULL;
	int n = fw_count(DP_TYPE);

	if (n >= DP_MAX)
		return FAULT_9004;	/* E_RESOURCES_EXCEEDED */
	dmuci_add_section(FW_PKG, DP_TYPE, &added, &name);
	if (!added)
		return FAULT_9002;
	fw_set(added, "active", "0");
	fw_set(added, "port", "NULL");
	fw_set(added, "name", "FWIPF1");
	fw_set(added, "interface", "WAN_1");
	dp_sync(n);
	dmasprintf(instance, "%d", n + 1);
	return 0;
}

static int del_dp(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	if (del_action != DEL_INST)
		return FAULT_9005;	/* the shell had no "delete all" */
	if (!data)
		return FAULT_9002;
	dmuci_delete_by_section((struct uci_section *)data, NULL, NULL);
	mtk_apply_service_once(DP_APPLY);
	return 0;
}

#define DP_GET(name, option)							\
static int get_dp_##name(char *refparam, struct dmctx *ctx, void *data,		\
			 char *instance, char **value)				\
{										\
	*value = fw_opt((struct uci_section *)data, option);			\
	return 0;								\
}
DP_GET(name, "name")
DP_GET(interface, "interface")
DP_GET(port, "port")
DP_GET(enable, "active")

static int set_dp_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || !*value || strlen(value) > 32)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	fw_set(data, "name", value);
	mtk_apply_service_once(DP_APPLY);
	return 0;
}

static int set_dp_interface(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	/* LAN|WAN_[1-6] */
	if (!value || !(strcmp(value, "LAN") == 0 ||
			(strncmp(value, "WAN_", 4) == 0 && value[4] >= '1' && value[4] <= '6' && !value[5])))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	fw_set(data, "interface", value);
	dp_sync(fw_position(DP_TYPE, data));
	mtk_apply_service_once(DP_APPLY);
	return 0;
}

static int set_dp_port(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || !*value || strspn(value, "0123456789") != strlen(value) || !fw_in_range(value, 1, 65535))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	fw_set(data, "port", value);
	mtk_apply_service_once(DP_APPLY);
	return 0;
}

static int set_dp_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!fw_01(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	fw_set(data, "active", value);
	mtk_apply_service_once(DP_APPLY);
	return 0;
}

/* ------------------------------------------------------------------ */
/* X_AIS_ServiceControl                                                */
/* ------------------------------------------------------------------ */

static int sc_match(struct uci_section *s, const void *ipversion)
{
	return strcmp(fw_opt(s, "ipversion"), (const char *)ipversion) == 0;
}

static int sc_count(const char *ipversion)
{
	struct uci_section *s;
	int n = 0;

	uci_foreach_sections(FW_PKG, PF_TYPE, s) {
		if (sc_match(s, ipversion))
			n++;
	}
	return n;
}

static int browse_sc4(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	return fw_browse(dmctx, parent_node, PF_TYPE, sc_match, "ipv4");
}

static int browse_sc6(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	return fw_browse(dmctx, parent_node, PF_TYPE, sc_match, "ipv6");
}

/* service_control_rule_add_by_ipversion */
static int sc_add(const char *ipversion, char **instance)
{
	struct uci_section *added = NULL;
	char *name = NULL;
	int v6 = strcmp(ipversion, "ipv6") == 0;
	int n = sc_count(ipversion);

	if (n >= SC_MAX)
		return FAULT_9004;
	dmuci_add_section(FW_PKG, PF_TYPE, &added, &name);
	if (!added)
		return FAULT_9002;
	fw_set(added, "enabled", "0");
	fw_set(added, "name", "FWSC1");
	fw_set(added, "action", "accept");
	fw_set(added, "editable", "1");
	fw_set(added, "interface", "wan");
	fw_set(added, "other_protocol", "udp");
	fw_set(added, "ipversion", ipversion);
	fw_set(added, "start_ip", v6 ? "::" : "0.0.0.0");
	fw_set(added, "end_ip", v6 ? "-" : "0.0.0.0");
	fw_set(added, "prefix_len", "0");
	fw_set(added, "other_port", "-");
	fw_set(added, "service_type", "-");
	fw_set(added, "other_enable", "0");
	mtk_apply_service_once(SC_APPLY);
	dmasprintf(instance, "%d", n + 1);
	return 0;
}

static int add_sc4(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	return sc_add("ipv4", instance);
}

static int add_sc6(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	return sc_add("ipv6", instance);
}

static int del_sc(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	if (del_action != DEL_INST)
		return FAULT_9005;
	if (!data)
		return FAULT_9002;
	dmuci_delete_by_section((struct uci_section *)data, NULL, NULL);
	mtk_apply_service_once(SC_APPLY);
	return 0;
}

/* the VALUESET tail of every ServiceControl setter */
static int sc_write(void *data, const char *option, const char *value)
{
	fw_set(data, option, value);
	mtk_apply_service_once(SC_APPLY);
	return 0;
}

static int get_sc_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = fw_opt(data, "enabled");
	return 0;
}

static int set_sc_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!fw_01(value))
		return FAULT_9007;
	return action == VALUECHECK ? 0 : sc_write(data, "enabled", value);
}

static int get_sc_ip_start(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = fw_opt(data, "start_ip");

	*value = strcmp(v, "-") == 0 ? "0.0.0.0" : v;
	return 0;
}

static int set_sc_ip_start(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!mtk_shell_ipv4(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(fw_opt(data, "end_ip"), "-") == 0)
		fw_set(data, "end_ip", "255.255.255.255");
	return sc_write(data, "start_ip", value);
}

static int get_sc_ip_end(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = fw_opt(data, "end_ip");

	*value = strcmp(v, "-") == 0 ? "255.255.255.255" : v;
	return 0;
}

static int set_sc_ip_end(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!mtk_shell_ipv4(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (strcmp(fw_opt(data, "start_ip"), "-") == 0)
		fw_set(data, "start_ip", "0.0.0.0");
	return sc_write(data, "end_ip", value);
}

static int get_sc_prefix(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = fw_opt(data, "start_ip");
	return 0;
}

static int set_sc_prefix(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!mtk_shell_ipv6(value))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	fw_set(data, "end_ip", "-");
	return sc_write(data, "start_ip", value);
}

static int get_sc_prefix_len(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = fw_opt(data, "prefix_len");

	*value = *v ? v : "0";
	return 0;
}

static int set_sc_prefix_len(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!fw_in_range(value, 0, 128))
		return FAULT_9007;
	return action == VALUECHECK ? 0 : sc_write(data, "prefix_len", value);
}

static int get_sc_ingress(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *iface = fw_opt(data, "interface"), *path;

	if (strcmp(iface, "all") == 0)
		*value = "WAN_ALL,LAN";
	else if (strcmp(iface, "wan") == 0)
		*value = "WAN_ALL";
	else if (strcmp(iface, "br-lan") == 0)
		*value = "LAN";
	else if (fw_nullable(iface))
		*value = "NULL";
	else
		*value = (path = fw_iface_to_wan_path(iface)) ? path : iface;
	return 0;
}

/* Ingress value -> firewall_clay interface, NULL when refused */
static char *sc_ingress_iface(const char *v)
{
	if (strcmp(v, "WAN_ALL,LAN") == 0 || strcmp(v, "LAN,WAN_ALL") == 0)
		return "all";
	if (strcmp(v, "WAN_ALL") == 0)
		return "wan";
	if (strcmp(v, "LAN") == 0)
		return "br-lan";
	if (fw_glob_wan(v, ".WANPPPConnection."))
		return fw_wan_path_to_iface(v, 1);
	if (fw_glob_wan(v, ".WANIPConnection."))
		return fw_wan_path_to_iface(v, 0);
	return NULL;
}

static int set_sc_ingress(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *iface;

	if (!value || !(iface = sc_ingress_iface(value)) || !*iface)
		return FAULT_9007;
	return action == VALUECHECK ? 0 : sc_write(data, "interface", iface);
}

static int get_sc_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *a = fw_opt(data, "action");

	*value = (strcmp(a, "block") == 0 || strcmp(a, "drop") == 0) ? "Drop" : "Accept";
	return 0;
}

static int set_sc_mode(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *a;

	if (value && strcmp(value, "Accept") == 0)
		a = "accept";
	else if (value && (strcmp(value, "Drop") == 0 || strcmp(value, "block") == 0))
		a = "block";
	else
		return FAULT_9007;
	return action == VALUECHECK ? 0 : sc_write(data, "action", a);
}

static int get_sc_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = fw_opt(data, "name");
	return 0;
}

static int set_sc_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (!value || !*value || strlen(value) > 32)
		return FAULT_9007;
	return action == VALUECHECK ? 0 : sc_write(data, "name", value);
}

/* the service names of the ACS and of the rule */
static const char *const sc_services[][2] = {
	{ "HTTP", "HTTP" }, { "HTTPS", "HTTPS" }, { "SSH", "SSH" },
	{ "FTP", "FTP" }, { "TELNET", "TELNET" }, { "PING", "ICMP" },
};
#define SC_N_SERVICES	(sizeof(sc_services) / sizeof(sc_services[0]))

static int get_sc_service_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = fw_opt(data, "service_type"), *out, *w;
	const char *p = v;
	size_t i;

	if (!*v || strcmp(v, "-") == 0) {
		*value = "NULL";
		return 0;
	}
	out = dmcalloc(1, strlen(v) * 2 + 8);
	if (!out)
		return 0;
	w = out;
	while (*p) {
		size_t l;
		const char *show = NULL;

		while (*p == ' ')
			p++;
		if (!*p)
			break;
		l = strcspn(p, " ");
		/* HTTP|http) HTTP ... ICMP|icmp) PING: the rule word in upper or
		 * lower case, nothing in between */
		for (i = 0; i < SC_N_SERVICES; i++) {
			const char *r = sc_services[i][1];
			size_t k;

			if (strlen(r) != l)
				continue;
			if (strncmp(p, r, l) == 0) {
				show = sc_services[i][0];
				break;
			}
			for (k = 0; k < l && p[k] == (char)(r[k] - 'A' + 'a'); k++)
				;
			if (k == l) {
				show = sc_services[i][0];
				break;
			}
		}
		if (w != out)
			*w++ = ',';
		if (show) {
			strcpy(w, show);
			w += strlen(show);
		} else {
			memcpy(w, p, l);
			w += l;
		}
		p += l;
	}
	*w = '\0';
	*value = *out ? out : "NULL";
	return 0;
}

/* service_control_normalize_service_type: "HTTP,PING" -> "HTTP ICMP", NULL
 * when refused */
static char *sc_service_rule(const char *v)
{
	char *out, *w;
	const char *p = v;
	size_t i, n;

	if (!*v || strcmp(v, "NULL") == 0)
		return "-";
	n = strlen(v);
	if (v[0] == ',' || v[n - 1] == ',' || strstr(v, ",,"))
		return NULL;
	out = dmcalloc(1, n + 8);
	if (!out)
		return NULL;
	w = out;
	while (*p) {
		size_t l = strcspn(p, ",");
		const char *rule = NULL;

		for (i = 0; i < SC_N_SERVICES; i++) {
			if (strlen(sc_services[i][0]) == l && strncmp(p, sc_services[i][0], l) == 0) {
				rule = sc_services[i][1];
				break;
			}
		}
		if (!rule)
			return NULL;
		if (w != out)
			*w++ = ' ';
		strcpy(w, rule);
		w += strlen(rule);
		p += l;
		if (*p == ',')
			p++;
	}
	return out;
}

static int set_sc_service_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *rule;

	if (!value || !(rule = sc_service_rule(value)))
		return FAULT_9007;
	return action == VALUECHECK ? 0 : sc_write(data, "service_type", rule);
}

static int get_sc_other_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = fw_opt(data, "other_protocol");

	if (strcmp(v, "tcp") == 0)
		*value = "TCP";
	else if (strcmp(v, "udp") == 0)
		*value = "UDP";
	else if (strcmp(v, "tcp/udp") == 0)
		*value = "TCP/UDP";
	else
		*value = v;
	return 0;
}

static int set_sc_other_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *p;

	if (value && strcmp(value, "TCP") == 0)
		p = "tcp";
	else if (value && strcmp(value, "UDP") == 0)
		p = "udp";
	else if (value && strcmp(value, "TCP/UDP") == 0)
		p = "tcp/udp";
	else
		return FAULT_9007;
	return action == VALUECHECK ? 0 : sc_write(data, "other_protocol", p);
}

static int get_sc_other_port(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = fw_opt(data, "other_port");

	*value = (*v && strcmp(v, "-") != 0) ? v : "NULL";
	return 0;
}

static int set_sc_other_port(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *colon;

	if (fw_nullable(value)) {
		if (action == VALUECHECK)
			return 0;
		fw_set(data, "other_enable", "0");
		return sc_write(data, "other_port", "-");
	}
	colon = strchr(value, ':');
	if (colon) {
		char first[24];
		long long a, b;
		size_t l = (size_t)(colon - value);

		/* *:*:* refused; a:b with 1 <= a <= b <= 65535 */
		if (strchr(colon + 1, ':') || l >= sizeof(first))
			return FAULT_9007;
		memcpy(first, value, l);
		first[l] = '\0';
		if (!fw_in_range(first, 1, 65535) || !fw_in_range(colon + 1, 1, 65535))
			return FAULT_9007;
		fw_integer(first, &a);
		fw_integer(colon + 1, &b);
		if (a > b)
			return FAULT_9007;
	} else if (!fw_in_range(value, 1, 65535)) {
		return FAULT_9007;
	}
	if (action == VALUECHECK)
		return 0;
	fw_set(data, "other_enable", "1");
	return sc_write(data, "other_port", value);
}

/* ------------------------------------------------------------------ */
/* X_AIS_IPFilter                                                      */
/* ------------------------------------------------------------------ */

static int browse_ipf(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	return fw_browse(dmctx, parent_node, IPF_TYPE, NULL, NULL);
}

/* ipfilter2_uci_get: the option, or dflt when unset or empty */
static char *ipf_get(void *data, const char *option, char *dflt)
{
	char *v = fw_opt(data, option);

	return *v ? v : dflt;
}

/* priority of a rule, defaulting to its position + 1 */
static char *ipf_priority(struct uci_section *s, int pos)
{
	char *v = fw_opt(s, "priority"), *d = NULL;

	if (*v)
		return v;
	dmasprintf(&d, "%d", pos + 1);
	return d ? d : "";
}

/* ipfilter2_get_new_priority: lowest of 1..20 no rule uses, 0 if none */
static int ipf_new_priority(void)
{
	struct uci_section *s;
	int used[IPF_MAX + 1] = { 0 };
	int pos = 0, p;

	uci_foreach_sections(FW_PKG, IPF_TYPE, s) {
		long long n;

		if (fw_integer(ipf_priority(s, pos), &n) && n >= 1 && n <= IPF_MAX)
			used[n] = 1;
		pos++;
	}
	for (p = 1; p <= IPF_MAX; p++) {
		if (!used[p])
			return p;
	}
	return 0;
}

static int add_ipf(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	struct uci_section *added = NULL;
	char *name = NULL, buf[16];
	int n = fw_count(IPF_TYPE), prio;
	static const char *const defaults[][2] = {
		{ "active", "0" }, { "target", "allow" }, { "ipversion", "-1" },
		{ "src_addr_exclude", "0" }, { "dst_addr_exclude", "0" },
		{ "protocol", "-1" }, { "protocol_exclude", "0" },
		{ "src_port", "-1" }, { "src_port_end", "-1" }, { "src_port_exclude", "0" },
		{ "dst_port", "-1" }, { "dst_port_end", "-1" }, { "dst_port_exclude", "0" },
		{ "ingress_all_iface", "0" }, { "ingress_exclude", "0" },
		{ "egress_all_iface", "0" }, { "egress_exclude", "0" },
		{ "dscp", "-1" }, { "dscp_exclude", "0" },
	};
	size_t i;

	if (n >= IPF_MAX)
		return FAULT_9004;
	prio = ipf_new_priority();
	if (!prio)
		return FAULT_9004;
	dmuci_add_section(FW_PKG, IPF_TYPE, &added, &name);
	if (!added)
		return FAULT_9002;
	for (i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
		fw_set(added, defaults[i][0], defaults[i][1]);
	snprintf(buf, sizeof(buf), "FWIPF%d", n + 1);
	fw_set(added, "name", buf);
	snprintf(buf, sizeof(buf), "%d", prio);
	fw_set(added, "priority", buf);
	mtk_apply_service_once(IPF_APPLY);
	dmasprintf(instance, "%d", n + 1);
	return 0;
}

static int del_ipf(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	if (del_action != DEL_INST)
		return FAULT_9005;
	if (!data)
		return FAULT_9002;
	dmuci_delete_by_section((struct uci_section *)data, NULL, NULL);
	mtk_apply_service_once(IPF_APPLY);
	return 0;
}

/* set_X_AIS_IPFilter_Param prologue: rules past the 20th are not writable */
static int ipf_writable(void *data)
{
	int pos = fw_position(IPF_TYPE, data);

	if (pos < 0)
		return FAULT_9002;
	return pos < IPF_MAX ? 0 : FAULT_9007;
}

/* the VALUESET tail */
static int ipf_write(void *data, const char *option, const char *value)
{
	fw_set(data, option, value);
	mtk_apply_service_once(IPF_APPLY);
	return 0;
}

static int ipf_delete_option(void *data, const char *option)
{
	dmuci_delete_by_section((struct uci_section *)data, (char *)option, NULL);
	mtk_apply_service_once(IPF_APPLY);
	return 0;
}

/* a 0/1 leaf: getter "1" only for "1"; setter only "0" or "1" */
#define IPF_BOOL(name, option)							\
static int get_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	*value = strcmp(ipf_get(data, option, "0"), "1") == 0 ? "1" : "0";	\
	return 0;								\
}										\
static int set_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char *value, int action)		\
{										\
	int f = ipf_writable(data);						\
										\
	if (f)									\
		return f;							\
	if (!fw_01(value))							\
		return FAULT_9007;						\
	return action == VALUECHECK ? 0 : ipf_write(data, option, value);	\
}
IPF_BOOL(enable, "active")
IPF_BOOL(src_ip_exclude, "src_addr_exclude")
IPF_BOOL(dst_ip_exclude, "dst_addr_exclude")
IPF_BOOL(protocol_exclude, "protocol_exclude")
IPF_BOOL(src_port_exclude, "src_port_exclude")
IPF_BOOL(dst_port_exclude, "dst_port_exclude")
IPF_BOOL(src_all_iface, "ingress_all_iface")
IPF_BOOL(src_iface_exclude, "ingress_exclude")
IPF_BOOL(dst_all_iface, "egress_all_iface")
IPF_BOOL(dst_iface_exclude, "egress_exclude")
IPF_BOOL(dscp_exclude, "dscp_exclude")

/* a number leaf, "-1" or min..max */
#define IPF_NUMBER(name, option, min, max)					\
static int get_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	*value = ipf_get(data, option, "-1");					\
	return 0;								\
}										\
static int set_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char *value, int action)		\
{										\
	int f = ipf_writable(data);						\
										\
	if (f)									\
		return f;							\
	if (!value || (strcmp(value, "-1") != 0 && !fw_in_range(value, min, max)))	\
		return FAULT_9007;						\
	return action == VALUECHECK ? 0 : ipf_write(data, option, value);	\
}
IPF_NUMBER(src_port, "src_port", 1, 65535)
IPF_NUMBER(src_port_end, "src_port_end", 1, 65535)
IPF_NUMBER(dst_port, "dst_port", 1, 65535)
IPF_NUMBER(dst_port_end, "dst_port_end", 1, 65535)
IPF_NUMBER(dscp, "dscp", 0, 63)

static int get_ipf_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = fw_opt(data, "name");

	if (*v)
		*value = v;
	else
		dmasprintf(value, "FWIPF%d", fw_position(IPF_TYPE, data) + 1);
	return 0;
}

static int set_ipf_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *v = value;
	int f = ipf_writable(data);

	if (f)
		return f;
	if (v && strlen(v) > 32)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	if (!v || !*v)
		dmasprintf(&v, "FWIPF%d", fw_position(IPF_TYPE, data) + 1);
	return ipf_write(data, "name", v);
}

/* the spellings of normalize_target / target_to_tr069 */
static const char *const ipf_allow_words[] = { "Accept", "ACCEPT", "accept", "Allow", "ALLOW", "allow", NULL };
static const char *const ipf_block_words[] = { "Drop", "DROP", "drop", "Block", "BLOCK", "block",
					       "Deny", "DENY", "deny", NULL };

static int fw_word_in(const char *v, const char *const *list)
{
	int i;

	for (i = 0; v && list[i]; i++)
		if (strcmp(v, list[i]) == 0)
			return 1;
	return 0;
}

static int get_ipf_target(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	/* target_to_tr069: a drop spelling is "Drop", anything else "Accept" */
	*value = fw_word_in(ipf_get(data, "target", "allow"), ipf_block_words) ? "Drop" : "Accept";
	return 0;
}

/* normalize_target: the three spellings of each word only */
static const char *ipf_target_rule(const char *v)
{
	if (fw_word_in(v, ipf_allow_words))
		return "allow";
	if (fw_word_in(v, ipf_block_words))
		return "block";
	return NULL;
}

static int set_ipf_target(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *t;
	int f = ipf_writable(data);

	if (f)
		return f;
	if (!(t = ipf_target_rule(value)))
		return FAULT_9007;
	return action == VALUECHECK ? 0 : ipf_write(data, "target", t);
}

static int get_ipf_order(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ipf_priority(data, fw_position(IPF_TYPE, data));
	return 0;
}

/* ipfilter2_validate_order, at VALUESET: no other rule holds that order */
static int set_ipf_order(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct uci_section *s;
	long long want, n;
	int pos = 0, f = ipf_writable(data);

	if (f)
		return f;
	if (!fw_in_range(value, 1, IPF_MAX))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	fw_integer(value, &want);
	uci_foreach_sections(FW_PKG, IPF_TYPE, s) {
		if (s != data && fw_integer(ipf_priority(s, pos), &n) && n == want)
			return FAULT_9007;
		pos++;
	}
	return ipf_write(data, "priority", value);
}

static int get_ipf_ipversion(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = ipf_get(data, "ipversion", "-1");

	*value = (strcmp(v, "4") == 0 || strcmp(v, "6") == 0) ? v : "-1";
	return 0;
}

static int set_ipf_ipversion(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int f = ipf_writable(data);

	if (f)
		return f;
	if (!value || (strcmp(value, "-1") != 0 && strcmp(value, "4") != 0 && strcmp(value, "6") != 0))
		return FAULT_9007;
	return action == VALUECHECK ? 0 : ipf_write(data, "ipversion", value);
}

/* ipfilter2_validate_ip against the rule's ipversion as it stands now */
static int ipf_ip_ok(void *data, const char *v)
{
	char *ver = ipf_get(data, "ipversion", "-1");

	if (strcmp(ver, "4") == 0)
		return mtk_shell_ipv4(v);
	if (strcmp(ver, "6") == 0)
		return mtk_shell_ipv6(v);
	if (strcmp(ver, "-1") == 0)
		return mtk_shell_ipv4(v) || mtk_shell_ipv6(v);
	return 0;
}

/* ipfilter2_validate_mask, same */
static int ipf_mask_ok(void *data, const char *v)
{
	char *ver = ipf_get(data, "ipversion", "-1");

	if (strcmp(ver, "4") == 0)
		return fw_in_range(v, 0, 32);
	if (strcmp(ver, "6") == 0)
		return fw_in_range(v, 0, 128);
	return 0;
}

#define IPF_ADDR(name, option, dflt, okfn, precheck)				\
static int get_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	*value = ipf_get(data, option, dflt);					\
	return 0;								\
}										\
static int set_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char *value, int action)		\
{										\
	int f = ipf_writable(data);						\
										\
	if (f)									\
		return f;							\
	if (fw_nullable(value))							\
		return action == VALUECHECK ? 0 : ipf_delete_option(data, option);	\
	if (!(precheck))							\
		return FAULT_9007;						\
	if (action == VALUECHECK)						\
		return 0;							\
	if (!okfn(data, value))							\
		return FAULT_9007;						\
	return ipf_write(data, option, value);					\
}
IPF_ADDR(src_ip, "src_addr", "", ipf_ip_ok, mtk_shell_ipv4(value) || mtk_shell_ipv6(value))
IPF_ADDR(dst_ip, "dst_addr", "", ipf_ip_ok, mtk_shell_ipv4(value) || mtk_shell_ipv6(value))
IPF_ADDR(src_mask, "src_mask", "NULL", ipf_mask_ok, fw_in_range(value, 0, 128))
IPF_ADDR(dst_mask, "dst_mask", "NULL", ipf_mask_ok, fw_in_range(value, 0, 128))

static int get_ipf_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *p = ipf_get(data, "protocol", "-1");

	/* protocol_to_tr069 */
	if (strcmp(p, "TCP") == 0 || strcmp(p, "tcp") == 0)
		*value = "TCP";
	else if (strcmp(p, "UDP") == 0 || strcmp(p, "udp") == 0)
		*value = "UDP";
	else if (strcmp(p, "ICMP") == 0 || strcmp(p, "icmp") == 0)
		*value = "ICMP";
	else if (strcmp(p, "58") == 0 || strcmp(p, "ICMPv6") == 0 || strcmp(p, "icmpv6") == 0 || strcmp(p, "ICMPV6") == 0)
		*value = "ICMPv6";
	else if (strcmp(p, "tcp_udp") == 0 || strcmp(p, "TCP/UDP") == 0 || strcmp(p, "tcp/udp") == 0)
		*value = "TCP and UDP";
	else
		*value = "-1";
	return 0;
}

/* normalize_protocol */
static const char *ipf_protocol_rule(const char *v)
{
	static const char *const map[][2] = {
		{ "", "-1" }, { "-1", "-1" }, { "TCP", "tcp" }, { "tcp", "tcp" },
		{ "UDP", "udp" }, { "udp", "udp" }, { "ICMP", "icmp" }, { "icmp", "icmp" },
		{ "58", "icmpv6" }, { "ICMPv6", "icmpv6" }, { "icmpv6", "icmpv6" }, { "ICMPV6", "icmpv6" },
		{ "TCP and UDP", "tcp_udp" }, { "TCP AND UDP", "tcp_udp" }, { "tcp and udp", "tcp_udp" },
		{ "TCP/UDP", "tcp_udp" }, { "tcp/udp", "tcp_udp" },
	};
	size_t i;

	for (i = 0; v && i < sizeof(map) / sizeof(map[0]); i++)
		if (strcmp(v, map[i][0]) == 0)
			return map[i][1];
	return NULL;
}

static int set_ipf_protocol(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *p;
	int f = ipf_writable(data);

	if (f)
		return f;
	if (!(p = ipf_protocol_rule(value)))
		return FAULT_9007;
	return action == VALUECHECK ? 0 : ipf_write(data, "protocol", p);
}

/* iface_to_tr069; "" when the device maps to nothing (the shell printed
 * nothing) */
static char *ipf_iface_path(const char *iface)
{
	char *path;

	if (fw_nullable(iface))
		return "NULL";
	if (strcmp(iface, "br-lan") == 0)
		return LAN_IPIF_PATH;
	path = fw_iface_to_wan_path(iface);
	return path ? path : "";
}

/* normalize_iface: exactly the LAN IPInterface or WANDevice.1's
 * connections */
static char *ipf_iface_rule(const char *v)
{
	if (strcmp(v, LAN_IPIF_PATH) == 0)
		return "br-lan";
	if (strncmp(v, WAN_CD_PATH "WANPPPConnection.", sizeof(WAN_CD_PATH "WANPPPConnection.") - 1) == 0)
		return fw_wan_path_to_iface(v, 1);
	if (strncmp(v, WAN_CD_PATH "WANIPConnection.", sizeof(WAN_CD_PATH "WANIPConnection.") - 1) == 0)
		return fw_wan_path_to_iface(v, 0);
	return NULL;
}

#define IPF_IFACE(name, option)							\
static int get_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char **value)				\
{										\
	*value = ipf_iface_path(ipf_get(data, option, "NULL"));			\
	return 0;								\
}										\
static int set_ipf_##name(char *refparam, struct dmctx *ctx, void *data,	\
			  char *instance, char *value, int action)		\
{										\
	char *iface;								\
	int f = ipf_writable(data);						\
										\
	if (f)									\
		return f;							\
	if (fw_nullable(value))							\
		return action == VALUECHECK ? 0 : ipf_delete_option(data, option);	\
	if (!(iface = ipf_iface_rule(value)))					\
		return FAULT_9007;						\
	return action == VALUECHECK ? 0 : ipf_write(data, option, iface);	\
}
IPF_IFACE(src_iface, "ingress_ifname")
IPF_IFACE(dst_iface, "egress_ifname")

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tFwDisablePortParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Name", &DMWRITE, DMT_STRING, get_dp_name, set_dp_name, NULL, NULL},
{"Interface", &DMWRITE, DMT_STRING, get_dp_interface, set_dp_interface, NULL, NULL},
{"Port", &DMWRITE, DMT_STRING, get_dp_port, set_dp_port, NULL, NULL},
{"Enable", &DMWRITE, DMT_STRING, get_dp_enable, set_dp_enable, NULL, NULL},
{0}
};

static DMLEAF tFwSc4Params[] = {
{"Enable", &DMWRITE, DMT_STRING, get_sc_enable, set_sc_enable, NULL, NULL},
{"IPEnd", &DMWRITE, DMT_STRING, get_sc_ip_end, set_sc_ip_end, NULL, NULL},
{"IPStart", &DMWRITE, DMT_STRING, get_sc_ip_start, set_sc_ip_start, NULL, NULL},
{"Ingress", &DMWRITE, DMT_STRING, get_sc_ingress, set_sc_ingress, NULL, NULL},
{"Mode", &DMWRITE, DMT_STRING, get_sc_mode, set_sc_mode, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_sc_name, set_sc_name, NULL, NULL},
{"ServiceType", &DMWRITE, DMT_STRING, get_sc_service_type, set_sc_service_type, NULL, NULL},
{"OtherProtocol", &DMWRITE, DMT_STRING, get_sc_other_protocol, set_sc_other_protocol, NULL, NULL},
{"OtherPort", &DMWRITE, DMT_STRING, get_sc_other_port, set_sc_other_port, NULL, NULL},
{0}
};

static DMLEAF tFwSc6Params[] = {
{"Enable", &DMWRITE, DMT_STRING, get_sc_enable, set_sc_enable, NULL, NULL},
{"Ingress", &DMWRITE, DMT_STRING, get_sc_ingress, set_sc_ingress, NULL, NULL},
{"Mode", &DMWRITE, DMT_STRING, get_sc_mode, set_sc_mode, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_sc_name, set_sc_name, NULL, NULL},
{"Prefix", &DMWRITE, DMT_STRING, get_sc_prefix, set_sc_prefix, NULL, NULL},
{"PrefixLen", &DMWRITE, DMT_STRING, get_sc_prefix_len, set_sc_prefix_len, NULL, NULL},
{"ServiceType", &DMWRITE, DMT_STRING, get_sc_service_type, set_sc_service_type, NULL, NULL},
{"OtherProtocol", &DMWRITE, DMT_STRING, get_sc_other_protocol, set_sc_other_protocol, NULL, NULL},
{"OtherPort", &DMWRITE, DMT_STRING, get_sc_other_port, set_sc_other_port, NULL, NULL},
{0}
};

static DMLEAF tFwIpFilterParams[] = {
{"Enable", &DMWRITE, DMT_STRING, get_ipf_enable, set_ipf_enable, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_ipf_name, set_ipf_name, NULL, NULL},
{"Target", &DMWRITE, DMT_STRING, get_ipf_target, set_ipf_target, NULL, NULL},
{"Order", &DMWRITE, DMT_STRING, get_ipf_order, set_ipf_order, NULL, NULL},
{"IPVersion", &DMWRITE, DMT_STRING, get_ipf_ipversion, set_ipf_ipversion, NULL, NULL},
{"SourceIP", &DMWRITE, DMT_STRING, get_ipf_src_ip, set_ipf_src_ip, NULL, NULL},
{"SourceMask", &DMWRITE, DMT_STRING, get_ipf_src_mask, set_ipf_src_mask, NULL, NULL},
{"SourceIPExclude", &DMWRITE, DMT_STRING, get_ipf_src_ip_exclude, set_ipf_src_ip_exclude, NULL, NULL},
{"DestIP", &DMWRITE, DMT_STRING, get_ipf_dst_ip, set_ipf_dst_ip, NULL, NULL},
{"DestMask", &DMWRITE, DMT_STRING, get_ipf_dst_mask, set_ipf_dst_mask, NULL, NULL},
{"DestIPExclude", &DMWRITE, DMT_STRING, get_ipf_dst_ip_exclude, set_ipf_dst_ip_exclude, NULL, NULL},
{"Protocol", &DMWRITE, DMT_STRING, get_ipf_protocol, set_ipf_protocol, NULL, NULL},
{"ProtocolExclude", &DMWRITE, DMT_STRING, get_ipf_protocol_exclude, set_ipf_protocol_exclude, NULL, NULL},
{"SourcePort", &DMWRITE, DMT_STRING, get_ipf_src_port, set_ipf_src_port, NULL, NULL},
{"SourcePortRangeMax", &DMWRITE, DMT_STRING, get_ipf_src_port_end, set_ipf_src_port_end, NULL, NULL},
{"SourcePortExclude", &DMWRITE, DMT_STRING, get_ipf_src_port_exclude, set_ipf_src_port_exclude, NULL, NULL},
{"DestPort", &DMWRITE, DMT_STRING, get_ipf_dst_port, set_ipf_dst_port, NULL, NULL},
{"DestPortRangeMax", &DMWRITE, DMT_STRING, get_ipf_dst_port_end, set_ipf_dst_port_end, NULL, NULL},
{"DestPortExclude", &DMWRITE, DMT_STRING, get_ipf_dst_port_exclude, set_ipf_dst_port_exclude, NULL, NULL},
{"SourceAllInterface", &DMWRITE, DMT_STRING, get_ipf_src_all_iface, set_ipf_src_all_iface, NULL, NULL},
{"SourceInterface", &DMWRITE, DMT_STRING, get_ipf_src_iface, set_ipf_src_iface, NULL, NULL},
{"SourceInterfaceExclude", &DMWRITE, DMT_STRING, get_ipf_src_iface_exclude, set_ipf_src_iface_exclude, NULL, NULL},
{"DestAllInterface", &DMWRITE, DMT_STRING, get_ipf_dst_all_iface, set_ipf_dst_all_iface, NULL, NULL},
{"DestInterface", &DMWRITE, DMT_STRING, get_ipf_dst_iface, set_ipf_dst_iface, NULL, NULL},
{"DestInterfaceExclude", &DMWRITE, DMT_STRING, get_ipf_dst_iface_exclude, set_ipf_dst_iface_exclude, NULL, NULL},
{"DSCP", &DMWRITE, DMT_STRING, get_ipf_dscp, set_ipf_dscp, NULL, NULL},
{"DSCPExclude", &DMWRITE, DMT_STRING, get_ipf_dscp_exclude, set_ipf_dscp_exclude, NULL, NULL},
{0}
};

static DMOBJ tFwServiceControlObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"IPV4ServiceControl", &DMWRITE, add_sc4, del_sc, NULL, browse_sc4, NULL, NULL, NULL, tFwSc4Params, NULL},
{"IPV6ServiceControl", &DMWRITE, add_sc6, del_sc, NULL, browse_sc6, NULL, NULL, NULL, tFwSc6Params, NULL},
{0}
};

static DMLEAF tFirewallParams[] = {
{"Config", &DMREAD, DMT_STRING, get_fw_config, NULL, NULL, NULL},
{"Enable", &DMREAD, DMT_BOOL, get_fw_enable, NULL, NULL, NULL},
{0}
};

static DMOBJ tFirewallObj[] = {
{"X_AIS_DisablePort", &DMWRITE, add_dp, del_dp, NULL, browse_dp, NULL, NULL, NULL, tFwDisablePortParams, NULL},
{"X_AIS_ServiceControl", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tFwServiceControlObj, NULL, NULL},
{"X_AIS_IPFilter", &DMWRITE, add_ipf, del_ipf, NULL, browse_ipf, NULL, NULL, NULL, tFwIpFilterParams, NULL},
{0}
};

static DMOBJ tFirewallRoot[] = {
{"Firewall", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tFirewallObj, tFirewallParams, NULL},
{0}
};

static const char *const firewall_mtk_paths[] = {
	"InternetGatewayDevice.Firewall.",
	NULL
};

static const struct dm_module firewall_mtk_module = {
	.name  = "mtk-firewall",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tFirewallRoot,
	.paths = firewall_mtk_paths,
};
DM_MODULE_REGISTER(firewall_mtk_module);

/* ------------------------------------------------------------------ */
/* TR-181 (cwmp.cpe.datamodel=tr181)                                    */
/* ------------------------------------------------------------------ */

/*
 * Device.Firewall: the same sections, rules, Add/Delete and reloads.  The
 * three leaves that carry TR-098 interface paths -- ServiceControl Ingress
 * and IPFilter Source/DestInterface -- show and take Device.IP.Interface
 * references instead (wan181_paths_to181/to098 around the TR-098
 * getter/setter); "LAN", "WAN_ALL", "NULL" ... stay.  DisablePort.Interface
 * is a LAN / WAN_<n> token, unchanged.
 */
PATH181_GET(sc_ingress, get_sc_ingress)
PATH181_SET(sc_ingress, set_sc_ingress)
PATH181_GET(ipf_src_iface, get_ipf_src_iface)
PATH181_SET(ipf_src_iface, set_ipf_src_iface)
PATH181_GET(ipf_dst_iface, get_ipf_dst_iface)
PATH181_SET(ipf_dst_iface, set_ipf_dst_iface)

static DMLEAF tFwSc4181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_STRING, get_sc_enable, set_sc_enable, NULL, NULL},
{"IPEnd", &DMWRITE, DMT_STRING, get_sc_ip_end, set_sc_ip_end, NULL, NULL},
{"IPStart", &DMWRITE, DMT_STRING, get_sc_ip_start, set_sc_ip_start, NULL, NULL},
{"Ingress", &DMWRITE, DMT_STRING, get_path181_sc_ingress, set_path181_sc_ingress, NULL, NULL},
{"Mode", &DMWRITE, DMT_STRING, get_sc_mode, set_sc_mode, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_sc_name, set_sc_name, NULL, NULL},
{"ServiceType", &DMWRITE, DMT_STRING, get_sc_service_type, set_sc_service_type, NULL, NULL},
{"OtherProtocol", &DMWRITE, DMT_STRING, get_sc_other_protocol, set_sc_other_protocol, NULL, NULL},
{"OtherPort", &DMWRITE, DMT_STRING, get_sc_other_port, set_sc_other_port, NULL, NULL},
{0}
};

static DMLEAF tFwSc6181Params[] = {
{"Enable", &DMWRITE, DMT_STRING, get_sc_enable, set_sc_enable, NULL, NULL},
{"Ingress", &DMWRITE, DMT_STRING, get_path181_sc_ingress, set_path181_sc_ingress, NULL, NULL},
{"Mode", &DMWRITE, DMT_STRING, get_sc_mode, set_sc_mode, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_sc_name, set_sc_name, NULL, NULL},
{"Prefix", &DMWRITE, DMT_STRING, get_sc_prefix, set_sc_prefix, NULL, NULL},
{"PrefixLen", &DMWRITE, DMT_STRING, get_sc_prefix_len, set_sc_prefix_len, NULL, NULL},
{"ServiceType", &DMWRITE, DMT_STRING, get_sc_service_type, set_sc_service_type, NULL, NULL},
{"OtherProtocol", &DMWRITE, DMT_STRING, get_sc_other_protocol, set_sc_other_protocol, NULL, NULL},
{"OtherPort", &DMWRITE, DMT_STRING, get_sc_other_port, set_sc_other_port, NULL, NULL},
{0}
};

static DMLEAF tFwIpFilter181Params[] = {
{"Enable", &DMWRITE, DMT_STRING, get_ipf_enable, set_ipf_enable, NULL, NULL},
{"Name", &DMWRITE, DMT_STRING, get_ipf_name, set_ipf_name, NULL, NULL},
{"Target", &DMWRITE, DMT_STRING, get_ipf_target, set_ipf_target, NULL, NULL},
{"Order", &DMWRITE, DMT_STRING, get_ipf_order, set_ipf_order, NULL, NULL},
{"IPVersion", &DMWRITE, DMT_STRING, get_ipf_ipversion, set_ipf_ipversion, NULL, NULL},
{"SourceIP", &DMWRITE, DMT_STRING, get_ipf_src_ip, set_ipf_src_ip, NULL, NULL},
{"SourceMask", &DMWRITE, DMT_STRING, get_ipf_src_mask, set_ipf_src_mask, NULL, NULL},
{"SourceIPExclude", &DMWRITE, DMT_STRING, get_ipf_src_ip_exclude, set_ipf_src_ip_exclude, NULL, NULL},
{"DestIP", &DMWRITE, DMT_STRING, get_ipf_dst_ip, set_ipf_dst_ip, NULL, NULL},
{"DestMask", &DMWRITE, DMT_STRING, get_ipf_dst_mask, set_ipf_dst_mask, NULL, NULL},
{"DestIPExclude", &DMWRITE, DMT_STRING, get_ipf_dst_ip_exclude, set_ipf_dst_ip_exclude, NULL, NULL},
{"Protocol", &DMWRITE, DMT_STRING, get_ipf_protocol, set_ipf_protocol, NULL, NULL},
{"ProtocolExclude", &DMWRITE, DMT_STRING, get_ipf_protocol_exclude, set_ipf_protocol_exclude, NULL, NULL},
{"SourcePort", &DMWRITE, DMT_STRING, get_ipf_src_port, set_ipf_src_port, NULL, NULL},
{"SourcePortRangeMax", &DMWRITE, DMT_STRING, get_ipf_src_port_end, set_ipf_src_port_end, NULL, NULL},
{"SourcePortExclude", &DMWRITE, DMT_STRING, get_ipf_src_port_exclude, set_ipf_src_port_exclude, NULL, NULL},
{"DestPort", &DMWRITE, DMT_STRING, get_ipf_dst_port, set_ipf_dst_port, NULL, NULL},
{"DestPortRangeMax", &DMWRITE, DMT_STRING, get_ipf_dst_port_end, set_ipf_dst_port_end, NULL, NULL},
{"DestPortExclude", &DMWRITE, DMT_STRING, get_ipf_dst_port_exclude, set_ipf_dst_port_exclude, NULL, NULL},
{"SourceAllInterface", &DMWRITE, DMT_STRING, get_ipf_src_all_iface, set_ipf_src_all_iface, NULL, NULL},
{"SourceInterface", &DMWRITE, DMT_STRING, get_path181_ipf_src_iface, set_path181_ipf_src_iface, NULL, NULL},
{"SourceInterfaceExclude", &DMWRITE, DMT_STRING, get_ipf_src_iface_exclude, set_ipf_src_iface_exclude, NULL, NULL},
{"DestAllInterface", &DMWRITE, DMT_STRING, get_ipf_dst_all_iface, set_ipf_dst_all_iface, NULL, NULL},
{"DestInterface", &DMWRITE, DMT_STRING, get_path181_ipf_dst_iface, set_path181_ipf_dst_iface, NULL, NULL},
{"DestInterfaceExclude", &DMWRITE, DMT_STRING, get_ipf_dst_iface_exclude, set_ipf_dst_iface_exclude, NULL, NULL},
{"DSCP", &DMWRITE, DMT_STRING, get_ipf_dscp, set_ipf_dscp, NULL, NULL},
{"DSCPExclude", &DMWRITE, DMT_STRING, get_ipf_dscp_exclude, set_ipf_dscp_exclude, NULL, NULL},
{0}
};

static DMOBJ tFwServiceControl181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"IPV4ServiceControl", &DMWRITE, add_sc4, del_sc, NULL, browse_sc4, NULL, NULL, NULL, tFwSc4181Params, NULL},
{"IPV6ServiceControl", &DMWRITE, add_sc6, del_sc, NULL, browse_sc6, NULL, NULL, NULL, tFwSc6181Params, NULL},
{0}
};

static DMOBJ tFirewall181Obj[] = {
{"X_AIS_DisablePort", &DMWRITE, add_dp, del_dp, NULL, browse_dp, NULL, NULL, NULL, tFwDisablePortParams, NULL},
{"X_AIS_ServiceControl", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tFwServiceControl181Obj, NULL, NULL},
{"X_AIS_IPFilter", &DMWRITE, add_ipf, del_ipf, NULL, browse_ipf, NULL, NULL, NULL, tFwIpFilter181Params, NULL},
{0}
};

/* TR-181 Config and Enable are readWrite: the product's firewall level and
 * state are fixed, the values they read are taken, others are 9007 (T7 S3);
 * a copy of tFirewallParams, keep in step */
MTK_SET_SAME(fw_config, get_fw_config)
MTK_SET_SAME_BOOL(fw_enable, get_fw_enable)

static DMLEAF tFirewall181Params[] = {
{"Config", &DMWRITE, DMT_STRING, get_fw_config, set_same_fw_config, NULL, NULL},
{"Enable", &DMWRITE, DMT_BOOL, get_fw_enable, set_same_fw_enable, NULL, NULL},
{0}
};

static DMOBJ tFirewall181Root[] = {
{"Firewall", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tFirewall181Obj, tFirewall181Params, NULL},
{0}
};

static const char *const firewall181_mtk_paths[] = {
	"Device.Firewall.",
	NULL
};

static const struct dm_module firewall181_mtk_module = {
	.name  = "mtk-firewall-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tFirewall181Root,
	.paths = firewall181_mtk_paths,
};
DM_MODULE_REGISTER(firewall181_mtk_module);
