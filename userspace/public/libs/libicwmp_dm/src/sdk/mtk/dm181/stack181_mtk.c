/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-181 interface stack of the MTK / Airoha product
 *	(docs/plan/tr181_mtk_design.md T7 S4): Ethernet.Link,
 *	Ethernet.VLANTermination, Bridging.Bridge, the LowerLayers references of
 *	IP.Interface and PPP.Interface (stack181_ipif_lower / stack181_ppp_lower,
 *	used by device_ip_mtk.c and device_ppp_mtk.c) and InterfaceStack, built
 *	from them.
 *
 *	The product's devices (netifd, hal_unify hal_network.c): the LAN bridge
 *	br-lan over eth0.1..eth0.4 and the twelve Wi-Fi interfaces; the WAN
 *	uplink wan_uplink_iface() (pon, eth1, eth0.<n>); a connection's VLAN
 *	<uplink>.<vlan_id> (8021q); a bridged connection's bridge over its VLAN
 *	(or the uplink) and the LAN ports and SSIDs bound to it (wan.@entry
 *	lan<k>, ssid<n>, mlo).
 *
 *	Instances, numbered on what does not move (the connection's id, the
 *	port), so that they keep their number across reboots and changes:
 *	  Ethernet.Link.1              br-lan, on Bridging.Bridge.1.Port.1
 *	  Ethernet.Link.2              the uplink, on Optical.Interface.1 for
 *	                               pon, else on Ethernet.Interface.5
 *	  Ethernet.Link.<id+11>        the bridge of bridged connection <id>, on
 *	                               Bridging.Bridge.<id+2>.Port.1
 *	  Ethernet.VLANTermination.<id+1>  connection <id>'s VLAN (vlan_id set),
 *	                               on Ethernet.Link.2; Enable/VLANID/
 *	                               VLANPriority are the connection's
 *	                               X_AIS_VLANEnable/X_AIS_VLANID/
 *	                               X_AIS_VLAN8021P (same setters)
 *	  Bridging.Bridge.1            br-lan: Port.1 the management port,
 *	                               Port.<k+1> Ethernet.Interface.k (k 1..4),
 *	                               Port.<n+5> WiFi.SSID.n (n 1..12, the
 *	                               interface of wlan_mtk.c's fixed map) --
 *	                               those not bound to a bridged connection,
 *	                               nor the LAN port the uplink took
 *	  Bridging.Bridge.<id+2>       bridged connection <id>: Port.1 management,
 *	                               its bound LAN ports and SSIDs numbered as
 *	                               in Bridge.1, Port.18 its WAN side (the
 *	                               VLAN, or the uplink link)
 *	  InterfaceStack.{i}           one row per LowerLayers item, numbered in
 *	                               order (the table is generated)
 *	A connection sits on its VLAN when it has one and it is active, else on
 *	the uplink link; IP.Interface of a PPP connection on its PPP.Interface,
 *	of a bridged one on the bridge's link, if<id>_6 on what if<id> sits on,
 *	the LAN on Ethernet.Link.1.
 *
 *	Fixed by the product, so writable only with the value they have
 *	(dmmtk.h MTK_SET_SAME): LowerLayers but PPP's, Enable of links, bridges
 *	and ports, ManagementPort, Standard, TPID, MACAddress.  PPP.Interface
 *	LowerLayers takes the uplink link or the connection's own
 *	VLANTermination and goes to the product's setter as pon / pon.<vid>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <uci.h>
#include <json-c/json.h>

#include "dmtr098.h"
#include "dmmem.h"
#include "dmuci.h"
#include "dmcommon.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "device_ip_mtk.h"
#include "wanconn_mtk.h"
#include "wan_mtk.h"
#include "wlan_mtk.h"
#include "stack181_mtk.h"

#define LINK_LAN		1
#define LINK_UPLINK		2
#define LINK_WANBR(id)		((id) + 11)
#define BRIDGE_LAN		1
#define BRIDGE_WAN(id)		((id) + 2)
#define VLAN_OF(id)		((id) + 1)
#define PORT_MGMT		1
#define PORT_ETH(k)		((k) + 1)	/* k 1..4 */
#define PORT_SSID(n)		((n) + 5)	/* n 1..12 */
#define PORT_WANSIDE		18
#define ETH_PORTS		4
#define SSID_PORTS		12
#define STACK_MAX		256

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* Up / Down from operstate, NotPresent without the device */
static char *netdev_status(const char *dev)
{
	char path[96], *st;

	if (!dev || !*dev || !mtk_netdev_exists(dev))
		return "NotPresent";
	snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", dev);
	st = mtk_file_line(path);
	return (st && strcmp(st, "up") == 0) ? "Up" : "Down";
}

static char *netdev_mac(const char *dev)
{
	char path[96];

	snprintf(path, sizeof(path), "/sys/class/net/%s/address", dev);
	return mtk_file_line(path);
}

/* LastChange: the uptime of the netifd interface on the device (the time
 * it has been up), 0 when none is up -- the kernel keeps no time of a
 * netdev's last state change */
static char *iface_uptime(const char *iface)
{
	json_object *res = (iface && *iface) ? wan_iface_status(iface) : NULL, *up, *t;

	if (!res || !json_object_object_get_ex(res, "up", &up) || !json_object_get_boolean(up) ||
	    !json_object_object_get_ex(res, "uptime", &t))
		return "0";
	return dmstrdup(json_object_get_string(t));
}

static int conn_vlan_id(struct wan_entry *e, long *vid)
{
	return wan_str_is_uint(wan_entry_opt(e, "vlan_id"), vid) && *vid > 0;
}

static int conn_vlan_active(struct wan_entry *e)
{
	long vid;

	return conn_vlan_id(e, &vid) && strcmp(wan_entry_opt(e, "vlan_active"), "1") == 0;
}

/* what a connection's PPP / IP / WAN bridge port sits on */
static char *conn_lower(struct wan_entry *e)
{
	char *v;

	if (conn_vlan_active(e))
		dmasprintf(&v, "Device.Ethernet.VLANTermination.%d", VLAN_OF(e->id));
	else
		dmasprintf(&v, "Device.Ethernet.Link.%d", LINK_UPLINK);
	return v;
}

/* wan.@entry options of the LAN ports / SSIDs bound to a connection */
static int conn_binds_eth(struct wan_entry *e, int k)
{
	char opt[8];

	snprintf(opt, sizeof(opt), "lan%d", k);
	return strcmp(wan_entry_opt(e, opt), "1") == 0;
}

static int conn_binds_ssid(struct wan_entry *e, int n)
{
	char opt[8];

	/* ssid1..ssid8, "mlo" for the MLO SSIDs 11 and 12, nothing for 9 and 10
	 * (wanip_mtk.c get_x_lan_interface) */
	if (n == 11 || n == 12)
		return strcmp(wan_entry_opt(e, "mlo"), "1") == 0;
	if (n > 8)
		return 0;
	snprintf(opt, sizeof(opt), "ssid%d", n);
	return strcmp(wan_entry_opt(e, opt), "1") == 0;
}

/* the bridged connections, in config order */
static int bridged_conns(struct wan_entry *out, int max)
{
	struct wan_entry *all = dmcalloc(WAN_MAX_ENTRIES, sizeof(*all));
	int n, i, m = 0;

	if (!all)
		return 0;
	n = wan_entries_all(&all, WAN_MAX_ENTRIES);
	for (i = 0; i < n && m < max; i++) {
		if (all[i].bridge)
			out[m++] = all[i];
	}
	return m;
}

static int bound_to_a_bridge(int is_ssid, int idx)
{
	struct wan_entry br[WAN_MAX_ENTRIES];
	int n = bridged_conns(br, WAN_MAX_ENTRIES), i;

	for (i = 0; i < n; i++) {
		if (is_ssid ? conn_binds_ssid(&br[i], idx) : conn_binds_eth(&br[i], idx))
			return 1;
	}
	return 0;
}

/* the LAN port the uplink took (clay opermode uplink eth2..eth4 is
 * eth0.2..eth0.4, wan_mtk.c): out of br-lan, 0 when none */
static int uplink_lan_port(void)
{
	int k;

	return sscanf(wan_uplink_iface(), "eth0.%d", &k) == 1 ? k : 0;
}

/* the netdev of a bridged connection's bridge: network.<dev>.name */
static char *wanbr_netdev(struct wan_entry *e)
{
	char *nm = mtk_uci("network", e->dev, "name");

	return *nm ? nm : e->dev;
}

/* ------------------------------------------------------------------ */
/* LowerLayers of IP.Interface and PPP.Interface                        */
/* ------------------------------------------------------------------ */

char *stack181_ipif_lower(const char *sec)
{
	struct wan_entry e;
	char base[64], *v;
	size_t l;

	if (!sec || !*sec)
		return "";
	if (strcmp(sec, "lan") == 0) {
		dmasprintf(&v, "Device.Ethernet.Link.%d", LINK_LAN);
		return v;
	}
	/* if<id>_6 sits on what if<id> sits on */
	snprintf(base, sizeof(base), "%s", sec);
	l = strlen(base);
	if (l > 2 && strcmp(base + l - 2, "_6") == 0)
		base[l - 2] = '\0';
	if (!wan_entry_of_sec(base, &e))
		return "";
	if (e.bridge) {
		dmasprintf(&v, "Device.Ethernet.Link.%d", LINK_WANBR(e.id));
		return v;
	}
	if (e.ppp) {
		/* numbered by device_ppp_mtk.c's browse; none yet: no reference */
		if (!*wan_entry_opt(&e, "ppp_int_instance"))
			return "";
		dmasprintf(&v, "Device.PPP.Interface.%s", wan_entry_opt(&e, "ppp_int_instance"));
		return v;
	}
	return conn_lower(&e);
}

/* "@entry[<n>]" of device_ppp_mtk.c */
static int ppp_entry(const char *wan_sec, struct wan_entry *e)
{
	int idx;

	if (!wan_sec || sscanf(wan_sec, "@entry[%d]", &idx) != 1)
		return 0;
	return wan_entry_of_idx(idx, e);
}

char *stack181_ppp_lower(const char *wan_sec)
{
	struct wan_entry e;

	return ppp_entry(wan_sec, &e) ? conn_lower(&e) : "";
}

char *stack181_ppp_lower_to_product(const char *wan_sec, const char *ref)
{
	struct wan_entry e;
	char want[64], *v;
	long vid;

	if (!ref || !ppp_entry(wan_sec, &e))
		return NULL;
	/* the product's setter knows a PON uplink only (pon, pon.<vid>) */
	if (strcmp(wan_uplink_iface(), "pon") != 0)
		return NULL;
	snprintf(want, sizeof(want), "Device.Ethernet.Link.%d", LINK_UPLINK);
	if (strcmp(ref, want) == 0)
		return "pon";
	snprintf(want, sizeof(want), "Device.Ethernet.VLANTermination.%d", VLAN_OF(e.id));
	if (strcmp(ref, want) == 0 && conn_vlan_id(&e, &vid)) {
		dmasprintf(&v, "pon.%ld", vid);
		return v;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Ethernet.Link                                                       */
/* ------------------------------------------------------------------ */

struct link181 {
	int inst;
	char name[32];
	char lower[64];
};

static int links(struct link181 *out, int max)
{
	struct wan_entry br[WAN_MAX_ENTRIES];
	const char *up = wan_uplink_iface();
	int n = 0, m, i;

	out[n].inst = LINK_LAN;
	snprintf(out[n].name, sizeof(out[n].name), "br-lan");
	snprintf(out[n].lower, sizeof(out[n].lower), "Device.Bridging.Bridge.%d.Port.%d", BRIDGE_LAN, PORT_MGMT);
	n++;
	out[n].inst = LINK_UPLINK;
	snprintf(out[n].name, sizeof(out[n].name), "%s", up);
	snprintf(out[n].lower, sizeof(out[n].lower), "%s",
		 strcmp(up, "pon") == 0 ? "Device.Optical.Interface.1" : "Device.Ethernet.Interface.5");
	n++;
	m = bridged_conns(br, WAN_MAX_ENTRIES);
	for (i = 0; i < m && n < max; i++, n++) {
		out[n].inst = LINK_WANBR(br[i].id);
		snprintf(out[n].name, sizeof(out[n].name), "%s", wanbr_netdev(&br[i]));
		snprintf(out[n].lower, sizeof(out[n].lower), "Device.Bridging.Bridge.%d.Port.%d",
			 BRIDGE_WAN(br[i].id), PORT_MGMT);
	}
	return n;
}

static int browse_link181(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct link181 *l = dmcalloc(2 + WAN_MAX_ENTRIES, sizeof(*l));
	int n, i;
	char *inst;

	if (!l)
		return 0;
	n = links(l, 2 + WAN_MAX_ENTRIES);
	for (i = 0; i < n; i++) {
		dmasprintf(&inst, "%d", l[i].inst);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, &l[i], inst) == DM_STOP)
			break;
	}
	return 0;
}

#define LINK(data)	((struct link181 *)(data))

static int get_true181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

static int get_link_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = netdev_status(LINK(data)->name);
	return 0;
}

static int get_link_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(LINK(data)->name);
	return 0;
}

static int get_link_lower(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(LINK(data)->lower);
	return 0;
}

static int get_link_mac(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = netdev_mac(LINK(data)->name);
	return 0;
}

static char *default_alias(const char *kind, char *instance)
{
	char *d;

	dmasprintf(&d, "cpe-%s-%s", kind, instance);
	return d;
}

#define ALIAS181(kind)								\
static int get_alias_##kind(char *refparam, struct dmctx *ctx, void *data,	\
			    char *instance, char **value)			\
{										\
	*value = mtk_alias181_get(refparam, default_alias(#kind, instance));	\
	return 0;								\
}										\
static int set_alias_##kind(char *refparam, struct dmctx *ctx, void *data,	\
			    char *instance, char *value, int action)		\
{										\
	return mtk_alias181_set(refparam, default_alias(#kind, instance), value, action); \
}

ALIAS181(link)
ALIAS181(vlan)
ALIAS181(bridge)
ALIAS181(port)
ALIAS181(stack)

/* the netifd interface on a link: lan on br-lan, if_wanbr<id> on a bridged
 * connection's bridge; none on the uplink itself */
static int get_link_lastchange(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int inst = LINK(data)->inst;
	char sec[32];

	if (inst == LINK_LAN)
		*value = iface_uptime("lan");
	else if (inst > LINK_UPLINK) {
		snprintf(sec, sizeof(sec), "if_wanbr%d", inst - LINK_WANBR(0));
		*value = iface_uptime(sec);
	} else
		*value = "0";
	return 0;
}

static int get_link_stat(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_stat181(LINK(data)->name, STATS181_LEAF(refparam));
	return 0;
}

static DMLEAF tLink181StatsParams[] = {
{"BytesSent", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_link_stat, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_link_stat, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, get_link_stat, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, get_link_stat, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_UNLONG, get_link_stat, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_link_stat, NULL, NULL, NULL},
{0}
};

static DMOBJ tLink181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tLink181StatsParams, NULL},
{0}
};

MTK_SET_SAME_BOOL(true181, get_true181)
MTK_SET_SAME(link_lower, get_link_lower)
MTK_SET_SAME(link_mac, get_link_mac)

static DMLEAF tLink181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_true181, set_same_true181, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_link_status, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_alias_link, set_alias_link, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_link_name, NULL, NULL, NULL},
{"LastChange", &DMREAD, DMT_UNINT, get_link_lastchange, NULL, NULL, NULL},
{"LowerLayers", &DMWRITE, DMT_STRING, get_link_lower, set_same_link_lower, NULL, NULL},
{"MACAddress", &DMWRITE, DMT_STRING, get_link_mac, set_same_link_mac, NULL, NULL},
{0}
};

/* ------------------------------------------------------------------ */
/* Ethernet.VLANTermination                                            */
/* ------------------------------------------------------------------ */

static int browse_vlan181(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct wan_entry *all = dmcalloc(WAN_MAX_ENTRIES, sizeof(*all));
	int n, i;
	long vid;
	char *inst;

	if (!all)
		return 0;
	n = wan_entries_all(&all, WAN_MAX_ENTRIES);
	for (i = 0; i < n; i++) {
		if (!conn_vlan_id(&all[i], &vid))
			continue;
		dmasprintf(&inst, "%d", VLAN_OF(all[i].id));
		if (DM_LINK_INST_OBJ(dmctx, parent_node, &all[i], inst) == DM_STOP)
			break;
	}
	return 0;
}

#define VLAN_E(data)	((struct wan_entry *)(data))

static int get_vlan_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%s.%s", wan_uplink_iface(), wan_entry_opt(data, "vlan_id"));
	return 0;
}

static int get_vlan_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *name = NULL;

	get_vlan_name(refparam, ctx, data, instance, &name);
	*value = conn_vlan_active(VLAN_E(data)) ? netdev_status(name) : "Down";
	return 0;
}

static int get_vlan_lower(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "Device.Ethernet.Link.%d", LINK_UPLINK);
	return 0;
}

static int get_vlan_tpid(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "33024";	/* 0x8100, C-tag */
	return 0;
}

#define VLAN_LEAF(name, leaf)							\
static int get_vlan_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char **value)			\
{										\
	return wan181_get(VLAN_E(data), leaf, value);				\
}										\
static int set_vlan_##name(char *refparam, struct dmctx *ctx, void *data,	\
			   char *instance, char *value, int action)		\
{										\
	return wan181_set(VLAN_E(data), leaf, value, action);			\
}

VLAN_LEAF(enable, "X_AIS_VLANEnable")
VLAN_LEAF(id, "X_AIS_VLANID")
VLAN_LEAF(priority, "X_AIS_VLAN8021P")

/* the connection's netifd interface runs on the VLAN */
static int get_vlan_lastchange(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = conn_vlan_active(VLAN_E(data)) ? iface_uptime(VLAN_E(data)->if4) : "0";
	return 0;
}

static int get_vlan_stat(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *name = NULL;

	get_vlan_name(refparam, ctx, data, instance, &name);
	*value = dip_stat181(name, STATS181_LEAF(refparam));
	return 0;
}

static DMLEAF tVlan181StatsParams[] = {
{"BytesSent", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_vlan_stat, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_vlan_stat, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, get_vlan_stat, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, get_vlan_stat, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_UNLONG, get_vlan_stat, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_vlan_stat, NULL, NULL, NULL},
{0}
};

static DMOBJ tVlan181Obj[] = {
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tVlan181StatsParams, NULL},
{0}
};

MTK_SET_SAME(vlan_lower, get_vlan_lower)
MTK_SET_SAME(vlan_tpid, get_vlan_tpid)

static DMLEAF tVlan181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_vlan_enable, set_vlan_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_vlan_status, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_alias_vlan, set_alias_vlan, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_vlan_name, NULL, NULL, NULL},
{"LastChange", &DMREAD, DMT_UNINT, get_vlan_lastchange, NULL, NULL, NULL},
{"LowerLayers", &DMWRITE, DMT_STRING, get_vlan_lower, set_same_vlan_lower, NULL, NULL},
{"VLANID", &DMWRITE, DMT_UNINT, get_vlan_id, set_vlan_id, NULL, NULL},
{"VLANPriority", &DMWRITE, DMT_INT, get_vlan_priority, set_vlan_priority, NULL, NULL},
{"TPID", &DMWRITE, DMT_UNINT, get_vlan_tpid, set_same_vlan_tpid, NULL, NULL},
{0}
};

static int get_link_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct link181 *l = dmcalloc(2 + WAN_MAX_ENTRIES, sizeof(*l));

	dmasprintf(value, "%d", l ? links(l, 2 + WAN_MAX_ENTRIES) : 0);
	return 0;
}

static int get_vlan_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct wan_entry *all = dmcalloc(WAN_MAX_ENTRIES, sizeof(*all));
	int n = 0, i, c = 0;
	long vid;

	if (all)
		n = wan_entries_all(&all, WAN_MAX_ENTRIES);
	for (i = 0; i < n; i++)
		c += conn_vlan_id(&all[i], &vid);
	dmasprintf(value, "%d", c);
	return 0;
}

static DMOBJ tEthernetStack181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Link", &DMREAD, NULL, NULL, NULL, browse_link181, NULL, NULL, tLink181Obj, tLink181Params, NULL},
{"VLANTermination", &DMREAD, NULL, NULL, NULL, browse_vlan181, NULL, NULL, tVlan181Obj, tVlan181Params, NULL},
{0}
};

static DMLEAF tEthernetStack181Params[] = {
{"LinkNumberOfEntries", &DMREAD, DMT_UNINT, get_link_count, NULL, NULL, NULL},
{"VLANTerminationNumberOfEntries", &DMREAD, DMT_UNINT, get_vlan_count, NULL, NULL, NULL},
{0}
};

static DMOBJ tEthernetStack181Root[] = {
{"Ethernet", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tEthernetStack181Obj, tEthernetStack181Params, NULL},
{0}
};

/* Device.Ethernet. is laneth_mtk.c's claim: Link and VLANTermination join it
 * by the merge (no .paths, dm_registry.c check_claims) */
static const struct dm_module ethernet_stack181_mtk_module = {
	.name  = "mtk-ethernet-stack-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tEthernetStack181Root,
};
DM_MODULE_REGISTER(ethernet_stack181_mtk_module);

/* ------------------------------------------------------------------ */
/* Bridging                                                            */
/* ------------------------------------------------------------------ */

struct bridge181 {
	int inst;
	int has_conn;
	struct wan_entry e;
};

struct port181 {
	int bridge;
	int port;
	char name[32];
	char lower[1024];
};

static int bridges(struct bridge181 *out, int max)
{
	struct wan_entry br[WAN_MAX_ENTRIES];
	int m = bridged_conns(br, WAN_MAX_ENTRIES), i, n = 0;

	out[n].inst = BRIDGE_LAN;
	out[n].has_conn = 0;
	n++;
	for (i = 0; i < m && n < max; i++, n++) {
		out[n].inst = BRIDGE_WAN(br[i].id);
		out[n].has_conn = 1;
		out[n].e = br[i];
	}
	return n;
}

/* the ports of a bridge, management port first */
static int ports(struct bridge181 *b, struct port181 *out, int max)
{
	int n = 1, k, len = 0;

	for (k = 1; k <= ETH_PORTS && n < max; k++) {
		if (k == uplink_lan_port())
			continue;
		if (b->has_conn ? !conn_binds_eth(&b->e, k) : bound_to_a_bridge(0, k))
			continue;
		out[n].bridge = b->inst;
		out[n].port = PORT_ETH(k);
		snprintf(out[n].name, sizeof(out[n].name), "eth0.%d", k);
		snprintf(out[n].lower, sizeof(out[n].lower), "Device.Ethernet.Interface.%d", k);
		n++;
	}
	for (k = 1; k <= SSID_PORTS && n < max; k++) {
		if (b->has_conn ? !conn_binds_ssid(&b->e, k) : bound_to_a_bridge(1, k))
			continue;
		out[n].bridge = b->inst;
		out[n].port = PORT_SSID(k);
		snprintf(out[n].name, sizeof(out[n].name), "%s", wlan_ifname_of_index(k));
		snprintf(out[n].lower, sizeof(out[n].lower), "Device.WiFi.SSID.%d", k);
		n++;
	}
	if (b->has_conn && n < max) {
		char *low = conn_lower(&b->e);

		out[n].bridge = b->inst;
		out[n].port = PORT_WANSIDE;
		if (conn_vlan_active(&b->e))
			snprintf(out[n].name, sizeof(out[n].name), "%s.%s", wan_uplink_iface(), wan_entry_opt(&b->e, "vlan_id"));
		else
			snprintf(out[n].name, sizeof(out[n].name), "%s", wan_uplink_iface());
		/* a bridge port on the uplink itself sits on the physical
		 * interface the uplink link sits on */
		if (strncmp(low, "Device.Ethernet.Link.", 21) == 0)
			snprintf(out[n].lower, sizeof(out[n].lower), "%s",
				 strcmp(wan_uplink_iface(), "pon") == 0 ? "Device.Optical.Interface.1" : "Device.Ethernet.Interface.5");
		else
			snprintf(out[n].lower, sizeof(out[n].lower), "%s", low);
		n++;
	}
	/* the management port: on top of all the others */
	out[0].bridge = b->inst;
	out[0].port = PORT_MGMT;
	snprintf(out[0].name, sizeof(out[0].name), "%s", b->has_conn ? wanbr_netdev(&b->e) : "br-lan");
	out[0].lower[0] = '\0';
	for (k = 1; k < n && len < (int)sizeof(out[0].lower) - 1; k++)
		len += snprintf(out[0].lower + len, sizeof(out[0].lower) - len, "%sDevice.Bridging.Bridge.%d.Port.%d",
				len ? "," : "", b->inst, out[k].port);
	return n;
}

static int browse_bridge181(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct bridge181 *b = dmcalloc(1 + WAN_MAX_ENTRIES, sizeof(*b));
	int n, i;
	char *inst;

	if (!b)
		return 0;
	n = bridges(b, 1 + WAN_MAX_ENTRIES);
	for (i = 0; i < n; i++) {
		dmasprintf(&inst, "%d", b[i].inst);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, &b[i], inst) == DM_STOP)
			break;
	}
	return 0;
}

#define PORTS_MAX	(1 + ETH_PORTS + SSID_PORTS + 1)

static int browse_port181(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct port181 *p = dmcalloc(PORTS_MAX, sizeof(*p));
	int n, i;
	char *inst;

	if (!p)
		return 0;
	n = ports((struct bridge181 *)prev_data, p, PORTS_MAX);
	for (i = 0; i < n; i++) {
		dmasprintf(&inst, "%d", p[i].port);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, &p[i], inst) == DM_STOP)
			break;
	}
	return 0;
}

#define BRIDGE(data)	((struct bridge181 *)(data))
#define PORT(data)	((struct port181 *)(data))

static int get_bridge_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Enabled";
	return 0;
}

static int get_bridge_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = BRIDGE(data)->has_conn ? wanbr_netdev(&BRIDGE(data)->e) : "br-lan";
	return 0;
}

static int get_bridge_standard(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "802.1D-2004";
	return 0;
}

static int get_bridge_ports(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct port181 *p = dmcalloc(PORTS_MAX, sizeof(*p));

	dmasprintf(value, "%d", p ? ports(BRIDGE(data), p, PORTS_MAX) : 0);
	return 0;
}

static int get_zero181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0";
	return 0;
}

static int get_port_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = netdev_status(PORT(data)->name);
	return 0;
}

static int get_port_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(PORT(data)->name);
	return 0;
}

static int get_port_lower(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(PORT(data)->lower);
	return 0;
}

static int get_port_mgmt(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = PORT(data)->port == PORT_MGMT ? "true" : "false";
	return 0;
}

/* the management port is the bridge itself: the netifd interface on it */
static int get_port_lastchange(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char sec[32];

	if (PORT(data)->port != PORT_MGMT)
		*value = "0";
	else if (PORT(data)->bridge == BRIDGE_LAN)
		*value = iface_uptime("lan");
	else {
		snprintf(sec, sizeof(sec), "if_wanbr%d", PORT(data)->bridge - BRIDGE_WAN(0));
		*value = iface_uptime(sec);
	}
	return 0;
}

/* /sys/class/net/<port>/brport/state (br_private.h BR_STATE_*); the
 * management port forwards while the bridge is up */
static int get_port_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	static const char *const st[] = { "Disabled", "Listening", "Learning", "Forwarding", "Blocking" };
	char path[96], *v;
	int n;

	if (PORT(data)->port == PORT_MGMT) {
		*value = strcmp(netdev_status(PORT(data)->name), "Up") == 0 ? "Forwarding" : "Disabled";
		return 0;
	}
	snprintf(path, sizeof(path), "/sys/class/net/%s/brport/state", PORT(data)->name);
	v = mtk_file_line(path);
	n = (v && *v) ? atoi(v) : 0;
	*value = (char *)st[n >= 0 && n <= 4 ? n : 0];
	return 0;
}

static int get_port_stat(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dip_stat181(PORT(data)->name, STATS181_LEAF(refparam));
	return 0;
}

static DMLEAF tPort181StatsParams[] = {
{"BytesSent", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_port_stat, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_port_stat, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, get_port_stat, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, get_port_stat, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_UNLONG, get_port_stat, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_port_stat, NULL, NULL, NULL},
{0}
};

static DMOBJ tPort181Obj[] = {
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tPort181StatsParams, NULL},
{0}
};

MTK_SET_SAME(bridge_standard, get_bridge_standard)
MTK_SET_SAME(port_lower, get_port_lower)
MTK_SET_SAME_BOOL(port_mgmt, get_port_mgmt)

static DMLEAF tPort181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMWRITE, DMT_BOOL, get_true181, set_same_true181, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_port_status, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_alias_port, set_alias_port, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_port_name, NULL, NULL, NULL},
{"LastChange", &DMREAD, DMT_UNINT, get_port_lastchange, NULL, NULL, NULL},
{"LowerLayers", &DMWRITE, DMT_STRING, get_port_lower, set_same_port_lower, NULL, NULL},
{"ManagementPort", &DMWRITE, DMT_BOOL, get_port_mgmt, set_same_port_mgmt, NULL, NULL},
{"PortState", &DMREAD, DMT_STRING, get_port_state, NULL, NULL, NULL},
{0}
};

static DMOBJ tBridge181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Port", &DMREAD, NULL, NULL, NULL, browse_port181, NULL, NULL, tPort181Obj, tPort181Params, NULL},
{0}
};

static DMLEAF tBridge181Params[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_true181, set_same_true181, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_bridge_status, NULL, NULL, NULL},
{"Alias", &DMWRITE, DMT_STRING, get_alias_bridge, set_alias_bridge, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_bridge_name, NULL, NULL, NULL},
{"Standard", &DMWRITE, DMT_STRING, get_bridge_standard, set_same_bridge_standard, NULL, NULL},
{"PortNumberOfEntries", &DMREAD, DMT_UNINT, get_bridge_ports, NULL, NULL, NULL},
{"VLANNumberOfEntries", &DMREAD, DMT_UNINT, get_zero181, NULL, NULL, NULL},
{"VLANPortNumberOfEntries", &DMREAD, DMT_UNINT, get_zero181, NULL, NULL, NULL},
{0}
};

static int get_bridge_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bridge181 *b = dmcalloc(1 + WAN_MAX_ENTRIES, sizeof(*b));

	dmasprintf(value, "%d", b ? bridges(b, 1 + WAN_MAX_ENTRIES) : 0);
	return 0;
}

static DMOBJ tBridging181Obj[] = {
{"Bridge", &DMREAD, NULL, NULL, NULL, browse_bridge181, NULL, NULL, tBridge181Obj, tBridge181Params, NULL},
{0}
};

/* br-lan and one bridge per bridged connection: 1 + WAN_MAX_ENTRIES, all
 * of them 802.1D bridges */
static int get_bridge_max(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "%d", 1 + WAN_MAX_ENTRIES);
	return 0;
}

static DMLEAF tBridging181Params[] = {
{"MaxBridgeEntries", &DMREAD, DMT_UNINT, get_bridge_max, NULL, NULL, NULL},
{"MaxDBridgeEntries", &DMREAD, DMT_UNINT, get_bridge_max, NULL, NULL, NULL},
{"BridgeNumberOfEntries", &DMREAD, DMT_UNINT, get_bridge_count, NULL, NULL, NULL},
{0}
};

/* ------------------------------------------------------------------ */
/* InterfaceStack                                                      */
/* ------------------------------------------------------------------ */

struct stack181 {
	char higher[64];
	char lower[64];
};

static void stack_add(struct stack181 *s, int *n, int max, const char *higher, const char *lowers)
{
	char *copy = dmstrdup(lowers ? lowers : ""), *tok, *save = NULL;

	for (tok = strtok_r(copy, ",", &save); tok && *n < max; tok = strtok_r(NULL, ",", &save)) {
		snprintf(s[*n].higher, sizeof(s[*n].higher), "%s", higher);
		snprintf(s[*n].lower, sizeof(s[*n].lower), "%s", tok);
		(*n)++;
	}
}

static int stack_rows(struct stack181 *s, int max)
{
	const char *secs[64], *insts[64];
	struct wan_entry *all = dmcalloc(WAN_MAX_ENTRIES, sizeof(*all));
	struct link181 *l = dmcalloc(2 + WAN_MAX_ENTRIES, sizeof(*l));
	struct bridge181 *b = dmcalloc(1 + WAN_MAX_ENTRIES, sizeof(*b));
	struct port181 *p = dmcalloc(PORTS_MAX, sizeof(*p));
	char higher[64], low[64];
	int n = 0, i, j, c, nb;
	long vid;

	if (!all || !l || !b || !p)
		return 0;
	/* IP.Interface */
	c = dip_all(secs, insts, 64);
	for (i = 0; i < c; i++) {
		snprintf(higher, sizeof(higher), "Device.IP.Interface.%s", insts[i]);
		stack_add(s, &n, max, higher, stack181_ipif_lower(secs[i]));
	}
	/* PPP.Interface, VLANTermination */
	c = wan_entries_all(&all, WAN_MAX_ENTRIES);
	for (i = 0; i < c; i++) {
		if (all[i].ppp && *wan_entry_opt(&all[i], "ppp_int_instance")) {
			snprintf(higher, sizeof(higher), "Device.PPP.Interface.%s", wan_entry_opt(&all[i], "ppp_int_instance"));
			stack_add(s, &n, max, higher, conn_lower(&all[i]));
		}
	}
	for (i = 0; i < c; i++) {
		if (!conn_vlan_id(&all[i], &vid))
			continue;
		snprintf(higher, sizeof(higher), "Device.Ethernet.VLANTermination.%d", VLAN_OF(all[i].id));
		snprintf(low, sizeof(low), "Device.Ethernet.Link.%d", LINK_UPLINK);
		stack_add(s, &n, max, higher, low);
	}
	/* Ethernet.Link */
	c = links(l, 2 + WAN_MAX_ENTRIES);
	for (i = 0; i < c; i++) {
		snprintf(higher, sizeof(higher), "Device.Ethernet.Link.%d", l[i].inst);
		stack_add(s, &n, max, higher, l[i].lower);
	}
	/* Bridging.Bridge.{i}.Port */
	nb = bridges(b, 1 + WAN_MAX_ENTRIES);
	for (i = 0; i < nb; i++) {
		c = ports(&b[i], p, PORTS_MAX);
		for (j = 0; j < c; j++) {
			snprintf(higher, sizeof(higher), "Device.Bridging.Bridge.%d.Port.%d", b[i].inst, p[j].port);
			stack_add(s, &n, max, higher, p[j].lower);
		}
	}
	/* WiFi.SSID on its Radio: rai* on the 5 GHz Radio.2 (wlan_mtk.c
	 * radio181_number) */
	for (i = 1; i <= SSID_PORTS; i++) {
		snprintf(higher, sizeof(higher), "Device.WiFi.SSID.%d", i);
		snprintf(low, sizeof(low), "Device.WiFi.Radio.%d",
			 strncmp(wlan_ifname_of_index(i), "rai", 3) == 0 ? 2 : 1);
		stack_add(s, &n, max, higher, low);
	}
	return n;
}

static int browse_stack181(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct stack181 *s = dmcalloc(STACK_MAX, sizeof(*s));
	int n, i;
	char *inst;

	if (!s)
		return 0;
	n = stack_rows(s, STACK_MAX);
	for (i = 0; i < n; i++) {
		dmasprintf(&inst, "%d", i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, &s[i], inst) == DM_STOP)
			break;
	}
	return 0;
}

#define STACK(data)	((struct stack181 *)(data))

static int get_stack_higher(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(STACK(data)->higher);
	return 0;
}

static int get_stack_lower(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = dmstrdup(STACK(data)->lower);
	return 0;
}

static int get_stack_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct stack181 *s = dmcalloc(STACK_MAX, sizeof(*s));

	dmasprintf(value, "%d", s ? stack_rows(s, STACK_MAX) : 0);
	return 0;
}

static DMLEAF tStack181Params[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Alias", &DMWRITE, DMT_STRING, get_alias_stack, set_alias_stack, NULL, NULL},
{"HigherLayer", &DMREAD, DMT_STRING, get_stack_higher, NULL, NULL, NULL},
{"LowerLayer", &DMREAD, DMT_STRING, get_stack_lower, NULL, NULL, NULL},
{0}
};

static DMOBJ tStack181Root[] = {
{"Bridging", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tBridging181Obj, tBridging181Params, NULL},
{"InterfaceStack", &DMREAD, NULL, NULL, NULL, browse_stack181, NULL, NULL, NULL, tStack181Params, NULL},
{0}
};

static DMLEAF tStack181RootParams[] = {
{"InterfaceStackNumberOfEntries", &DMREAD, DMT_UNINT, get_stack_count, NULL, NULL, NULL},
{0}
};

static const char *const stack181_mtk_paths[] = {
	"Device.Bridging.",
	"Device.InterfaceStack.",
	"Device.InterfaceStackNumberOfEntries",
	NULL
};

static const struct dm_module stack181_mtk_module = {
	.name   = "mtk-stack-181",
	.model  = DM_MODEL_TR181,
	.order  = DM_ORDER_SDK,
	.objs   = tStack181Root,
	.params = tStack181RootParams,
	.paths  = stack181_mtk_paths,
};
DM_MODULE_REGISTER(stack181_mtk_module);
