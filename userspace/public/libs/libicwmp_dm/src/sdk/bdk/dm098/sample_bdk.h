/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 */
#ifndef __SAMPLE_BDK_H
#define __SAMPLE_BDK_H

#include "dmtr098.h"

/* X_MARUSYS_COM_Sample.: developer template (tr098/bdk/sample_bdk.c), built
 * with -DBDK_SAMPLE_OBJECT only, visible when UCI cwmp.sample.enable='1'.
 * Same tables under InternetGatewayDevice. and Device. */
extern DMLEAF tSampleParam[];
extern DMOBJ tSampleObj[];
bool sample_enabled(struct dmctx *ctx, void *data);
void sample_bdk_register(void);

/* case 7: leaf added to DeviceInfo. of both models */
int get_sample_uptime_minutes(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value);

#endif
