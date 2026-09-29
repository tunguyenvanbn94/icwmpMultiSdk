/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice.1. on Broadcom BDK.
 *
 *	TR-098 LANDevice is a semantic join of several TR-181 objects:
 *
 *	  LANHostConfigManagement.        <- Device.DHCPv4.Server.Pool.1.
 *	  LANHostConfigManagement.DHCPStaticAddress.{i}. <- Pool.1.StaticAddress.{i} (Add/Delete)
 *	  LANHostConfigManagement.DHCPOption.{i}.        <- Pool.1.Option.{i}        (Add/Delete)
 *	  LANHostConfigManagement.IPInterface.1. <- Device.IP.Interface.1. (br0)
 *	  LANEthernetInterfaceConfig.{i}. <- Device.Ethernet.Interface.{i} with Upstream=0
 *	  LANEthernetInterfaceConfig.{i}.Stats. <- Device.Ethernet.Interface.{i}.Stats.
 *	  WLANConfiguration.{i}.          <- Device.WiFi.SSID.{i}
 *	                                     + Device.WiFi.Radio.{aux0}   (LowerLayers)
 *	                                     + Device.WiFi.AccessPoint.{aux1} (SSIDReference)
 *	  WLANConfiguration.{i}.WEPKey.{1..4}  <- AccessPoint.{aux1}.Security.X_BROADCOM_COM_WlKey1..4
 *	  WLANConfiguration.{i}.WPS.      <- AccessPoint.{aux1}.WPS. + WiFi.X_BROADCOM_COM_WpsCfg.
 *	  WLANConfiguration.{i}.Stats.    <- Device.WiFi.SSID.{i}.Stats.
 *	  Hosts.Host.{i}.                 <- Device.Hosts.Host.{i}
 *
 *	The TR-098 instance number of every multi-instance object is the TR-181
 *	instance number of the primary object, so instances stay stable across
 *	reboots as long as the MDM keeps them stable (it does: SSID/AccessPoint
 *	instances are created from the wlan template at first boot).
 *
 *	Only the parameters an ACS commonly provisions are mapped.  Adding one
 *	more is one line in a bdk_leafmap table + one DMLEAF line.  Anything that
 *	needs logic (BeaconType, Standard, ConnectionStatus...) is a small hand
 *	written getter/setter next to the table.
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
#include "dmbdk.h"

/* ---------------------------------------------------------------------- */
/* LANDevice.1.                                                            */
/* ---------------------------------------------------------------------- */

static int get_lan_eth_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_lan_wlan_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_lan_usb_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

DMOBJ tLANDeviceObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"LANHostConfigManagement", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tLANHostConfigManagementObj, tLANHostConfigManagementParam, NULL},
{"LANEthernetInterfaceConfig", &DMREAD, NULL, NULL, NULL, browseLANEthernetInterfaceConfigInst, NULL, &DMNONE, tLANEthernetInterfaceConfigObj, tLANEthernetInterfaceConfigParam, NULL},
{"WLANConfiguration", &DMREAD, NULL, NULL, NULL, browseWLANConfigurationInst, NULL, &DMNONE, tWLANConfigurationObj, tWLANConfigurationParam, NULL},
{"Hosts", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, tHostsObj, tHostsParam, NULL},
{0}
};

DMLEAF tLANDeviceParam[] = {
{"LANEthernetInterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_lan_eth_count, NULL, NULL, NULL},
{"LANUSBInterfaceNumberOfEntries", &DMREAD, DMT_UNINT, get_lan_usb_count, NULL, NULL, NULL},
{"LANWLANConfigurationNumberOfEntries", &DMREAD, DMT_UNINT, get_lan_wlan_count, NULL, NULL, NULL},
{0}
};

/* exactly one LANDevice on this platform (one LAN bridge br0) */
int browselandeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_data;
	(void)prev_instance;
	DM_LINK_INST_OBJ(dmctx, parent_node, NULL, "1");
	return 0;
}

static unsigned int count_eth_lan_ports(void)
{
	unsigned int *inst = NULL, num = 0, i, n = 0;
	char path[96], *v;

	if (bdk_get_instances("Device.Ethernet.Interface.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), "Device.Ethernet.Interface.%u.Upstream", inst[i]);
		bdk_get_value_default(path, "0", &v);
		if (strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0)
			n++;
	}
	return n;
}

static int get_lan_eth_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	dmasprintf(value, "%u", count_eth_lan_ports());
	return 0;
}

static int get_lan_wlan_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	bdk_get_value_default("Device.WiFi.SSIDNumberOfEntries", "0", value);
	return 0;
}

/* no USB LAN interface in the TR-181 model of this SDK */
static int get_lan_usb_count(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = "0";
	return 0;
}

/* ---------------------------------------------------------------------- */
/* LANDevice.1.LANHostConfigManagement.  <- Device.DHCPv4.Server.Pool.1.    */
/* ---------------------------------------------------------------------- */

static const struct bdk_leafmap lanhostcfg_map[] = {
	{"DHCPServerConfigurable", "1",          BDK_MAP_CONST},
	{"DHCPServerEnable",       "Enable",     BDK_MAP_RW | BDK_MAP_BOOL},
	{"DHCPRelay",              "0",          BDK_MAP_CONST},
	{"MinAddress",             "MinAddress", BDK_MAP_RW},
	{"MaxAddress",             "MaxAddress", BDK_MAP_RW},
	{"ReservedAddresses",      "",           BDK_MAP_CONST},
	{"SubnetMask",             "SubnetMask", BDK_MAP_RW},
	{"DomainName",             "DomainName", BDK_MAP_RW},
	{"IPRouters",              "IPRouters",  BDK_MAP_RW},
	{"DHCPLeaseTime",          "LeaseTime",  BDK_MAP_RW},
	{"IPInterfaceNumberOfEntries", "1",      BDK_MAP_CONST},
	{"DHCPStaticAddressNumberOfEntries", "StaticAddressNumberOfEntries", BDK_MAP_RO},
	{"DHCPOptionNumberOfEntries",        "OptionNumberOfEntries",        BDK_MAP_RO},
	{0}
};

/* DNS servers handed to the DHCP clients = Pool.1.DNSServers.  The SDK
 * stores "0.0.0.0,0.0.0.0" (default) when dnsmasq should offer the router
 * itself (LAN IP, DNS proxy to the WAN servers); TR-098 wants the address the
 * clients really get, so report IPRouters in that case.  The WAN side DNS
 * list is WANIPConnection.DNSServers, not this parameter. */
static int lan_dns_is_unset(const char *v)
{
	const char *p = v;

	if (!v || !*v)
		return 1;
	while (*p) {
		if (strncmp(p, "0.0.0.0", 7) != 0)
			return 0;
		p += 7;
		if (*p == ',')
			p++;
		else if (*p)
			return 0;
	}
	return 1;
}

static int get_lan_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	char *v;

	(void)refparam; (void)ctx; (void)data; (void)instance;
	bdk_get_value_default(BDK_LAN_DHCP_POOL "DNSServers", "", &v);
	if (lan_dns_is_unset(v))
		bdk_get_value_default(BDK_LAN_DHCP_POOL "IPRouters", "", &v);
	*value = v;
	return 0;
}

static int set_lan_dns_servers(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	(void)instance; (void)data;
	switch (action) {
	case VALUECHECK:
		return bdk_check_writable(BDK_LAN_DHCP_POOL "DNSServers");
	case VALUESET:
		return bdk_queue_set(ctx, refparam, BDK_LAN_DHCP_POOL "DNSServers", value);
	}
	return 0;
}

/* DHCPStaticAddress.{i}. <- Pool.1.StaticAddress.{i}.  and
 * DHCPOption.{i}.        <- Pool.1.Option.{i}.
 * TR-098 instance number = TR-181 instance number; AddObject/DeleteObject go
 * straight to the MDM (bdk_add_object/bdk_del_object), the config is saved
 * at the end of the session. */
static const struct bdk_leafmap dhcpstatic_map[] = {
	{"Enable", "Enable", BDK_MAP_RW | BDK_MAP_BOOL},
	{"Chaddr", "Chaddr", BDK_MAP_RW},
	{"Yiaddr", "Yiaddr", BDK_MAP_RW},
	{0}
};

static const struct bdk_leafmap dhcpoption_map[] = {
	{"Enable", "Enable", BDK_MAP_RW | BDK_MAP_BOOL},
	{"Tag",    "Tag",    BDK_MAP_RW},
	{"Value",  "Value",  BDK_MAP_RW},
	{0}
};

DMLEAF tDHCPStaticAddressParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Chaddr", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"Yiaddr", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{0}
};

DMLEAF tDHCPOptionParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Tag", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"Value", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{0}
};

/* instances of one multi-instance child of the LAN pool */
static int browse_pool_child(struct dmctx *dmctx, DMNODE *parent_node, const char *child)
{
	unsigned int *inst = NULL, num = 0, i;
	char path[128], *sinst;

	snprintf(path, sizeof(path), "%s%s", BDK_LAN_DHCP_POOL, child);
	if (bdk_get_instances(path, &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "%s%u.", path, inst[i]);
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

static int add_pool_child(const char *child, char **instancepara)
{
	char path[128];
	unsigned int inst = 0;
	int fault;

	snprintf(path, sizeof(path), "%s%s", BDK_LAN_DHCP_POOL, child);
	fault = bdk_add_object(path, &inst);
	if (fault)
		return fault;
	dmasprintf(instancepara, "%u", inst);
	return 0;
}

static int del_pool_child(const char *child, void *data, unsigned char del_action)
{
	struct bdk_objctx *oc = data;
	unsigned int *inst = NULL, num = 0, i;
	char path[128];
	int fault = 0;

	if (del_action == DEL_INST)
		return (oc && oc->tr181_base[0]) ? bdk_del_object(oc->tr181_base) : FAULT_9005;
	/* DEL_ALL: DeleteObject on the table itself */
	snprintf(path, sizeof(path), "%s%s", BDK_LAN_DHCP_POOL, child);
	if (bdk_get_instances(path, &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		char ipath[160];

		snprintf(ipath, sizeof(ipath), "%s%u.", path, inst[i]);
		if (bdk_del_object(ipath) && !fault)
			fault = FAULT_9002;
	}
	return fault;
}

int browseDHCPStaticAddressInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_data; (void)prev_instance;
	return browse_pool_child(dmctx, parent_node, "StaticAddress.");
}

int browseDHCPOptionInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_data; (void)prev_instance;
	return browse_pool_child(dmctx, parent_node, "Option.");
}

int add_dhcp_static_address(char *refparam, struct dmctx *ctx, void *data, char **instancepara)
{
	(void)refparam; (void)ctx; (void)data;
	return add_pool_child("StaticAddress.", instancepara);
}

int delete_dhcp_static_address(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	(void)refparam; (void)ctx; (void)instance;
	return del_pool_child("StaticAddress.", data, del_action);
}

int add_dhcp_option(char *refparam, struct dmctx *ctx, void *data, char **instancepara)
{
	(void)refparam; (void)ctx; (void)data;
	return add_pool_child("Option.", instancepara);
}

int delete_dhcp_option(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action)
{
	(void)refparam; (void)ctx; (void)instance;
	return del_pool_child("Option.", data, del_action);
}

DMOBJ tLANHostConfigManagementObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"IPInterface", &DMREAD, NULL, NULL, NULL, browseIPInterfaceInst, NULL, &DMNONE, NULL, tIPInterfaceParam, NULL},
{"DHCPStaticAddress", &DMWRITE, add_dhcp_static_address, delete_dhcp_static_address, NULL, browseDHCPStaticAddressInst, NULL, &DMNONE, NULL, tDHCPStaticAddressParam, NULL},
{"DHCPOption", &DMWRITE, add_dhcp_option, delete_dhcp_option, NULL, browseDHCPOptionInst, NULL, &DMNONE, NULL, tDHCPOptionParam, NULL},
{0}
};

DMLEAF tLANHostConfigManagementParam[] = {
{"DHCPServerConfigurable", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"DHCPServerEnable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"DHCPRelay", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"MinAddress", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"MaxAddress", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"ReservedAddresses", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"SubnetMask", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"DNSServers", &DMWRITE, DMT_STRING, get_lan_dns_servers, set_lan_dns_servers, NULL, NULL},
{"DomainName", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"IPRouters", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"DHCPLeaseTime", &DMWRITE, DMT_INT, bdk_map_get, bdk_map_set, NULL, NULL},
{"IPInterfaceNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"DHCPStaticAddressNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"DHCPOptionNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* LANHostConfigManagement.IPInterface.1. <- Device.IP.Interface.1. (br0) */
static const struct bdk_leafmap ipinterface_map[] = {
	{"Enable",                   "Enable",                    BDK_MAP_RW | BDK_MAP_BOOL},
	{"IPInterfaceIPAddress",     "IPv4Address.1.IPAddress",   BDK_MAP_RW},
	{"IPInterfaceSubnetMask",    "IPv4Address.1.SubnetMask",  BDK_MAP_RW},
	{"IPInterfaceAddressingType","Static",                    BDK_MAP_CONST},
	{0}
};

DMLEAF tIPInterfaceParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"IPInterfaceIPAddress", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"IPInterfaceSubnetMask", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"IPInterfaceAddressingType", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

int browseIPInterfaceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_data;
	(void)prev_instance;
	DM_LINK_INST_OBJ(dmctx, parent_node, NULL, "1");
	return 0;
}

/* ---------------------------------------------------------------------- */
/* LANDevice.1.LANEthernetInterfaceConfig.{i}. <- Device.Ethernet.Interface */
/* ---------------------------------------------------------------------- */

static int get_eth_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

static const struct bdk_leafmap laneth_map[] = {
	{"Enable",     "Enable",     BDK_MAP_RW | BDK_MAP_BOOL},
	{"MACAddress", "MACAddress", BDK_MAP_RO},
	{"Name",       "Name",       BDK_MAP_RO},
	{"MaxBitRate", "MaxBitRate", BDK_MAP_RO},
	{"DuplexMode", "DuplexMode", BDK_MAP_RO},
	{"MACAddressControlEnabled", "0", BDK_MAP_CONST},
	{0}
};

DMLEAF tLANEthernetInterfaceConfigParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_eth_status, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MaxBitRate", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"DuplexMode", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MACAddressControlEnabled", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* LANEthernetInterfaceConfig.{i}.Stats. <- Device.Ethernet.Interface.{i}.Stats.
 * (static child object: libtr098 hands it the bdk_objctx of the instance) */
static const struct bdk_leafmap laneth_stats_map[] = {
	{"BytesSent",       "Stats.BytesSent",       BDK_MAP_RO},
	{"BytesReceived",   "Stats.BytesReceived",   BDK_MAP_RO},
	{"PacketsSent",     "Stats.PacketsSent",     BDK_MAP_RO},
	{"PacketsReceived", "Stats.PacketsReceived", BDK_MAP_RO},
	{0}
};

DMLEAF tLANEthernetInterfaceConfigStatsParam[] = {
{"BytesSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"BytesReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMOBJ tLANEthernetInterfaceConfigObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tLANEthernetInterfaceConfigStatsParam, NULL},
{0}
};

/* TR-181 Status "Up/Down/Dormant/NotPresent/LowerLayerDown/Error" ->
 * TR-098 "Up/NoLink/Error/Disabled" */
static int get_eth_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[160], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "%sStatus", oc ? oc->tr181_base : "");
	bdk_get_value_default(path, "Error", &v);
	if (strcmp(v, "Up") == 0)
		*value = "Up";
	else if (strcmp(v, "Down") == 0 || strcmp(v, "LowerLayerDown") == 0)
		*value = "NoLink";
	else if (strcmp(v, "NotPresent") == 0 || strcmp(v, "Dormant") == 0)
		*value = "Disabled";
	else
		*value = "Error";
	return 0;
}

int browseLANEthernetInterfaceConfigInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	unsigned int *inst = NULL, num = 0, i;
	char path[96], *v, *sinst;

	(void)prev_data;
	(void)prev_instance;
	if (bdk_get_instances("Device.Ethernet.Interface.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc;

		snprintf(path, sizeof(path), "Device.Ethernet.Interface.%u.Upstream", inst[i]);
		bdk_get_value_default(path, "0", &v);
		if (!(strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0))
			continue;  /* WAN port belongs to WANDevice */

		oc = dmcalloc(1, sizeof(*oc));
		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.Ethernet.Interface.%u.", inst[i]);
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* LANDevice.1.WLANConfiguration.{i}.                                      */
/*   <- Device.WiFi.SSID.{i} + Radio.{aux0} + AccessPoint.{aux1}           */
/* ---------------------------------------------------------------------- */

static int get_wlan_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wlan_standard(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wlan_beacon_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int set_wlan_beacon_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action);
static int get_wlan_band(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wlan_wpa_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int set_wlan_wpa_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action);
static int get_wlan_wpa_auth(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wlan_basic_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wlan_macfilter(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int set_wlan_macfilter(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action);
static int get_wlan_wepkey(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int set_wlan_wepkey(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action);
static int get_wlan_wps_config_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);
static int get_wlan_assoc_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

static const struct bdk_leafmap wlan_map[] = {
	/* Device.WiFi.SSID.{i}. */
	{"Enable",                  "Enable",   BDK_MAP_RW | BDK_MAP_BOOL},
	{"BSSID",                   "BSSID",    BDK_MAP_RO},
	{"SSID",                    "SSID",     BDK_MAP_RW},
	{"Name",                    "Name",     BDK_MAP_RO},
	{"MACAddress",              "MACAddress", BDK_MAP_RO},
	/* Device.WiFi.Radio.{aux0}. */
	{"RadioEnabled",            "Device.WiFi.Radio.{aux0}.Enable",            BDK_MAP_RW | BDK_MAP_BOOL},
	{"Channel",                 "Device.WiFi.Radio.{aux0}.Channel",           BDK_MAP_RW},
	{"AutoChannelEnable",       "Device.WiFi.Radio.{aux0}.AutoChannelEnable", BDK_MAP_RW | BDK_MAP_BOOL},
	{"PossibleChannels",        "Device.WiFi.Radio.{aux0}.PossibleChannels",  BDK_MAP_RO},
	{"RegulatoryDomain",        "Device.WiFi.Radio.{aux0}.RegulatoryDomain",  BDK_MAP_RW},
	{"TransmitPower",           "Device.WiFi.Radio.{aux0}.TransmitPower",     BDK_MAP_RW},
	{CUSTOM_PREFIX"OperatingChannelBandwidth", "Device.WiFi.Radio.{aux0}.OperatingChannelBandwidth", BDK_MAP_RW},
	/* Device.WiFi.AccessPoint.{aux1}. */
	{"SSIDAdvertisementEnabled","Device.WiFi.AccessPoint.{aux1}.SSIDAdvertisementEnabled", BDK_MAP_RW | BDK_MAP_BOOL},
	{"WMMEnable",               "Device.WiFi.AccessPoint.{aux1}.WMMEnable",   BDK_MAP_RW | BDK_MAP_BOOL},
	{"TotalAssociations",       "Device.WiFi.AccessPoint.{aux1}.AssociatedDeviceNumberOfEntries", BDK_MAP_RO},
	{"KeyPassphrase",           "Device.WiFi.AccessPoint.{aux1}.Security.KeyPassphrase", BDK_MAP_RW | BDK_MAP_EMPTY},
	{"WEPKeyIndex",             "Device.WiFi.AccessPoint.{aux1}.Security.X_BROADCOM_COM_WlKeyIndex", BDK_MAP_RW},
	{CUSTOM_PREFIX"SecurityModeEnabled", "Device.WiFi.AccessPoint.{aux1}.Security.ModeEnabled", BDK_MAP_RW},
	{CUSTOM_PREFIX"SecurityModesSupported", "Device.WiFi.AccessPoint.{aux1}.Security.ModesSupported", BDK_MAP_RO},
	/* Device.WiFi.Radio.{aux0}. rates (part 3) */
	{"MaxBitRate",              "Device.WiFi.Radio.{aux0}.MaxBitRate",                   BDK_MAP_RO},
	{"BasicDataTransmitRates",  "Device.WiFi.Radio.{aux0}.BasicDataTransmitRates",       BDK_MAP_RW},
	{"OperationalDataTransmitRates", "Device.WiFi.Radio.{aux0}.OperationalDataTransmitRates", BDK_MAP_RW},
	{"PossibleDataTransmitRates","Device.WiFi.Radio.{aux0}.SupportedDataTransmitRates",  BDK_MAP_RO},
	{"ChannelsInUse",           "Device.WiFi.Radio.{aux0}.ChannelsInUse",                BDK_MAP_RO},
	/* Device.WiFi.SSID.{i}.Stats. totals (part 3) */
	{"TotalBytesSent",          "Stats.BytesSent",       BDK_MAP_RO},
	{"TotalBytesReceived",      "Stats.BytesReceived",   BDK_MAP_RO},
	{"TotalPacketsSent",        "Stats.PacketsSent",     BDK_MAP_RO},
	{"TotalPacketsReceived",    "Stats.PacketsReceived", BDK_MAP_RO},
	/* constants / not modelled in TR-181 */
	{"BasicAuthenticationMode", "None",               BDK_MAP_CONST},
	{"WEPEncryptionLevel",      "Disabled,40-bit,104-bit", BDK_MAP_CONST},
	{"InsecureOOBAccessEnabled","0",                  BDK_MAP_CONST},
	{"BeaconAdvertisementEnabled","1",                BDK_MAP_CONST},
	{"AutoRateFallBackEnabled", "1",                  BDK_MAP_CONST},
	{"LocationDescription",     "",                   BDK_MAP_CONST},
	{"TotalIntegrityFailures",  "0",                  BDK_MAP_CONST},
	{"DistanceFromRoot",        "0",                  BDK_MAP_CONST},
	{"PeerBSSID",               "",                   BDK_MAP_CONST},
	{"AuthenticationServiceMode","None",              BDK_MAP_CONST},
	{"DeviceOperationMode",     "InfrastructureAccessPoint", BDK_MAP_CONST},
	{"TotalPSKFailures",        "0",                  BDK_MAP_CONST},
	{0}
};

DMOBJ tWLANConfigurationObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"WEPKey", &DMREAD, NULL, NULL, NULL, browseWLANWEPKeyInst, NULL, &DMNONE, NULL, tWLANWEPKeyParam, NULL},
{"PreSharedKey", &DMREAD, NULL, NULL, NULL, browseWLANPreSharedKeyInst, NULL, &DMNONE, NULL, tWLANPreSharedKeyParam, NULL},
{"AssociatedDevice", &DMREAD, NULL, NULL, NULL, browseWLANAssociatedDeviceInst, NULL, &DMNONE, NULL, tWLANAssociatedDeviceParam, NULL},
{"WPS", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tWLANWPSParam, NULL},
{"Stats", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tWLANStatsParam, NULL},
{0}
};

DMLEAF tWLANConfigurationParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Status", &DMREAD, DMT_STRING, get_wlan_status, NULL, NULL, NULL},
{"BSSID", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Name", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"SSID", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"RadioEnabled", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"Channel", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"AutoChannelEnable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"PossibleChannels", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"RegulatoryDomain", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"TransmitPower", &DMWRITE, DMT_INT, bdk_map_get, bdk_map_set, NULL, NULL},
{"Standard", &DMREAD, DMT_STRING, get_wlan_standard, NULL, NULL, NULL},
{"BeaconType", &DMWRITE, DMT_STRING, get_wlan_beacon_type, set_wlan_beacon_type, NULL, NULL},
{"SSIDAdvertisementEnabled", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"WMMEnable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"TotalAssociations", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"KeyPassphrase", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"WPAEncryptionModes", &DMWRITE, DMT_STRING, get_wlan_wpa_encryption, set_wlan_wpa_encryption, NULL, NULL},
{"IEEE11iEncryptionModes", &DMWRITE, DMT_STRING, get_wlan_wpa_encryption, set_wlan_wpa_encryption, NULL, NULL},
{"WPAAuthenticationMode", &DMREAD, DMT_STRING, get_wlan_wpa_auth, NULL, NULL, NULL},
{"IEEE11iAuthenticationMode", &DMREAD, DMT_STRING, get_wlan_wpa_auth, NULL, NULL, NULL},
{"BasicAuthenticationMode", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"BasicEncryptionModes", &DMREAD, DMT_STRING, get_wlan_basic_encryption, NULL, NULL, NULL},
{"WEPKeyIndex", &DMWRITE, DMT_UNINT, bdk_map_get, bdk_map_set, NULL, NULL},
{"WEPEncryptionLevel", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MaxBitRate", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"MACAddressControlEnabled", &DMWRITE, DMT_BOOL, get_wlan_macfilter, set_wlan_macfilter, NULL, NULL},
{"BasicDataTransmitRates", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"OperationalDataTransmitRates", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"PossibleDataTransmitRates", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ChannelsInUse", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"InsecureOOBAccessEnabled", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"BeaconAdvertisementEnabled", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"AutoRateFallBackEnabled", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"LocationDescription", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"TotalIntegrityFailures", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"DistanceFromRoot", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"PeerBSSID", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"AuthenticationServiceMode", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"TotalBytesSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"TotalBytesReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"TotalPacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"TotalPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"DeviceOperationMode", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"TotalPSKFailures", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{CUSTOM_PREFIX"Band", &DMREAD, DMT_STRING, get_wlan_band, NULL, NULL, NULL},
{CUSTOM_PREFIX"OperatingChannelBandwidth", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{CUSTOM_PREFIX"SecurityModeEnabled", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{CUSTOM_PREFIX"SecurityModesSupported", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* Parse the instance number out of a TR-181 reference like
 * "Device.WiFi.Radio.2" (LowerLayers / SSIDReference values). */
static unsigned int ref_instance(const char *ref, const char *prefix)
{
	size_t l = strlen(prefix);

	if (!ref || strncmp(ref, prefix, l) != 0)
		return 0;
	return (unsigned int)strtoul(ref + l, NULL, 10);
}

/* Find the AccessPoint whose SSIDReference points at SSID.ssid_inst.  On
 * Broadcom the AccessPoint instance normally equals the SSID instance, so
 * check that first and only scan when it does not hold. */
static unsigned int find_accesspoint_for_ssid(unsigned int ssid_inst)
{
	char path[96], want[64], *v;
	unsigned int *inst = NULL, num = 0, i;

	snprintf(want, sizeof(want), "Device.WiFi.SSID.%u", ssid_inst);
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.SSIDReference", ssid_inst);
	bdk_get_value_default(path, "", &v);
	if (strcmp(v, want) == 0)
		return ssid_inst;

	if (bdk_get_instances("Device.WiFi.AccessPoint.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.SSIDReference", inst[i]);
		bdk_get_value_default(path, "", &v);
		if (strcmp(v, want) == 0)
			return inst[i];
	}
	return 0;
}

int browseWLANConfigurationInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	unsigned int *inst = NULL, num = 0, i;
	char path[96], *v, *sinst;

	(void)prev_data;
	(void)prev_instance;
	if (bdk_get_instances("Device.WiFi.SSID.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.WiFi.SSID.%u.", inst[i]);
		snprintf(path, sizeof(path), "Device.WiFi.SSID.%u.LowerLayers", inst[i]);
		bdk_get_value_default(path, "", &v);
		oc->aux[0] = ref_instance(v, "Device.WiFi.Radio.");
		oc->aux[1] = find_accesspoint_for_ssid(inst[i]);
		oc->aux[2] = inst[i];
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/* TR-181 SSID.Status Up/Down -> TR-098 Up/Disabled/Error */
static int get_wlan_status(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[160], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "%sStatus", oc ? oc->tr181_base : "");
	bdk_get_value_default(path, "Error", &v);
	if (strcmp(v, "Up") == 0)
		*value = "Up";
	else if (strcmp(v, "Down") == 0 || strcmp(v, "Dormant") == 0 || strcmp(v, "LowerLayerDown") == 0)
		*value = "Disabled";
	else
		*value = "Error";
	return 0;
}

/* TR-181 Radio.OperatingStandards "b,g,n,ax" -> TR-098 Standard: the newest
 * one, TR-098 enumerates a/b/g/n only, ac/ax are reported as "n" plus the
 * exact list in CUSTOM_PREFIX"OperatingStandards" if the ACS ever needs it. */
static int get_wlan_standard(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[96], *v, *last;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.Radio.%u.OperatingStandards", oc ? oc->aux[0] : 0);
	bdk_get_value_default(path, "", &v);
	last = strrchr(v, ',');
	last = last ? last + 1 : v;
	if (strcmp(last, "ac") == 0 || strcmp(last, "ax") == 0 || strcmp(last, "be") == 0)
		*value = "n";
	else
		*value = dmstrdup(last);
	return 0;
}

static int get_wlan_band(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[96];

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.Radio.%u.OperatingFrequencyBand", oc ? oc->aux[0] : 0);
	bdk_get_value_default(path, "", value);
	return 0;
}

/* Security.ModeEnabled (TR-181) <-> BeaconType (TR-098)
 *   None                  <-> None
 *   WEP-64 / WEP-128      <-> Basic
 *   WPA-Personal          <-> WPA
 *   WPA2-Personal, WPA3-* <-> 11i
 *   WPA-WPA2-Personal     <-> WPAand11i
 */
static const struct { const char *mode; const char *beacon; } beacon_tbl[] = {
	{"None",              "None"},
	{"WEP-64",            "Basic"},
	{"WEP-128",           "Basic"},
	{"WPA-Personal",      "WPA"},
	{"WPA2-Personal",     "11i"},
	{"WPA-WPA2-Personal", "WPAand11i"},
	{"WPA3-Personal",     "11i"},
	{"WPA3-Personal-Transition", "11i"},
	{"WPA-Enterprise",    "WPA"},
	{"WPA2-Enterprise",   "11i"},
	{"WPA-WPA2-Enterprise", "WPAand11i"},
	{NULL, NULL}
};

static int get_wlan_beacon_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[128], *v;
	int i;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.Security.ModeEnabled", oc ? oc->aux[1] : 0);
	bdk_get_value_default(path, "None", &v);
	for (i = 0; beacon_tbl[i].mode; i++) {
		if (strcmp(v, beacon_tbl[i].mode) == 0) {
			*value = (char *)beacon_tbl[i].beacon;
			return 0;
		}
	}
	*value = "None";
	return 0;
}

static int set_wlan_beacon_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct bdk_objctx *oc = data;
	char path[128];
	const char *mode = NULL;

	(void)instance;
	if (strcmp(value, "None") == 0) mode = "None";
	else if (strcmp(value, "Basic") == 0) mode = "WEP-64";
	else if (strcmp(value, "WPA") == 0) mode = "WPA-Personal";
	else if (strcmp(value, "11i") == 0) mode = "WPA2-Personal";
	else if (strcmp(value, "WPAand11i") == 0) mode = "WPA-WPA2-Personal";
	else if (strcmp(value, "BasicandWPA") == 0 || strcmp(value, "Basicand11i") == 0 || strcmp(value, "BasicandWPAand11i") == 0)
		return FAULT_9007;  /* mixed WEP modes not supported by the Broadcom driver */

	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.Security.ModeEnabled", oc ? oc->aux[1] : 0);
	switch (action) {
	case VALUECHECK:
		if (!mode)
			return FAULT_9007;
		return bdk_check_writable(path);
	case VALUESET:
		if (!mode)
			return FAULT_9007;
		return bdk_queue_set(ctx, refparam, path, mode);
	}
	return 0;
}

/* Security.X_BROADCOM_COM_WlWpaEncryption "aes" / "tkip" / "tkip+aes" <->
 * TR-098 WPAEncryptionModes / IEEE11iEncryptionModes */
static int get_wlan_wpa_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[128], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.Security.X_BROADCOM_COM_WlWpaEncryption", oc ? oc->aux[1] : 0);
	bdk_get_value_default(path, "aes", &v);
	if (strcmp(v, "tkip") == 0)
		*value = "TKIPEncryption";
	else if (strcmp(v, "tkip+aes") == 0 || strcmp(v, "aes+tkip") == 0)
		*value = "TKIPandAESEncryption";
	else
		*value = "AESEncryption";
	return 0;
}

static int set_wlan_wpa_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct bdk_objctx *oc = data;
	char path[128];
	const char *enc = NULL;

	(void)instance;
	if (strcmp(value, "AESEncryption") == 0) enc = "aes";
	else if (strcmp(value, "TKIPEncryption") == 0) enc = "tkip";
	else if (strcmp(value, "TKIPandAESEncryption") == 0) enc = "tkip+aes";

	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.Security.X_BROADCOM_COM_WlWpaEncryption", oc ? oc->aux[1] : 0);
	switch (action) {
	case VALUECHECK:
		return enc ? bdk_check_writable(path) : FAULT_9007;
	case VALUESET:
		return enc ? bdk_queue_set(ctx, refparam, path, enc) : FAULT_9007;
	}
	return 0;
}

/* PSK vs RADIUS follows Security.ModeEnabled (…-Enterprise) */
static int get_wlan_wpa_auth(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[128], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.Security.ModeEnabled", oc ? oc->aux[1] : 0);
	bdk_get_value_default(path, "", &v);
	*value = strstr(v, "Enterprise") ? "EAPAuthentication" : "PSKAuthentication";
	return 0;
}

static int get_wlan_basic_encryption(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[128], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.Security.ModeEnabled", oc ? oc->aux[1] : 0);
	bdk_get_value_default(path, "", &v);
	*value = (strncmp(v, "WEP", 3) == 0) ? "WEPEncryption" : "None";
	return 0;
}

/* MACAddressControlEnabled <-> AccessPoint.X_BROADCOM_COM_WlFltMacMode
 * (disabled / allow / deny): enabling means "allow list only" */
static int get_wlan_macfilter(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[128], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.X_BROADCOM_COM_WlFltMacMode", oc ? oc->aux[1] : 0);
	bdk_get_value_default(path, "disabled", &v);
	*value = (v[0] && strcmp(v, "disabled") != 0) ? "1" : "0";
	return 0;
}

static int set_wlan_macfilter(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct bdk_objctx *oc = data;
	char path[128];
	bool b;

	(void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.X_BROADCOM_COM_WlFltMacMode", oc ? oc->aux[1] : 0);
	switch (action) {
	case VALUECHECK:
		if (string_to_bool(value, &b))
			return FAULT_9007;
		return bdk_check_writable(path);
	case VALUESET:
		string_to_bool(value, &b);
		return bdk_queue_set(ctx, refparam, path, b ? "allow" : "disabled");
	}
	return 0;
}

/* WLANConfiguration.{i}.WEPKey.{1..4}.WEPKey <-> Security.X_BROADCOM_COM_WlKey1..4
 * (write-only for the ACS, TR-098 says reading returns an empty string) */
DMLEAF tWLANWEPKeyParam[] = {
{"WEPKey", &DMWRITE, DMT_STRING, get_wlan_wepkey, set_wlan_wepkey, NULL, NULL},
{0}
};

int browseWLANWEPKeyInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct bdk_objctx *parent = prev_data;
	unsigned int j;
	char *sinst;

	(void)prev_instance;
	if (!parent)
		return 0;
	for (j = 1; j <= 4; j++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		memcpy(oc->aux, parent->aux, sizeof(oc->aux));
		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.WiFi.AccessPoint.%u.Security.X_BROADCOM_COM_WlKey%u", parent->aux[1], j);
		dmasprintf(&sinst, "%u", j);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

static int get_wlan_wepkey(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = "";
	return 0;
}

static int set_wlan_wepkey(char *refparam, struct dmctx *ctx, void *data, char *instance, char *value, int action)
{
	struct bdk_objctx *oc = data;

	(void)instance;
	if (!oc || !oc->tr181_base[0])
		return FAULT_9005;
	switch (action) {
	case VALUECHECK:
		return bdk_check_writable(oc->tr181_base);
	case VALUESET:
		return bdk_queue_set(ctx, refparam, oc->tr181_base, value);
	}
	return 0;
}

/* WLANConfiguration.{i}.WPS. <- AccessPoint.{aux1}.WPS. + WiFi.X_BROADCOM_COM_WpsCfg.
 * ConfigMethods* keep the TR-181 words (PushButton, PIN...); TR-098 spells
 * PIN as "Keypad"/"Label" — translate on the ACS side if it matters. */
static const struct bdk_leafmap wlan_wps_map[] = {
	{"Enable",                 "Device.WiFi.AccessPoint.{aux1}.WPS.Enable",                 BDK_MAP_RW | BDK_MAP_BOOL},
	{"DeviceName",             "Device.WiFi.X_BROADCOM_COM_WpsCfg.WpsDeviceName",           BDK_MAP_RW},
	{"DevicePassword",         "Device.WiFi.X_BROADCOM_COM_WpsCfg.WpsDevicePin",            BDK_MAP_RW | BDK_MAP_EMPTY},
	{"UUID",                   "",                                                          BDK_MAP_CONST},
	{"Version",                "Device.WiFi.AccessPoint.{aux1}.WPS.Version",                BDK_MAP_RO},
	{"ConfigMethodsSupported", "Device.WiFi.AccessPoint.{aux1}.WPS.ConfigMethodsSupported", BDK_MAP_RO},
	{"ConfigMethodsEnabled",   "Device.WiFi.AccessPoint.{aux1}.WPS.ConfigMethodsEnabled",   BDK_MAP_RW},
	{"SetupLockedState",       "Unlocked",                                                  BDK_MAP_CONST},
	{"ConfigurationError",     "NoError",                                                   BDK_MAP_CONST},
	{"RegistrarNumberOfEntries", "0",                                                       BDK_MAP_CONST},
	{0}
};

DMLEAF tWLANWPSParam[] = {
{"Enable", &DMWRITE, DMT_BOOL, bdk_map_get, bdk_map_set, NULL, NULL},
{"DeviceName", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"DevicePassword", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"UUID", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"Version", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ConfigMethodsSupported", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ConfigMethodsEnabled", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"SetupLockedState", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ConfigurationState", &DMREAD, DMT_STRING, get_wlan_wps_config_state, NULL, NULL, NULL},
{"ConfigurationError", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"RegistrarNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* X_BROADCOM_COM_Wsc_config_state "1" = configured (nvram wl_wps_config_state) */
static int get_wlan_wps_config_state(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[128], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.WPS.X_BROADCOM_COM_Wsc_config_state", oc ? oc->aux[1] : 0);
	bdk_get_value_default(path, "0", &v);
	*value = (strcmp(v, "1") == 0) ? "Configured" : "Not configured";
	return 0;
}

/* WLANConfiguration.{i}.Stats. <- Device.WiFi.SSID.{i}.Stats. (TR-098 Amd 2, WiFiLAN:2) */
static const struct bdk_leafmap wlan_stats_map[] = {
	{"ErrorsSent",                  "Stats.ErrorsSent",                  BDK_MAP_RO},
	{"ErrorsReceived",              "Stats.ErrorsReceived",              BDK_MAP_RO},
	{"UnicastPacketsSent",          "Stats.UnicastPacketsSent",          BDK_MAP_RO},
	{"UnicastPacketsReceived",      "Stats.UnicastPacketsReceived",      BDK_MAP_RO},
	{"DiscardPacketsSent",          "Stats.DiscardPacketsSent",          BDK_MAP_RO},
	{"DiscardPacketsReceived",      "Stats.DiscardPacketsReceived",      BDK_MAP_RO},
	{"MulticastPacketsSent",        "Stats.MulticastPacketsSent",        BDK_MAP_RO},
	{"MulticastPacketsReceived",    "Stats.MulticastPacketsReceived",    BDK_MAP_RO},
	{"BroadcastPacketsSent",        "Stats.BroadcastPacketsSent",        BDK_MAP_RO},
	{"BroadcastPacketsReceived",    "Stats.BroadcastPacketsReceived",    BDK_MAP_RO},
	{"UnknownProtoPacketsReceived", "Stats.UnknownProtoPacketsReceived", BDK_MAP_RO},
	{0}
};

DMLEAF tWLANStatsParam[] = {
{"ErrorsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"ErrorsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"UnicastPacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"UnicastPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"DiscardPacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"DiscardPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"MulticastPacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"MulticastPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"BroadcastPacketsSent", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"BroadcastPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{"UnknownProtoPacketsReceived", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* WLANConfiguration.{i}.PreSharedKey.1. <- AccessPoint.{aux1}.Security. */
static const struct bdk_leafmap wlan_psk_map[] = {
	{"PreSharedKey",  "Device.WiFi.AccessPoint.{aux1}.Security.PreSharedKey",  BDK_MAP_RW | BDK_MAP_EMPTY},
	{"KeyPassphrase", "Device.WiFi.AccessPoint.{aux1}.Security.KeyPassphrase", BDK_MAP_RW | BDK_MAP_EMPTY},
	{"AssociatedDeviceMACAddress", "", BDK_MAP_CONST},
	{0}
};

DMLEAF tWLANPreSharedKeyParam[] = {
{"PreSharedKey", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"KeyPassphrase", &DMWRITE, DMT_STRING, bdk_map_get, bdk_map_set, NULL, NULL},
{"AssociatedDeviceMACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

int browseWLANPreSharedKeyInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	(void)prev_instance;
	/* one PSK entry, same context (aux[1] = AccessPoint) as the parent */
	DM_LINK_INST_OBJ(dmctx, parent_node, prev_data, "1");
	return 0;
}

/* WLANConfiguration.{i}.AssociatedDevice.{j}. <- AccessPoint.{aux1}.AssociatedDevice.{j}. */
static const struct bdk_leafmap wlan_assoc_map[] = {
	{"AssociatedDeviceMACAddress",        "MACAddress",          BDK_MAP_RO},
	{"AssociatedDeviceAuthenticationState","AuthenticationState", BDK_MAP_RO | BDK_MAP_BOOL},
	{"LastDataTransmitRate",              "LastDataDownlinkRate", BDK_MAP_RO},
	{"LastRequestedUnicastCipher",        "",                    BDK_MAP_CONST},
	{"LastRequestedMulticastCipher",      "",                    BDK_MAP_CONST},
	{"LastPMKId",                         "",                    BDK_MAP_CONST},
	{CUSTOM_PREFIX"SignalStrength",     "SignalStrength",      BDK_MAP_RO},
	{CUSTOM_PREFIX"LastDataDownlinkRate","LastDataDownlinkRate", BDK_MAP_RO},
	{CUSTOM_PREFIX"LastDataUplinkRate", "LastDataUplinkRate",  BDK_MAP_RO},
	{0}
};

DMLEAF tWLANAssociatedDeviceParam[] = {
{"AssociatedDeviceMACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"AssociatedDeviceIPAddress", &DMREAD, DMT_STRING, get_wlan_assoc_ip, NULL, NULL, NULL},
{"AssociatedDeviceAuthenticationState", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"LastRequestedUnicastCipher", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"LastRequestedMulticastCipher", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"LastPMKId", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"LastDataTransmitRate", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{CUSTOM_PREFIX"SignalStrength", &DMREAD, DMT_INT, bdk_map_get, NULL, NULL, NULL},
{CUSTOM_PREFIX"LastDataDownlinkRate", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{CUSTOM_PREFIX"LastDataUplinkRate", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

/* IP of a station: TR-181 keeps it in Device.Hosts.Host.{i} (PhysAddress) */
static int get_wlan_assoc_ip(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	unsigned int *inst = NULL, num = 0, i;
	char path[160], *mac, *v;

	(void)refparam; (void)ctx; (void)instance;
	*value = "";
	if (!oc)
		return 0;
	snprintf(path, sizeof(path), "%sMACAddress", oc->tr181_base);
	bdk_get_value_default(path, "", &mac);
	if (!mac[0] || bdk_get_instances("Device.Hosts.Host.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		snprintf(path, sizeof(path), "Device.Hosts.Host.%u.PhysAddress", inst[i]);
		bdk_get_value_default(path, "", &v);
		if (strcasecmp(v, mac) != 0)
			continue;
		snprintf(path, sizeof(path), "Device.Hosts.Host.%u.IPAddress", inst[i]);
		bdk_get_value_default(path, "", value);
		break;
	}
	return 0;
}

int browseWLANAssociatedDeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	struct bdk_objctx *parent = prev_data;
	unsigned int *inst = NULL, num = 0, i;
	char path[96], *sinst;

	(void)prev_instance;
	if (!parent)
		return 0;
	snprintf(path, sizeof(path), "Device.WiFi.AccessPoint.%u.AssociatedDevice.", parent->aux[1]);
	if (bdk_get_instances(path, &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "%s%u.", path, inst[i]);
		memcpy(oc->aux, parent->aux, sizeof(oc->aux));
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* LANDevice.1.Hosts.  <- Device.Hosts.                                    */
/* ---------------------------------------------------------------------- */

static int get_host_interface_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

static const struct bdk_leafmap hosts_map[] = {
	{"HostNumberOfEntries", "HostNumberOfEntries", BDK_MAP_RO},
	{0}
};

static const struct bdk_leafmap host_map[] = {
	{"IPAddress",          "IPAddress",          BDK_MAP_RO},
	{"AddressSource",      "AddressSource",      BDK_MAP_RO},
	{"LeaseTimeRemaining", "LeaseTimeRemaining", BDK_MAP_RO},
	{"MACAddress",         "PhysAddress",        BDK_MAP_RO},
	{"HostName",           "HostName",           BDK_MAP_RO},
	{"Active",             "Active",             BDK_MAP_RO | BDK_MAP_BOOL},
	{"Layer2Interface",    "Layer1Interface",    BDK_MAP_RO},
	{"VendorClassID",      "VendorClassID",      BDK_MAP_RO},
	{"ClientID",           "ClientID",           BDK_MAP_RO},
	{"UserClassID",        "UserClassID",        BDK_MAP_RO},
	{0}
};

DMOBJ tHostsObj[] = {
{"Host", &DMREAD, NULL, NULL, NULL, browseHostInst, NULL, &DMNONE, NULL, tHostParam, NULL},
{0}
};

DMLEAF tHostsParam[] = {
{"HostNumberOfEntries", &DMREAD, DMT_UNINT, bdk_map_get, NULL, NULL, NULL},
{0}
};

DMLEAF tHostParam[] = {
{"IPAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"AddressSource", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"LeaseTimeRemaining", &DMREAD, DMT_INT, bdk_map_get, NULL, NULL, NULL},
{"MACAddress", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"HostName", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"InterfaceType", &DMREAD, DMT_STRING, get_host_interface_type, NULL, NULL, NULL},
{"Active", &DMREAD, DMT_BOOL, bdk_map_get, NULL, NULL, NULL},
{"Layer2Interface", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"VendorClassID", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"ClientID", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{"UserClassID", &DMREAD, DMT_STRING, bdk_map_get, NULL, NULL, NULL},
{0}
};

static int get_host_interface_type(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	struct bdk_objctx *oc = data;
	char path[160], *v;

	(void)refparam; (void)ctx; (void)instance;
	snprintf(path, sizeof(path), "%sLayer1Interface", oc ? oc->tr181_base : "");
	bdk_get_value_default(path, "", &v);
	if (strncmp(v, "Device.WiFi.", 12) == 0)
		*value = "802.11";
	else if (strncmp(v, "Device.Ethernet.", 16) == 0)
		*value = "Ethernet";
	else
		*value = "Other";
	return 0;
}

int browseHostInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance)
{
	unsigned int *inst = NULL, num = 0, i;
	char *sinst;

	(void)prev_data;
	(void)prev_instance;
	if (bdk_get_instances("Device.Hosts.Host.", &inst, &num))
		return 0;
	for (i = 0; i < num; i++) {
		struct bdk_objctx *oc = dmcalloc(1, sizeof(*oc));

		snprintf(oc->tr181_base, sizeof(oc->tr181_base), "Device.Hosts.Host.%u.", inst[i]);
		dmasprintf(&sinst, "%u", inst[i]);
		if (DM_LINK_INST_OBJ(dmctx, parent_node, oc, sinst) == DM_STOP)
			break;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */

void landevice_bdk_register(void)
{
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.LANHostConfigManagement.", lanhostcfg_map, BDK_LAN_DHCP_POOL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.LANHostConfigManagement.IPInterface.{i}.", ipinterface_map, BDK_LAN_IP_INTERFACE);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.LANHostConfigManagement.DHCPStaticAddress.{i}.", dhcpstatic_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.LANHostConfigManagement.DHCPOption.{i}.", dhcpoption_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.LANEthernetInterfaceConfig.{i}.", laneth_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.LANEthernetInterfaceConfig.{i}.Stats.", laneth_stats_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.WLANConfiguration.{i}.", wlan_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.WLANConfiguration.{i}.PreSharedKey.{i}.", wlan_psk_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.WLANConfiguration.{i}.AssociatedDevice.{i}.", wlan_assoc_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.WLANConfiguration.{i}.WPS.", wlan_wps_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.WLANConfiguration.{i}.Stats.", wlan_stats_map, NULL);
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.Hosts.", hosts_map, "Device.Hosts.");
	bdk_register_objmap("InternetGatewayDevice.LANDevice.{i}.Hosts.Host.{i}.", host_map, NULL);
}
