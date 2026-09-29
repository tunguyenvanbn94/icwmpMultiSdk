/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Root data model module of the MTK / Airoha OpenWrt SDK.
 *
 *	There is no list of objects here on purpose.  Every module in this
 *	directory registers itself (dm_registry.h) and declares the paths it
 *	owns, the registry merges them into the root.  Porting one object from
 *	the product's easycwmp shell library to C is therefore:
 *
 *	  1. write sdk/mtk/dm098/<object>_mtk.c with its DMOBJ/DMLEAF tables,
 *	     reading the same UCI option / ubus call / file the shell function
 *	     read (helpers in sdk/mtk/dmmtk.h),
 *	  2. end it with a struct dm_module + DM_MODULE_REGISTER(), listing the
 *	     full path in .paths,
 *	  3. add the file to sdk/mtk/sdk.mk.
 *
 *	From then on that subtree is answered by the C tree and dropped from the
 *	replies of sdk/mtk/compat/ (dmplatform_mtk.c asks dm_registry_owns()),
 *	so exactly one owner answers a path -- no duplicate rows in a GPN.
 *
 *	This module itself only carries icwmpd's own settings object.
 */
#include "dmtr098.h"
#include "dm_registry.h"
#include "root_mtk.h"
#include "icwmpcfg.h"

static DMOBJ tRootMtkObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
/* icwmpd's own settings (tr098/common/icwmpcfg.c): log level, CWMP amendment,
 * session timeout, connection request host/port override, backend name */
{CUSTOM_PREFIX"Icwmp", &DMREAD, NULL, NULL, NULL, NULL, NULL, &DMNONE, NULL, tIcwmpCfgParam, NULL},
{0}
};

static const char *const root_mtk_paths[] = {
	"InternetGatewayDevice." CUSTOM_PREFIX "Icwmp.",
	NULL
};

static const struct dm_module root_mtk_module = {
	.name  = "mtk-icwmpcfg",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tRootMtkObj,
	.paths = root_mtk_paths,
};
DM_MODULE_REGISTER(root_mtk_module);
