/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice.1.WLANConfiguration.{i}.AssociatedDevice.{i}.
 *	and its .Stats., ported from the assoc_* helpers of
 *	functions/tr098/lan_device.
 *
 *	One source: ubus call hni getWlanDeviceList {"interface":"<ra*>"}, whose
 *	"infor" object carries one member per station.  The shell had to spool
 *	that into /tmp cache files because a shell function cannot hold state
 *	between the getter calls of one RPC -- here the browse callback reads it
 *	once and hands each station's fields to its own instance, so the cache
 *	and its cleanup disappear.
 *
 *	A disabled interface is not queried at all, like assoc_build_cache().
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dmubus.h"
#include "dmjson.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wlan_mtk.h"

/* one station, dm-allocated for the lifetime of the request */
struct assoc_entry {
	char *mac;
	char *ip;
	char *rate;
	char *rssi;
	char *tx_packets;
	char *rx_packets;
	char *tx_bytes;
	char *rx_bytes;
	char *tx_fail;
	char *tx_drop;
	char *rx_error;
	char *rx_drop;
};

static char *field_or(json_object *o, char *name, char *fallback)
{
	char *v = dmjson_get_value(o, 1, name);

	return (v && *v) ? v : fallback;
}

/* The station list of one interface, or NULL when the radio is down. */
static json_object *assoc_list(const struct wlan_iface *w)
{
	json_object *res = NULL;

	if (!w || !w->name)
		return NULL;
	if (strcmp(mtk_uci("wireless", (char *)w->name, "disabled"), "1") == 0)
		return NULL;
	dmubus_call("hni", "getWlanDeviceList",
		    UBUS_ARGS{{"interface", (char *)w->name, String}}, 1, &res);
	if (!res)
		return NULL;
	return json_object_object_get(res, "infor");
}

int wlan_assoc_count(const struct wlan_iface *w)
{
	json_object *infor = assoc_list(w);
	int n = 0;

	if (!infor)
		return 0;
	json_object_object_foreach(infor, key, val) {
		char *mac;

		(void)key;
		mac = dmjson_get_value(val, 1, "MAC");
		if (mac && *mac)
			n++;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* leaves                                                              */
/* ------------------------------------------------------------------ */

#define ASSOC_GET(fn, member)								\
	static int fn(char *refparam, struct dmctx *ctx, void *data, char *instance,	\
		      char **value)							\
	{										\
		struct assoc_entry *a = (struct assoc_entry *)data;			\
											\
		*value = (a && a->member) ? a->member : "";				\
		return 0;								\
	}

ASSOC_GET(get_assoc_mac, mac)
ASSOC_GET(get_assoc_ip, ip)
ASSOC_GET(get_assoc_rate, rate)
ASSOC_GET(get_assoc_rssi, rssi)
ASSOC_GET(get_assoc_tx_packets, tx_packets)
ASSOC_GET(get_assoc_rx_packets, rx_packets)
ASSOC_GET(get_assoc_tx_bytes, tx_bytes)
ASSOC_GET(get_assoc_rx_bytes, rx_bytes)
ASSOC_GET(get_assoc_tx_fail, tx_fail)
ASSOC_GET(get_assoc_tx_drop, tx_drop)
ASSOC_GET(get_assoc_rx_error, rx_error)
ASSOC_GET(get_assoc_rx_drop, rx_drop)

/* the shell answered both with a constant: a station in the list is by
 * definition associated and authenticated */
static int get_assoc_true(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "1";
	return 0;
}

static int get_assoc_zero(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "0";
	return 0;
}

static int browseAssocInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	const struct wlan_iface *w = wlan_iface_of(prev_data);
	json_object *infor = assoc_list(w);
	char *idx, *idx_last = NULL;
	int id = 0;

	if (!infor)
		return 0;
	json_object_object_foreach(infor, key, val) {
		struct assoc_entry a = {0};

		(void)key;
		a.mac = dmjson_get_value(val, 1, "MAC");
		if (!a.mac || !*a.mac)
			continue;
		a.ip         = field_or(val, "IP", "0.0.0.0");
		a.rate       = field_or(val, "LastDataTransmitRate", "0");
		a.rssi       = field_or(val, "RSSI", "0");
		a.tx_packets = field_or(val, "TxPackets", "0");
		a.rx_packets = field_or(val, "RxPackets", "0");
		a.tx_bytes   = field_or(val, "TxBytes", "0");
		a.rx_bytes   = field_or(val, "RxBytes", "0");
		a.tx_fail    = field_or(val, "TxFailCount", "0");
		a.tx_drop    = field_or(val, "TxDropCount", "0");
		a.rx_error   = field_or(val, "RxErrorPkt", "0");
		a.rx_drop    = field_or(val, "RxDropPkt", "0");

		idx = handle_update_instance(3, dmctx, &idx_last, update_instance_without_section,
					     1, ++id);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&a, idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tAssocStatsParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"BytesSent", &DMREAD, DMT_UNINT, get_assoc_tx_bytes, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNINT, get_assoc_rx_bytes, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNINT, get_assoc_tx_packets, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNINT, get_assoc_rx_packets, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_assoc_tx_fail, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_assoc_rx_error, NULL, NULL, NULL},
{"RetransCount", &DMREAD, DMT_UNINT, get_assoc_zero, NULL, NULL, NULL},
{"RetryCount", &DMREAD, DMT_UNINT, get_assoc_zero, NULL, NULL, NULL},
{"TxDropCount", &DMREAD, DMT_UNINT, get_assoc_tx_drop, NULL, NULL, NULL},
{"RxDropCount", &DMREAD, DMT_UNINT, get_assoc_rx_drop, NULL, NULL, NULL},
{0}
};

static DMOBJ tAssocObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tAssocStatsParam, NULL},
{0}
};

static DMLEAF tAssocParam[] = {
{"Active", &DMREAD, DMT_BOOL, get_assoc_true, NULL, NULL, NULL},
{"AssociatedDeviceAuthenticationState", &DMREAD, DMT_BOOL, get_assoc_true, NULL, NULL, NULL},
{"AssociatedDeviceIPAddress", &DMREAD, DMT_STRING, get_assoc_ip, NULL, NULL, NULL},
{"AssociatedDeviceMACAddress", &DMREAD, DMT_STRING, get_assoc_mac, NULL, NULL, NULL},
{"LastDataTransmitRate", &DMREAD, DMT_STRING, get_assoc_rate, NULL, NULL, NULL},
{"RSSI", &DMREAD, DMT_INT, get_assoc_rssi, NULL, NULL, NULL},
{"SignalStrength", &DMREAD, DMT_INT, get_assoc_rssi, NULL, NULL, NULL},
{0}
};

static DMOBJ tWlanAssocObj[] = {
{"AssociatedDevice", &DMREAD, NULL, NULL, NULL, browseAssocInst, NULL, &DMNONE, tAssocObj, tAssocParam, NULL},
{0}
};

static DMOBJ tLanDeviceAssocObj[] = {
{"WLANConfiguration", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tWlanAssocObj, NULL, NULL},
{0}
};

/* browseinstobj left NULL twice on purpose: lan_mtk.c owns the LANDevice
 * instance and wlan_mtk.c owns the WLANConfiguration instance */
static DMOBJ tLanDeviceAssocRoot[] = {
{"LANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tLanDeviceAssocObj, NULL, NULL},
{0}
};

/* No .paths here: wlan_mtk.c claims the whole WLANConfiguration branch for
 * all three modules of this object.  Claiming the subtree again would be a
 * duplicate owner, which check_claims() reports. */
static const struct dm_module wlanassoc_mtk_module = {
	.name  = "mtk-wlan-assoc",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanDeviceAssocRoot,
};
DM_MODULE_REGISTER(wlanassoc_mtk_module);
