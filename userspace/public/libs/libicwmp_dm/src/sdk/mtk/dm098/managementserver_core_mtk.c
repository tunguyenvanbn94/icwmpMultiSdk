/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Registers the portable ManagementServer tree (tr098/managementserver.c)
 *	as a data model module.  That file is shared with the uci and bdk SDKs
 *	and carries no registration of its own, because each SDK decides where
 *	the object sits: here it is a direct child of the root.
 */
#include "dmtr098.h"
#include "dm_registry.h"
#include "managementserver.h"

static DMOBJ tManagementServerCoreRoot[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
{"ManagementServer", &DMREAD, NULL, NULL, NULL, NULL, &DMFINFRM, &DMNONE, NULL, tManagementServerParams, NULL},
{0}
};

static const struct dm_module ms_core_module = {
	.name  = "mtk-managementserver-core",
	.model = DM_MODEL_TR098,
	.order = DM_ORDER_SDK,
	.objs  = tManagementServerCoreRoot,
	/* no .paths: the mtk-managementserver module claims the object */
};
DM_MODULE_REGISTER(ms_core_module);
