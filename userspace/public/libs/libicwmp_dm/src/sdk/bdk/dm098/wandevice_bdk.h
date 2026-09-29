/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice.WANDevice. on Broadcom BDK (TR-181 MDM backend).
 */
#ifndef __WANDEVICE_BDK_H
#define __WANDEVICE_BDK_H

#include "dmtr098.h"

extern DMOBJ tWANDeviceObj[];
extern DMLEAF tWANDeviceParam[];
extern DMLEAF tWANCommonInterfaceConfigParam[];
extern DMLEAF tWANEthernetInterfaceConfigParam[];
extern DMOBJ tWANEthernetInterfaceConfigObj[];
extern DMLEAF tWANEthernetInterfaceConfigStatsParam[];
extern DMOBJ tWANConnectionDeviceObj[];
extern DMLEAF tWANConnectionDeviceParam[];
extern DMLEAF tWANEthernetLinkConfigParam[];
extern DMOBJ tWANIPConnectionObj[];
extern DMLEAF tWANIPConnectionParam[];
extern DMOBJ tWANPPPConnectionObj[];
extern DMLEAF tWANPPPConnectionParam[];
extern DMLEAF tWANPortMappingParam[];
extern DMLEAF tWANConnectionStatsParam[];

int browsewandeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWANConnectionDeviceInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWANIPConnectionInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWANPPPConnectionInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseWANPortMappingInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int add_wan_portmapping(char *refparam, struct dmctx *ctx, void *data, char **instancepara);
int delete_wan_portmapping(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action);

void wandevice_bdk_register(void);

#endif
