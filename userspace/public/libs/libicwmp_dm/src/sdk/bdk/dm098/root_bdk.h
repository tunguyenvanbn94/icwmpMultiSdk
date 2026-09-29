/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 */
#ifndef __ROOT_BDK_H
#define __ROOT_BDK_H

#include "dmtr098.h"

/* chạy các đăng ký per-object mà cây BDK cần trước khi engine đi cây */
void tr098_bdk_register_all(void);

extern DMOBJ tRoot_098_Obj[];
/* TR-181 mode (cwmp.cpe.datamodel=tr181): "Device." root, root181_bdk.c */

/* Device.ManagementServer. in TR-181 mode: the leaves icwmpd owns and the
 * MDM does not have (HTTP compression, lightweight notification, alias
 * based addressing, instance mode), served from the cwmp UCI config by the
 * stock managementserver.c getters (tr098/bdk/root181_bdk.c). */
extern DMLEAF tManagementServer181Params[];
/* Device.DeviceInfo. static leaves (root181_bdk.c), may be empty */
extern DMLEAF tDeviceInfo181Params[];

/* register every TR-098 -> TR-181 mapping table (idempotent) */

/* deviceinfo_bdk.c: identity override from UCI cwmp.cpe.* (manufacturer, oui,
 * product_class, serial_number, ...).  1 and *value (dm-allocated) when the
 * option is set for that DeviceInfo leaf, 0 = use the MDM value.  Used by the
 * TR-181 passthrough too so GPV and the Inform DeviceId agree. */
int devinfo_uci_override(const char *leaf, char **value);

#endif
