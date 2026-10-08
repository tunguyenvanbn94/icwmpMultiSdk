/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	TR-181 root ("Device.") of the MTK / Airoha OpenWrt SDK, served when
 *	icwmpd latched cwmp.cpe.datamodel=tr181 (dm_entry_load_model(),
 *	dm_platform_select_root() in dmplatform_mtk.c).
 *
 *	Like sdk/mtk/dm098/, there is no list of objects here: every module of
 *	sdk/mtk/dm181/ registers itself with .model = DM_MODEL_TR181 and the
 *	registry merges them under Device.  A TR-181 module carries tables only;
 *	the reading and writing of the product stays in the getters/setters of
 *	sdk/mtk/dm098/, shared by both models (docs/plan/tr181_mtk_design.md).
 *
 *	This module carries the root leaves and icwmpd's own settings object.
 */
#include "dmtr098.h"
#include "dm_registry.h"
#include "icwmpcfg.h"

/* TR-181 issue of the names the tables follow (checked against the BDK
 * data model, docs/issue/tr181-schema.py); forced in every Inform */
#define MTK_TR181_ROOT_VERSION	"2.19"

static int get_root_data_model_version(char *refparam, struct dmctx *ctx, void *data, char *instance, char **value)
{
	(void)refparam; (void)ctx; (void)data; (void)instance;
	*value = MTK_TR181_ROOT_VERSION;
	return 0;
}

static DMLEAF tRoot181MtkParams[] = {
/* PARAM, permission, type, getvalue, setvalue, forced_inform, notification */
{"RootDataModelVersion", &DMREAD, DMT_STRING, get_root_data_model_version, NULL, &DMFINFRM, NULL},
{0}
};

static DMOBJ tRoot181MtkObj[] = {
/* OBJ, permission, addobj, delobj, checkobj, browseinstobj, forced_inform, notification, nextobj, leaf, linker */
/* icwmpd's own settings, the same table as InternetGatewayDevice.X_HNI_Icwmp. */
{CUSTOM_PREFIX"Icwmp", &DMREAD, NULL, NULL, NULL, NULL, NULL, NULL, NULL, tIcwmpCfgParam, NULL},
{0}
};

static const char *const root181_mtk_paths[] = {
	"Device.RootDataModelVersion",
	"Device." CUSTOM_PREFIX "Icwmp.",
	NULL
};

static const struct dm_module root181_mtk_module = {
	.name   = "mtk-tr181-root",
	.model  = DM_MODEL_TR181,
	.order  = DM_ORDER_SDK,
	.objs   = tRoot181MtkObj,
	.params = tRoot181MtkParams,
	.paths  = root181_mtk_paths,
};
DM_MODULE_REGISTER(root181_mtk_module);
