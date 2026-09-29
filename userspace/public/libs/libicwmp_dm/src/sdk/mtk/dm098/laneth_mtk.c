/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice.1.LANEthernetInterfaceConfig.{i}. of the
 *	MTK/Airoha product, including .Stats.
 *
 *	Ported from functions/tr098/lan_device: sub_entry_LANEthIfConfig_params(),
 *	LANEthernet_Stats_execute_params() and the get_Stats_* helpers.  Four
 *	instances are always published, exactly like sub_entry_LANEthIfConfig_all()
 *	which loops 1..4 whatever LANEthernetInterfaceNumberOfEntries says.
 *
 *	Counters: byte counts come from the netdev of the port (eth0.{i}), every
 *	packet counter from the switch dump /proc/tc3162/gsw_stats, whose section
 *	header is the PHYSICAL port -- the same 1->6, 2->3, 3->2, 4->1 mapping the
 *	shell's toPhysical() did.  Reading the dump once per parameter is what the
 *	shell did outside its bulk-get cache, and it is a sysfs-cheap read.
 *
 *	The LANDevice object and its single instance are browsed in lan_mtk.c.
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dmtr098.h"
#include "dmuci.h"
#include "dmmem.h"
#include "dm_registry.h"
#include "dmmtk.h"

#define LAN_ETH_PORTS		4
#define GSW_STATS_FILE		"/proc/tc3162/gsw_stats"
#define SYSFS_NET_DIR		"/sys/devices/virtual/net"

/* instance number 1..4, handed to the getters as data */
static const int eth_instances[LAN_ETH_PORTS] = { 1, 2, 3, 4 };

static int eth_inst(void *data)
{
	return data ? *(const int *)data : 1;
}

/* toPhysical(): data model instance -> switch port in the dump */
static int eth_physical(int inst)
{
	switch (inst) {
	case 1: return 6;
	case 2: return 3;
	case 3: return 2;
	case 4: return 1;
	}
	return inst;
}

/* network.@SwitchPara[<inst-1>] */
static char *eth_switchpara(int inst, char *option)
{
	char section[32];

	snprintf(section, sizeof(section), "@SwitchPara[%d]", inst - 1);
	return mtk_uci("network", section, option);
}

/* ------------------------------------------------------------------ */
/* switch counter dump                                                  */
/* ------------------------------------------------------------------ */

/*
 * The dump is a flat file of "[ Port N ]" sections, each a list of
 * "<name> = <value>" rows (decimal, sometimes 0x hex).  Sum the rows whose
 * name appears in keys[], inside this port's section only.
 */
static unsigned long long gsw_sum(int inst, const char *const keys[])
{
	char header[32], line[256];
	unsigned long long total = 0;
	int in_port = 0, i;
	FILE *f;

	snprintf(header, sizeof(header), "[ Port %d ]", eth_physical(inst));
	f = fopen(GSW_STATS_FILE, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');

		if (nl)
			*nl = '\0';
		if (strcmp(line, header) == 0) {
			in_port = 1;
			continue;
		}
		if (strncmp(line, "[ Port ", 7) == 0) {
			if (in_port)
				break;
			continue;
		}
		if (!in_port)
			continue;
		for (i = 0; keys[i]; i++) {
			char *p = strstr(line, keys[i]);
			char *eq;

			if (!p)
				continue;
			eq = strchr(p, '=');
			if (!eq)
				continue;
			total += strtoull(eq + 1, NULL, 0);
			break;
		}
	}
	fclose(f);
	return total;
}

static int gsw_value(char **value, int inst, const char *const keys[])
{
	dmasprintf(value, "%llu", gsw_sum(inst, keys));
	return 0;
}

static int sysfs_counter(char **value, int inst, const char *name)
{
	char path[128];
	char *v;

	snprintf(path, sizeof(path), "%s/eth0.%d/statistics/%s", SYSFS_NET_DIR, inst, name);
	v = mtk_file_line(path);
	*value = (v && *v) ? v : "0";
	return 0;
}

/* ------------------------------------------------------------------ */
/* port parameters                                                      */
/* ------------------------------------------------------------------ */

static int get_eth_alias(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "Ethernet-%d", eth_inst(data));
	return 0;
}

static int get_eth_name(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	dmasprintf(value, "eth0.%d", eth_inst(data));
	return 0;
}

static int get_eth_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = strcmp(eth_switchpara(eth_inst(data), "enable"), "Yes") == 0 ? "1" : "0";
	return 0;
}

/*
 * The shell drove the PHY before touching UCI, and did nothing at all when the
 * port was already in the requested state.  Port 1 is the 2.5G PHY behind
 * /proc/tc3162/en8811_power, the other three are switch ports.
 */
static int set_eth_enable(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int inst = eth_inst(data), phy = inst - 1, b = mtk_parse_bool(value);
	char *old, *bit_rate;
	char section[32], port[8];

	if (b < 0)
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	old = eth_switchpara(inst, "enable");
	if ((b && strcmp(old, "Yes") == 0) || (!b && strcmp(old, "No") == 0))
		return 0;
	snprintf(section, sizeof(section), "@SwitchPara[%d]", phy);
	snprintf(port, sizeof(port), "%d", phy);
	if (b) {
		bit_rate = eth_switchpara(inst, "maxBitRate");
		if (phy == 0) {
			mtk_file_write("/proc/tc3162/en8811_power", "power on");
		} else if (strcmp(bit_rate, "auto") == 0 || strcmp(bit_rate, "1000") == 0) {
			char *argv[] = { "/userfs/bin/ethphxcmd", "eth0", "media-type",
					       "auto", "port", port, NULL };
			mtk_exec(argv);
		} else {
			char media[16];
			char *argv[] = { "/userfs/bin/ethphxcmd", "eth0", "media-type",
					       media, "port", port, NULL };

			snprintf(media, sizeof(media), "%sFD", bit_rate);
			mtk_exec(argv);
		}
		dmuci_set_value("network", section, "enable", "Yes");
	} else {
		if (phy == 0) {
			mtk_file_write("/proc/tc3162/en8811_power", "power off");
		} else {
			char *argv[] = { "/userfs/bin/ethphxcmd", "eth0", "lanchip",
					       "disable", "port", port, NULL };
			mtk_exec(argv);
		}
		dmuci_set_value("network", section, "enable", "No");
	}
	return 0;
}

static int get_eth_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	int inst = eth_inst(data), phy = inst - 1;

	if (strcmp(eth_switchpara(inst, "enable"), "No") == 0) {
		*value = "Disable";
		return 0;
	}
	if (phy == 0) {
		/* "PHY[eth0.1]: 1000Mbps/Full" or "PHY[eth0.1]: Down" */
		char *line = mtk_file_line("/proc/tc3162/en8811_link_st");

		if (!line || !*line)
			*value = "Error";
		else
			*value = strstr(line, ": Down") ? "NoLink" : "Up";
	} else {
		char port[8];
		char *argv[] = { "/userfs/bin/switchmgr", "port", "linkstate", port, NULL };
		char *out, *p;

		snprintf(port, sizeof(port), "%d", phy);
		out = mtk_exec(argv);
		p = out ? strstr(out, "link_sate") : NULL;
		if (!p)
			*value = "Error";
		else if (strstr(p, "up"))
			*value = "Up";
		else if (strstr(p, "down"))
			*value = "NoLink";
		else
			*value = "Error";
	}
	return 0;
}

static int get_eth_mac(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *mac = mtk_file_line(SYSFS_NET_DIR "/br-lan/address");

	if (!mac || !*mac)
		mac = mtk_file_line("/sys/class/net/br-lan/address");
	*value = (mac && *mac) ? mac : "";
	return 0;
}

static int get_eth_mac_control(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "1";
	return 0;
}

static int get_eth_maxbitrate(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v = eth_switchpara(eth_inst(data), "maxBitRate");

	*value = strcmp(v, "auto") == 0 ? "Auto" : v;
	return 0;
}

static int set_eth_maxbitrate(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	int inst = eth_inst(data), phy = inst - 1;
	char section[32], port[8];
	char *enable;

	if (strcmp(value, "Auto") && strcmp(value, "10") && strcmp(value, "100") && strcmp(value, "1000"))
		return FAULT_9007;
	if (action == VALUECHECK)
		return 0;
	snprintf(section, sizeof(section), "@SwitchPara[%d]", phy);
	snprintf(port, sizeof(port), "%d", phy);
	enable = eth_switchpara(inst, "enable");
	if (phy == 0) {
		const char *mode;

		if (strcmp(value, "Auto") == 0)
			mode = "Auto";
		else if (strcmp(value, "1000") == 0)
			mode = "1Gbps";
		else if (strcmp(value, "100") == 0)
			mode = "100Mbps";
		else
			return FAULT_9007;	/* the 2.5G PHY has no 10Mbps mode */
		mtk_file_write("/proc/tc3162/en8811_speed_mode", mode);
	} else if (strcmp(enable, "Yes") == 0) {
		if (strcmp(value, "Auto") == 0 || strcmp(value, "1000") == 0) {
			char *argv[] = { "/userfs/bin/ethphxcmd", "eth0", "media-type",
					       "auto", "port", port, NULL };
			mtk_exec(argv);
		} else {
			char media[16];
			char *argv[] = { "/userfs/bin/ethphxcmd", "eth0", "media-type",
					       media, "port", port, NULL };

			snprintf(media, sizeof(media), "%sFD", value);
			mtk_exec(argv);
		}
	}
	dmuci_set_value("network", section, "maxBitRate",
			strcmp(value, "Auto") == 0 ? "auto" : value);
	return 0;
}

static int get_eth_duplex(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	*value = "Full";
	return 0;
}

static int set_eth_duplex(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	if (strcmp(value, "Full") != 0)
		return FAULT_9007;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Stats                                                                */
/* ------------------------------------------------------------------ */

static const char *const k_tx_unicast[]	  = { "Tx Unicase Pkts", NULL };
static const char *const k_rx_unicast[]	  = { "Rx Unicase Pkts", NULL };
static const char *const k_tx_multicast[] = { "Tx Multicast Pkts", NULL };
static const char *const k_rx_multicast[] = { "Rx Multicast Pkts", NULL };
static const char *const k_tx_broadcast[] = { "Tx Broadcast Pkts", NULL };
static const char *const k_rx_broadcast[] = { "Rx Broadcast Pkts", NULL };
static const char *const k_tx_packets[]	  = { "Tx Unicase Pkts", "Tx Multicast Pkts",
					      "Tx Broadcast Pkts", NULL };
static const char *const k_rx_packets[]	  = { "Rx Unicase Pkts", "Rx Multicast Pkts",
					      "Rx Broadcast Pkts", NULL };
static const char *const k_tx_errors[]	  = { "Tx Collision", "Tx Late Collision",
					      "Tx eXcessive Collision", NULL };
static const char *const k_rx_errors[]	  = { "Rx CRC Error", "Rx Fragment Error",
					      "Rx Align Error", "Rx Jabber Error", NULL };
static const char *const k_tx_discard[]	  = { "Tx Drop Pkts", NULL };
static const char *const k_rx_discard[]	  = { "Rx Drop Pkts", "Rx ING Drop Pkts",
					      "Rx ARL Drop Pkts", "Rx FILTER Drop Pkts", NULL };
static const char *const k_rx_unknown[]	  = { "Rx Under Size Pkts", "Rx Over Size Pkts", NULL };

static int get_st_bytes_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return sysfs_counter(value, eth_inst(data), "tx_bytes");
}

static int get_st_bytes_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return sysfs_counter(value, eth_inst(data), "rx_bytes");
}

static int get_st_packets_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_tx_packets);
}

static int get_st_packets_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_rx_packets);
}

static int get_st_errors_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_tx_errors);
}

static int get_st_errors_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_rx_errors);
}

static int get_st_discard_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_tx_discard);
}

static int get_st_discard_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_rx_discard);
}

static int get_st_multicast_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_tx_multicast);
}

static int get_st_multicast_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_rx_multicast);
}

static int get_st_broadcast_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_tx_broadcast);
}

static int get_st_broadcast_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_rx_broadcast);
}

static int get_st_unknown_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_rx_unknown);
}

static int get_st_unicast_sent(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_tx_unicast);
}

static int get_st_unicast_recv(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	return gsw_value(value, eth_inst(data), k_rx_unicast);
}

static int browseLanEthInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	char *idx, *idx_last = NULL;
	int i;

	for (i = 0; i < LAN_ETH_PORTS; i++) {
		idx = handle_update_instance(2, dmctx, &idx_last, update_instance_without_section, 1, i + 1);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, (void *)&eth_instances[i], idx) == DM_STOP)
			break;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tables                                                              */
/* ------------------------------------------------------------------ */

static DMLEAF tLanEthStatsParam[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"BytesSent", &DMREAD, DMT_UNINT, get_st_bytes_sent, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNINT, get_st_bytes_recv, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNINT, get_st_packets_sent, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNINT, get_st_packets_recv, NULL, NULL, NULL},
{"ErrorsSent", &DMREAD, DMT_UNINT, get_st_errors_sent, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, get_st_errors_recv, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, get_st_discard_sent, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, get_st_discard_recv, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_UNINT, get_st_multicast_sent, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_UNINT, get_st_multicast_recv, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_UNINT, get_st_broadcast_sent, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_UNINT, get_st_broadcast_recv, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, get_st_unknown_recv, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_UNINT, get_st_unicast_sent, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_UNINT, get_st_unicast_recv, NULL, NULL, NULL},
{0}
};

static DMOBJ tLanEthInstObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tLanEthStatsParam, NULL},
{0}
};

static DMLEAF tLanEthInstParam[] = {
{"Alias", &DMREAD, DMT_STRING, get_eth_alias, NULL, NULL, NULL},
{"Enable", &DMWRITE, DMT_BOOL, get_eth_enable, set_eth_enable, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_eth_status, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, get_eth_name, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, get_eth_mac, NULL, NULL, NULL},
{"MACAddressControlEnabled", &DMWRITE, DMT_BOOL, get_eth_mac_control, NULL, NULL, NULL},
{"MaxBitRate", &DMWRITE, DMT_STRING, get_eth_maxbitrate, set_eth_maxbitrate, NULL, NULL},
{"DuplexMode", &DMWRITE, DMT_STRING, get_eth_duplex, set_eth_duplex, NULL, NULL},
{0}
};

static DMOBJ tLanDeviceEthObj[] = {
{"LANEthernetInterfaceConfig", &DMREAD, NULL, NULL, NULL, browseLanEthInst, NULL, &DMNONE, tLanEthInstObj, tLanEthInstParam, NULL},
{0}
};

/* browseinstobj left NULL on purpose: lan_mtk.c owns the LANDevice instance */
static DMOBJ tLanDeviceEthRoot[] = {
{"LANDevice", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tLanDeviceEthObj, NULL, NULL},
{0}
};

static const char *const laneth_mtk_paths[] = {
	"InternetGatewayDevice.LANDevice.1.LANEthernetInterfaceConfig.",
	NULL
};

static const struct dm_module laneth_mtk_module = {
	.name  = "mtk-laneth",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tLanDeviceEthRoot,
	.paths = laneth_mtk_paths,
};
DM_MODULE_REGISTER(laneth_mtk_module);
