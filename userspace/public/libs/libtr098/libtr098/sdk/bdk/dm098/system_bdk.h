/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	InternetGatewayDevice. root leaves, Time., IPPingDiagnostics.,
 *	TraceRouteDiagnostics. on Broadcom BDK (TR-181 MDM backend).
 */
#ifndef __SYSTEM_BDK_H
#define __SYSTEM_BDK_H

#include "dmtr098.h"

extern DMLEAF tRoot_098_Params[];
extern DMLEAF tTimeParams[];
extern DMLEAF tIPPingDiagnosticsParam[];
extern DMOBJ tTraceRouteDiagnosticsObj[];
extern DMLEAF tTraceRouteDiagnosticsParam[];
extern DMLEAF tRouteHopsParam[];
extern DMOBJ tLayer3ForwardingObj[];
extern DMLEAF tLayer3ForwardingParam[];
extern DMLEAF tLayer3ForwardingEntryParam[];

int browseRouteHopsInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int browseLayer3ForwardingInst(struct dmctx *dmctx, DMNODE *parent_node, void *prev_data, char *prev_instance);
int add_l3_forwarding(char *refparam, struct dmctx *ctx, void *data, char **instancepara);
int delete_l3_forwarding(char *refparam, struct dmctx *ctx, void *data, char *instance, unsigned char del_action);

void system_bdk_register(void);

#endif
