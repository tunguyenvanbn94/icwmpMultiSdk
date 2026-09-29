/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 */
#ifndef __ICWMPCFG_H
#define __ICWMPCFG_H

#include "dmtr098.h"

/* <CUSTOM_PREFIX>Icwmp.: icwmpd's own settings (UCI cwmp.cpe.*) that the
 * platform data model has no place for — data model root selection (BDK),
 * CWMP amendment, log level, session timeout, connection request override.
 * Platform independent leaf table (tr098/common/icwmpcfg.c); each root file
 * decides where to hang it. */
extern DMLEAF tIcwmpCfgParam[];
/* BDK: ManagementServer.<CUSTOM_PREFIX>Icwmp. under both roots */
extern DMOBJ tManagementServerIcwmpObj[];

#endif
