/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 */
#ifndef __MLO_BDK_H
#define __MLO_BDK_H

#include "dmtr098.h"

/* Wi-Fi 7 MLO access point configuration, served by icwmpd itself (no
 * SDK data model change).  Same leaf table under both roots:
 *   TR-098  InternetGatewayDevice.X_MARUSYS_COM_MloCfg.
 *   TR-181  Device.WiFi.X_MARUSYS_COM_MloCfg.
 * Parameter names are provisional (CUSTOM_PREFIX), see tr098/bdk/mlo_bdk.c. */
extern DMLEAF tMloCfgParam[];

#endif
