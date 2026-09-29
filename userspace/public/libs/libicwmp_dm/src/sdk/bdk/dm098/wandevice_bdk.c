/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.WANDevice.1. on Broadcom BDK.
 *
 *	One WANDevice = the upstream Ethernet port (Device.Ethernet.Interface.{i}
 *	with Upstream=1, "eth1" on MO77300EB, "eth0" on the modded reference
 *	board).  One WANConnectionDevice.  One WANIPConnection per
 *	Device.IP.Interface.{i} that is not the LAN bridge (br0) and not loopback
 *	and not on top of PPP, joined with:
 *	  aux0 = Device.DHCPv4.Client.{i} whose Interface points at it
 *	  aux1 = Device.NAT.InterfaceSetting.{i} whose Interface points at it
 *	  aux2 = Device.Routing.Router.1.IPv4Forwarding.{i} default route on it
 *	  aux3 = Device.Ethernet.Interface.{i} upstream port
 *	One WANPPPConnection per Device.IP.Interface.{i} whose LowerLayers is
 *	Device.PPP.Interface.{p} (aux0 = p, aux1/aux2/aux3 as above).  Existing
 *	PPP instances only: creating a PPP stack (PPP.Interface + IP.Interface +
 *	LowerLayers) through AddObject is not done here.
 *	Both connection types carry PortMapping.{i} <- Device.NAT.PortMapping.{i}
 *	bound to that IP.Interface (Add/Delete) and Stats. <- IP.Interface.Stats.
 *
 *	Reference: dump logs/20260828_referenceBoard: WAN = Device.IP.Interface.2
 *	(LowerLayers Ethernet.VLANTermination.1 -> Ethernet.Link.2 ->
 *	Ethernet.Interface.1 Upstream=TRUE), DHCPv4.Client.1, NAT.InterfaceSetting.1,
 *	Routing.Router.1.IPv4Forwarding.1 (Origin DHCPv4, DestIPAddress "").
 *
 *	NOT BUILD-TESTED YET.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "dmtr098.h"
#include "dmcommon.h"
#include "landevice_bdk.h"
#include "wandevice_bdk.h"
#include "dmbdk.h"

static int is_true(const char *v)
{
	return v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0);
}

/* upstream Ethernet.Interface instance (0 if none) */
static unsigned int find_upstream_eth(void)
{
	unsigned int *inst = NULL, num = 0, i;
	char path[96], *v;

	if (bdk_get_instances("Device.Ethernet.Interface.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), "Device.Ethernet.Interface.%u.Upstream", inst[i]);
		bdk_get_value_default(path, "0", &v);
		if (is_true(v))
			return inst[i];
	}
	return 0;
}

/* first instance of objpath whose leaf "Interface" equals want ("" if none) */
static unsigned int find_by_interface_ref(const char *objpath, const char *want, const char *extra_leaf, const char *extra_want)
{
	unsigned int *inst = NULL, num = 0, i;
	char path[128], *v;

	if (bdk_get_instances(objpath, &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), "%s%u.Interface", objpath, inst[i]);
		bdk_get_value_default(path, "", &v);
		if (strcmp(v, want) != 0)
			continue;
		if (extra_leaf) {
			snprintf(path, sizeof(path), "%s%u.%s", objpath, inst[i], extra_leaf);
			bdk_get_value_default(path, "", &v);
			if (strcmp(v, extra_want) != 0)
				continue;
		}
		return inst[i];
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* WANDevice.1.                                                            */
/* ---------------------------------------------------------------------- */

static int get_wan_physical_link_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wan_eth_link_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wan_ipconn_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wan_pppconn_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wan_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wan_addressing_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wan_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wan_portmapping_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

DMOBJ tWANDeviceObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"WANCommonInterfaceConfig", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tWANCommonInterfaceConfigParam, NULL},
{"WANEthernetInterfaceConfig", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tWANEthernetInterfaceConfigObj, tWANEthernetInterfaceConfigParam, NULL},
{"WANConnectionDevice", &DMREAD, NULL, NULL, NULL, browseWANConnectionDeviceInst, NULL, &DMNONE, tWANConnectionDeviceObj, tWANConnectionDeviceParam, NULL},
{0}
};

static const struct bdk_leafmap wandevice_map[] = {
	{"WANConnectionNumberOfEntries", "1", BDK_MAP_CONST},
	{0}
};

DMLEAF tWANDeviceParam[] = {
{"WANConnectionNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

int browsewandeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

	(void)prev_data;
	(void)prev_instance;
	oc->aux[3] = find_upstream_eth();
	snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.Ethernet.Interface.%u.", oc->aux[3]);
	DM_LINK_INST_OBJ(dmctx, parent_node, oc, "1");
	return 0;
}

/* WANDevice.1.WANCommonInterfaceConfig. <- Device.Ethernet.Interface.{aux3}. */
static const struct bdk_leafmap wancommon_map[] = {
	{"EnabledForInternet",        "1",                    BDK_MAP_CONST},
	{"WANAccessType",             "Ethernet",             BDK_MAP_CONST},
	{"Layer1UpstreamMaxBitRate",  "Device.Ethernet.Interface.{aux3}.MaxBitRate", BDK_MAP_RO},
	{"Layer1DownstreamMaxBitRate","Device.Ethernet.Interface.{aux3}.MaxBitRate", BDK_MAP_RO},
	{"TotalBytesSent",            "Device.Ethernet.Interface.{aux3}.Stats.BytesSent",      BDK_MAP_RO},
	{"TotalBytesReceived",        "Device.Ethernet.Interface.{aux3}.Stats.BytesReceived",  BDK_MAP_RO},
	{"TotalPacketsSent",          "Device.Ethernet.Interface.{aux3}.Stats.PacketsSent",    BDK_MAP_RO},
	{"TotalPacketsReceived",      "Device.Ethernet.Interface.{aux3}.Stats.PacketsReceived",BDK_MAP_RO},
	{"MaximumActiveConnections",  "0",                    BDK_MAP_CONST},
	{"NumberOfActiveConnections", "0",                    BDK_MAP_CONST},
	{0}
};

DMLEAF tWANCommonInterfaceConfigParam[] = {
{"EnabledForInternet", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"WANAccessType", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Layer1UpstreamMaxBitRate", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"Layer1DownstreamMaxBitRate", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PhysicalLinkStatus", &DMREAD, DMT_STRING, get_wan_physical_link_status, NULL, NULL, NULL},
{"TotalBytesSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"TotalBytesReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"TotalPacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"TotalPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"MaximumActiveConnections", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"NumberOfActiveConnections", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

static int get_wan_physical_link_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[96], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.Ethernet.Interface.%u.Status", oc ? oc->aux[3] : 0);
	bdk_get_value_default(path, "Unavailable", &v);
	if (strcmp(v, "Up") == 0)
		*value = "Up";
	else if (strcmp(v, "Down") == 0 || strcmp(v, "LowerLayerDown") == 0)
		*value = "Down";
	else if (strcmp(v, "Dormant") == 0)
		*value = "Initializing";
	else
		*value = "Unavailable";
	return 0;
}

/* WANDevice.1.WANEthernetInterfaceConfig. <- Device.Ethernet.Interface.{aux3}. */
static const struct bdk_leafmap waneth_map[] = {
	{"Enable",     "Device.Ethernet.Interface.{aux3}.Enable",     BDK_MAP_RW | BDK_MAP_BOOL},
	{"MACAddress", "Device.Ethernet.Interface.{aux3}.MACAddress", BDK_MAP_RO},
	{"MaxBitRate", "Device.Ethernet.Interface.{aux3}.MaxBitRate", BDK_MAP_RO},
	{"DuplexMode", "Device.Ethernet.Interface.{aux3}.DuplexMode", BDK_MAP_RO},
	/* same names as cms-dm-tr98.xml (Broadcom Legacy98), -1 = no shaping */
	{"ShapingRate",      "Device.Ethernet.Interface.{aux3}.X_BROADCOM_COM_ShapingRate",      BDK_MAP_RW},
	{"ShapingBurstSize", "Device.Ethernet.Interface.{aux3}.X_BROADCOM_COM_ShapingBurstSize", BDK_MAP_RW},
	{CUSTOM_PREFIX"IfName", "Device.Ethernet.Interface.{aux3}.Name", BDK_MAP_RO},
	{0}
};

DMLEAF tWANEthernetInterfaceConfigParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_wan_physical_link_status, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MaxBitRate", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"DuplexMode", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ShapingRate", &DMWRITE, DMT_INT, bdk_map_get, bdk_map_set, NULL, NULL},
{"ShapingBurstSize", &DMWRITE, DMT_INT, bdk_map_get, bdk_map_set, NULL, NULL},
{CUSTOM_PREFIX"IfName", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* WANEthernetInterfaceConfig.Stats. <- Device.Ethernet.Interface.{aux3}.Stats. */
static const struct bdk_leafmap waneth_stats_map[] = {
	{"BytesSent",       "Device.Ethernet.Interface.{aux3}.Stats.BytesSent",       BDK_MAP_RO},
	{"BytesReceived",   "Device.Ethernet.Interface.{aux3}.Stats.BytesReceived",   BDK_MAP_RO},
	{"PacketsSent",     "Device.Ethernet.Interface.{aux3}.Stats.PacketsSent",     BDK_MAP_RO},
	{"PacketsReceived", "Device.Ethernet.Interface.{aux3}.Stats.PacketsReceived", BDK_MAP_RO},
	{0}
};

DMLEAF tWANEthernetInterfaceConfigStatsParam[] = {
{"BytesSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMOBJ tWANEthernetInterfaceConfigObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tWANEthernetInterfaceConfigStatsParam, NULL},
{0}
};

/* ---------------------------------------------------------------------- */
/* WANDevice.1.WANConnectionDevice.1.                                      */
/* ---------------------------------------------------------------------- */

DMOBJ tWANConnectionDeviceObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"WANEthernetLinkConfig", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tWANEthernetLinkConfigParam, NULL},
{"WANIPConnection", &DMREAD, NULL, NULL, NULL, browseWANIPConnectionInst, NULL, &DMNONE, tWANIPConnectionObj, tWANIPConnectionParam, NULL},
{"WANPPPConnection", &DMREAD, NULL, NULL, NULL, browseWANPPPConnectionInst, NULL, &DMNONE, tWANPPPConnectionObj, tWANPPPConnectionParam, NULL},
{0}
};

DMLEAF tWANConnectionDeviceParam[] = {
{"WANIPConnectionNumberOfEntries", &DMREAD, DMT_UNINT, get_wan_ipconn_count, NULL, NULL, NULL},
{"WANPPPConnectionNumberOfEntries", &DMREAD, DMT_UNINT, get_wan_pppconn_count, NULL, NULL, NULL},
{0}
};

/* WANConnectionDevice.1.WANEthernetLinkConfig.EthernetLinkStatus: Up/Down of
 * the upstream port (TR-181 keeps the link state on Ethernet.Interface) */
DMLEAF tWANEthernetLinkConfigParam[] = {
{"EthernetLinkStatus", &DMREAD, DMT_STRING, get_wan_eth_link_status, NULL, NULL, NULL},
{0}
};

static int get_wan_eth_link_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[96], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.Ethernet.Interface.%u.Status", oc ? oc->aux[3] : 0);
	bdk_get_value_default(path, "Down", &v);
	*value = (strcmp(v, "Up") == 0) ? "Up" : "Down";
	return 0;
}

int browseWANConnectionDeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_instance;
	DM_LINK_INST_OBJ(dmctx, parent_node, prev_data, "1");
	return 0;
}

/* an IP.Interface is a WAN connection when it is neither the LAN bridge nor
 * loopback.  (TR-181 has no Upstream flag on IP.Interface, the real test is
 * the LowerLayers chain down to Ethernet.Interface.Upstream, kept simple here) */
static int ip_interface_is_wan(unsigned int inst)
{
	char path[96], *v;

	snprintf(path, sizeof(path), "Device.IP.Interface.%u.Name", inst);
	bdk_get_value_default(path, "", &v);
	if (strcmp(v, BDK_LAN_BRIDGE_NAME) == 0 || strcmp(v, "lo") == 0 || strncmp(v, "br", 2) == 0)
		return 0;
	snprintf(path, sizeof(path), "Device.IP.Interface.%u.Type", inst);
	bdk_get_value_default(path, "Normal", &v);
	if (strcmp(v, "Loopback") == 0)
		return 0;
	return 1;
}

/* PPP.Interface instance under an IP.Interface (LowerLayers), 0 if the IP
 * interface does not sit on PPP */
static unsigned int ip_interface_ppp_inst(unsigned int inst)
{
	char path[96], *v;
	static const char pfx[] = "Device.PPP.Interface.";

	snprintf(path, sizeof(path), "Device.IP.Interface.%u.LowerLayers", inst);
	bdk_get_value_default(path, "", &v);
	if (strncmp(v, pfx, sizeof(pfx) - 1) != 0)
		return 0;
	return (unsigned int)strtoul(v + sizeof(pfx) - 1, NULL, 10);
}

/* 0 = not WAN, 1 = WANIPConnection, 2 = WANPPPConnection */
static int ip_interface_kind(unsigned int inst)
{
	if (!ip_interface_is_wan(inst))
		return 0;
	return ip_interface_ppp_inst(inst) ? 2 : 1;
}

static unsigned int count_wan_kind(int kind)
{
	unsigned int *inst = NULL, num = 0, i, n = 0;

	if (bdk_get_instances("Device.IP.Interface.", &inst, &num) == 0)
		for (i = 0; i < num; i++)
			if (ip_interface_kind(inst[i]) == kind)
				n++;
	return n;
}

static int get_wan_ipconn_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	dmasprintf(value, "%u", count_wan_kind(1));
	return 0;
}

static int get_wan_pppconn_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	dmasprintf(value, "%u", count_wan_kind(2));
	return 0;
}

/* ---------------------------------------------------------------------- */
/* PortMapping.{i}. <- Device.NAT.PortMapping.{i} bound to this IP.Interface */
/* (shared by WANIPConnection and WANPPPConnection)                        */
/* ---------------------------------------------------------------------- */

static const struct bdk_leafmap portmapping_map[] = {
	{"PortMappingEnabled",       "Enable",               BDK_MAP_RW | BDK_MAP_BOOL},
	{"PortMappingLeaseDuration", "LeaseDuration",        BDK_MAP_RW},
	{"RemoteHost",               "RemoteHost",           BDK_MAP_RW},
	{"ExternalPort",             "ExternalPort",         BDK_MAP_RW},
	{"ExternalPortEndRange",     "ExternalPortEndRange", BDK_MAP_RW},
	{"InternalPort",             "InternalPort",         BDK_MAP_RW},
	{"PortMappingProtocol",      "Protocol",             BDK_MAP_RW},
	{"InternalClient",           "InternalClient",       BDK_MAP_RW},
	{"PortMappingDescription",   "Description",          BDK_MAP_RW},
	{0}
};

DMLEAF tWANPortMappingParam[] = {
{"PortMappingEnabled", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"PortMappingLeaseDuration", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"RemoteHost", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"ExternalPort", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"ExternalPortEndRange", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"InternalPort", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"PortMappingProtocol", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"InternalClient", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"PortMappingDescription", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{0}
};

/* "Device.IP.Interface.2." (objctx base) -> "Device.IP.Interface.2" */
static void ipif_ref_of(const struct bdk_objctx *oc, char *ref, size_t len)
{
	size_t n;

	snprintf(ref, len, "%s", oc ? oc->tr181_base : "");
	n = strlen(ref);
	if (n && ref[n - 1] == '.')
		ref[n - 1] = '\0';
}

/* a NAT.PortMapping belongs to this connection when Interface points at its
 * IP.Interface, or AllInterfaces is set */
static int portmapping_on(unsigned int pm, const char *ref)
{
	char path[96], *v;

	snprintf(path, sizeof(path), "Device.NAT.PortMapping.%u.Interface", pm);
	bdk_get_value_default(path, "", &v);
	if (strcmp(v, ref) == 0)
		return 1;
	snprintf(path, sizeof(path), "Device.NAT.PortMapping.%u.AllInterfaces", pm);
	bdk_get_value_default(path, "0", &v);
	return is_true(v);
}

int browseWANPortMappingInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct bdk_objctx *parent = prev_data;
	unsigned int *inst = NULL, num = 0, i;
	char ref[64], *sinst;

	(void)prev_instance;
	if (!parent)
		return 0;
	ipif_ref_of(parent, ref, sizeof(ref));
	if (bdk_get_instances("Device.NAT.PortMapping.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc;

		if (!portmapping_on(inst[i], ref))
			continue;
		oc = dmcalloc(1, sizeof(*oc));
		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.NAT.PortMapping.%u.", inst[i]);
		memcpy(oc->aux, parent->aux, sizeof(oc->aux));
		oc->priv = parent;
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/* AddObject: create the NAT.PortMapping and bind it to this IP.Interface right
 * away (the ACS sets the other fields + Enable in a following SPV) */
int add_wan_portmapping(char *refparam, struct dmctx *ctx, void *data, char **instancepara)
{
	struct bdk_objctx *parent = data;
	unsigned int inst = 0;
	char path[128], ref[64];
	int fault;

	(void)refparam; (void)ctx;
	if (!parent)
		return FAULT_9005;
	fault = bdk_add_object("Device.NAT.PortMapping.", &inst);
	if (fault)
		return fault;
	ipif_ref_of(parent, ref, sizeof(ref));
	snprintf(path, sizeof(path), "Device.NAT.PortMapping.%u.Interface", inst);
	if (bdk_set_value_now(path, NULL, ref)) {
		snprintf(path, sizeof(path), "Device.NAT.PortMapping.%u.", inst);
		bdk_del_object(path);
		return FAULT_9002;
	}
	dmasprintf(instancepara, "%u", inst);
	return 0;
}

int delete_wan_portmapping(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	struct bdk_objctx *oc = data;
	unsigned int *inst = NULL, num = 0, i;
	char ref[64], path[96];
	int fault = 0;

	(void)refparam; (void)ctx; (void)instance;
	if (del_action == DEL_INST)
		return (oc && oc->tr181_base[0]) ? bdk_del_object(oc->tr181_base) : FAULT_9005;
	/* DEL_ALL: data is the connection's objctx */
	ipif_ref_of(oc, ref, sizeof(ref));
	if (bdk_get_instances("Device.NAT.PortMapping.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		if (!portmapping_on(inst[i], ref))
			continue;
		snprintf(path, sizeof(path), "Device.NAT.PortMapping.%u.", inst[i]);
		if (bdk_del_object(path) && !fault)
			fault = FAULT_9002;
	}
	return fault;
}

static int get_wan_portmapping_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	unsigned int *inst = NULL, num = 0, i, n = 0;
	char ref[64];

	(void)refparam; (void)ctx; (void)instance;
	ipif_ref_of(oc, ref, sizeof(ref));
	if (oc && bdk_get_instances("Device.NAT.PortMapping.", &inst, &num) == 0)
		for (i = 0; i < num; i++)
			if (portmapping_on(inst[i], ref))
				n++;
	dmasprintf(value, "%u", n);
	return 0;
}

/* WAN*Connection.{i}.Stats. <- Device.IP.Interface.{i}.Stats. */
static const struct bdk_leafmap wanconn_stats_map[] = {
	{"EthernetBytesSent",       "Stats.BytesSent",       BDK_MAP_RO},
	{"EthernetBytesReceived",   "Stats.BytesReceived",   BDK_MAP_RO},
	{"EthernetPacketsSent",     "Stats.PacketsSent",     BDK_MAP_RO},
	{"EthernetPacketsReceived", "Stats.PacketsReceived", BDK_MAP_RO},
	{0}
};

DMLEAF tWANConnectionStatsParam[] = {
{"EthernetBytesSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"EthernetBytesReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"EthernetPacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"EthernetPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMOBJ tWANIPConnectionObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"PortMapping", &DMWRITE, add_wan_portmapping, delete_wan_portmapping, NULL, browseWANPortMappingInst, NULL, &DMNONE, NULL, tWANPortMappingParam, NULL},
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tWANConnectionStatsParam, NULL},
{0}
};

DMOBJ tWANPPPConnectionObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"PortMapping", &DMWRITE, add_wan_portmapping, delete_wan_portmapping, NULL, browseWANPortMappingInst, NULL, &DMNONE, NULL, tWANPortMappingParam, NULL},
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tWANConnectionStatsParam, NULL},
{0}
};

/* WANIPConnection.{i}. <- Device.IP.Interface.{i}. (+ aux0 DHCPv4.Client,
 * aux1 NAT.InterfaceSetting, aux2 default IPv4Forwarding, aux3 upstream eth) */
static const struct bdk_leafmap wanip_map[] = {
	{"Enable",            "Enable",                     BDK_MAP_RW | BDK_MAP_BOOL},
	{"Name",              "Name",                       BDK_MAP_RO},
	{"Uptime",            "LastChange",                 BDK_MAP_RO},
	{"ExternalIPAddress", "IPv4Address.1.IPAddress",    BDK_MAP_RO},
	{"SubnetMask",        "IPv4Address.1.SubnetMask",   BDK_MAP_RO},
	{"DefaultGateway",    "Device.Routing.Router.1.IPv4Forwarding.{aux2}.GatewayIPAddress", BDK_MAP_RO},
	{"NATEnabled",        "Device.NAT.InterfaceSetting.{aux1}.Enable", BDK_MAP_RW | BDK_MAP_BOOL},
	{"MACAddress",        "Device.Ethernet.Interface.{aux3}.MACAddress", BDK_MAP_RO},
	{"ConnectionType",    "IP_Routed",                  BDK_MAP_CONST},
	{"PossibleConnectionTypes", "IP_Routed",            BDK_MAP_CONST},
	{"RSIPAvailable",     "0",                          BDK_MAP_CONST},
	{"LastConnectionError", "ERROR_NONE",               BDK_MAP_CONST},
	{"DNSEnabled",        "Device.DNS.Client.Enable",   BDK_MAP_RO | BDK_MAP_BOOL},
	{"DNSOverrideAllowed","0",                          BDK_MAP_CONST},
	{"MaxMTUSize",        "MaxMTUSize",                 BDK_MAP_RW},
	{"MACAddressOverride","0",                          BDK_MAP_CONST},
	{"RouteProtocolRx",   "Off",                        BDK_MAP_CONST},
	{CUSTOM_PREFIX"IfName", "Name",                   BDK_MAP_RO},
	{0}
};

DMLEAF tWANIPConnectionParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"ConnectionStatus", &DMREAD, DMT_STRING, get_wan_conn_status, NULL, NULL, NULL},
{"PossibleConnectionTypes", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ConnectionType", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Uptime", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"LastConnectionError", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"RSIPAvailable", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"NATEnabled", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"AddressingType", &DMREAD, DMT_STRING, get_wan_addressing_type, NULL, NULL, NULL},
{"ExternalIPAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, &DMACTIVE},
{"SubnetMask", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"DefaultGateway", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"DNSServers", &DMREAD, DMT_STRING, get_wan_dns_servers, NULL, NULL, NULL},
{"DNSEnabled", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"DNSOverrideAllowed", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"MaxMTUSize", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MACAddressOverride", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"RouteProtocolRx", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"PortMappingNumberOfEntries", &DMREAD, DMT_UNINT, get_wan_portmapping_count, NULL, NULL, NULL},
{CUSTOM_PREFIX"IfName", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* one WAN connection instance: kind 1 = IP (aux0 DHCPv4.Client), kind 2 = PPP
 * (aux0 PPP.Interface) */
static int browse_wan_connections(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, int kind)
{
	struct bdk_objctx *parent = prev_data;
	unsigned int *inst = NULL, num = 0, i;
	char ref[64], *sinst;

	if (bdk_get_instances("Device.IP.Interface.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc;

		if (ip_interface_kind(inst[i]) != kind)
			continue;
		oc = dmcalloc(1, sizeof(*oc));
		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.IP.Interface.%u.", inst[i]);
		snprintf(ref, sizeof(ref), "Device.IP.Interface.%u", inst[i]);
		if (kind == 2)
			oc->aux[0] = ip_interface_ppp_inst(inst[i]);
		else
			oc->aux[0] = find_by_interface_ref("Device.DHCPv4.Client.", ref, NULL, NULL);
		oc->aux[1] = find_by_interface_ref("Device.NAT.InterfaceSetting.", ref, NULL, NULL);
		oc->aux[2] = find_by_interface_ref("Device.Routing.Router.1.IPv4Forwarding.", ref, "DestIPAddress", "");
		if (oc->aux[2] == 0)
			oc->aux[2] = find_by_interface_ref("Device.Routing.Router.1.IPv4Forwarding.", ref, "DestIPAddress", "0.0.0.0");
		oc->aux[3] = parent ? parent->aux[3] : find_upstream_eth();
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

int browseWANIPConnectionInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_instance;
	return browse_wan_connections(dmctx, parent_node, prev_data, 1);
}

int browseWANPPPConnectionInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_instance;
	return browse_wan_connections(dmctx, parent_node, prev_data, 2);
}

/* ---------------------------------------------------------------------- */
/* WANPPPConnection.{i}. <- Device.IP.Interface.{i} on Device.PPP.Interface.{aux0} */
/* ---------------------------------------------------------------------- */

static int get_ppp_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

static const struct bdk_leafmap wanppp_map[] = {
	/* Device.PPP.Interface.{aux0}. — TR-181 enumerations of ConnectionStatus,
	 * LastConnectionError and ConnectionTrigger are the TR-098 ones */
	{"Enable",                 "Device.PPP.Interface.{aux0}.Enable",              BDK_MAP_RW | BDK_MAP_BOOL},
	{"LastConnectionError",    "Device.PPP.Interface.{aux0}.LastConnectionError", BDK_MAP_RO},
	{"IdleDisconnectTime",     "Device.PPP.Interface.{aux0}.IdleDisconnectTime",  BDK_MAP_RW},
	{"Username",               "Device.PPP.Interface.{aux0}.Username",            BDK_MAP_RW},
	{"Password",               "Device.PPP.Interface.{aux0}.Password",            BDK_MAP_RW | BDK_MAP_EMPTY},
	{"PPPEncryptionProtocol",  "Device.PPP.Interface.{aux0}.EncryptionProtocol",  BDK_MAP_RO},
	{"PPPCompressionProtocol", "Device.PPP.Interface.{aux0}.CompressionProtocol", BDK_MAP_RO},
	{"PPPAuthenticationProtocol", "Device.PPP.Interface.{aux0}.AuthenticationProtocol", BDK_MAP_RW},
	{"RemoteIPAddress",        "Device.PPP.Interface.{aux0}.IPCP.RemoteIPAddress", BDK_MAP_RO},
	{"MaxMRUSize",             "Device.PPP.Interface.{aux0}.MaxMRUSize",          BDK_MAP_RW},
	{"CurrentMRUSize",         "Device.PPP.Interface.{aux0}.CurrentMRUSize",      BDK_MAP_RO},
	{"ConnectionTrigger",      "Device.PPP.Interface.{aux0}.ConnectionTrigger",   BDK_MAP_RW},
	{"PPPLCPEcho",             "Device.PPP.Interface.{aux0}.LCPEcho",             BDK_MAP_RO},
	{"PPPLCPEchoRetry",        "Device.PPP.Interface.{aux0}.LCPEchoRetry",        BDK_MAP_RO},
	{"PPPoEACName",            "Device.PPP.Interface.{aux0}.PPPoE.ACName",        BDK_MAP_RW},
	{"PPPoEServiceName",       "Device.PPP.Interface.{aux0}.PPPoE.ServiceName",   BDK_MAP_RW},
	/* Device.IP.Interface.{i}. (objctx base) */
	{"Name",                   "Name",                       BDK_MAP_RO},
	{"Uptime",                 "LastChange",                 BDK_MAP_RO},
	{"ExternalIPAddress",      "IPv4Address.1.IPAddress",    BDK_MAP_RO},
	{"NATEnabled",             "Device.NAT.InterfaceSetting.{aux1}.Enable", BDK_MAP_RW | BDK_MAP_BOOL},
	{"MACAddress",             "Device.Ethernet.Interface.{aux3}.MACAddress", BDK_MAP_RO},
	{"DNSEnabled",             "Device.DNS.Client.Enable",   BDK_MAP_RO | BDK_MAP_BOOL},
	/* constants */
	{"ConnectionType",         "IP_Routed",                  BDK_MAP_CONST},
	{"PossibleConnectionTypes","IP_Routed",                  BDK_MAP_CONST},
	{"RSIPAvailable",          "0",                          BDK_MAP_CONST},
	{"DNSOverrideAllowed",     "0",                          BDK_MAP_CONST},
	{"MACAddressOverride",     "0",                          BDK_MAP_CONST},
	{"TransportType",          "PPPoE",                      BDK_MAP_CONST},
	{"RouteProtocolRx",        "Off",                        BDK_MAP_CONST},
	{CUSTOM_PREFIX"IfName",    "Name",                       BDK_MAP_RO},
	{0}
};

DMLEAF tWANPPPConnectionParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"ConnectionStatus", &DMREAD, DMT_STRING, get_ppp_conn_status, NULL, NULL, NULL},
{"PossibleConnectionTypes", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ConnectionType", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Uptime", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"LastConnectionError", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"IdleDisconnectTime", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"RSIPAvailable", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"NATEnabled", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Username", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"Password", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"PPPEncryptionProtocol", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"PPPCompressionProtocol", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"PPPAuthenticationProtocol", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"ExternalIPAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, &DMACTIVE},
{"RemoteIPAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MaxMRUSize", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"CurrentMRUSize", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"DNSEnabled", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"DNSOverrideAllowed", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"DNSServers", &DMREAD, DMT_STRING, get_wan_dns_servers, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MACAddressOverride", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"TransportType", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"PPPoEACName", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"PPPoEServiceName", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"ConnectionTrigger", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"RouteProtocolRx", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"PPPLCPEcho", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PPPLCPEchoRetry", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PortMappingNumberOfEntries", &DMREAD, DMT_UNINT, get_wan_portmapping_count, NULL, NULL, NULL},
{CUSTOM_PREFIX"IfName", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* PPP.Interface.ConnectionStatus already uses the TR-098 words; fall back to
 * the IP.Interface derived status when the MDM leaves it empty */
static int get_ppp_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[96], *v;

	snprintf(path, sizeof(path), "Device.PPP.Interface.%u.ConnectionStatus", oc ? oc->aux[0] : 0);
	bdk_get_value_default(path, "", &v);
	if (v[0]) {
		*value = v;
		return 0;
	}
	return get_wan_conn_status(refparam, ctx, data, instance, value);
}

/* TR-181 IP.Interface.Status -> TR-098 ConnectionStatus */
static int get_wan_conn_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[160], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "%sStatus", oc ? oc->tr181_base : "");
	bdk_get_value_default(path, "Unconfigured", &v);
	if (strcmp(v, "Up") == 0) {
		/* Up but no address yet = still connecting */
		snprintf(path, sizeof(path), "%sIPv4Address.1.IPAddress", oc ? oc->tr181_base : "");
		bdk_get_value_default(path, "", &v);
		*value = (v[0] && strcmp(v, "0.0.0.0") != 0) ? "Connected" : "Connecting";
	} else if (strcmp(v, "Down") == 0 || strcmp(v, "LowerLayerDown") == 0)
		*value = "Disconnected";
	else if (strcmp(v, "Dormant") == 0)
		*value = "Connecting";
	else
		*value = "Unconfigured";
	return 0;
}

static int get_wan_addressing_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[96], *v;

	(void)refparam; (void)ctx; (void)instance;
	if (oc && oc->aux[0]) {
		snprintf(path, sizeof(path), "Device.DHCPv4.Client.%u.Enable", oc->aux[0]);
		bdk_get_value_default(path, "0", &v);
		if (is_true(v)) {
			*value = "DHCP";
			return 0;
		}
	}
	*value = "Static";
	return 0;
}

/* comma separated list of Device.DNS.Client.Server.{i}.DNSServer bound to this
 * IP interface (or unbound ones) */
static int get_wan_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	unsigned int *inst = NULL, num = 0, i;
	char path[128], ref[64], *v, buf[256] = "";
	size_t o = 0;

	(void)refparam; (void)ctx; (void)instance;
	*value = "";
	if (!oc)
		return 0;
	/* "Device.IP.Interface.2." -> "Device.IP.Interface.2" */
	snprintf(ref, sizeof(ref), "%s", oc->tr181_base);
	if (strlen(ref) && ref[strlen(ref) - 1] == '.')
		ref[strlen(ref) - 1] = '\0';

	if (bdk_get_instances("Device.DNS.Client.Server.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), "Device.DNS.Client.Server.%u.Interface", inst[i]);
		bdk_get_value_default(path, "", &v);
		if (v[0] && strcmp(v, ref) != 0)
			continue;
		snprintf(path, sizeof(path), "Device.DNS.Client.Server.%u.DNSServer", inst[i]);
		bdk_get_value_default(path, "", &v);
		if (!v[0])
			continue;
		if (o + strlen(v) + 2 >= sizeof(buf))
			break;
		o += (size_t)snprintf(buf + o, sizeof(buf) - o, "%s%s", o ? "," : "", v);
	}
	*value = dmstrdup(buf);
	return 0;
}

/* ---------------------------------------------------------------------- */

void wandevice_bdk_register(void)
{
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.", wandevice_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANCommonInterfaceConfig.", wancommon_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANEthernetInterfaceConfig.", waneth_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANEthernetInterfaceConfig.Stats.", waneth_stats_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANIPConnection.{i}.", wanip_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANIPConnection.{i}.PortMapping.{i}.", portmapping_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANIPConnection.{i}.Stats.", wanconn_stats_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANPPPConnection.{i}.", wanppp_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANPPPConnection.{i}.PortMapping.{i}.", portmapping_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.WANDevice.{i}.WANConnectionDevice.{i}.WANPPPConnection.{i}.Stats.", wanconn_stats_map, NULL);
}
