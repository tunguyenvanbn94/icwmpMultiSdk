/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.WANDevice. of the MTK/Airoha product -- the frame
 *	of the branch and the three objects that do not depend on a WAN entry:
 *
 *	  WANDevice.{i}.WANCommonInterfaceConfig.               9 leaves
 *	  WANDevice.{i}.WANEthernetInterfaceConfig.             4 leaves
 *	  WANDevice.{i}.WANEthernetInterfaceConfig.Stats.       4 leaves
 *	  WANDevice.{i}.WANConnectionDevice.{i}.WANDSLLinkConfig. 5 leaves
 *
 *	Ported from functions/tr098/wan_device (entry_execute_method_root_WANDevice
 *	and everything below wancommoninterfaceconfig_execute_params).  The
 *	WANIPConnection / WANPPPConnection instances under WANConnectionDevice.1
 *	are the modules after this one (wanip_mtk.c and the P4b-P4f files).  This
 *	file claims the whole WANDevice branch for all of them (.paths at the
 *	bottom), the others declare no .paths and dm_registry merges their trees.
 *
 *	Three things of the product are kept verbatim because the ACS has been
 *	reading them for years:
 *
 *	1. WANAccessType is EMPTY.  The shell registered it with the getter
 *	   "wan_common_get_access_type", and that function does not exist
 *	   anywhere in the function library (grep over the whole ext/ tree finds
 *	   the one reference and no definition), so the command substitution
 *	   produced the empty string.  TR-098 wants "DSL" | "Ethernet" | "POTS";
 *	   filling one in is a product decision, not a CWMP one, so this port
 *	   answers "" exactly like the device does today.
 *	2. WANDSLLinkConfig and WANEthernetInterfaceConfig are constants.  This
 *	   is a PON/Ethernet gateway with no DSL line -- the shell called those
 *	   getters "fake" itself.  They stay so the objects keep existing for an
 *	   ACS template that walks them.
 *	3. The writable-but-inert leaves (Enable of both objects, LinkType,
 *	   ATMEncapsulation, DestinationAddress) accept a write and drop it,
 *	   which is what "wan_dsl_link_set_fake" and the literal setter "true"
 *	   did.  A 9008 here would fault provisioning that has always succeeded.
 *
 *	Booleans keep the exact spelling the shell echoed ("true", "1"), so a
 *	value-by-value comparison against the old client on the board does not
 *	report a difference where there is none.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"
#include "wan_mtk.h"

/* ------------------------------------------------------------------ */
/* uplink                                                              */
/* ------------------------------------------------------------------ */

const char *wan_uplink_iface(void)
{
	char *uplink = mtk_uci("clay", "opermode", "uplink");

	if (strcmp(uplink, "eth1") == 0)
		return "eth1";
	if (strcmp(uplink, "eth2") == 0)
		return "eth0.2";
	if (strcmp(uplink, "eth3") == 0)
		return "eth0.3";
	if (strcmp(uplink, "eth4") == 0)
		return "eth0.4";
	if (strcmp(uplink, "pon") == 0)
		return "pon";
	return "eth0";
}

char *wan_netdev_stat(const char *iface, const char *counter)
{
	char path[128];

	if (!iface || !iface[0] || !counter || !counter[0])
		return "";
	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", iface, counter);
	return mtk_file_line(path);
}

static int uplink_is_pon(void)
{
	return strcmp(mtk_uci("clay", "opermode", "uplink"), "pon") == 0;
}

/* ------------------------------------------------------------------ */
/* WANCommonInterfaceConfig                                            */
/* ------------------------------------------------------------------ */

static int get_wancommon_enabled(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	/* the shell getter was "echo 1" */
	*value = "1";
	return 0;
}

/* see the header comment: the product's getter does not exist */
static int get_wancommon_access_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "";
	return 0;
}

/* GPON upstream line rate, or 1 Gbit/s when the uplink is an Ethernet port */
static int get_wancommon_up_rate(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = uplink_is_pon() ? "1244160000" : "1000000000";
	return 0;
}

static int get_wancommon_down_rate(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = uplink_is_pon() ? "2488320000" : "1000000000";
	return 0;
}

/* pon.xpon_link.trafficStatus, written by the PON stack */
static int get_wancommon_link_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = (strcmp(mtk_uci("pon", "xpon_link", "trafficStatus"), "up") == 0) ? "Up" : "Down";
	return 0;
}

static int get_uplink_tx_bytes(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wan_netdev_stat(wan_uplink_iface(), "tx_bytes");
	return 0;
}

static int get_uplink_rx_bytes(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wan_netdev_stat(wan_uplink_iface(), "rx_bytes");
	return 0;
}

static int get_uplink_tx_packets(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wan_netdev_stat(wan_uplink_iface(), "tx_packets");
	return 0;
}

static int get_uplink_rx_packets(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = wan_netdev_stat(wan_uplink_iface(), "rx_packets");
	return 0;
}

/* ------------------------------------------------------------------ */
/* WANEthernetInterfaceConfig / WANDSLLinkConfig -- constants          */
/* ------------------------------------------------------------------ */

/*
 * Writable in the product's tree with nothing behind them: the shell gave
 * WANEthernetInterfaceConfig.Enable the literal setter "true" (/bin/true,
 * succeeds and changes nothing) and every WANDSLLinkConfig setter was
 * wan_dsl_link_set_fake().  Kept identical.
 */
static int set_accept_and_drop(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	return 0;
}

static int get_eth_duplex(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Full Duplex";
	return 0;
}

static int get_eth_maxbitrate(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "1000";
	return 0;
}

/*
 * "Down" is what the product reports, on every board, whatever the uplink is
 * doing: get_fake_WANEthernetStatus() was a constant.  PhysicalLinkStatus of
 * WANCommonInterfaceConfig above is the one that tracks the real link.
 */
static int get_eth_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Down";
	return 0;
}

static int get_true(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "true";
	return 0;
}

static int get_dsl_link_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Up";
	return 0;
}

static int get_dsl_link_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "EoA";
	return 0;
}

static int get_dsl_atm_encapsulation(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "LLC";
	return 0;
}

static int get_dsl_destination(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "PVC:0/35";
	return 0;
}

/* ------------------------------------------------------------------ */
/* instances                                                           */
/* ------------------------------------------------------------------ */

/* one WANDevice, hardcoded as .1 in every path of the shell library */
static int browseWanDeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;

	idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, 1);
	DM_LINK_INST_OBJ(dmctx, parent_node, NULL, idx);
	return 0;
}

/* likewise one WANConnectionDevice: the connections are its children */
static int browseWanConnectionDeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;

	idx = handle_update_instance(2, dmctx, &idx_last, update_instance_without_section, 1, 1);
	DM_LINK_INST_OBJ(dmctx, parent_node, NULL, idx);
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tWanCommonParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"EnabledForInternet", &DMREAD, DMT_BOOL, get_wancommon_enabled, NULL, NULL, NULL},
{"WANAccessType", &DMREAD, DMT_STRING, get_wancommon_access_type, NULL, NULL, NULL},
{"Layer1UpstreamMaxBitRate", &DMREAD, DMT_UNINT, get_wancommon_up_rate, NULL, NULL, NULL},
{"Layer1DownstreamMaxBitRate", &DMREAD, DMT_UNINT, get_wancommon_down_rate, NULL, NULL, NULL},
{"PhysicalLinkStatus", &DMREAD, DMT_STRING, get_wancommon_link_status, NULL, NULL, NULL},
{"TotalBytesSent", &DMREAD, DMT_UNINT, get_uplink_tx_bytes, NULL, NULL, NULL},
{"TotalBytesReceived", &DMREAD, DMT_UNINT, get_uplink_rx_bytes, NULL, NULL, NULL},
{"TotalPacketsSent", &DMREAD, DMT_UNINT, get_uplink_tx_packets, NULL, NULL, NULL},
{"TotalPacketsReceived", &DMREAD, DMT_UNINT, get_uplink_rx_packets, NULL, NULL, NULL},
{0}
};

static DMLEAF tWanEthStatsParam[] = {
{"PacketsReceived", &DMREAD, DMT_UNINT, get_uplink_rx_packets, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNINT, get_uplink_tx_packets, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNINT, get_uplink_rx_bytes, NULL, NULL, NULL},
{"BytesSent", &DMREAD, DMT_UNINT, get_uplink_tx_bytes, NULL, NULL, NULL},
{0}
};

/*
 * MaxBitRate and Status are DMT_STRING on purpose: the shell passed no type
 * for those two rows, and an empty type is sent as xsd:string
 * (mtk_xsd_type() in dmplatform_mtk.c).  TR-098 types MaxBitRate as a string
 * enum anyway, so the wire format stays both correct and unchanged.
 */
static DMLEAF tWanEthParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_true, set_accept_and_drop, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_eth_status, NULL, NULL, NULL},
{"MaxBitRate", &DMREAD, DMT_STRING, get_eth_maxbitrate, NULL, NULL, NULL},
{"DuplexMode", &DMREAD, DMT_STRING, get_eth_duplex, NULL, NULL, NULL},
{0}
};

static DMOBJ tWanEthObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWanEthStatsParam, NULL},
{0}
};

static DMLEAF tWanDslLinkParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, get_true, set_accept_and_drop, NULL, NULL},
{"LinkStatus", &DMREAD, DMT_STRING, get_dsl_link_status, NULL, NULL, NULL},
{"LinkType", &DMWRITE, DMT_STRING, get_dsl_link_type, set_accept_and_drop, NULL, NULL},
{"ATMEncapsulation", &DMWRITE, DMT_STRING, get_dsl_atm_encapsulation, set_accept_and_drop, NULL, NULL},
{"DestinationAddress", &DMWRITE, DMT_STRING, get_dsl_destination, set_accept_and_drop, NULL, NULL},
{0}
};

/*
 * WANIPConnection / WANPPPConnection are not here: wanip_mtk.c declares them
 * under the same object names and dm_registry merges the two tables.
 */
static DMOBJ tWanConnectionDeviceObj[] = {
{"WANDSLLinkConfig", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWanDslLinkParam, NULL},
{0}
};

static DMOBJ tWanDeviceObj[] = {
{"WANCommonInterfaceConfig", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tWanCommonParam, NULL},
{"WANEthernetInterfaceConfig", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tWanEthObj, tWanEthParam, NULL},
{"WANConnectionDevice", &DMREAD, NULL, NULL, NULL, browseWanConnectionDeviceInst, NULL, NULL, tWanConnectionDeviceObj, NULL, NULL},
{0}
};

static DMOBJ tWanDeviceRoot[] = {
{"WANDevice", &DMREAD, NULL, NULL, NULL, browseWanDeviceInst, NULL, NULL, tWanDeviceObj, NULL, NULL},
{0}
};

/*
 * The whole branch, for this file and the four merged into it (wanip_mtk.c,
 * wanipv6_mtk.c, servicelist_mtk.c, portmapping_mtk.c): every leaf below
 * WANDevice. is C since P4, and the object paths are what sends AddObject /
 * DeleteObject of WANIPConnection / WANPPPConnection to wanip_mtk.c instead
 * of sdk/mtk/compat/ (K8).  With the branch claimed the compat walk never
 * descends into WANDevice. either.
 */
static const char *const wan_mtk_paths[] = {
	"InternetGatewayDevice.WANDevice.",
	NULL
};

static const struct dm_module wan_mtk_module = {
	.name  = "mtk-wan",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tWanDeviceRoot,
	.paths = wan_mtk_paths,
};
DM_MODULE_REGISTER(wan_mtk_module);

/* ------------------------------------------------------------------ */
/* TR-181 (cwmp.cpe.datamodel=tr181)                                    */
/* ------------------------------------------------------------------ */

/*
 * WANEthernetInterfaceConfig (+Stats) and the uplink totals of
 * WANCommonInterfaceConfig are Device.Ethernet.Interface.<WAN> (Upstream
 * true, built in laneth_mtk.c through wan_eth181_get/set below), the same
 * constants and the same uplink counters.  DuplexMode is spelled the TR-181
 * way ("Full Duplex" -> "Full").  PhysicalLinkStatus is the PON link:
 * Device.Optical.Interface.1.Status.  No TR-181 counterpart:
 * EnabledForInternet, WANAccessType (empty on the product), the two Layer1
 * line rates, and WANDSLLinkConfig (no DSL line).
 */

int wan_eth181_get(const char *leaf, char **value)
{
	static const struct {
		const char *leaf;
		const char *counter;	/* statistics/<counter> of the uplink */
	} counters[] = {
		{ "ErrorsSent", "tx_errors" }, { "ErrorsReceived", "rx_errors" },
		{ "DiscardPacketsSent", "tx_dropped" }, { "DiscardPacketsReceived", "rx_dropped" },
		{ "MulticastPacketsReceived", "multicast" },
	};
	char *v;
	int i;

	if (strcmp(leaf, "Enable") == 0)
		return get_true(NULL, NULL, NULL, NULL, value);
	if (strcmp(leaf, "Status") == 0)
		return get_eth_status(NULL, NULL, NULL, NULL, value);
	if (strcmp(leaf, "MaxBitRate") == 0)
		return get_eth_maxbitrate(NULL, NULL, NULL, NULL, value);
	if (strcmp(leaf, "DuplexMode") == 0) {
		*value = "Full";
		return 0;
	}
	if (strcmp(leaf, "Name") == 0) {
		*value = (char *)wan_uplink_iface();
		return 0;
	}
	if (strcmp(leaf, "MACAddress") == 0) {
		char path[96];

		snprintf(path, sizeof(path), "/sys/class/net/%s/address", wan_uplink_iface());
		*value = mtk_file_line(path);
		return 0;
	}
	if (strcmp(leaf, "BytesSent") == 0)
		return get_uplink_tx_bytes(NULL, NULL, NULL, NULL, value);
	if (strcmp(leaf, "BytesReceived") == 0)
		return get_uplink_rx_bytes(NULL, NULL, NULL, NULL, value);
	if (strcmp(leaf, "PacketsSent") == 0)
		return get_uplink_tx_packets(NULL, NULL, NULL, NULL, value);
	if (strcmp(leaf, "PacketsReceived") == 0)
		return get_uplink_rx_packets(NULL, NULL, NULL, NULL, value);
	for (i = 0; i < (int)(sizeof(counters) / sizeof(counters[0])); i++) {
		if (strcmp(leaf, counters[i].leaf) == 0) {
			v = wan_netdev_stat(wan_uplink_iface(), counters[i].counter);
			*value = (v && *v) ? v : "0";
			return 0;
		}
	}
	*value = "0";
	return 0;
}

/* Enable is accepted and dropped as on WANEthernetInterfaceConfig; the rest
 * was read only there */
int wan_eth181_set(const char *leaf, char *value, int action)
{
	if (strcmp(leaf, "Enable") == 0)
		return mtk_parse_bool(value) < 0 ? FAULT_9007 : 0;
	return FAULT_9008;
}

static int get_optical181_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "pon";
	return 0;
}

static int browseOptical181Inst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;

	idx = handle_update_instance(1, dmctx, &idx_last, update_instance_without_section, 1, 1);
	DM_LINK_INST_OBJ(dmctx, parent_node, NULL, idx);
	return 0;
}

static int get_one181(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "1";
	return 0;
}

static DMLEAF tOptical181IfParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"Enable", &DMREAD, DMT_BOOL, get_true, NULL, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_wancommon_link_status, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_optical181_name, NULL, NULL, NULL},
{0}
};

static DMLEAF tOptical181Param[] = {
{"InterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_one181, NULL, NULL, NULL},
{0}
};

static DMOBJ tOptical181Obj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Interface", &DMREAD, NULL, NULL, NULL, browseOptical181Inst, NULL, NULL, NULL, tOptical181IfParam, NULL},
{0}
};

static DMOBJ tWan181Root[] = {
{"Optical", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, tOptical181Obj, tOptical181Param, NULL},
{0}
};

static const char *const wan181_mtk_paths[] = {
	"Device.Optical.",
	NULL
};

static const struct dm_module wan181_mtk_module = {
	.name  = "mtk-wan-181",
	.model = DM_MODEL_TR181,
	.order = DM_ORDER_SDK,
	.objs  = tWan181Root,
	.paths = wan181_mtk_paths,
};
DM_MODULE_REGISTER(wan181_mtk_module);
