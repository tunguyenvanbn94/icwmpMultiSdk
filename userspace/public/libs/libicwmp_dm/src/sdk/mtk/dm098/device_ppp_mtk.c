/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.Device.PPP.Interface.{i}. -- the PPPoE WAN
 *	connections seen the TR-181 way, ported from functions/tr098/device_ppp.
 *
 *	An interface is an anonymous wan "entry" (wan.@entry[<n>]) with
 *	conn_type "2", in the order of the config.  Its number is
 *	wan.@entry[<n>].ppp_int_instance; one without gets the highest number in
 *	use plus one, committed at once (on a GET too, as the shell did).  The
 *	netifd interface is the entry's name when netifd knows it, else
 *	"if<id>".
 *	  Enable       entry active ("0" when unset); a set writes active and
 *	               network.<if>.auto, hni_wan_reload.sh queued
 *	  Status, ConnectionStatus, LastChange, LastConnectionError
 *	               "ubus call network.interface.<if> status": up, pending,
 *	               errors[0].code mapped to the TR-181 error names
 *	  Name         the entry's name, else "if<id>"
 *	  LowerLayers  "pon", or "pon.<vlan_id>" when vlan_active is 1; a set
 *	               takes those two forms ("" counts as "pon"), writes
 *	               vlan_active/vlan_id and network.<if>.device
 *	  Username     ppp_username, also written to network.<if>.username
 *	  Password     write only, ppp_password and network.<if>.password
 *	DeleteObject removes the entry and network.<if>.
 *
 *	One difference, on purpose -- AddObject.  The shell took the output of
 *	"uci add wan entry" for an index; it is the new section's NAME
 *	(cfgXXXXXX), so every "uci set wan.@entry[cfgXXXXXX]..." failed: the
 *	entry stayed empty (no conn_type, not a PPP interface), a network
 *	section "ifcfgXXXXXX" was made, and the instance number answered to the
 *	ACS did not exist.  Here the entry gets what the shell meant: id <n>
 *	(its index), active 0, conn_type 2 (PPPoE), service_type 3,
 *	vlan_active 0, name if<n>, mtu 1492, the next number; and network.if<n>
 *	a disabled pppoe interface on "pon", mtu 1492.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <json-c/json.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dmmem.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wanconn_mtk.h"
#include "stack181_mtk.h"
#include "device_ip_mtk.h"

#define WAN_PKG		"wan"
#define WAN_RELOAD	"/usr/sbin/hni_wan_reload.sh"

struct ppp_if {
	char *sec;	/* "@entry[<n>]" */
	char *inst;
};

static struct uci_package *ppp_pkg(void)
{
	struct uci_ptr ptr = {0};

	if (dmuci_lookup_ptr(uci_ctx, &ptr, WAN_PKG, NULL, NULL, NULL) || !ptr.p)
		return NULL;
	return ptr.p;
}

/* ppp_get_max_instance: the highest ppp_int_instance in wan (non-digits
 * removed), 0 when none */
static int ppp_max_instance(void)
{
	struct uci_package *p = ppp_pkg();
	struct uci_element *e;
	int max = 0;

	if (!p)
		return 0;
	uci_foreach_element(&p->sections, e) {
		char *v = NULL, digits[16];
		size_t n = 0;
		const char *c;

		dmuci_get_value_by_section_string(uci_to_section(e), "ppp_int_instance", &v);
		for (c = v ? v : ""; *c && n < sizeof(digits) - 1; c++) {
			if (isdigit((unsigned char)*c))
				digits[n++] = *c;
		}
		digits[n] = '\0';
		if (n && atoi(digits) > max)
			max = atoi(digits);
	}
	return max;
}

/* how many "entry" sections there are, named ones included: the @entry[]
 * index of the next one added */
static int ppp_entry_count(void)
{
	struct uci_package *p = ppp_pkg();
	struct uci_element *e;
	int n = 0;

	if (!p)
		return 0;
	uci_foreach_element(&p->sections, e) {
		if (strcmp(uci_to_section(e)->type, "entry") == 0)
			n++;
	}
	return n;
}

#define PPP_MAX	32

/* the PPP interfaces, numbered: what browse_ppp links, and what
 * PPP.InterfaceNumberOfEntries counts (TR-181) */
static int ppp_list(struct ppp_if *list, int max)
{
	struct uci_package *p = ppp_pkg();
	struct uci_element *e;
	int n = 0, idx = 0;

	if (!p)
		return 0;
	/* "wan.@entry[<n>]=entry" lines of "uci show wan": the anonymous ones */
	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		char *sec, *inst, buf[16];

		if (strcmp(s->type, "entry") != 0)
			continue;
		dmasprintf(&sec, "@entry[%d]", idx++);
		if (!s->anonymous || !sec || n >= max)
			continue;
		if (strcmp(mtk_uci(WAN_PKG, sec, "conn_type"), "2") != 0)
			continue;
		inst = mtk_uci(WAN_PKG, sec, "ppp_int_instance");
		if (!*inst) {
			snprintf(buf, sizeof(buf), "%d", ppp_max_instance() + 1);
			mtk_uci_set_persist(WAN_PKG, sec, "ppp_int_instance", buf);
			inst = dmstrdup(buf);
		}
		list[n].sec = sec;
		list[n].inst = inst;
		n++;
	}
	return n;
}

static int browse_ppp(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct ppp_if *list = dmcalloc(PPP_MAX, sizeof(*list));
	int n, i;

	if (!list)
		return 0;
	n = ppp_list(list, PPP_MAX);
	for (i = 0; i < n; i++) {
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&list[i], list[i].inst) == DM_STOP)
			break;
	}
	return 0;
}

#define PPP_SEC(data)	(((struct ppp_if *)(data))->sec)

static json_object *ppp_iface_status(const char *ifname)
{
	json_object *res = NULL;
	char obj[96];

	if (!ifname || !*ifname)
		return NULL;
	snprintf(obj, sizeof(obj), "network.interface.%s", ifname);
	dmubus_call(obj, "status", UBUS_ARGS{}, 0, &res);
	return res;
}

/* _ppp_resolve_ifname: the entry's name when netifd answers for it, else
 * "if<id>" ("if0" without an id) */
static char *ppp_ifname(const char *sec)
{
	char *nm = mtk_uci(WAN_PKG, sec, "name"), *id, *out = NULL;

	if (*nm && ppp_iface_status(nm))
		return nm;
	id = mtk_uci(WAN_PKG, sec, "id");
	dmasprintf(&out, "if%s", *id ? id : "0");
	return out ? out : "if0";
}

static json_object *ppp_status(void *data)
{
	return ppp_iface_status(ppp_ifname(PPP_SEC(data)));
}

static int ppp_is_true(json_object *res, const char *key)
{
	return res && strcmp(dmjson_get_value(res, 1, (char *)key), "true") == 0;
}

/* errors[0].code */
static const char *ppp_error_code(json_object *res)
{
	json_object *arr, *first, *code;

	if (!res || !json_object_object_get_ex(res, "errors", &arr) ||
	    !json_object_is_type(arr, json_type_array) || json_object_array_length(arr) < 1)
		return "";
	first = json_object_array_get_idx(arr, 0);
	if (!first || !json_object_object_get_ex(first, "code", &code))
		return "";
	return json_object_get_string(code);
}

/* ------------------------------------------------------------------ */
/* AddObject / DeleteObject                                            */
/* ------------------------------------------------------------------ */

static int add_ppp(char *refparam, struct dmctx *ctx, void *data, char **instance)
{
	struct uci_section *s = NULL;
	char *name = NULL, idx[16], ifname[24], inst[16];
	int n = ppp_entry_count();

	snprintf(inst, sizeof(inst), "%d", ppp_max_instance() + 1);
	dmuci_add_section(WAN_PKG, "entry", &s, &name);
	if (!s)
		return FAULT_9002;
	snprintf(idx, sizeof(idx), "%d", n);
	snprintf(ifname, sizeof(ifname), "if%d", n);
	dmuci_set_value_by_section(s, "id", idx);
	dmuci_set_value_by_section(s, "active", "0");
	dmuci_set_value_by_section(s, "conn_type", "2");	/* PPPoE */
	dmuci_set_value_by_section(s, "service_type", "3");
	dmuci_set_value_by_section(s, "vlan_active", "0");
	dmuci_set_value_by_section(s, "name", ifname);
	dmuci_set_value_by_section(s, "mtu", "1492");
	dmuci_set_value_by_section(s, "ppp_int_instance", inst);

	dmuci_set_value("network", ifname, "", "interface");
	dmuci_set_value("network", ifname, "proto", "pppoe");
	dmuci_set_value("network", ifname, "device", "pon");
	dmuci_set_value("network", ifname, "mtu", "1492");
	dmuci_set_value("network", ifname, "auto", "0");
	*instance = dmstrdup(inst);
	return 0;
}

static int del_ppp(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	char *ifname;

	if (del_action != DEL_INST)
		return FAULT_9005;	/* the shell had no "delete all" */
	if (!data)
		return FAULT_9002;
	ifname = ppp_ifname(PPP_SEC(data));
	dmuci_delete(WAN_PKG, PPP_SEC(data), NULL, NULL);
	dmuci_delete("network", ifname, NULL, NULL);
	return 0;
}

/* ------------------------------------------------------------------ */
/* parameters                                                          */
/* ------------------------------------------------------------------ */

static int get_ppp_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = mtk_uci(WAN_PKG, PPP_SEC(data), "active");

	*value = *v ? v : "0";
	return 0;
}

static int set_ppp_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int b = mtk_parse_bool(value);

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(WAN_PKG, PPP_SEC(data), "active", b ? "1" : "0");
	dmuci_set_value("network", ppp_ifname(PPP_SEC(data)), "auto", b ? "1" : "0");
	mtk_apply_service_once(WAN_RELOAD);
	return 0;
}

static int get_ppp_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = ppp_is_true(ppp_status(data), "up") ? "Up" : "Down";
	return 0;
}

static int get_ppp_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *nm = mtk_uci(WAN_PKG, PPP_SEC(data), "name");

	if (*nm)
		*value = nm;
	else
		dmasprintf(value, "if%s", mtk_uci(WAN_PKG, PPP_SEC(data), "id"));
	return 0;
}

static int get_ppp_lastchange(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = ppp_status(data);
	char *v = res ? dmjson_get_value(res, 1, "uptime") : "";

	*value = (v && *v) ? v : "0";
	return 0;
}

static int get_ppp_lowerlayers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *vid = mtk_uci(WAN_PKG, PPP_SEC(data), "vlan_id");

	if (strcmp(mtk_uci(WAN_PKG, PPP_SEC(data), "vlan_active"), "1") == 0 && *vid)
		dmasprintf(value, "pon.%s", vid);
	else
		*value = "pon";
	return 0;
}

static int set_ppp_lowerlayers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	const char *vid = NULL;

	if (strncmp(value, "pon.", 4) == 0) {
		vid = value + 4;
		if (!mtk_ere_match("^[0-9]+$", vid))
			return FAULT_9007;
	} else if (*value && strcmp(value, "pon") != 0) {
		return FAULT_9007;
	}
	if (action == VALUECHECK)
		return 0;
	if (vid) {
		dmuci_set_value(WAN_PKG, PPP_SEC(data), "vlan_active", "1");
		dmuci_set_value(WAN_PKG, PPP_SEC(data), "vlan_id", (char *)vid);
	} else {
		dmuci_set_value(WAN_PKG, PPP_SEC(data), "vlan_active", "0");
		dmuci_set_value(WAN_PKG, PPP_SEC(data), "vlan_id", "");
	}
	dmuci_set_value("network", ppp_ifname(PPP_SEC(data)), "device", value);
	mtk_apply_service_once(WAN_RELOAD);
	return 0;
}

static int get_ppp_connstatus(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = ppp_status(data);
	const char *code;

	if (!res) {
		*value = "Disconnected";
		return 0;
	}
	if (ppp_is_true(res, "up")) {
		*value = "Connected";
		return 0;
	}
	if (!ppp_is_true(res, "pending")) {
		*value = "Disconnected";
		return 0;
	}
	code = ppp_error_code(res);
	*value = (strstr(code, "AUTH") || strstr(code, "auth")) ? "Authenticating" : "Connecting";
	return 0;
}

/* _ppp_map_ubus_error_inline */
static const char *ppp_map_error(const char *code)
{
	static const struct { const char *code, *name; } map[] = {
		{ "AUTH_TOPEER_FAILED", "ERROR_AUTHENTICATION_FAILURE" },
		{ "PEER_AUTH_FAILED", "ERROR_AUTHENTICATION_FAILURE" },
		{ "CNID_AUTH_FAILED", "ERROR_AUTHENTICATION_FAILURE" },
		{ "NEGOTIATION_FAILED", "ERROR_NO_CARRIER" },
		{ "PEER_DEAD", "ERROR_NO_CARRIER" },
		{ "HANGUP", "ERROR_NO_CARRIER" },
		{ "LOOPBACK", "ERROR_NO_CARRIER" },
		{ "CONNECT_FAILED", "ERROR_CONNECT_FAILED" },
		{ "OPEN_FAILED", "ERROR_CONNECT_FAILED" },
		{ "LOCK_FAILED", "ERROR_CONNECT_FAILED" },
		{ "NO_KERNEL_SUPPORT", "ERROR_CONNECT_FAILED" },
		{ "INIT_FAILED", "ERROR_CONNECT_FAILED" },
		{ "OPTION_ERROR", "ERROR_CONNECT_FAILED" },
		{ "IDLE_TIMEOUT", "ERROR_USER_DISCONNECT" },
		{ "CONNECT_TIME", "ERROR_USER_DISCONNECT" },
		{ "CALLBACK", "ERROR_USER_DISCONNECT" },
		{ "USER_REQUEST", "ERROR_USER_DISCONNECT" },
	};
	size_t i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (strcmp(code, map[i].code) == 0)
			return map[i].name;
	}
	return NULL;
}

static int get_ppp_lasterror(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	json_object *res = ppp_status(data);
	const char *code, *name;

	if (!res) {
		*value = "ERROR_UNKNOWN";
		return 0;
	}
	if (ppp_is_true(res, "up")) {
		*value = "ERROR_NONE";
		return 0;
	}
	code = ppp_error_code(res);
	name = *code ? ppp_map_error(code) : NULL;
	*value = (char *)(name ? name : "ERROR_UNKNOWN");
	return 0;
}

static int get_ppp_username(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = mtk_uci(WAN_PKG, PPP_SEC(data), "ppp_username");
	return 0;
}

/* ppp_set_username / ppp_set_password: the entry and the netifd interface */
static int ppp_set_account(void *data, const char *wan_option, const char *net_option,
			   const char *value, int action)
{
	if (action == VALUECHECK)
		return 0;
	dmuci_set_value(WAN_PKG, PPP_SEC(data), (char *)wan_option, (char *)value);
	dmuci_set_value("network", ppp_ifname(PPP_SEC(data)), (char *)net_option, (char *)value);
	mtk_apply_service_once(WAN_RELOAD);
	return 0;
}

static int set_ppp_username(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ppp_set_account(data, "ppp_username", "username", value, action);
}

static int get_ppp_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

static int set_ppp_password(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return ppp_set_account(data, "ppp_password", "password", value, action);
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tPppIfParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_ppp_enable, set_ppp_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_ppp_status, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_ppp_name, NULL, NULL, NULL},
{"LastChange", &DMREAD, DMT_UNINT, get_ppp_lastchange, NULL, NULL, NULL},
{"LowerLayers", &DMWRITE, DMT_STRING, get_ppp_lowerlayers, set_ppp_lowerlayers, NULL, NULL},
{"ConnectionStatus", &DMREAD, DMT_STRING, get_ppp_connstatus, NULL, NULL, NULL},
{"LastConnectionError", &DMREAD, DMT_STRING, get_ppp_lasterror, NULL, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_ppp_username, set_ppp_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_ppp_password, set_ppp_password, NULL, NULL},
{0}
};

static DMOBJ tPppObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Interface", &DMWRITE, add_ppp, del_ppp, NULL, browse_ppp, NULL, NULL, NULL, tPppIfParams, NULL},
{0}
};

static DMOBJ tPppDeviceObj[] = {
{"PPP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tPppObj, NULL, NULL},
{0}
};

static DMOBJ tPppRoot[] = {
{"Device", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tPppDeviceObj, NULL, NULL},
{0}
};

static const char *const device_ppp_mtk_paths[] = {
	"InternetGatewayDevice.Device.PPP.",
	NULL
};

static const struct dm_module device_ppp_mtk_module = {
	.name  = "mtk-device-ppp",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tPppRoot,
	.paths = device_ppp_mtk_paths,
};
DM_MODULE_REGISTER(device_ppp_mtk_module);

/* TR-181 (cwmp.cpe.datamodel=tr181): this branch is TR-181 already, the
 * product grafted it under InternetGatewayDevice.Device.; the same tables at
 * the root (type A of docs/plan/tr181_mtk_design.md).  References to
 * IP.Interface follow the root (mtk_ipif_prefix()). */
static const char *const device_ppp_mtk_paths181[] = {
	"Device.PPP.",
	NULL
};

/*
 * TR-181 leaves of Interface.{i} this branch did not have, the
 * WANPPPConnection leaves of the same entry (wanip_mtk.c, wan181_get/set):
 * MaxMRUSize, CurrentMRUSize, Reset and IPCP.RemoteIPAddress.
 */
static struct wan_entry *ppp_wan(void *data)
{
	struct wan_entry *e = dmcalloc(1, sizeof(*e));
	int n;

	if (!e || sscanf(PPP_SEC(data), "@entry[%d]", &n) != 1 || !wan_entry_of_idx(n, e) || !e->ppp)
		return NULL;
	return e;
}

#define PPP181_GET(name, leaf)							\
static int get_ppp181_##name(char *refparam, struct dmctx *ctx, void *data,	\
			     char *instance, char **value)			\
{										\
	struct wan_entry *e = ppp_wan(data);					\
										\
	*value = "";								\
	return e ? wan181_get(e, leaf, value) : 0;				\
}

#define PPP181_SET(name, leaf)							\
static int set_ppp181_##name(char *refparam, struct dmctx *ctx, void *data,	\
			     char *instance, char *value, int action)		\
{										\
	struct wan_entry *e = ppp_wan(data);					\
										\
	return e ? wan181_set(e, leaf, value, action) : FAULT_9002;		\
}

PPP181_GET(mru, "MaxMRUSize")
PPP181_SET(mru, "MaxMRUSize")
PPP181_GET(current_mru, "CurrentMRUSize")
PPP181_GET(reset, "Reset")
PPP181_SET(reset, "Reset")
PPP181_GET(remote_ip, "RemoteIPAddress")

/* TR-181: Ethernet.Link.2 (the uplink) or the connection's own
 * Ethernet.VLANTermination (stack181_mtk.c), going to the product's setter
 * as pon / pon.<vid>; anything else, or another uplink than pon, is 9007 */
static int get_ppp181_lowerlayers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = stack181_ppp_lower(PPP_SEC(data));
	return 0;
}

static int set_ppp181_lowerlayers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	char *v;

	if (value && *value && strcmp(value, stack181_ppp_lower(PPP_SEC(data))) == 0)
		return 0;
	v = stack181_ppp_lower_to_product(PPP_SEC(data), value);
	if (!v)
		return FAULT_9007;
	return set_ppp_lowerlayers(refparam, ctx, data, instance, v, action);
}

static DMLEAF tPpp181IfParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_ppp_enable, set_ppp_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_ppp_status, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_ppp_name, NULL, NULL, NULL},
{"LastChange", &DMREAD, DMT_UNINT, get_ppp_lastchange, NULL, NULL, NULL},
{"LowerLayers", &DMWRITE, DMT_STRING, get_ppp181_lowerlayers, set_ppp181_lowerlayers, NULL, NULL},
{"Reset", &DMWRITE, DMT_BOOL, get_ppp181_reset, set_ppp181_reset, NULL, NULL},
{"ConnectionStatus", &DMREAD, DMT_STRING, get_ppp_connstatus, NULL, NULL, NULL},
{"LastConnectionError", &DMREAD, DMT_STRING, get_ppp_lasterror, NULL, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, get_ppp_username, set_ppp_username, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, get_ppp_password, set_ppp_password, NULL, NULL},
{"MaxMRUSize", &DMWRITE, DMT_UNINT, get_ppp181_mru, set_ppp181_mru, NULL, NULL},
{"CurrentMRUSize", &DMREAD, DMT_UNINT, get_ppp181_current_mru, NULL, NULL, NULL},
{0}
};

static DMLEAF tPpp181IpcpParams[] = {
{"RemoteIPAddress", &DMREAD, DMT_STRING, get_ppp181_remote_ip, NULL, NULL, NULL},
{0}
};

/* T7 S5a: the counters of the session's netdev (netifd's l3_device,
 * pppoe-<interface>), device_ip_mtk.c dip_stat181 */
static int get_ppp181_stat(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *dev = wan_iface_l3_device(ppp_ifname(PPP_SEC(data)));

	if (!dev || !*dev)
		dmasprintf(&dev, "pppoe-%s", ppp_ifname(PPP_SEC(data)));
	*value = dip_stat181(dev, STATS181_LEAF(refparam));
	return 0;
}

static DMLEAF tPpp181StatsParams[] = {
{"BytesSent", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_ppp181_stat, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_ppp181_stat, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, get_ppp181_stat, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, get_ppp181_stat, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_UNLONG, get_ppp181_stat, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_ppp181_stat, NULL, NULL, NULL},
{0}
};

static DMOBJ tPpp181IfObj[] = {
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tPpp181StatsParams, NULL},
{"IPCP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tPpp181IpcpParams, NULL},
{0}
};

static DMOBJ tPpp181Obj[] = {
{"Interface", &DMWRITE, add_ppp, del_ppp, NULL, browse_ppp, NULL, NULL, tPpp181IfObj, tPpp181IfParams, NULL},
{0}
};

/* T7 S4b: the root leaves of Device.PPP.  The product's pppoe interfaces
 * (hal_unify hal_network.c) run IPCP, and IPv6CP when the connection has
 * IPv6 (option ipv6 1; "noip" without IPv4) */
static int get_ppp181_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct ppp_if *list = dmcalloc(PPP_MAX, sizeof(*list));

	dmasprintf(value, "%d", list ? ppp_list(list, PPP_MAX) : 0);
	return 0;
}

static int get_ppp181_ncps(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "IPCP,IPv6CP";
	return 0;
}

static DMLEAF tPpp181Params[] = {
{"InterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_ppp181_count, NULL, NULL, NULL},
{"SupportedNCPs", &DMREAD, DMT_STRING, get_ppp181_ncps, NULL, NULL, NULL},
{0}
};

static DMOBJ tPpp181DeviceObj[] = {
{"PPP", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tPpp181Obj, tPpp181Params, NULL},
{0}
};

static const struct dm_module device_ppp_mtk_module181 = {
	.name  = "mtk-device-ppp-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tPpp181DeviceObj,
	.paths = device_ppp_mtk_paths181,
};
DM_MODULE_REGISTER(device_ppp_mtk_module181);
