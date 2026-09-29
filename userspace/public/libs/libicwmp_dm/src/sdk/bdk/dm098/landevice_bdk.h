/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.LANDevice. on Broadcom BDK (TR-181 MDM backend).
 */
#ifndef __LANDEVICE_BDK_H
#define __LANDEVICE_BDK_H

#include "dmtr098.h"

/* TR-181 instances the LAN side is projected on.  Verified on the reference
 * board dump logs/20260828_referenceBoard (br0 = Device.IP.Interface.1,
 * Device.DHCPv4.Server.Pool.1.Interface = Device.IP.Interface.1).  Re-check
 * with "tr69_mdmcli" / dumpmdm on MO77300EB before trusting them. */
#define BDK_LAN_IP_INTERFACE   "Device.IP.Interface.1."
#define BDK_LAN_DHCP_POOL      "Device.DHCPv4.Server.Pool.1."
#define BDK_LAN_BRIDGE_NAME    "br0"

extern DMOBJ tLANDeviceObj[];
extern DMLEAF tLANDeviceParam[];
extern DMOBJ tLANHostConfigManagementObj[];
extern DMLEAF tLANHostConfigManagementParam[];
extern DMLEAF tIPInterfaceParam[];
extern DMLEAF tDHCPStaticAddressParam[];
extern DMLEAF tDHCPOptionParam[];
extern DMOBJ tLANEthernetInterfaceConfigObj[];
extern DMLEAF tLANEthernetInterfaceConfigParam[];
extern DMLEAF tLANEthernetInterfaceConfigStatsParam[];
extern DMOBJ tWLANConfigurationObj[];
extern DMLEAF tWLANConfigurationParam[];
extern DMLEAF tWLANPreSharedKeyParam[];
extern DMLEAF tWLANAssociatedDeviceParam[];
extern DMLEAF tWLANWEPKeyParam[];
extern DMLEAF tWLANWPSParam[];
extern DMLEAF tWLANStatsParam[];
extern DMOBJ tHostsObj[];
extern DMLEAF tHostsParam[];
extern DMLEAF tHostParam[];

int browselandeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseIPInterfaceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseDHCPStaticAddressInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseDHCPOptionInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int add_dhcp_static_address(char *refparam, struct dmctx *ctx, void *data, char **instancepara);
int delete_dhcp_static_address(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action);
int add_dhcp_option(char *refparam, struct dmctx *ctx, void *data, char **instancepara);
int delete_dhcp_option(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action);
int browseLANEthernetInterfaceConfigInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWLANConfigurationInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWLANPreSharedKeyInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWLANWEPKeyInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWLANAssociatedDeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseHostInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);

void landevice_bdk_register(void);

#endif
